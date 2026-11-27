// =================================================================================================
//  Json.h  -  JSON mínimo: leer la petición y escribir la respuesta
// =================================================================================================
//  El navegador y el anfitrión se pasan mensajes en JSON (texto UTF-8). No se usa ninguna
//  biblioteca: basta con un lector pequeño para la petición y unas funciones para montar la
//  respuesta.
//
//  LEER   AnalizarJson(texto, valor) convierte el texto en un ValorJson. Después:
//           valor.Texto("operacion")       -> el campo de texto "operacion" ("" si no está)
//           valor.Campo("que")             -> el campo "que" (NULL si no está)
//
//  ESCRIBIR  ObjetoJson monta un objeto campo a campo:
//           ObjetoJson r;
//           r.Bool("correcto", true).Texto("version", "4.0");
//           std::string json = r.Json();   // {"correcto":true,"version":"4.0"}
//
//  Todos los textos van en UTF-8 (std::string). Para los de Windows (UTF-16, std::wstring) están
//  AUtf8 y DeUtf8.
// =================================================================================================
#pragma once

#include <string>
#include <vector>

typedef std::vector<unsigned char> Bytes;

// Un valor JSON cualquiera.
struct ValorJson
{
	enum Tipo { Nulo, Logico, Numero, Cadena, Lista, Objeto };

	Tipo                                            tipo = Nulo;
	bool                                            logico = false;
	double                                          numero = 0;
	std::string                                     texto;      // Cadena (UTF-8)
	std::vector<ValorJson>                          elementos;  // Lista
	std::vector<std::pair<std::string, ValorJson>>  campos;     // Objeto

	// Campo de un objeto (NULL si no es un objeto o no tiene ese campo).
	const ValorJson* Campo(const char* nombre) const;
	// Campo de texto ("" si no está o no es texto).
	std::string Texto(const char* nombre) const;
	// ¿Tiene ese campo y es de texto?
	bool TieneTexto(const char* nombre) const;
};

// Analiza un texto JSON. Devuelve false si no es JSON válido (o anida más de 16 niveles).
bool AnalizarJson(const std::string& texto, ValorJson& valor);

// Objeto JSON que se va escribiendo campo a campo.
class ObjetoJson
{
public:
	ObjetoJson& Texto(const char* nombre, const std::string& utf8);
	ObjetoJson& Texto(const char* nombre, const std::wstring& texto);
	ObjetoJson& Bool(const char* nombre, bool valor);
	ObjetoJson& Entero(const char* nombre, long long valor);
	ObjetoJson& Base64(const char* nombre, const Bytes& datos);   // datos binarios, en Base64
	ObjetoJson& Crudo(const char* nombre, const std::string& json); // un valor ya escrito (objeto, lista)
	std::string Json() const { return "{" + m_json + "}"; }
private:
	void Nombre(const char* nombre);
	std::string m_json;
};

// Lista JSON de valores ya escritos: ["...", {...}]
std::string ListaJson(const std::vector<std::string>& valoresJson);

// Texto -> valor JSON entre comillas, con los caracteres especiales escapados.
std::string TextoJson(const std::string& utf8);

// Conversiones de texto
std::string  AUtf8(const std::wstring& texto);
std::wstring DeUtf8(const std::string& utf8);
// Texto leído de la tarjeta: si es UTF-8 válido se deja igual; si no, se interpreta como Latin-1.
// (Un JSON con UTF-8 inválido haría que el navegador cerrase la conexión.)
std::string  TextoSeguro(const std::string& bytes);

// Base64 (el formato habitual para meter datos binarios en un JSON)
std::string  ABase64(const Bytes& datos);
bool         DeBase64(const std::string& base64, Bytes& datos);
