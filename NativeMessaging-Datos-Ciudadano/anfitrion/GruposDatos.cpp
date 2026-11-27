// =================================================================================================
//  GruposDatos.cpp  -  Interpretar los grupos de datos del DNIe (ver GruposDatos.h)
// =================================================================================================
#include "stdafx.h"
#include "GruposDatos.h"

#include <cstring>

// ---------------------------------------------------------------------------------------------
// TLV (BER) de los grupos de datos
// ---------------------------------------------------------------------------------------------
// Los grupos de datos del DNIe (DG1, DG2...) están codificados en ASN.1 BER, como los certificados.
// Todo son bloques TLV: Tipo (etiqueta), Longitud y Valor.
//
//   Etiqueta: 1 byte, o varios si los 5 bits bajos del primero son 11111 (p. ej. 5F 1F = MRZ).
//             El bit 0x20 del primer byte indica "construido": su valor contiene otros TLV dentro.
//   Longitud: si es menor de 128, un solo byte. Si no, 0x81, 0x82 o 0x83 seguido de 1, 2 o 3 bytes
//             con la longitud (p. ej. 82 1A 2B = 0x1A2B bytes).
//   Valor:    los bytes del dato.
//
// Ejemplo (DG1 del DNIe):  61 5D  5F 1F 5A  <90 caracteres de la MRZ>
//   61 = grupo DG1 (construido), 5D = 93 bytes de contenido;
//   5F 1F = etiqueta de la MRZ (dos bytes), 5A = 90 bytes de valor.

// Lee la cabecera TLV en p: etiqueta (1 o más bytes), posición y longitud del valor.
static bool LeerTLV(const Bytes& b, size_t p, size_t fin, unsigned& tag, size_t& valor, size_t& len, bool& construido)
{
	if (p >= fin)
		return false;
	construido = (b[p] & 0x20) != 0;
	tag = b[p++];
	if ((tag & 0x1F) == 0x1F)
	{
		do
		{
			if (p >= fin || tag > 0xFFFFFF)
				return false;
			tag = (tag << 8) | b[p];
		} while (b[p++] & 0x80);
	}
	if (p >= fin)
		return false;
	len = b[p++];
	if (len & 0x80)
	{
		size_t n = len & 0x7F;
		if (n == 0 || n > 3)
			return false;
		len = 0;
		for (size_t k = 0; k < n; k++)
		{
			if (p >= fin)
				return false;
			len = (len << 8) | b[p++];
		}
	}
	if (len > fin - p)
		return false;
	valor = p;
	return true;
}

// Recorre los TLV uno tras otro y, si alguno es construido, busca también dentro de él (recursión).
// Todas las longitudes se comprueban en LeerTLV, así que un grupo de datos mal formado no puede
// hacer que se lea fuera del búfer.
bool BuscarTag(const Bytes& b, size_t ini, size_t fin, unsigned buscada, size_t& off, size_t& len)
{
	size_t p = ini;
	while (p < fin)
	{
		unsigned tag;
		size_t v, l;
		bool construido;
		if (!LeerTLV(b, p, fin, tag, v, l, construido))
			return false;
		if (tag == buscada)
		{
			off = v;
			len = l;
			return true;
		}
		if (construido && BuscarTag(b, v, v + l, buscada, off, len))
			return true;
		p = v + l;
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// DG13: datos personales
// ---------------------------------------------------------------------------------------------
// El DG13 es un grupo "opcional" del estándar que cada país usa a su manera; el DNIe guarda en él
// los datos personales como una secuencia (etiqueta 0x30) de 16 cadenas UTF8String /
// PrintableString:   6D { 30 { cadena, cadena, ... } }
// Como no llevan etiqueta propia, cada dato se identifica por su POSICIÓN en la secuencia (ver la
// lista DG13_xxx en GruposDatos.h).
bool CamposDG13(const Bytes& dg13, std::vector<std::string>& campos)
{
	campos.assign(DG13_CAMPOS, std::string());
	size_t off, len;
	if (!BuscarTag(dg13, 0, dg13.size(), 0x30, off, len))
		return false;
	size_t p = off, fin = off + len;
	for (int i = 0; p < fin; i++)
	{
		unsigned tag;
		size_t v, l;
		bool construido;
		if (!LeerTLV(dg13, p, fin, tag, v, l, construido))
			return false;
		if (i < DG13_CAMPOS)
			campos[i].assign((const char*)dg13.data() + v, l);
		p = v + l;
	}
	return true;
}

// ---------------------------------------------------------------------------------------------
// DG1: MRZ
// ---------------------------------------------------------------------------------------------
// La MRZ (zona de lectura mecánica) son las líneas de letras y "<" de la parte de atrás del DNI. En
// el DNIe son 3 líneas de 30 caracteres (90 en total) y están en el DG1, igual que en un pasaporte.
bool MrzDG1(const Bytes& dg1, std::string& mrz)
{
	mrz.clear();
	size_t off, len;
	if (!BuscarTag(dg1, 0, dg1.size(), 0x5F1F, off, len))
		return false;
	mrz.assign((const char*)dg1.data() + off, len);
	return true;
}

// ---------------------------------------------------------------------------------------------
// DG2 y DG7: imágenes
// ---------------------------------------------------------------------------------------------
// El DG2 sigue el estándar de biometría: dentro del bloque 5F2E hay una cabecera con datos de la
// imagen (tamaño, tipo...) y después la imagen. La imagen se localiza por su "firma" de formato
// (los bytes fijos con que empieza cada tipo de archivo):
//   JP2 (fichero JPEG 2000): 00 00 00 0C 6A 50 20 20 0D 0A 87 0A   ("jP  " + control)
//   J2K (flujo JPEG 2000):   FF 4F FF 51
//   JPEG:                    FF D8 FF
// Si no se reconoce, se devuelve el contenido completo con formato "".
bool ImagenDeGrupo(const Bytes& g, unsigned tag, Bytes& imagen, std::string& formato)
{
	imagen.clear();
	formato.clear();
	size_t off, len;
	if (!BuscarTag(g, 0, g.size(), tag, off, len))
		return false;

	static const unsigned char jp2[] = { 0x00, 0x00, 0x00, 0x0C, 0x6A, 0x50, 0x20, 0x20, 0x0D, 0x0A, 0x87, 0x0A };
	static const unsigned char j2k[] = { 0xFF, 0x4F, 0xFF, 0x51 };
	static const unsigned char jpg[] = { 0xFF, 0xD8, 0xFF };
	struct { const unsigned char* f; size_t n; const char* nombre; } firmas[] = {
		{ jp2, sizeof(jp2), "jp2" }, { j2k, sizeof(j2k), "j2k" }, { jpg, sizeof(jpg), "jpg" } };

	size_t ini = off;
	for (auto& f : firmas)
	{
		for (size_t i = off; i + f.n <= off + len; i++)
			if (memcmp(&g[i], f.f, f.n) == 0)
			{
				ini = i;
				formato = f.nombre;
				break;
			}
		if (!formato.empty())
			break;
	}
	imagen.assign(g.begin() + ini, g.begin() + off + len);
	return true;
}
