// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Joseph C McGinty Jr.
#include "dict.h"
#include "vdict.h"
#include <vector>
#include <cstdlib>
#include <map>
#include <set>
#include <cstring>
#include <cctype>
namespace hl7dict {
static std::string up(std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }
static const std::map<std::string, const SegDef*>& segMap() { static std::map<std::string, const SegDef*> m; if (m.empty()) for (int i = 0; i < N_SEGMENTS; i++) m[SEGMENTS[i].id] = &SEGMENTS[i]; return m; }
static const std::map<std::string, const TypeDef*>& typeMap() { static std::map<std::string, const TypeDef*> m; if (m.empty()) for (int i = 0; i < N_DATATYPES; i++) m[DATATYPES[i].id] = &DATATYPES[i]; return m; }
static const std::map<std::string, const TableDef*>& tableMap() { static std::map<std::string, const TableDef*> m; if (m.empty()) for (int i = 0; i < N_TABLES; i++) m[TABLES[i].key] = &TABLES[i]; return m; }
static std::map<std::string, std::string> kvMap(const KV* a, int n) { std::map<std::string, std::string> m; for (int i = 0; i < n; i++) m[a[i].k] = a[i].v; return m; }
static const std::map<std::string, std::string>& codesysMap() { static auto m = kvMap(CODESYS, N_CODESYS); return m; }
static const std::map<std::string, std::string>& msgMap() { static auto m = kvMap(MSGTYPES, N_MSGTYPES); return m; }
static const std::map<std::string, std::string>& ftMap() { static auto m = kvMap(FIELDTYPES, N_FIELDTYPES); return m; }
static const std::set<std::string>& tsSet() { static std::set<std::string> s; if (s.empty()) for (int i = 0; i < N_TSFIELDS; i++) s.insert(TSFIELDS[i]); return s; }
const char* const VERSION_SOURCE = hl7v::VSOURCE;
static int g_ver = -1;
struct CustomSeg { std::string desc; std::vector<CustomField> f; };
static std::map<std::string, CustomSeg>& custom() { static std::map<std::string, CustomSeg> m; return m; }
void clearCustom() { custom().clear(); }
void defineCustom(const std::string& id, const std::string& desc, const std::vector<CustomField>& f) { custom()[up(id)] = { desc, f }; }
bool isCustom(const std::string& id) { return custom().count(up(id)) > 0; }
int nCustom() { return (int)custom().size(); }
static const CustomSeg* cseg(const std::string& id) { auto& m = custom(); auto it = m.find(up(id)); return it == m.end() ? nullptr : &it->second; }
static std::vector<int> vparts(const std::string& v) { std::vector<int> p; size_t i = 0; while (i < v.size() && p.size() < 4) { if (!isdigit((unsigned char)v[i])) return {}; int n = 0; while (i < v.size() && isdigit((unsigned char)v[i])) n = n * 10 + (v[i++] - '0'); p.push_back(n); if (i < v.size()) { if (v[i] != '.') break; i++; } } return p; }
static int verIndex(const std::string& msh12) {
    std::string v = msh12.substr(0, msh12.find_first_of("^&~ ")); std::vector<int> want = vparts(v); if (want.empty() || want[0] != 2) return -1;
    int best = 0; for (int i = 0; i < hl7v::nVersions(); i++) { std::vector<int> have = vparts(hl7v::versions()[i].name); if (have == want) return i; if (have <= want) best = i; }
    return best;                                                     // newest definitions not newer than the message (2.8+ -> 2.7.1, 2.0 -> 2.1)
}
std::string resolveVersion(const std::string& msh12) { int i = verIndex(msh12); return i < 0 ? "" : hl7v::versions()[i].name; }
std::string setVersion(const std::string& msh12) { g_ver = verIndex(msh12); return activeVersion(); }
std::string activeVersion() { return g_ver < 0 ? "" : hl7v::versions()[g_ver].name; }
VersionScope::VersionScope(const std::string& msh12) : prev(g_ver) { g_ver = verIndex(msh12); }
VersionScope::~VersionScope() { g_ver = prev; }
static const hl7v::VEntry* ventry(bool segs, const std::string& id) {
    if (g_ver < 0) return nullptr; static std::vector<std::map<std::string, const hl7v::VEntry*>> cache[2];
    auto& c = cache[segs ? 0 : 1]; if (c.empty()) c.resize(hl7v::nVersions()); auto& m = c[g_ver];
    if (m.empty()) { const hl7v::VVersion& V = hl7v::versions()[g_ver]; const hl7v::VEntry* a = segs ? V.segs : V.types; int n = segs ? V.nSegs : V.nTypes; for (int i = 0; i < n; i++) m[hl7v::str(a[i].id)] = &a[i]; }
    auto it = m.find(up(id)); return it == m.end() ? nullptr : it->second;
}
static const hl7v::VItem* vitem(bool segs, const std::string& id, int pos) { const hl7v::VEntry* e = ventry(segs, id); if (!e || pos < 1 || pos > e->n) return nullptr; return &hl7v::items()[e->first + pos - 1]; }
static const CustomField* cfield(const std::string& sid, int pos) { const CustomSeg* c = cseg(sid); if (!c || pos < 1 || pos > (int)c->f.size()) return nullptr; return &c->f[pos - 1]; }
bool segKnown(const std::string& id) { return cseg(id) || ventry(true, id) || seg(id); }
int fieldCount(const std::string& id) { if (auto c = cseg(id)) return (int)c->f.size(); if (auto e = ventry(true, id)) return e->n; if (auto s = seg(id)) return s->nFields; return -1; }
int fieldRepeat(const std::string& id, int pos) { if (auto f = cfield(id, pos)) return f->rep; if (auto v = vitem(true, id, pos)) return v->rep; return -1; }
int fieldTable(const std::string& id, int pos) { if (cseg(id)) return 0; if (auto v = vitem(true, id, pos)) return v->table; return 0; }
const SegDef* seg(const std::string& id) { auto& m = segMap(); auto it = m.find(up(id)); return it == m.end() ? nullptr : it->second; }
std::string segDesc(const std::string& id) { if (auto c = cseg(id)) return c->desc.empty() ? "Custom segment" : c->desc; if (auto e = ventry(true, id)) return hl7v::str(e->desc); const SegDef* s = seg(id); return s ? s->desc : ""; }
static const FieldDef* fdef(const std::string& sid, int pos) { const SegDef* s = seg(sid); if (!s || pos < 1 || pos > s->nFields) return nullptr; return &s->fields[pos - 1]; }
bool fieldKnown(const std::string& sid, int pos) { if (cseg(sid)) return cfield(sid, pos) != nullptr; if (ventry(true, sid)) return vitem(true, sid, pos) != nullptr; return fdef(sid, pos) != nullptr; }
std::string fieldName(const std::string& sid, int pos) { if (cseg(sid)) { auto c = cfield(sid, pos); return c && !c->name.empty() ? c->name : "Field " + std::to_string(pos); } if (ventry(true, sid)) { auto v = vitem(true, sid, pos); return v && *hl7v::str(v->name) ? hl7v::str(v->name) : "Field " + std::to_string(pos); } const FieldDef* f = fdef(sid, pos); return f ? f->name : "Field " + std::to_string(pos); }
std::string fieldType(const std::string& sid, int pos) { if (cseg(sid)) { auto c = cfield(sid, pos); return c && !c->type.empty() ? up(c->type) : "ST"; } if (ventry(true, sid)) { auto v = vitem(true, sid, pos); return v && *hl7v::str(v->type) ? hl7v::str(v->type) : "ST"; } auto& m = ftMap(); auto it = m.find(up(sid) + "-" + std::to_string(pos)); if (it != m.end()) return it->second; const FieldDef* f = fdef(sid, pos); return f ? f->type : "ST"; }
char fieldFlag(const std::string& sid, int pos) { if (cseg(sid)) return ' '; if (g_ver >= 0 && hl7v::versions()[g_ver].name < std::string("2.5")) return ' '; const FieldDef* f = fdef(sid, pos); return f ? f->flag : ' '; }   // "backward compatible" marks start with 2.5
const TypeDef* type(const std::string& id) { auto& m = typeMap(); auto it = m.find(up(id)); return it == m.end() ? nullptr : it->second; }
int compCount(const std::string& t) { if (auto e = ventry(false, t)) return e->n; const TypeDef* d = type(t); return d ? d->nComps : 0; }
std::string componentName(const std::string& t, int idx) { if (ventry(false, t)) { auto v = vitem(false, t, idx); return v && *hl7v::str(v->name) ? hl7v::str(v->name) : "Component " + std::to_string(idx); } const TypeDef* d = type(t); if (!d || idx < 1 || idx > d->nComps) return "Component " + std::to_string(idx); return d->comps[idx - 1].name; }
std::string componentType(const std::string& t, int idx) { if (ventry(false, t)) { auto v = vitem(false, t, idx); return v && *hl7v::str(v->type) ? hl7v::str(v->type) : "ST"; } const TypeDef* d = type(t); if (!d || idx < 1 || idx > d->nComps) return "ST"; return d->comps[idx - 1].type; }
bool hasTable(const std::string& table) { return tableMap().count(table) > 0; }
std::string tableLabel(const std::string& table, const std::string& code) { auto& m = tableMap(); auto it = m.find(table); if (it == m.end()) return ""; for (int i = 0; i < it->second->n; i++) if (code == it->second->entries[i].k) return it->second->entries[i].v; return ""; }
std::string codesysLabel(const std::string& code) { auto& m = codesysMap(); auto it = m.find(code); return it == m.end() ? "" : it->second; }
std::string msgTypeDesc(const std::string& t) {
    auto& m = msgMap(); auto it = m.find(t); if (it != m.end()) return it->second;
    size_t a = t.find('^'); if (a != std::string::npos) { size_t b = t.find('^', a + 1); std::string two = b == std::string::npos ? t : t.substr(0, b); it = m.find(two); if (it != m.end()) return it->second; it = m.find(t.substr(0, a)); if (it != m.end()) return it->second; }
    return "";
}
bool isTsField(const std::string& sid, int pos) { return tsSet().count(up(sid) + "-" + std::to_string(pos)) > 0; }
}
