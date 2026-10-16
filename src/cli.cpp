// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Joseph C McGinty Jr.
// hl7_cli - command-line front end of the HL7 core (used by the tests; also handy for batch work).
#include "hl7.h"
#include "dict.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace hl7;
static std::string readInput(const std::string& p) { if (p == "-") { std::ostringstream ss; ss << std::cin.rdbuf(); return ss.str(); } std::ifstream f(p, std::ios::binary); std::ostringstream ss; ss << f.rdbuf(); return ss.str(); }
static std::string js(const std::string& s) { return "\"" + jsonEscape(s) + "\""; }
static std::string jsarr(const std::vector<std::string>& v) { std::string o = "["; for (size_t i = 0; i < v.size(); i++) { if (i) o += ","; o += js(v[i]); } return o + "]"; }
static std::string jsIssues(const std::vector<Issue>& v) { std::string o = "["; for (size_t i = 0; i < v.size(); i++) { if (i) o += ","; o += "{\"level\":" + js(v[i].level) + ",\"pos\":" + js(v[i].pos) + ",\"text\":" + js(v[i].text) + "}"; } return o + "]"; }
static std::string jsMessage(const Message& m) {
    std::string o = "{\"version\":" + js(m.version) + ",\"enc\":{\"fs\":" + js(std::string(1, m.enc.fs)) + ",\"comp\":" + js(std::string(1, m.enc.comp)) + ",\"rep\":" + js(std::string(1, m.enc.rep)) + ",\"esc\":" + js(std::string(1, m.enc.esc)) + ",\"sub\":" + js(std::string(1, m.enc.sub)) + "},\"notes\":[";
    for (size_t i = 0; i < m.notes.size(); i++) { if (i) o += ","; o += "{\"level\":" + js(m.notes[i].level) + ",\"text\":" + js(m.notes[i].text) + "}"; }
    o += "],\"segments\":[";
    for (size_t si = 0; si < m.segments.size(); si++) {
        const Segment& s = m.segments[si]; if (si) o += ","; o += "{\"id\":" + js(s.id) + ",\"raw\":" + js(s.raw) + ",\"desc\":" + js(hl7dict::segDesc(s.id)) + ",\"fields\":[";
        for (size_t i = 0; i < s.fields.size(); i++) {
            const Field& f = s.fields[i]; if (i) o += ","; o += "{\"value\":" + js(f.value) + ",\"reps\":[";
            for (size_t r = 0; r < f.reps.size(); r++) { if (r) o += ","; o += "["; for (size_t c = 0; c < f.reps[r].size(); c++) { if (c) o += ","; o += jsarr(f.reps[r][c]); } o += "]"; }
            o += "],\"name\":" + js(hl7dict::fieldName(s.id, (int)i + 1)) + ",\"type\":" + js(hl7dict::fieldType(s.id, (int)i + 1)) + ",\"meaning\":" + js(fieldMeaning(s, (int)i + 1)) + "}";
        }
        o += "]}";
    }
    o += "],\"issues\":" + jsIssues(validateStructural(m));
    VersionReport vr = versionReport(m); o += ",\"version_report\":{\"declared\":" + js(vr.declared) + ",\"inferred\":[" + std::to_string(vr.inferred[0]) + "," + std::to_string(vr.inferred[1]) + "," + std::to_string(vr.inferred[2]) + "],\"feats\":[";
    for (size_t i = 0; i < vr.feats.size(); i++) { if (i) o += ","; o += "[" + js(vr.feats[i].first) + "," + js(vr.feats[i].second) + "]"; }
    o += "],\"used\":" + jsarr(vr.used) + ",\"notes\":" + jsIssues(vr.notes) + ",\"style\":" + js(vr.style) + ",\"content\":" + js(vr.content) + "},\"elr\":[";
    auto elr = elrChecks(m); for (size_t i = 0; i < elr.size(); i++) { if (i) o += ","; o += std::string("{\"ok\":") + (elr[i].ok ? "true" : "false") + ",\"text\":" + js(elr[i].text) + "}"; }
    return o + "]}";
}
int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: hl7_cli json|locate|pretty|repair|escape|unescape|ack|deid|summary|messages|validate <file|-> [args]\n"); return 2; }
    std::string cmd = argv[1], text = readInput(argv[2]); std::string out;
    if (cmd == "diff") { auto a = parseAll(text), b = parseAll(readInput(argv[3])); out = a.empty() || b.empty() ? "need one message in each file\n" : diffReport(a[0], b[0], argv[2], argv[3]); }
    else if (cmd == "vcheck") { for (auto& m : parseAll(text)) for (auto& i : versionFieldChecks(m)) out += i.level + "\t" + i.pos + "\t" + i.text + "\n"; }
    else if (cmd == "vlocate") { auto ms = parseAll(text); hl7dict::VersionScope vs(ms.empty() ? "" : ms[0].version); Pos p = locate(text, (size_t)atol(argc > 3 ? argv[3] : "0")); out = p.path + "\t" + p.fieldName + "\t" + p.fieldType + "\t" + p.compName + "\t" + p.compType + "\t" + std::to_string(p.nreps) + "\t" + hl7dict::activeVersion() + "\n"; }
    else if (cmd == "json") {
        std::string stamp = argc > 3 ? argv[3] : ""; auto msgs = parseAll(text); out = "{\"messages\":[";
        for (size_t i = 0; i < msgs.size(); i++) { if (i) out += ","; out += jsMessage(msgs[i]); }
        out += "],\"split\":" + jsarr(splitMessages(text)) + ",\"ack\":" + js(buildAck(text, "AA", "OK", stamp)) + ",\"deid\":" + js(deidentify(text, true, true)) + "}";
    } else if (cmd == "locate") {
        Pos p = locate(text, (size_t)atol(argc > 3 ? argv[3] : "0"));
        out = std::string("{\"ok\":") + (p.ok ? "true" : "false") + ",\"segId\":" + js(p.segId) + ",\"line\":" + std::to_string(p.line) + ",\"field\":" + std::to_string(p.field) + ",\"rep\":" + std::to_string(p.rep) + ",\"comp\":" + std::to_string(p.comp) + ",\"sub\":" + std::to_string(p.sub) + ",\"path\":" + js(p.path) + ",\"fieldName\":" + js(p.fieldName) + ",\"fieldType\":" + js(p.fieldType) + ",\"compName\":" + js(p.compName) + ",\"subName\":" + js(p.subName) + ",\"label\":" + js(p.label) + ",\"fieldText\":" + js(p.fieldText) + ",\"compText\":" + js(p.compText) + ",\"subText\":" + js(p.subText) + ",\"meaning\":" + js(p.meaning) + ",\"fieldStart\":" + std::to_string(p.fieldStart) + ",\"fieldEnd\":" + std::to_string(p.fieldEnd) + "}";
    } else if (cmd == "pretty") { for (auto& m : parseAll(text)) out += prettyPrint(m, true) + "\n"; }
    else if (cmd == "repair") { Repair r = repair(text, argc > 3 && strstr(argv[3], "zero"), argc > 3 && strstr(argv[3], "trail")); out = "{\"repaired\":" + js(r.repaired) + ",\"changed\":" + (r.changed ? "true" : "false") + ",\"changes\":["; for (size_t i = 0; i < r.changes.size(); i++) { if (i) out += ","; out += "[" + js(r.changes[i].first) + "," + js(r.changes[i].second) + "]"; } out += "]}"; }
    else if (cmd == "escape") out = escapeText(text, Enc());
    else if (cmd == "unescape") out = unescape(text, Enc());
    else if (cmd == "ack") out = buildAck(text, argc > 3 ? argv[3] : "AA", argc > 4 ? argv[4] : "", argc > 5 ? argv[5] : "");
    else if (cmd == "deid") out = deidentify(text, argc > 3 && strstr(argv[3], "order"), argc > 3 && strstr(argv[3], "text"));
    else if (cmd == "summary") { auto m = parseAll(text); out = "["; if (!m.empty()) { auto s = summary(m[0]); for (size_t i = 0; i < s.size(); i++) { if (i) out += ","; out += "[" + js(s[i].first) + "," + js(s[i].second) + "]"; } } out += "]"; }
    else if (cmd == "messages") { auto st = messageStarts(text); out = "["; for (size_t i = 0; i < st.size(); i++) { if (i) out += ","; out += std::to_string(st[i]); } out += "]"; }
    else if (cmd == "validate") { for (auto& m : parseAll(text)) { for (auto& i : validateStructural(m)) out += i.level + "\t" + i.pos + "\t" + i.text + "\n"; for (auto& i : versionReport(m).notes) out += i.level + "\t" + i.pos + "\t" + i.text + "\n"; } }
    else if (cmd == "scan") { auto ms = scanDocument(text); out = "["; for (size_t i = 0; i < ms.size(); i++) { if (i) out += ","; out += "{\"a\":" + std::to_string(ms[i].s.a) + ",\"b\":" + std::to_string(ms[i].s.b) + ",\"skipped\":" + std::to_string(ms[i].skippedLines) + ",\"segs\":["; for (size_t k = 0; k < ms[i].segs.size(); k++) { if (k) out += ","; out += "[" + js(ms[i].segs[k].id) + "," + std::to_string(ms[i].segs[k].fields.size()) + "]"; } out += "]}"; } out += "]"; }
    else if (cmd == "resolve") { auto ms = scanDocument(text); int mi = argc > 4 ? atoi(argv[4]) : 0; Span sp; std::string n; bool ok = mi < (int)ms.size() && resolvePath(ms[mi], argc > 3 ? argv[3] : "", sp, &n); out = ok ? "{\"ok\":true,\"path\":" + js(n) + ",\"text\":" + js(text.substr(sp.a, sp.b - sp.a)) + "}" : "{\"ok\":false}"; }
    else if (cmd == "occ") { auto ms = scanDocument(text); auto v = occurrences(ms, argv[3], atoi(argv[4]), argc > 5 ? atoi(argv[5]) : 0, argc > 6 ? atoi(argv[6]) : 0); std::vector<std::string> vals; for (auto& s : v) vals.push_back(text.substr(s.a, s.b - s.a)); out = jsarr(vals); }
    else { fprintf(stderr, "unknown command %s\n", cmd.c_str()); return 2; }
    fwrite(out.data(), 1, out.size(), stdout); return 0;
}
