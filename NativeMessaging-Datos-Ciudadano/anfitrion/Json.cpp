// =================================================================================================
//  Json.cpp  -  JSON mínimo (ver Json.h)
// =================================================================================================
#include "stdafx.h"
#include "Json.h"

#include <wincrypt.h>    // CryptBinaryToStringA / CryptStringToBinaryA (Base64)
#include <cstdio>
#include <cstdlib>
#pragma comment(lib, "crypt32.lib")

// -------------------------------------------------------------------------------------------------
//  Leer
// -------------------------------------------------------------------------------------------------
// Analizador "descendente recursivo": una función por cada tipo de valor. Cada una mira el carácter
// en el que está (p) y avanza hasta el final de su valor. Ante cualquier cosa inesperada devuelve
// false, y la petición se rechaza entera.

namespace
{
	const int PROFUNDIDAD_MAXIMA = 16;

	class Lector
	{
	public:
		explicit Lector(const std::string& t) : m_t(t), m_p(0) {}

		bool Documento(ValorJson& v)
		{
			if (!Valor(v, 0))
				return false;
			Espacios();
			return m_p == m_t.size();          // no puede sobrar nada detrás
		}

	private:
		const std::string& m_t;
		size_t             m_p;

		void Espacios()
		{
			while (m_p < m_t.size() && (m_t[m_p] == ' ' || m_t[m_p] == '\t' || m_t[m_p] == '\r' || m_t[m_p] == '\n'))
				m_p++;
		}

		bool Literal(const char* palabra)
		{
			size_t n = strlen(palabra);
			if (m_t.compare(m_p, n, palabra) != 0)
				return false;
			m_p += n;
			return true;
		}

		bool Valor(ValorJson& v, int nivel)
		{
			if (nivel > PROFUNDIDAD_MAXIMA)
				return false;
			Espacios();
			if (m_p >= m_t.size())
				return false;
			char c = m_t[m_p];
			if (c == '{') return Objeto(v, nivel);
			if (c == '[') return Lista(v, nivel);
			if (c == '"') { v.tipo = ValorJson::Cadena; return Cadena(v.texto); }
			if (c == 't') { v.tipo = ValorJson::Logico; v.logico = true;  return Literal("true"); }
			if (c == 'f') { v.tipo = ValorJson::Logico; v.logico = false; return Literal("false"); }
			if (c == 'n') { v.tipo = ValorJson::Nulo; return Literal("null"); }
			return Numero(v);
		}

		bool Objeto(ValorJson& v, int nivel)
		{
			v.tipo = ValorJson::Objeto;
			m_p++;                              // '{'
			Espacios();
			if (m_p < m_t.size() && m_t[m_p] == '}') { m_p++; return true; }
			for (;;)
			{
				Espacios();
				std::string nombre;
				if (m_p >= m_t.size() || m_t[m_p] != '"' || !Cadena(nombre))
					return false;
				Espacios();
				if (m_p >= m_t.size() || m_t[m_p] != ':')
					return false;
				m_p++;
				ValorJson hijo;
				if (!Valor(hijo, nivel + 1))
					return false;
				v.campos.emplace_back(nombre, std::move(hijo));
				Espacios();
				if (m_p >= m_t.size())
					return false;
				if (m_t[m_p] == ',') { m_p++; continue; }
				if (m_t[m_p] == '}') { m_p++; return true; }
				return false;
			}
		}

		bool Lista(ValorJson& v, int nivel)
		{
			v.tipo = ValorJson::Lista;
			m_p++;                              // '['
			Espacios();
			if (m_p < m_t.size() && m_t[m_p] == ']') { m_p++; return true; }
			for (;;)
			{
				ValorJson hijo;
				if (!Valor(hijo, nivel + 1))
					return false;
				v.elementos.push_back(std::move(hijo));
				Espacios();
				if (m_p >= m_t.size())
					return false;
				if (m_t[m_p] == ',') { m_p++; continue; }
				if (m_t[m_p] == ']') { m_p++; return true; }
				return false;
			}
		}

