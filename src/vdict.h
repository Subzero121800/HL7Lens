// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Joseph C McGinty Jr.
// Per-version HL7 v2 definitions (2.1 .. 2.7.1), generated into vdict_gen.cpp by tools/gen_vdict.py.
// Source data: hl7-dictionary (npm), MIT License, Copyright (c) 2015 Fernando Serrano.
#pragma once
namespace hl7v {
struct VItem { int name, type, rep, table; };          // string-pool indexes; rep 0 = unbounded, 1 = single, n = max n
struct VEntry { int id, desc, first, n; };             // segment or data type: items[first .. first+n)
struct VVersion { const char* name; const VEntry* segs; int nSegs; const VEntry* types; int nTypes; };
const VVersion* versions(); int nVersions();
const char* str(int i); const VItem* items();
extern const char* const VSOURCE;
}
