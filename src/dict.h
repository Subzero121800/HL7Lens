// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Joseph C McGinty Jr.
// HL7 v2 dictionary (segment/field names, data types, tables, message types) - data generated into dict_gen.cpp.
#pragma once
#include <string>
#include <vector>
namespace hl7dict {
struct FieldDef { const char* name; const char* type; char flag; };           // flag 'B' = backward compatibility only
struct SegDef { const char* id; const char* desc; const FieldDef* fields; int nFields; int max23; const char* only; bool partial; const int* n23pos; const char* const* n23name; int n23; };
struct CompDef { const char* name; const char* type; };
struct TypeDef { const char* id; const CompDef* comps; int nComps; int v23Count; const CompDef* v23comps; int nV23; };
struct KV { const char* k; const char* v; };
struct TableDef { const char* key; const KV* entries; int n; };
extern const SegDef SEGMENTS[]; extern const int N_SEGMENTS;
extern const TypeDef DATATYPES[]; extern const int N_DATATYPES;
extern const TableDef TABLES[]; extern const int N_TABLES;
extern const KV CODESYS[]; extern const int N_CODESYS;
extern const KV MSGTYPES[]; extern const int N_MSGTYPES;
extern const KV FIELDTYPES[]; extern const int N_FIELDTYPES;
extern const char* const TSFIELDS[]; extern const int N_TSFIELDS;
extern const char* const SOURCE;
// lookups (all case-insensitive on segment / type ids; "" when unknown)
const SegDef* seg(const std::string& id);
std::string segDesc(const std::string& id);
std::string fieldName(const std::string& seg, int pos);           // "Field n" when unknown
bool fieldKnown(const std::string& seg, int pos);
std::string fieldType(const std::string& seg, int pos);           // "ST" when unknown
char fieldFlag(const std::string& seg, int pos);
const TypeDef* type(const std::string& id);
std::string componentName(const std::string& type, int idx);      // "Component n" when unknown
std::string componentType(const std::string& type, int idx);
std::string tableLabel(const std::string& table, const std::string& code);   // "" when unknown
bool hasTable(const std::string& table);
std::string codesysLabel(const std::string& code);
std::string msgTypeDesc(const std::string& type);                 // tries full, first two components, first component
bool isTsField(const std::string& seg, int pos);

// ---- context: message version (MSH-12) and user-defined segments.  Lookups above consult, in order,
//      custom segment definitions, then the active version's definitions (2.1 .. 2.7.1), then the base dictionary.
std::string setVersion(const std::string& msh12);                // returns the version actually used ("" = base dictionary)
std::string activeVersion();
std::string resolveVersion(const std::string& msh12);            // "2.8" -> "2.7.1", "2.3.1" -> "2.3.1", "" -> ""
struct VersionScope { int prev; explicit VersionScope(const std::string& msh12); ~VersionScope(); };
bool segKnown(const std::string& seg);                           // in custom, active version or base dictionary
int fieldCount(const std::string& seg);                          // fields defined for the segment, -1 when unknown
int fieldRepeat(const std::string& seg, int pos);                // -1 unknown, 0 repeats without limit, 1 single, n up to n
int fieldTable(const std::string& seg, int pos);                 // HL7 table number, 0 when none
int compCount(const std::string& type);                          // components of a data type in the active version, 0 unknown
struct CustomField { std::string name, type; int rep = 1; };
void clearCustom();
void defineCustom(const std::string& seg, const std::string& desc, const std::vector<CustomField>& fields);
bool isCustom(const std::string& seg);
int nCustom();
extern const char* const VERSION_SOURCE;
}