		bool Numero(ValorJson& v)
		{
			size_t ini = m_p;
			while (m_p < m_t.size() && strchr("+-0123456789.eE", m_t[m_p]))
				m_p++;
			if (m_p == ini)
				return false;
			std::string s = m_t.substr(ini, m_p - ini);
			char* fin = NULL;
			v.tipo = ValorJson::Numero;
			v.numero = strtod(s.c_str(), &fin);
			return fin && *fin == 0;
		}

		static int Hex(char c)
		{
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		}

		bool Cuatro(unsigned& u)
		{
			if (m_p + 4 > m_t.size())
				return false;
			u = 0;
			for (int i = 0; i < 4; i++)
			{
				int h = Hex(m_t[m_p++]);
				if (h < 0)
					return false;
				u = (u << 4) | (unsigned)h;
			}
			return true;
		}

		static void AnadirUtf8(std::string& s, unsigned cp)
		{
			if (cp < 0x80) s += (char)cp;
			else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
			else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
			else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
		}

		// Cadena entre comillas. Las secuencias \n, \", é... se convierten en sus caracteres;
		// 😀 (pares "sustitutos" de UTF-16) se juntan en un solo carácter.
		bool Cadena(std::string& s)
		{
			s.clear();
			m_p++;                              // '"'
			while (m_p < m_t.size())
			{
				char c = m_t[m_p++];
				if (c == '"')
					return true;
				if ((unsigned char)c < 0x20)
					return false;               // los caracteres de control tienen que ir escapados
				if (c != '\\') { s += c; continue; }
				if (m_p >= m_t.size())
					return false;
				char e = m_t[m_p++];
				switch (e)
				{
				case '"':  s += '"';  break;
				case '\\': s += '\\'; break;
				case '/':  s += '/';  break;
				case 'b':  s += '\b'; break;
				case 'f':  s += '\f'; break;
				case 'n':  s += '\n'; break;
				case 'r':  s += '\r'; break;
				case 't':  s += '\t'; break;
				case 'u':
				{
					unsigned u;
					if (!Cuatro(u))
						return false;
					if (u >= 0xD800 && u <= 0xDBFF)
					{
						unsigned b;
						if (m_p + 2 > m_t.size() || m_t[m_p] != '\\' || m_t[m_p + 1] != 'u')
							return false;
						m_p += 2;
						if (!Cuatro(b) || b < 0xDC00 || b > 0xDFFF)
							return false;
						u = 0x10000 + ((u - 0xD800) << 10) + (b - 0xDC00);
					}
					else if (u >= 0xDC00 && u <= 0xDFFF)
						return false;
					AnadirUtf8(s, u);
					break;
				}
				default:
					return false;
				}
			}
			return false;                       // falta la comilla final
		}
	};
}

bool AnalizarJson(const std::string& texto, ValorJson& valor)
{
	valor = ValorJson();
	Lector l(texto);
	return l.Documento(valor);
}

const ValorJson* ValorJson::Campo(const char* nombre) const
{
	if (tipo != Objeto)
		return NULL;
	for (const auto& c : campos)
		if (c.first == nombre)
			return &c.second;
	return NULL;
}

std::string ValorJson::Texto(const char* nombre) const
{
	const ValorJson* c = Campo(nombre);
	return (c && c->tipo == Cadena) ? c->texto : std::string();
}

bool ValorJson::TieneTexto(const char* nombre) const
{
	const ValorJson* c = Campo(nombre);
	return c && c->tipo == Cadena;
}

// -------------------------------------------------------------------------------------------------
//  Escribir
// -------------------------------------------------------------------------------------------------

// Entre comillas y con los caracteres especiales escapados. Sin esto, un nombre de archivo o un
// domicilio con comillas rompería la respuesta.
std::string TextoJson(const std::string& utf8)
{
	std::string r = "\"";
	for (unsigned char c : utf8)
	{
		if (c == '"' || c == '\\') { r += '\\'; r += (char)c; }
		else if (c < 0x20)
		{
			char codigo[8];
			sprintf_s(codigo, "\\u%04x", c);
			r += codigo;
		}
		else
			r += (char)c;
	}
	return r + "\"";
}

void ObjetoJson::Nombre(const char* nombre)
{
	if (!m_json.empty())
		m_json += ',';
	m_json += TextoJson(nombre) + ":";
}

