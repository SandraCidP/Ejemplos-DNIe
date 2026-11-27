// =================================================================================================
//  Operaciones.h  -  Lo que sabe hacer el anfitrión
// =================================================================================================
//  AtenderPeticion recibe la petición ya analizada (Json.h) y devuelve el JSON de la respuesta.
//  La lista de operaciones, con sus parámetros y respuestas, está al principio de Operaciones.cpp
//  y en docs\GUIA-DESARROLLADORES.md (apartado 8).
// =================================================================================================
#pragma once

#include "Json.h"
#include <string>

std::string AtenderPeticion(const ValorJson& peticion);

// Respuesta de fallo: {"correcto":false,"codigo":"...","error":"..."}
std::string RespuestaDeError(const char* codigo, const std::wstring& explicacion);
