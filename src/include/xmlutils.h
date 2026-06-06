/*
 * xmlutils.h declares some useful xml helper function
 */

#ifndef FILEZILLA_XMLUTILS_HEADER
#define FILEZILLA_XMLUTILS_HEADER

#include "libfilezilla_engine.h"
#include "visibility.h"

#ifdef HAVE_LIBPUGIXML
#include <pugixml.hpp>
#else
#include "../pugixml/pugixml.hpp"
#endif

void FZC_PUBLIC_SYMBOL SetTextAttribute(pugi::xml_node node, char const* name, std::string const& value);
void FZC_PUBLIC_SYMBOL SetTextAttribute(pugi::xml_node node, char const* name, std::wstring const& value);
void FZC_PUBLIC_SYMBOL SetTextAttributeUtf8(pugi::xml_node node, char const* name, std::string const& utf8);
std::wstring FZC_PUBLIC_SYMBOL GetTextAttribute(pugi::xml_node node, char const* name);

int FZC_PUBLIC_SYMBOL GetAttributeInt(pugi::xml_node node, char const* name);
void FZC_PUBLIC_SYMBOL SetAttributeInt(pugi::xml_node node, char const* name, int value);

pugi::xml_node FZC_PUBLIC_SYMBOL FindElementWithAttribute(pugi::xml_node node, char const* element, char const* attribute, char const* value);

// Add a new child element with the specified name and value to the xml document
pugi::xml_node FZC_PUBLIC_SYMBOL AddTextElement(pugi::xml_node node, char const* name, std::string const& value, bool overwrite = false);
pugi::xml_node FZC_PUBLIC_SYMBOL AddTextElement(pugi::xml_node node, char const* name, std::wstring const& value, bool overwrite = false);
void FZC_PUBLIC_SYMBOL AddTextElement(pugi::xml_node node, char const* name, int64_t value, bool overwrite = false);
pugi::xml_node FZC_PUBLIC_SYMBOL AddTextElementUtf8(pugi::xml_node node, char const* name, std::string const& value, bool overwrite = false);

// Set the current element's text value
void FZC_PUBLIC_SYMBOL AddTextElement(pugi::xml_node node, std::string const& value);
void FZC_PUBLIC_SYMBOL AddTextElement(pugi::xml_node node, std::wstring const& value);
void FZC_PUBLIC_SYMBOL AddTextElement(pugi::xml_node node, int64_t value);
void FZC_PUBLIC_SYMBOL AddTextElementUtf8(pugi::xml_node node, std::string const& value);

// Get string from named child element
std::wstring FZC_PUBLIC_SYMBOL GetTextElement(pugi::xml_node node, const char* name);
std::wstring FZC_PUBLIC_SYMBOL GetTextElement_Trimmed(pugi::xml_node node, const char* name);

// Get string from current element
std::wstring FZC_PUBLIC_SYMBOL GetTextElement(pugi::xml_node node);
std::wstring FZC_PUBLIC_SYMBOL GetTextElement_Trimmed(pugi::xml_node node);

// Get (64-bit) integer from named element
int64_t FZC_PUBLIC_SYMBOL GetTextElementInt(pugi::xml_node node, const char* name, int defValue = 0);

bool FZC_PUBLIC_SYMBOL GetTextElementBool(pugi::xml_node node, const char* name, bool defValue = false);

#endif
