// =================================================================================================
//  GruposDatos.h  -  Interpretar los grupos de datos del DNIe (DG1, DG2, DG7 y DG13)
// =================================================================================================
//  El driver publica los grupos de datos del DNIe como objetos de datos PKCS#11 con su nombre como
//  etiqueta ("DG1", "DG13"...). CTarjeta los lee tal cual; estas funciones sacan de ellos lo que
//  interesa a la página. No tocan la tarjeta: solo trabajan con los bytes ya leídos.
//
//    DG1   MRZ (las tres líneas de la parte de atrás del documento)
//    DG2   fotografía
//    DG7   imagen de la firma manuscrita
//    DG13  datos personales (propio del DNIe)
// =================================================================================================
#pragma once

#include <string>
#include <vector>

typedef std::vector<unsigned char> Bytes;

// Busca una etiqueta en profundidad dentro de [ini, fin). Devuelve la posición y la longitud de su
// valor. Nunca lee fuera del búfer, aunque el grupo esté mal formado.
bool BuscarTag(const Bytes& b, size_t ini, size_t fin, unsigned buscada, size_t& off, size_t& len);

// Posición de cada dato dentro del DG13 (16 cadenas de texto sin etiquetar, en este orden).
enum
{
	DG13_APELLIDO1 = 0,
	DG13_APELLIDO2 = 1,
	DG13_NOMBRE = 2,
	DG13_NUMERO_DNI = 3,
	DG13_FECHA_NACIMIENTO = 4,
	DG13_NACIONALIDAD = 5,
	DG13_FECHA_EXPIRACION = 6,
	DG13_SOPORTE = 7,
	DG13_SEXO = 8,
	DG13_LOCALIDAD_NACIMIENTO = 9,
	DG13_PROVINCIA_NACIMIENTO = 10,
	DG13_PADRES = 11,
	DG13_DOMICILIO = 12,
	DG13_LOCALIDAD_DOMICILIO = 13,
	// 14: vacío en el DNIe probado; no se usa
	DG13_PROVINCIA_DOMICILIO = 15,
	DG13_CAMPOS = 16
};

// Todas las cadenas del DG13, en orden (las que falten quedan vacías). false si no tiene el
// formato esperado.
bool CamposDG13(const Bytes& dg13, std::vector<std::string>& campos);

// MRZ del DG1 (etiqueta 5F1F), como texto. false si no está.
bool MrzDG1(const Bytes& dg1, std::string& mrz);

// Imagen dentro de un grupo (DG2: etiqueta 5F2E; DG7: 5F43), sin la cabecera biométrica que la
// precede. 'formato' = "jp2", "j2k", "jpg" o "" si no se reconoce. false si no está la etiqueta.
bool ImagenDeGrupo(const Bytes& grupo, unsigned tag, Bytes& imagen, std::string& formato);