ObjetoJson& ObjetoJson::Texto(const char* nombre, const std::string& utf8) { Nombre(nombre); m_json += TextoJson(utf8); return *this; }
ObjetoJson& ObjetoJson::Texto(const char* nombre, const std::wstring& texto) { return Texto(nombre, AUtf8(texto)); }
ObjetoJson& ObjetoJson::Bool(const char* nombre, bool valor) { Nombre(nombre); m_json += valor ? "true" : "false"; return *this; }
ObjetoJson& ObjetoJson::Entero(const char* nombre, long long valor) { Nombre(nombre); m_json += std::to_string(valor); return *this; }
ObjetoJson& ObjetoJson::Base64(const char* nombre, const Bytes& datos) { Nombre(nombre); m_json += TextoJson(ABase64(datos)); return *this; }
ObjetoJson& ObjetoJson::Crudo(const char* nombre, const std::string& json) { Nombre(nombre); m_json += json; return *this; }

std::string ListaJson(const std::vector<std::string>& valoresJson)
{
	std::string r = "[";
	for (size_t i = 0; i < valoresJson.size(); i++)
		r += (i ? "," : "") + valoresJson[i];
	return r + "]";
}

// -------------------------------------------------------------------------------------------------
//  Conversiones
// -------------------------------------------------------------------------------------------------

// Windows trabaja con texto UTF-16 (std::wstring); el navegador, con UTF-8. Primera llamada: cuántos
// bytes (o caracteres) hacen falta. Segunda: convertir.
std::string AUtf8(const std::wstring& texto)
{
	if (texto.empty())
		return std::string();
	int n = WideCharToMultiByte(CP_UTF8, 0, texto.data(), (int)texto.size(), NULL, 0, NULL, NULL);
	std::string r(n > 0 ? n : 0, '\0');
	if (n > 0)
		WideCharToMultiByte(CP_UTF8, 0, texto.data(), (int)texto.size(), &r[0], n, NULL, NULL);
	return r;
}

std::wstring DeUtf8(const std::string& utf8)
{
	if (utf8.empty())
		return std::wstring();
	int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), NULL, 0);
	std::wstring r(n > 0 ? n : 0, L'\0');
	if (n > 0)
		MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &r[0], n);
	return r;
}

std::string TextoSeguro(const std::string& bytes)
{
	// MB_ERR_INVALID_CHARS hace que la conversión falle si hay secuencias UTF-8 inválidas.
	if (bytes.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), (int)bytes.size(), NULL, 0) > 0)
		return bytes;
	// Latin-1 (ISO-8859-1, página de códigos 28591): cada byte es un carácter.
	int n = MultiByteToWideChar(28591, 0, bytes.data(), (int)bytes.size(), NULL, 0);
	std::wstring w(n > 0 ? n : 0, L'\0');
	if (n > 0)
		MultiByteToWideChar(28591, 0, bytes.data(), (int)bytes.size(), &w[0], n);
	return AUtf8(w);
}

// CRYPT_STRING_NOCRLF: todo seguido, sin saltos de línea.
std::string ABase64(const Bytes& datos)
{
	if (datos.empty())
		return std::string();
	const DWORD formato = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
	DWORD n = 0;
	if (!CryptBinaryToStringA(datos.data(), (DWORD)datos.size(), formato, NULL, &n))
		return std::string();
	std::string r(n, '\0');
	if (!CryptBinaryToStringA(datos.data(), (DWORD)datos.size(), formato, &r[0], &n))
		return std::string();
	r.resize(n);                                // sin el cero final
	return r;
}

bool DeBase64(const std::string& base64, Bytes& datos)
{
	datos.clear();
	if (base64.empty())
		return false;
	DWORD n = 0;
	if (!CryptStringToBinaryA(base64.data(), (DWORD)base64.size(), CRYPT_STRING_BASE64, NULL, &n, NULL, NULL))
		return false;
	datos.resize(n);
	if (!CryptStringToBinaryA(base64.data(), (DWORD)base64.size(), CRYPT_STRING_BASE64, datos.data(), &n, NULL, NULL))
		return false;
	datos.resize(n);
	return true;
}
