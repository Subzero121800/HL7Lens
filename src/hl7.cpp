// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Joseph C McGinty Jr.
#include "hl7.h"
#include "dict.h"
#include <cctype>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <set>
#include <algorithm>

namespace hl7 {
using namespace hl7dict;

// ------------------------------------------------------------------ small helpers (JS semantics)
static std::vector<std::string> jsSplit(const std::string& s, char sep) { std::vector<std::string> out; size_t p = 0; while (true) { size_t q = s.find(sep, p); if (q == std::string::npos) { out.push_back(s.substr(p)); break; } out.push_back(s.substr(p, q - p)); p = q + 1; } return out; }
static std::string join(const std::vector<std::string>& v, const std::string& sep) { std::string o; for (size_t i = 0; i < v.size(); i++) { if (i) o += sep; o += v[i]; } return o; }
static std::vector<std::string> nonEmpty(std::vector<std::string> v) { v.erase(std::remove_if(v.begin(), v.end(), [](const std::string& s) { return s.empty(); }), v.end()); return v; }
static bool isWs(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
static std::string trim(const std::string& s) { size_t a = 0, b = s.size(); while (a < b && isWs((unsigned char)s[a])) a++; while (b > a && isWs((unsigned char)s[b - 1])) b--; return s.substr(a, b - a); }
static std::string upper(std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }
static bool isDigits(const std::string& s) { if (s.empty()) return false; for (unsigned char c : s) if (!isdigit(c)) return false; return true; }
static bool segIdLike(const std::string& l) { return l.size() >= 3 && isupper((unsigned char)l[0]) && (isupper((unsigned char)l[1]) || isdigit((unsigned char)l[1])) && (isupper((unsigned char)l[2]) || isdigit((unsigned char)l[2])); }
static bool isHdr(const std::string& l) { return l.compare(0, 3, "MSH") == 0 || l.compare(0, 3, "FHS") == 0 || l.compare(0, 3, "BHS") == 0; }
static std::string replaceAllStr(std::string s, const std::string& a, const std::string& b) { if (a.empty()) return s; size_t p = 0; while ((p = s.find(a, p)) != std::string::npos) { s.replace(p, a.size(), b); p += b.size(); } return s; }
static void utf8Append(std::string& o, unsigned cp) { if (cp < 0x80) o += (char)cp; else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); } else { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); } }
static const char* MIDDOT = "\xC2\xB7";      // U+00B7 (the Inspector's separator)
static const char* EMDASH = "\xE2\x80\x94";  // U+2014
static const char* ARROW = "\xE2\x86\x92";   // U+2192

// ------------------------------------------------------------------ parser
std::string normalize(const std::string& text) {
    std::string s = text;
    if (s.compare(0, 3, "\xEF\xBB\xBF") == 0) s.erase(0, 3);
    for (auto& c : s) if (c == '\x0b' || c == '\x1c') c = '\r';
    // <cr> (any case) -> CR
    { std::string o; o.reserve(s.size()); for (size_t i = 0; i < s.size(); i++) { if (i + 4 <= s.size() && s[i] == '<' && tolower((unsigned char)s[i + 1]) == 'c' && tolower((unsigned char)s[i + 2]) == 'r' && s[i + 3] == '>') { o += '\r'; i += 3; } else o += s[i]; } s = o; }
    // literal \r / \n followed by a segment id and a separator -> CR
    { std::string o; o.reserve(s.size()); for (size_t i = 0; i < s.size(); i++) { if (s[i] == '\\' && i + 1 < s.size() && (s[i + 1] == 'r' || s[i + 1] == 'n') && i + 6 <= s.size() && segIdLike(s.substr(i + 2, 3)) && s[i + 5] == '|') { o += '\r'; i += 1; } else o += s[i]; } s = o; }
    { std::string o; o.reserve(s.size()); for (size_t i = 0; i < s.size(); i++) { if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') { o += '\r'; i++; } else if (s[i] == '\n' && i + 1 < s.size() && s[i + 1] == '\r') { o += '\r'; i++; } else if (s[i] == '\n') o += '\r'; else o += s[i]; } s = o; }
    return s;
}
Enc encFromHeader(const std::string& line) {
    Enc e; e.fs = line.size() > 3 ? line[3] : '|';
    std::string rest = line.size() > 4 ? line.substr(4) : ""; std::string encRaw = jsSplit(rest, e.fs)[0];
    e.comp = encRaw.size() > 0 ? encRaw[0] : '^'; e.rep = encRaw.size() > 1 ? encRaw[1] : '~'; e.esc = encRaw.size() > 2 ? encRaw[2] : '\\'; e.sub = encRaw.size() > 3 ? encRaw[3] : '&';
    return e;
}
std::string unescape(const std::string& s, const Enc& enc) {
    char e = enc.esc; if (!e || s.find(e) == std::string::npos) return s;
    std::string o; o.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] != e) { o += s[i++]; continue; }
        size_t j = s.find(e, i + 1); if (j == std::string::npos) { o += s.substr(i); break; }
        std::string code = s.substr(i + 1, j - i - 1); std::string all = s.substr(i, j - i + 1);
        if (code == "F") o += enc.fs; else if (code == "S") o += enc.comp; else if (code == "T") o += enc.sub; else if (code == "R") o += enc.rep; else if (code == "E") o += e;
        else if (code == "H" || code == "N") {}
        else if (code == ".br") o += '\n';
        else if (code.size() >= 3 && code.compare(0, 3, ".sp") == 0 && (code.size() == 3 || isDigits(code.substr(3)))) o += '\n';
        else if (code.size() >= 3 && code[0] == '.' && (code.compare(1, 2, "in") == 0 || code.compare(1, 2, "ti") == 0 || code.compare(1, 2, "sk") == 0 || code.compare(1, 2, "ce") == 0 || code.compare(1, 2, "fi") == 0 || code.compare(1, 2, "nf") == 0)) {}
        else if (code.size() >= 3 && code[0] == 'X' && (code.size() - 1) % 2 == 0 && std::all_of(code.begin() + 1, code.end(), [](unsigned char c) { return isxdigit(c); })) { for (size_t k = 1; k < code.size(); k += 2) utf8Append(o, (unsigned)strtoul(code.substr(k, 2).c_str(), nullptr, 16)); }
        else o += all;
        i = j + 1;
    }
    return o;
}
std::string escapeText(const std::string& s, const Enc& enc) {
    std::string o; std::string e(1, enc.esc);
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == enc.esc) o += e + "E" + e; else if (c == enc.fs) o += e + "F" + e; else if (c == enc.comp) o += e + "S" + e; else if (c == enc.rep) o += e + "R" + e; else if (c == enc.sub) o += e + "T" + e;
        else if (c == '\r' || c == '\n') { o += e + ".br" + e; if (c == '\r' && i + 1 < s.size() && s[i + 1] == '\n') i++; }
        else o += c;
    }
    return o;
}
static Field mkField(const std::string& raw, const Enc& enc, bool literal = false) {
    Field f; f.value = raw;
    if (literal) { f.reps.push_back(Rep{ { raw } }); return f; }
    if (raw.empty()) return f;
    for (auto& r : jsSplit(raw, enc.rep)) { Rep rep; for (auto& c : jsSplit(r, enc.comp)) { std::vector<std::string> subs; for (auto& sub : jsSplit(c, enc.sub)) subs.push_back(unescape(sub, enc)); rep.push_back(subs); } f.reps.push_back(rep); }
    return f;
}
static Segment parseSeg(const std::string& line, const Enc& enc) {
    Segment seg; seg.id = upper(line.substr(0, 3)); seg.raw = line;
    std::vector<std::string> parts = jsSplit(line, enc.fs);
    if (isHdr(seg.id)) { seg.fields.push_back(mkField(std::string(1, enc.fs), enc, true)); seg.fields.push_back(mkField(parts.size() > 1 ? parts[1] : "", enc, true)); for (size_t i = 2; i < parts.size(); i++) seg.fields.push_back(mkField(parts[i], enc)); }
    else for (size_t i = 1; i < parts.size(); i++) seg.fields.push_back(mkField(parts[i], enc));
    return seg;
}
Segment parseSegment(const std::string& line, const Enc& enc) { return parseSeg(line, enc); }
std::string fieldValue(const Segment* seg, int n, int c, int s) {
    if (!seg || n < 1 || n > (int)seg->fields.size()) return ""; const Field& f = seg->fields[n - 1]; if (f.reps.empty()) return "";
    const Rep& r = f.reps[0]; if (c < 1 || c > (int)r.size()) return ""; const auto& comp = r[c - 1]; if (s < 1 || s > (int)comp.size()) return ""; return comp[s - 1];
}
const Segment* findSeg(const Message& m, const std::string& id, int nth) { int k = 0; for (auto& s : m.segments) if (s.id == id) { if (k++ == nth) return &s; } return nullptr; }
struct SegLines { std::vector<std::string> lines; Enc enc; std::vector<Note> notes; };
static SegLines toSegmentLines(const std::string& text) {
    SegLines out; std::vector<std::string> lines0; for (auto& l : jsSplit(normalize(text), '\r')) { std::string t = trim(l); if (!t.empty()) lines0.push_back(t); }
    std::string firstHdr; for (auto& l : lines0) if (isHdr(l)) { firstHdr = l; break; }
    out.enc = encFromHeader(firstHdr.empty() ? "MSH|^~\\&" : firstHdr);
    if (firstHdr.empty()) out.notes.push_back({ "warn", "No MSH segment found. Assuming the default separators |^~\\&." });
    for (size_t i = 0; i < lines0.size(); i++) {
        const std::string& l = lines0[i];
        if (segIdLike(l) && (l.size() == 3 || l[3] == out.enc.fs || isHdr(l))) out.lines.push_back(l);
        else if (!out.lines.empty()) {
            std::string& prev = out.lines.back(); unsigned char a = (unsigned char)prev.back(), b = (unsigned char)l[0];
            bool space = (isalnum(a) || a == ')') && (isalnum(b) || b == '(') && !(isdigit(a) && isdigit(b));
            prev += (space ? " " : "") + l;
            out.notes.push_back({ "warn", "Input line " + std::to_string(i + 1) + " didn't start with a segment ID " + EMDASH + " treated as a wrapped continuation and joined to the previous segment " + (space ? "with a space" : "directly") + ". Check the joined value." });
        } else out.notes.push_back({ "warn", "Input line " + std::to_string(i + 1) + " was ignored because it isn't an HL7 segment." });
    }
    return out;
}
std::vector<std::string> splitMessages(const std::string& text) {
    SegLines sl = toSegmentLines(text); std::vector<std::vector<std::string>> groups;
    for (auto& l : sl.lines) { std::string id = upper(l.substr(0, 3)); if (id == "MSH" || groups.empty()) groups.push_back({}); groups.back().push_back(l); }
    std::vector<std::string> out; for (auto& g : groups) out.push_back(join(g, "\n")); return out;
}
Message parseMessage(const std::string& text) {
    SegLines sl = toSegmentLines(text); Message m; m.raw = text; m.enc = sl.enc; m.notes = sl.notes; Enc enc = sl.enc;
    for (auto& l : sl.lines) { if (isHdr(l)) enc = encFromHeader(l); m.segments.push_back(parseSeg(l, enc)); }
    if (!m.segments.empty() && m.segments[0].id != "MSH" && !isHdr(m.segments[0].id)) m.notes.push_back({ "warn", "This message does not start with an MSH segment." });
    const Segment* msh = findSeg(m, "MSH"); m.version = fieldValue(msh, 12, 1);
    return m;
}
std::vector<Message> parseAll(const std::string& text) { std::vector<Message> out; for (auto& t : splitMessages(text)) { Message m = parseMessage(t); if (!m.segments.empty()) out.push_back(m); } return out; }

// ------------------------------------------------------------------ formatting
std::string fmtTS(const std::string& v0) {
    std::string v = trim(v0); if (v.size() < 4) return "";
    size_t i = 0; auto dig = [&](size_t n) { if (i + n > v.size()) return false; for (size_t k = 0; k < n; k++) if (!isdigit((unsigned char)v[i + k])) return false; return true; };
    if (!dig(4)) return ""; std::string g[9]; g[1] = v.substr(0, 4); i = 4;
    for (int k = 2; k <= 6; k++) { if (dig(2)) { g[k] = v.substr(i, 2); i += 2; } else break; }
    if (i < v.size() && v[i] == '.') { size_t j = i + 1; while (j < v.size() && j - i - 1 < 4 && isdigit((unsigned char)v[j])) j++; if (j == i + 1) return ""; g[7] = v.substr(i, j - i); i = j; }
    if (i < v.size() && (v[i] == '+' || v[i] == '-')) { if (i + 5 > v.size()) return ""; for (size_t k = 1; k <= 4; k++) if (!isdigit((unsigned char)v[i + k])) return ""; g[8] = v.substr(i, 5); i += 5; }
    if (i != v.size()) return "";
    int mo = g[2].empty() ? 0 : atoi(g[2].c_str()), d = g[3].empty() ? 0 : atoi(g[3].c_str()), hh = g[4].empty() ? 0 : atoi(g[4].c_str()), mi = g[5].empty() ? 0 : atoi(g[5].c_str()), ss = g[6].empty() ? 0 : atoi(g[6].c_str());
    if ((!g[2].empty() && (mo < 1 || mo > 12)) || (!g[3].empty() && (d < 1 || d > 31)) || (!g[4].empty() && hh > 23) || (!g[5].empty() && mi > 59) || (!g[6].empty() && ss > 59)) return "";
    std::string s = g[1]; if (!g[2].empty()) s += "-" + g[2]; if (!g[3].empty()) s += "-" + g[3];
    if (!g[4].empty()) s += " " + g[4] + ":" + (g[5].empty() ? "00" : g[5]);
    if (!g[6].empty()) s += ":" + g[6] + g[7];
    if (!g[8].empty()) s += " (UTC" + g[8].substr(0, 3) + ":" + g[8].substr(3) + ")";
    return s;
}
std::string ageFrom(const std::string& v) {
    if (v.size() < 8) return ""; for (int k = 0; k < 8; k++) if (!isdigit((unsigned char)v[k])) return "";
    int y = atoi(v.substr(0, 4).c_str()), mo = atoi(v.substr(4, 2).c_str()), d = atoi(v.substr(6, 2).c_str());
    time_t t = time(nullptr); struct tm* now = localtime(&t); int a = (now->tm_year + 1900) - y;
    if (now->tm_mon + 1 < mo || (now->tm_mon + 1 == mo && now->tm_mday < d)) a--;
    return (a >= 0 && a < 150) ? std::to_string(a) + " yrs" : "";
}
std::string fmtPhone(const std::string& ln) { std::string d; for (unsigned char c : ln) if (isdigit(c)) d += (char)c; return d.size() == 7 ? d.substr(0, 3) + "-" + d.substr(3) : ln; }
static std::string sv(const Rep& r, int i, int s = 1) { if (i < 1 || i > (int)r.size()) return ""; const auto& c = r[i - 1]; return (s >= 1 && s <= (int)c.size()) ? c[s - 1] : ""; }
static std::string nameFmt(const std::string& fam, const std::string& given, const std::string& mid, const std::string& suf, const std::string& pre, const std::string& deg) {
    std::string s = fam; if (!given.empty()) s += (s.empty() ? "" : ", ") + given; if (!mid.empty()) s += " " + mid; if (!suf.empty()) s += " " + suf; if (!pre.empty()) s = pre + " " + s; if (!deg.empty()) s += ", " + deg; return trim(s);
}
std::string describeCode(const std::vector<std::string>& keys, const std::string& v) { if (v.empty()) return ""; for (auto& k : keys) { std::string l = tableLabel(k, v); if (!l.empty()) return l; } return ""; }
static std::string codeFmt(const Rep& r) {
    std::string id = sv(r, 1), tx = sv(r, 2), sys = sv(r, 3), ot = sv(r, 9); std::string main = !tx.empty() ? tx : (!ot.empty() ? ot : id);
    std::string meta = join(nonEmpty({ (!tx.empty() && !id.empty()) ? id : "", !sys.empty() ? (codesysLabel(sys).empty() ? sys : codesysLabel(sys)) : "" }), std::string(" ") + MIDDOT + " ");
    std::string s = main + (meta.empty() ? "" : "  [" + meta + "]");
    std::string aid = sv(r, 4), atx = sv(r, 5), asys = sv(r, 6);
    if (!aid.empty() || !atx.empty()) s += "  alt: " + (!atx.empty() ? atx : aid) + ((!atx.empty() && !aid.empty()) ? " (" + aid + ")" : "") + (!asys.empty() ? " [" + (codesysLabel(asys).empty() ? asys : codesysLabel(asys)) + "]" : "");
    return s;
}
static std::string orv(const std::string& a, const std::string& b) { return a.empty() ? b : a; }
static const std::map<std::string, std::function<std::string(const Rep&)>>& FRIENDLY() {
    static std::map<std::string, std::function<std::string(const Rep&)>> F;
    if (!F.empty()) return F;
    std::string MD = std::string(" ") + MIDDOT + " ";
    auto xpn = [](const Rep& r) { return nameFmt(sv(r, 1), sv(r, 2), sv(r, 3), sv(r, 4), sv(r, 5), sv(r, 6)); };
    F["XPN"] = xpn; F["PN"] = xpn;
    F["XCN"] = [](const Rep& r) { std::string n = nameFmt(sv(r, 2), sv(r, 3), sv(r, 4), sv(r, 5), sv(r, 6), sv(r, 7)); std::string id = sv(r, 1); return join(nonEmpty({ n, id.empty() ? "" : "(" + join(nonEmpty({ sv(r, 13), id }), " ") + ")" }), " "); };
    auto cn = [](const Rep& r) { std::string n = nameFmt(sv(r, 2), sv(r, 3), sv(r, 4), sv(r, 5), sv(r, 6), sv(r, 7)); return join(nonEmpty({ n, sv(r, 1).empty() ? "" : "(" + sv(r, 1) + ")" }), " "); };
    F["CN"] = cn; F["CNN"] = cn;
    F["NDL"] = [](const Rep& r) { if (r.empty()) return std::string(); const auto& c = r[0]; auto g = [&](size_t i) { return i < c.size() ? c[i] : ""; }; std::string n = nameFmt(g(1), g(2), g(3), "", "", ""); return n.empty() ? g(0) : n; };
    F["XAD"] = [](const Rep& r) { std::string s = join(nonEmpty({ sv(r, 1), sv(r, 2), sv(r, 3), join(nonEmpty({ sv(r, 4), sv(r, 5) }), " "), sv(r, 6) }), ", "); if (!sv(r, 7).empty()) s += "  (" + orv(describeCode({ "XAD.7" }, sv(r, 7)), sv(r, 7)) + ")"; return s; };
    F["AD"] = [](const Rep& r) { return join(nonEmpty({ sv(r, 1), sv(r, 2), sv(r, 3), join(nonEmpty({ sv(r, 4), sv(r, 5) }), " "), sv(r, 6) }), ", "); };
    F["CE"] = codeFmt; F["CWE"] = codeFmt; F["CNE"] = codeFmt;
    F["CX"] = [MD](const Rep& r) { std::string auth = orv(sv(r, 4, 1), sv(r, 4, 2)), ty = sv(r, 5); return sv(r, 1) + ((!ty.empty() || !auth.empty()) ? "  (" + join(nonEmpty({ ty.empty() ? "" : orv(describeCode({ "CX.5" }, ty), ty), auth }), MD) + ")" : ""); };
    F["XTN"] = [MD](const Rep& r) {
        if (!sv(r, 4).empty()) return sv(r, 4);
        std::string cc = sv(r, 5), ac = sv(r, 6), ln = sv(r, 7), ex = sv(r, 8);
        std::string num = (!ac.empty() || !ln.empty()) ? (cc.empty() ? "" : "+" + cc + " ") + (ac.empty() ? "" : "(" + ac + ") ") + fmtPhone(ln) + (ex.empty() ? "" : " x" + ex) : sv(r, 1);
        std::string use = sv(r, 2); return num + (use.empty() ? "" : " " + MD + orv(describeCode({ "XTN.2" }, use), use));
    };
    F["HD"] = [](const Rep& r) { return join(nonEmpty({ sv(r, 1), sv(r, 2).empty() ? "" : "(" + sv(r, 2) + (sv(r, 3).empty() ? "" : " " + sv(r, 3)) + ")" }), " "); };
    F["EI"] = [MD](const Rep& r) { auto in = nonEmpty({ sv(r, 2), sv(r, 3) }); return join(nonEmpty({ sv(r, 1), in.empty() ? "" : "(" + join(in, MD) + ")" }), " "); };
    F["EIP"] = [MD](const Rep& r) { auto ei = [](const std::vector<std::string>* c) { if (!c) return std::string(); std::string a = c->size() > 0 ? (*c)[0] : "", b = c->size() > 1 ? (*c)[1] : ""; return join(nonEmpty({ a, b.empty() ? "" : "(" + b + ")" }), " "); }; std::string p = ei(r.size() > 0 ? &r[0] : nullptr), f = ei(r.size() > 1 ? &r[1] : nullptr); return join(nonEmpty({ p.empty() ? "" : "Placer " + p, f.empty() ? "" : "Filler " + f }), " " + MD + " "); };
    F["XON"] = [](const Rep& r) { std::string x = orv(sv(r, 10), sv(r, 3)); return sv(r, 1) + (x.empty() ? "" : "  (" + join(nonEmpty({ sv(r, 7), x }), " ") + ")"); };
    F["PL"] = [MD](const Rep& r) { return join(nonEmpty({ sv(r, 1), sv(r, 2).empty() ? "" : "Room " + sv(r, 2), sv(r, 3).empty() ? "" : "Bed " + sv(r, 3), orv(sv(r, 4, 1), sv(r, 4, 2)) }), MD); };
    F["MSG"] = [](const Rep& r) { std::string code = join(nonEmpty({ sv(r, 1), sv(r, 2) }), "^"); std::string d = msgTypeDesc(code); return code + (d.empty() ? "" : ": " + d) + (sv(r, 3).empty() ? "" : "  (structure " + sv(r, 3) + ")"); };
    F["PT"] = [MD](const Rep& r) { return join(nonEmpty({ orv(describeCode({ "PT.1" }, sv(r, 1)), sv(r, 1)), sv(r, 2).empty() ? "" : orv(describeCode({ "MSH-11.2" }, sv(r, 2)), sv(r, 2)) }), MD); };
    F["TS"] = [](const Rep& r) { return orv(fmtTS(sv(r, 1)), sv(r, 1)); };
    F["DR"] = [](const Rep& r) { return join(nonEmpty({ orv(fmtTS(sv(r, 1)), sv(r, 1)), orv(fmtTS(sv(r, 2)), sv(r, 2)) }), std::string(" ") + ARROW + " "); };
    F["CQ"] = [](const Rep& r) { return join(nonEmpty({ sv(r, 1), orv(sv(r, 2, 2), sv(r, 2, 1)) }), " "); };
    F["SN"] = [](const Rep& r) { return sv(r, 1) + sv(r, 2) + sv(r, 3) + sv(r, 4); };
    F["SPS"] = [](const Rep& r) { if (r.empty()) return std::string(); const auto& c = r[0]; std::string b = c.size() > 1 ? c[1] : "", a = c.size() > 0 ? c[0] : ""; return orv(b, a); };
    F["DLN"] = [MD](const Rep& r) { return join(nonEmpty({ sv(r, 1), sv(r, 2), sv(r, 3).empty() ? "" : "exp " + orv(fmtTS(sv(r, 3)), sv(r, 3)) }), MD); };
    F["ERL"] = [](const Rep& r) { return join(nonEmpty({ sv(r, 1), sv(r, 2).empty() ? "" : "#" + sv(r, 2), sv(r, 3).empty() ? "" : "field " + sv(r, 3), sv(r, 4).empty() ? "" : "rep " + sv(r, 4), sv(r, 5).empty() ? "" : "comp " + sv(r, 5), sv(r, 6).empty() ? "" : "sub " + sv(r, 6) }), " "); };
    F["ELD"] = [](const Rep& r) { return join(nonEmpty({ sv(r, 1), sv(r, 2).empty() ? "" : "#" + sv(r, 2), sv(r, 3).empty() ? "" : "field " + sv(r, 3), orv(sv(r, 4, 2), sv(r, 4, 1)) }), " "); };
    F["VID"] = [](const Rep& r) { return sv(r, 1); };
    return F;
}
std::string friendlyRep(const std::string& type, const Rep& rep) { auto& F = FRIENDLY(); auto it = F.find(type); if (it == F.end()) return ""; return trim(it->second(rep)); }
std::string friendly(const std::string& type, const std::vector<Rep>& reps) { auto& F = FRIENDLY(); if (!F.count(type)) return ""; std::vector<std::string> parts; for (auto& r : reps) { std::string s = friendlyRep(type, r); if (!s.empty()) parts.push_back(s); } return join(parts, "  ~  "); }
std::string effectiveType(const std::string& segId, int field, const std::string& obx2) {
    std::string t = fieldType(segId, field);
    if ((segId == "OBX" && field == 5) || t == "*" || t == "varies") { std::string v = upper(trim(obx2)); if (!v.empty() && (type(v) || v == "NM" || v == "ST" || v == "TX" || v == "FT" || v == "DT" || v == "TM" || v == "DTM" || v == "ID" || v == "IS" || v == "SI")) return v; }
    return t;
}
std::string fieldMeaning(const Segment& seg, int n) {
    if (n < 1 || n > (int)seg.fields.size()) return ""; const Field& f = seg.fields[n - 1]; const std::string& val = f.value;
    if (seg.id == "MSH" && (n == 1 || n == 2)) return "";
    std::string type = fieldType(seg.id, n);
    if (isTsField(seg.id, n) && !val.empty()) return fmtTS(val);
    std::string fr = friendly(type, f.reps); return (!fr.empty() && fr != val) ? fr : "";
}

// ------------------------------------------------------------------ validation
static std::string g(const Segment* seg, int n) { return (seg && n >= 1 && n <= (int)seg->fields.size()) ? seg->fields[n - 1].value : ""; }
static std::string c0(const Segment* seg, int n, int ci, int si = 0) { if (!seg || n < 1 || n > (int)seg->fields.size()) return ""; const Field& f = seg->fields[n - 1]; if (f.reps.empty()) return ""; const Rep& r = f.reps[0]; if (ci >= (int)r.size()) return ""; const auto& c = r[ci]; return si < (int)c.size() ? c[si] : ""; }
static std::string rawEncOf(const Segment* msh, const Enc& enc) { std::string v = (msh && msh->fields.size() > 1) ? msh->fields[1].value : ""; if (!v.empty()) return v; return std::string() + enc.comp + enc.rep + enc.esc + enc.sub; }
static bool isNumber(const std::string& s0) { std::string s = trim(s0); size_t i = 0; if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++; size_t d1 = 0; while (i < s.size() && isdigit((unsigned char)s[i])) { i++; d1++; } if (d1) { if (i < s.size() && s[i] == '.') { i++; while (i < s.size() && isdigit((unsigned char)s[i])) i++; } return i == s.size(); } if (i < s.size() && s[i] == '.') { i++; size_t d2 = 0; while (i < s.size() && isdigit((unsigned char)s[i])) { i++; d2++; } return d2 > 0 && i == s.size(); } return false; }
std::vector<Issue> validateStructural(const Message& m) {
    std::vector<Issue> out; auto add = [&](const char* lvl, const std::string& pos, const std::string& text) { out.push_back({ std::string(lvl) == "error" ? "err" : lvl, pos, text }); };
    const auto& segs = m.segments; const Enc& enc = m.enc; const Segment* msh = findSeg(m, "MSH"); std::string rawEnc = rawEncOf(msh, enc);
    if (rawEnc.size() == 5 && rawEnc[4] == '#') add("info", "MSH-2", "Fifth encoding character # is the truncation character. It was formally added in v2.7; many 2.5.1 ELR senders include it and most receivers accept it.");
    else if (rawEnc.size() != 4) add("warn", "MSH-2", "Encoding characters should be exactly 4 characters (usually ^~\\&); found \"" + rawEnc + "\".");
    if (g(msh, 9).empty()) add("error", "MSH-9", "Message type is empty.");
    if (g(msh, 10).empty()) add("warn", "MSH-10", "Message control ID is empty; the receiver cannot reference this message in an ACK.");
    if (g(msh, 12).empty()) add("warn", "MSH-12", "Version ID is empty.");
    std::string type = upper(jsSplit(g(msh, 9), enc.comp)[0]); std::set<std::string> ids; for (auto& s : segs) ids.insert(s.id);
    for (size_t idx = 0; idx < segs.size(); idx++) {
        const Segment& s = segs[idx];
        if (!(s.id.size() == 3 && segIdLike(s.id))) add("error", "line " + std::to_string(idx + 1), "\"" + s.id + "\" is not a valid segment ID (three uppercase letters or digits).");
        else if (!seg(s.id) && s.id[0] != 'Z') add("info", s.id, "Segment " + s.id + " is not in this tool's dictionary; it is shown by position only.");
        for (int n = 1; n <= (int)s.fields.size(); n++) {
            if (!isTsField(s.id, n)) continue; std::string key = s.id + "-" + std::to_string(n);
            for (auto& r : s.fields[n - 1].reps) { std::string v = (!r.empty() && !r[0].empty()) ? r[0][0] : ""; if (!v.empty() && v != "\"\"" && fmtTS(v).empty()) add("warn", key, "\"" + v + "\" is not a valid HL7 date/time (YYYYMMDD[HHMM[SS[.S]]][+/-ZZZZ])."); }
        }
    }
    static const std::set<std::string> PT = { "ADT", "ORM", "ORU", "OML", "OUL", "DFT" };
    if (PT.count(type) && !ids.count("PID")) add("warn", "", type + " message has no PID segment.");
    if (type == "ORU" && !ids.count("OBX")) add("warn", "", "ORU message contains no OBX (result) segments.");
    if ((type == "ORM" || type == "OML") && !ids.count("ORC")) add("warn", "", "Order message has no ORC segment.");
    if (type == "ACK" && !ids.count("MSA")) add("error", "", "ACK message has no MSA segment.");
    const Segment* pid = findSeg(m, "PID");
    if (pid && g(pid, 3).empty() && g(pid, 2).empty()) add("warn", "PID-3", "No patient identifier in PID-3 (or PID-2).");
    if (pid && g(pid, 5).empty()) add("warn", "PID-5", "Patient name is empty.");
    int oi = 0;
    for (auto& s : segs) {
        if (s.id != "OBX") continue; oi++; std::string vt = g(&s, 2), val = c0(&s, 5, 0, 0), tag = "OBX[" + std::to_string(oi) + "]";
        if (vt == "NM" && !val.empty() && !isNumber(val)) add("warn", tag + "-5", "Value type is NM (numeric) but the value \"" + val + "\" is not a plain number. Consider SN (structured numeric) or ST.");
        if (vt.empty() && !val.empty()) add("warn", tag + "-2", "OBX has a value but no value type.");
        if (type == "ORU" && g(&s, 11).empty()) add("info", tag + "-11", "Observation result status is empty.");
        if (g(&s, 11) == "C") add("info", tag + "-11", "This is a corrected result.");
    }
    const Segment* msa = findSeg(m, "MSA");
    if (msa) { std::string code = g(msa, 1); if (!code.empty() && code != "AA" && code != "CA") add("error", "MSA-1", "Acknowledgment " + code + " (" + orv(tableLabel("MSA-1", code), "unknown") + ")" + (g(msa, 3).empty() ? "" : ": " + g(msa, 3)) + "."); }
    return out;
}
static bool vnum(const std::string& v, std::vector<int>& out) { size_t i = 0; auto num = [&](int& n) { size_t s = i; while (i < v.size() && isdigit((unsigned char)v[i])) i++; if (i == s) return false; n = atoi(v.substr(s, i - s).c_str()); return true; }; int a, b, c = 0; if (!num(a)) return false; if (i >= v.size() || v[i] != '.') return false; i++; if (!num(b)) return false; if (i < v.size() && v[i] == '.') { size_t save = i; i++; if (!num(c)) { c = 0; i = save; } } out = { a, b, c }; return true; }
static int vcmp(const std::vector<int>& a, const std::vector<int>& b) { for (int i = 0; i < 3; i++) if (a[i] != b[i]) return a[i] - b[i]; return 0; }
static std::string vstr(const std::vector<int>& v) { std::string s = std::to_string(v[0]) + "." + std::to_string(v[1]) + "." + std::to_string(v[2]); if (s.size() >= 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.erase(s.size() - 2); return s; }
static const std::vector<std::pair<std::string, std::string>> BACKCOMPAT = { { "PID-2", "Use PID-3 for all patient identifiers" }, { "PID-4", "Use PID-3" }, { "PID-12", "Use the county component of PID-11" }, { "PID-19", "Carry SSN in PID-3 with identifier type SS" }, { "PID-20", "Carry in PID-3 with identifier type DL" }, { "OBR-15", "Use the SPM segment" }, { "ORC-7", "Use the TQ1 segment" }, { "OBR-27", "Use the TQ1 segment" } };
VersionReport versionReport(const Message& m) {
    VersionReport R; const auto& segs = m.segments; const Enc& enc = m.enc; const Segment* msh = findSeg(m, "MSH");
    R.declared = trim(jsSplit(g(msh, 12), enc.comp)[0]); std::vector<int> dv; bool hasDv = vnum(R.declared, dv);
    std::set<std::string> ids; for (auto& s : segs) ids.insert(s.id);
    if (ids.count("SPM")) R.feats.push_back({ "SPM specimen segment", "2.5" });
    if (ids.count("SFT")) R.feats.push_back({ "SFT software segment", "2.5" });
    if (ids.count("TQ1")) R.feats.push_back({ "TQ1 timing segment", "2.5" });
    for (auto& s : segs) if (s.id == "OBX" && (!g(&s, 23).empty() || !g(&s, 24).empty() || !g(&s, 25).empty())) { R.feats.push_back({ "Performing organization in OBX-23 to OBX-25", "2.5" }); break; }
    for (auto& s : segs) if (s.id == "ERR" && (!g(&s, 2).empty() || !g(&s, 3).empty())) { R.feats.push_back({ "Restructured ERR segment (ERR-2, ERR-3)", "2.5" }); break; }
    if (!g(msh, 21).empty()) R.feats.push_back({ "Message profile in MSH-21", "2.4" });
    { auto p = jsSplit(g(msh, 9), enc.comp); if (p.size() >= 3) { const std::string& t = p[2]; size_t i = 0; while (i < t.size() && isupper((unsigned char)t[i])) i++; if (i > 0 && i < t.size() && t[i] == '_' && i + 1 < t.size() && (isupper((unsigned char)t[i + 1]) || isdigit((unsigned char)t[i + 1]))) R.feats.push_back({ "Message structure in MSH-9.3", "2.3.1" }); } }
    R.inferred = { 2, 3, 0 }; for (auto& f : R.feats) { std::vector<int> v; if (vnum(f.second, v) && vcmp(v, R.inferred) > 0) R.inferred = v; }
    for (auto& s : segs) for (auto& bc : BACKCOMPAT) { std::string sid = bc.first.substr(0, 3); int n = atoi(bc.first.substr(4).c_str()); if (s.id == sid && !g(&s, n).empty() && std::find(R.used.begin(), R.used.end(), bc.first) == R.used.end()) R.used.push_back(bc.first); }
    auto push = [&](const char* level, const std::string& text) { R.notes.push_back({ level, "MSH-12", text }); };
    if (!hasDv) push("warn", "MSH-12 does not contain a recognizable version (found \"" + R.declared + "\").");
    else if (!R.feats.empty() && vcmp(dv, R.inferred) < 0) { std::vector<std::string> names; for (auto& f : R.feats) { std::vector<int> v; if (vnum(f.second, v) && vcmp(v, dv) > 0) names.push_back(f.first); } push("warn", "Declares " + R.declared + " but uses features from " + vstr(R.inferred) + " or later: " + join(names, "; ") + ". A strict " + R.declared + " receiver may reject or ignore them."); }
    if (hasDv && vcmp(dv, { 2, 5, 0 }) >= 0 && !R.used.empty()) { std::vector<std::string> u; for (auto& k : R.used) for (auto& bc : BACKCOMPAT) if (bc.first == k) u.push_back(k + " (" + bc.second + ")"); push("info", "Declares " + R.declared + " but populates fields kept only for backward compatibility since 2.5: " + join(u, "; ") + "."); }
    R.style = hasDv ? (vcmp(dv, { 2, 5, 0 }) >= 0 ? "2.5 family" : (vcmp(dv, { 2, 4, 0 }) >= 0 ? "2.4" : "2.3 family")) : "unknown";
    R.content = R.feats.empty() ? "Content fits 2.3" : "Content looks like " + vstr(R.inferred) + "+";
    return R;
}
std::vector<ElrCheck> elrChecks(const Message& m) {
    const Segment* msh = findSeg(m, "MSH"); const Segment* pid = findSeg(m, "PID"); const Segment* spm = findSeg(m, "SPM"); const Segment* obr = findSeg(m, "OBR");
    std::vector<const Segment*> obxs; for (auto& s : m.segments) if (s.id == "OBX") obxs.push_back(&s);
    auto isLN = [](const Segment* s, int n) { return c0(s, n, 2) == "LN" || c0(s, n, 5) == "LN"; };
    std::vector<ElrCheck> R; auto t = [&](bool ok, const std::string& text) { R.push_back({ ok, text }); };
    t(!g(msh, 21).empty(), "MSH-21 names the message profile (implementation guide)");
    { std::string v = c0(msh, 7, 0); t(v.size() >= 5 && (v[v.size() - 5] == '+' || v[v.size() - 5] == '-') && isDigits(v.substr(v.size() - 4)), "MSH-7 timestamp includes a time zone offset"); }
    t(!c0(msh, 4, 0).empty() && !c0(msh, 4, 1).empty(), "MSH-4 sending facility has a universal ID and type (for example a CLIA number)");
    t(findSeg(m, "SFT") != nullptr, "SFT segment identifies the sending software");
    t(pid && !c0(pid, 3, 3).empty(), "PID-3 identifier carries an assigning authority (PID-3.4)");
    t(pid && !c0(pid, 3, 4).empty(), "PID-3 identifier carries an identifier type code (PID-3.5)");
    t(findSeg(m, "ORC") != nullptr, "ORC segment present");
    t(obr && isLN(obr, 4), "OBR-4 ordered test is coded in LOINC");
    { bool all = !obxs.empty(); for (auto o : obxs) if (!isLN(o, 3)) all = false; t(all, "Every OBX-3 is coded in LOINC"); }
    { std::vector<const Segment*> coded; for (auto o : obxs) if (g(o, 2) == "CE" || g(o, 2) == "CWE") coded.push_back(o); bool all = true; for (auto o : coded) if (!(c0(o, 5, 2) == "SCT" || c0(o, 5, 5) == "SCT")) all = false; t(all, std::string("Coded results (CE/CWE) use SNOMED CT") + (coded.empty() ? " (no coded results)" : "")); }
    { bool all = !obxs.empty(); for (auto o : obxs) if (g(o, 23).empty() || g(o, 24).empty()) all = false; t(all, "Every OBX has performing organization name and address (OBX-23, OBX-24)"); }
    { bool all = !obxs.empty(); for (auto o : obxs) if (g(o, 11).empty()) all = false; t(all, "Every OBX has a result status (OBX-11)"); }
    t(spm != nullptr, "SPM specimen segment present");
    t(spm && !g(spm, 4).empty(), "SPM-4 specimen type populated");
    t(spm && !g(spm, 17).empty(), "SPM-17 collection date/time populated");
    return R;
}

// ------------------------------------------------------------------ ack / deident / repair
std::string buildAck(const std::string& msgText, const std::string& code, const std::string& msaText, const std::string& stamp0) {
    Message m = parseMessage(msgText); const Segment* msh = findSeg(m, "MSH"); if (!msh) return ""; const Enc& e = m.enc;
    std::string stamp = stamp0; if (stamp.empty()) { time_t t = time(nullptr); char b[32]; strftime(b, sizeof b, "%Y%m%d%H%M%S", localtime(&t)); stamp = b; }
    auto f = [&](int n) { return g(msh, n); }; std::string trig = fieldValue(msh, 9, 2); std::string encRaw = std::string() + e.comp + e.rep + e.esc + e.sub;
    std::string fs(1, e.fs);
    std::string mshLine = join({ "MSH", encRaw, f(5), f(6), f(3), f(4), stamp, "", "ACK" + (trig.empty() ? "" : std::string(1, e.comp) + trig), "ACK" + stamp, orv(f(11), "P"), f(12) }, fs);
    std::string msaLine = join({ "MSA", code, f(10), msaText }, fs); while (!msaLine.empty() && msaLine.back() == e.fs) msaLine.pop_back();
    return mshLine + "\r" + msaLine;
}
static const std::map<std::string, std::vector<int>> DEID = { { "PID", { 2, 3, 4, 5, 6, 7, 9, 11, 13, 14, 18, 19, 20, 21, 23, 29 } }, { "NK1", { 2, 4, 5, 6 } }, { "PV1", { 19, 50 } }, { "GT1", { 2, 3, 4, 5, 6, 7, 8, 12 } }, { "IN1", { 16, 18, 19, 36 } }, { "MRG", { 1, 2, 3, 4, 5, 6, 7 } }, { "PD1", { 3, 4 } } };
static const std::map<std::string, std::vector<int>> ORDER_IDS = { { "ORC", { 2, 3, 4 } }, { "OBR", { 2, 3 } }, { "SPM", { 2 } }, { "OBX", {} } };
static std::string maskLeaf(std::string v) { for (auto& c : v) { if (isalpha((unsigned char)c)) c = 'X'; else if (isdigit((unsigned char)c)) c = '0'; else if (c == '\r' || c == '\n') c = ' '; } return v; }
std::string deidentify(const std::string& text, bool orderIds, bool freeText) {
    std::vector<std::string> outMsgs;
    for (auto& msgText : splitMessages(text)) {
        Enc enc = parseMessage(msgText).enc; std::vector<std::string> outLines;
        for (auto& line : jsSplit(msgText, '\n')) {
            std::string id = upper(line.substr(0, 3)); std::set<int> targets; auto it = DEID.find(id); if (it != DEID.end()) targets.insert(it->second.begin(), it->second.end());
            if (orderIds) { auto o = ORDER_IDS.find(id); if (o != ORDER_IDS.end()) targets.insert(o->second.begin(), o->second.end()); }
            if (freeText && id == "NTE") targets.insert(3);
            std::vector<std::string> parts = jsSplit(line, enc.fs);
            if (freeText && id == "OBX" && parts.size() > 2 && (parts[2] == "ST" || parts[2] == "TX" || parts[2] == "FT")) targets.insert(5);
            if (id == "MSH" || targets.empty()) { outLines.push_back(line); continue; }
            for (size_t n = 1; n < parts.size(); n++) {
                if (!targets.count((int)n) || parts[n].empty() || parts[n] == "\"\"") continue;
                std::vector<std::string> reps; for (auto& r : jsSplit(parts[n], enc.rep)) { std::vector<std::string> comps; for (auto& c : jsSplit(r, enc.comp)) { std::vector<std::string> subs; for (auto& s : jsSplit(c, enc.sub)) subs.push_back(s.empty() ? "" : maskLeaf(s)); comps.push_back(join(subs, std::string(1, enc.sub))); } reps.push_back(join(comps, std::string(1, enc.comp))); }
                parts[n] = join(reps, std::string(1, enc.rep));
            }
            outLines.push_back(join(parts, std::string(1, enc.fs)));
        }
        outMsgs.push_back(join(outLines, "\r"));
    }
    return join(outMsgs, "\r\n");
}
Repair repair(const std::string& text, bool blankZeroDates, bool trimTrailingEmpty) {
    Repair R; std::string t = text; auto log = [&](const char* type, const std::string& d) { R.changes.push_back({ type, d }); };
    if (t.find('\x0b') != std::string::npos || t.find('\x1c') != std::string::npos) { std::string o; for (char c : t) if (c != '\x0b' && c != '\x1c') o += c; t = o; log("framing", "Removed MLLP framing characters (VT/FS)."); }
    std::vector<std::string> lines; { std::string cur; for (size_t i = 0; i < t.size(); i++) { if (t[i] == '\r' || t[i] == '\n') { lines.push_back(cur); cur.clear(); if (t[i] == '\r' && i + 1 < t.size() && t[i + 1] == '\n') i++; } else cur += t[i]; } lines.push_back(cur); }
    std::vector<std::string> cleaned; int droppedBlank = 0, trimmed = 0, tabbed = 0;
    for (auto ln : lines) { std::string o = ln; if (ln.find('\t') != std::string::npos) { for (auto& c : ln) if (c == '\t') c = ' '; tabbed++; } std::string nt = trim(ln); if (nt.empty()) { droppedBlank++; continue; } if (nt != o) trimmed++; cleaned.push_back(nt); }
    if (droppedBlank) log("whitespace", "Removed " + std::to_string(droppedBlank) + " blank line(s)."); if (trimmed) log("whitespace", "Trimmed leading/trailing whitespace on " + std::to_string(trimmed) + " line(s)."); if (tabbed) log("whitespace", "Converted tabs to spaces on " + std::to_string(tabbed) + " line(s).");
    std::vector<std::string> segLines; int wrapped = 0; for (auto& ln : cleaned) { bool isSeg = segIdLike(ln) && (ln.size() == 3 || !isalnum((unsigned char)ln[3])); if (isSeg || segLines.empty()) segLines.push_back(ln); else { segLines.back() += ln; wrapped++; } }
    if (wrapped) log("structure", "Re-joined " + std::to_string(wrapped) + " wrapped continuation line(s) to their segment.");
    static const std::vector<std::pair<std::string, std::string>> UNI = { { "\xE2\x80\x9C", "\"" }, { "\xE2\x80\x9D", "\"" }, { "\xE2\x80\x9E", "\"" }, { "\xE2\x80\x98", "'" }, { "\xE2\x80\x99", "'" }, { "\xE2\x80\x93", "-" }, { "\xE2\x80\x94", "-" }, { "\xC2\xA0", " " }, { "\xE2\x80\xA6", "..." }, { "\xE2\x80\xA2", "*" } };
    int uni = 0, stripped = 0;
    for (auto& ln : segLines) { std::string before = ln; for (auto& p : UNI) ln = replaceAllStr(ln, p.first, p.second); if (ln != before) uni++; std::string o; for (unsigned char c : ln) { if (c >= 32 && c <= 126) o += (char)c; else stripped++; } ln = o; }
    if (uni) log("encoding", "Converted extended/smart Unicode (curly quotes, dashes, non-breaking spaces, etc.) to plain ASCII on " + std::to_string(uni) + " line(s).");
    if (stripped) log("encoding", "Removed " + std::to_string(stripped) + " remaining non-ASCII character(s) - HL7 is 7-bit ASCII and many receivers reject extended characters.");
    if (!segLines.empty() && segLines[0].compare(0, 3, "MSH") == 0) { char fs = segLines[0].size() > 3 ? segLines[0][3] : '|'; std::string encField = segLines[0].size() > 4 ? jsSplit(segLines[0].substr(4), fs)[0] : ""; if (encField != "^~\\&" && encField.size() < 4) { segLines[0] = std::string("MSH") + fs + "^~\\&" + segLines[0].substr(std::min(segLines[0].size(), (size_t)4 + encField.size())); log("encoding", "Set MSH-2 encoding characters to ^~\\& (were \"" + encField + "\")."); } }
    char fs = (!segLines.empty() && segLines[0].size() > 3) ? segLines[0][3] : '|';
    if (blankZeroDates) { int zc = 0; for (auto& ln : segLines) { auto p = jsSplit(ln, fs); for (size_t i = 1; i < p.size(); i++) if (p[i].size() >= 4 && std::all_of(p[i].begin(), p[i].end(), [](char c) { return c == '0'; })) { p[i] = ""; zc++; } ln = join(p, std::string(1, fs)); } if (zc) log("content", "Blanked " + std::to_string(zc) + " all-zero placeholder date/time field(s)."); }
    if (trimTrailingEmpty) { int tf = 0; for (auto& ln : segLines) { size_t n = ln.size(); while (n > 4 && ln[n - 1] == fs) { n--; tf++; } ln.resize(n); } if (tf) log("content", "Trimmed " + std::to_string(tf) + " trailing empty field separator(s)."); }
    R.repaired = join(segLines, "\r"); R.changed = !R.changes.empty();
    return R;
}

// ------------------------------------------------------------------ summary / pretty / json
std::vector<std::pair<std::string, std::string>> summary(const Message& m) {
    const Segment* msh = findSeg(m, "MSH"); const Segment* pid = findSeg(m, "PID"); std::string type = g(msh, 9); std::string dob = g(pid, 7);
    std::string two = type; { auto p = jsSplit(type, '^'); if (p.size() > 2) two = p[0] + "^" + p[1]; }
    std::string td = msgTypeDesc(two);
    std::vector<std::pair<std::string, std::string>> pairs = {
        { "Message type", type + (td.empty() ? "" : std::string(" ") + EMDASH + " " + td) }, { "Control ID", g(msh, 10) }, { "Version", m.version },
        { "Sending", g(msh, 3) + " / " + g(msh, 4) }, { "Receiving", g(msh, 5) + " / " + g(msh, 6) }, { "Timestamp", orv(fmtTS(g(msh, 7)), g(msh, 7)) },
        { "Patient", (pid && pid->fields.size() >= 5) ? friendly("XPN", pid->fields[4].reps) : "" },
        { "DOB", dob.empty() ? "" : orv(fmtTS(dob), dob) + (ageFrom(dob).empty() ? "" : " (" + ageFrom(dob) + ")") }, { "Sex", g(pid, 8) } };
    std::vector<std::pair<std::string, std::string>> out; for (auto& p : pairs) if (!p.second.empty() && p.second != " / ") out.push_back(p); return out;
}
static std::string pad(const std::string& s, size_t w) { return s.size() >= w ? s + " " : s + std::string(w - s.size(), ' '); }
std::string prettyPrint(const Message& m, bool hideEmpty) {
    std::string o;
    for (auto& kv : summary(m)) o += pad(kv.first + ":", 14) + kv.second + "\n";
    for (auto& n : m.notes) o += "[" + upper(n.level) + "] " + n.text + "\n";
    if (!o.empty()) o += "\n";
    for (auto& s : m.segments) {
        o += s.id + "  " + orv(segDesc(s.id), "Segment") + "  (" + std::to_string(s.fields.size()) + " fields)\n";
        for (int n = 1; n <= (int)s.fields.size(); n++) {
            const Field& f = s.fields[n - 1]; if (hideEmpty && f.value.empty()) continue;
            std::string type = fieldType(s.id, n), mean = fieldMeaning(s, n);
            o += "  " + pad(s.id + "-" + std::to_string(n), 10) + pad(fieldName(s.id, n), 34) + f.value + (mean.empty() ? "" : "    " + std::string(ARROW) + " " + mean) + "\n";
            if (!(s.id == "MSH" && n <= 2) && !f.reps.empty() && f.reps[0].size() > 1) {
                for (size_t ci = 0; ci < f.reps[0].size(); ci++) { std::string cv = join(f.reps[0][ci], "&"); while (!cv.empty() && cv.back() == '&') cv.pop_back(); if (cv.empty()) continue; o += "      " + pad(s.id + "-" + std::to_string(n) + "." + std::to_string(ci + 1), 12) + pad(componentName(type, (int)ci + 1), 30) + cv + "\n"; }
            }
        }
        o += "\n";
    }
    return o;
}
std::string jsonEscape(const std::string& s) { std::string o; for (unsigned char c : s) { switch (c) { case '"': o += "\\\""; break; case '\\': o += "\\\\"; break; case '\n': o += "\\n"; break; case '\r': o += "\\r"; break; case '\t': o += "\\t"; break; default: if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; } else o += (char)c; } } return o; }
static std::string jstr(const std::string& s) { return "\"" + jsonEscape(s) + "\""; }
std::string toJson(const Message& m) {
    std::string o = "{\"version\":" + jstr(m.version) + ",\"encoding\":{\"fs\":" + jstr(std::string(1, m.enc.fs)) + ",\"comp\":" + jstr(std::string(1, m.enc.comp)) + ",\"rep\":" + jstr(std::string(1, m.enc.rep)) + ",\"sub\":" + jstr(std::string(1, m.enc.sub)) + ",\"esc\":" + jstr(std::string(1, m.enc.esc)) + "},\"segments\":[";
    bool firstSeg = true;
    for (auto& s : m.segments) {
        if (!firstSeg) o += ","; firstSeg = false; o += "{\"id\":" + jstr(s.id) + ",\"description\":" + jstr(segDesc(s.id)) + ",\"fields\":{"; bool first = true;
        for (size_t i = 0; i < s.fields.size(); i++) {
            const Field& f = s.fields[i]; if (f.value.empty()) continue; if (!first) o += ","; first = false;
            std::string name = fieldName(s.id, (int)i + 1); o += jstr(s.id + "-" + std::to_string(i + 1) + (name.empty() ? "" : " " + name)) + ":";
            bool complex = f.reps.size() > 1 || (!f.reps.empty() && f.reps[0].size() > 1);
            if (!complex) o += jstr(f.value);
            else { o += "["; for (size_t r = 0; r < f.reps.size(); r++) { if (r) o += ","; o += "["; for (size_t c = 0; c < f.reps[r].size(); c++) { if (c) o += ","; const auto& comp = f.reps[r][c]; if (comp.size() > 1) { o += "["; for (size_t k = 0; k < comp.size(); k++) { if (k) o += ","; o += jstr(comp[k]); } o += "]"; } else o += jstr(comp.empty() ? "" : comp[0]); } o += "]"; } o += "]"; }
        }
        o += "}}";
    }
    return o + "]}";
}

// ------------------------------------------------------------------ locate (original text, no normalization)
Enc docEnc(const std::string& text) {
    size_t p = 0;
    while (p < text.size()) { size_t e = text.find_first_of("\r\n", p); if (e == std::string::npos) e = text.size(); size_t a = p; while (a < e && isWs((unsigned char)text[a])) a++; if (e - a >= 3 && isHdr(text.substr(a, 3))) return encFromHeader(text.substr(a, e - a)); p = e + 1; }
    return Enc();
}
std::vector<size_t> messageStarts(const std::string& text) {
    std::vector<size_t> out; size_t p = 0;
    while (p < text.size()) { size_t e = text.find_first_of("\r\n", p); if (e == std::string::npos) e = text.size(); size_t a = p; while (a < e && isWs((unsigned char)text[a])) a++; if (e - a >= 3 && text.compare(a, 3, "MSH") == 0) out.push_back(p); p = e + 1; }
    return out;
}
std::pair<size_t, size_t> messageBounds(const std::string& text, size_t caret) {
    std::vector<size_t> st = messageStarts(text); size_t start = 0, end = text.size();
    for (size_t s : st) { if (s <= caret) start = s; else { end = s; break; } }
    return { start, end };
}
Pos locate(const std::string& text, size_t caret) { return locate(text, caret, docEnc(text)); }
Pos locate(const std::string& text, size_t caret, const Enc& enc) {
    Pos P; if (caret > text.size()) caret = text.size();
    size_t ls = caret; while (ls > 0 && text[ls - 1] != '\r' && text[ls - 1] != '\n') ls--;
    size_t le = text.find_first_of("\r\n", caret); if (le == std::string::npos) le = text.size();
    P.line = (int)std::count_if(text.begin(), text.begin() + ls, [](char c) { return c == '\n'; }) ; { int cr = 0; for (size_t i = 0; i < ls; i++) if (text[i] == '\r' && !(i + 1 < text.size() && text[i + 1] == '\n')) cr++; P.line += cr; }
    size_t a = ls; while (a < le && isWs((unsigned char)text[a])) a++;
    if (le - a < 3 || !segIdLike(text.substr(a, 3))) return P;
    P.ok = true; P.segId = upper(text.substr(a, 3)); P.segStart = a; P.segEnd = le; bool hdr = isHdr(P.segId);
    std::string line = text.substr(a, le - a); Segment seg = parseSeg(line, enc);
    // which part (fs-separated) holds the caret
    int part = 0; size_t partStart = a; for (size_t i = a; i < caret && i < le; i++) if (text[i] == enc.fs) { part++; partStart = i + 1; }
    size_t partEnd = text.find(enc.fs, partStart); if (partEnd == std::string::npos || partEnd > le) partEnd = le;
    if (hdr && caret == a + 3 && le > a + 3) { P.field = 1; P.fieldStart = a + 3; P.fieldEnd = a + 4; P.path = P.segId + "-1"; P.fieldName = fieldName(P.segId, 1); P.fieldType = fieldType(P.segId, 1); P.fieldText = std::string(1, enc.fs); P.label = P.path + "  " + P.fieldName; return P; }
    if (part == 0) { P.field = 0; P.path = P.segId; P.fieldStart = a; P.fieldEnd = std::min(le, a + 3); P.label = P.segId + "  " + orv(segDesc(P.segId), "Segment"); return P; }
    P.field = hdr ? part + 1 : part; P.fieldStart = partStart; P.fieldEnd = partEnd; P.fieldText = text.substr(partStart, partEnd - partStart);
    P.fieldName = fieldName(P.segId, P.field); P.fieldType = effectiveType(P.segId, P.field, P.segId == "OBX" ? fieldValue(&seg, 2) : ""); P.path = P.segId + "-" + std::to_string(P.field);
    if (P.field <= (int)seg.fields.size()) P.meaning = fieldMeaning(seg, P.field);
    if (!(hdr && P.field <= 2)) {
        // repetition
        int nreps = 1; for (size_t i = partStart; i < partEnd; i++) if (text[i] == enc.rep) nreps++;
        int rep = 1; size_t rs = partStart; for (size_t i = partStart; i < caret && i < partEnd; i++) if (text[i] == enc.rep) { rep++; rs = i + 1; }
        size_t re = text.find(enc.rep, rs); if (re == std::string::npos || re > partEnd) re = partEnd;
        P.rep = rep; P.nreps = nreps; P.repStart = rs; P.repEnd = re; if (nreps > 1) P.path += "(" + std::to_string(rep) + ")";
        int ncomps = 1; for (size_t i = rs; i < re; i++) if (text[i] == enc.comp) ncomps++;
        int comp = 1; size_t cs = rs; for (size_t i = rs; i < caret && i < re; i++) if (text[i] == enc.comp) { comp++; cs = i + 1; }
        size_t ce = text.find(enc.comp, cs); if (ce == std::string::npos || ce > re) ce = re;
        bool composite = compCount(P.fieldType) > 1;
        if (ncomps > 1 || composite) { P.comp = comp; P.compStart = cs; P.compEnd = ce; P.compText = text.substr(cs, ce - cs); P.compName = componentName(P.fieldType, comp); P.compType = componentType(P.fieldType, comp); P.path += "." + std::to_string(comp); }
        int nsubs = 1; for (size_t i = cs; i < ce; i++) if (text[i] == enc.sub) nsubs++;
        int sub = 1; size_t ss = cs; for (size_t i = cs; i < caret && i < ce; i++) if (text[i] == enc.sub) { sub++; ss = i + 1; }
        size_t se = text.find(enc.sub, ss); if (se == std::string::npos || se > ce) se = ce;
        P.subStart = ss; P.subEnd = se; P.subText = text.substr(ss, se - ss);
        if (nsubs > 1) { P.sub = sub; P.subName = componentName(P.compType.empty() ? P.fieldType : P.compType, sub); if (P.comp) P.path += "." + std::to_string(sub); }
        if (P.comp == 0) { P.compText = P.fieldText; }
        // table / code lookups for the leaf
        std::string leaf = nsubs > 1 ? P.subText : (P.comp ? P.compText : P.fieldText); std::string lbl;
        if (!leaf.empty()) { lbl = tableLabel(P.segId + "-" + std::to_string(P.field) + (P.comp ? "." + std::to_string(P.comp) : ""), leaf); if (lbl.empty() && P.comp) lbl = tableLabel(P.fieldType + "." + std::to_string(P.comp), leaf); if (lbl.empty() && !P.comp) lbl = tableLabel(P.segId + "-" + std::to_string(P.field), leaf); if (lbl.empty() && P.comp == 0 && ncomps == 1) { std::string cl = codesysLabel(leaf); if (!cl.empty() && (P.fieldType == "ID" || P.fieldType == "IS")) lbl = cl; } }
        if (!lbl.empty()) P.meaning = P.meaning.empty() ? leaf + " = " + lbl : P.meaning + "   (" + leaf + " = " + lbl + ")";
    }
    P.label = P.path + "  " + P.fieldName + (P.compName.empty() ? "" : " > " + P.compName) + (P.subName.empty() ? "" : " > " + P.subName);
    return P;
}

// ------------------------------------------------------------------ document scan with offsets
static void splitSpans(const std::string& t, size_t a, size_t b, char sep, std::vector<Span>& out) { out.clear(); size_t p = a; for (size_t i = a; i < b; i++) if (t[i] == sep) { out.push_back({ p, i }); p = i + 1; } out.push_back({ p, b }); }
static FieldSpan fieldSpan(const std::string& t, size_t a, size_t b, const Enc& e, bool literal) {
    FieldSpan f; f.s = { a, b }; if (literal || a == b) { if (literal) { RepSpan r; r.s = { a, b }; CompSpan c; c.s = { a, b }; c.subs.push_back({ a, b }); r.comps.push_back(c); f.reps.push_back(r); } return f; }
    std::vector<Span> reps, comps, subs; splitSpans(t, a, b, e.rep, reps);
    for (auto& r : reps) { RepSpan rs; rs.s = r; splitSpans(t, r.a, r.b, e.comp, comps); for (auto& c : comps) { CompSpan cs; cs.s = c; splitSpans(t, c.a, c.b, e.sub, subs); cs.subs = subs; rs.comps.push_back(cs); } f.reps.push_back(rs); }
    return f;
}
std::vector<DocMessage> scanDocument(const std::string& t) {
    std::vector<DocMessage> out; Enc enc = docEnc(t); size_t p = 0; int skipped = 0;
    while (p <= t.size()) {
        size_t e = t.find_first_of("\r\n", p); if (e == std::string::npos) e = t.size();
        size_t a = p; while (a < e && (t[a] == ' ' || t[a] == '\t' || t[a] == '\x0b' || t[a] == '\x1c' || (a == 0 && (unsigned char)t[a] == 0xEF))) { if (a == 0 && (unsigned char)t[a] == 0xEF && t.compare(0, 3, "\xEF\xBB\xBF") == 0) a += 3; else a++; }
        size_t b = e; while (b > a && (t[b - 1] == ' ' || t[b - 1] == '\t' || t[b - 1] == '\x1c')) b--;
        if (b > a) {
            std::string head = t.substr(a, std::min<size_t>(4, b - a));
            bool hdr = head.size() >= 3 && isHdr(head);
            if (hdr) enc = encFromHeader(t.substr(a, b - a));
            bool isSeg = head.size() >= 3 && segIdLike(head) && (b - a == 3 || t[a + 3] == enc.fs || hdr);
            if (isSeg) {
                std::string id = upper(head.substr(0, 3));
                if (id == "MSH" || out.empty() || (hdr && id != "MSH" && !out.back().segs.empty() && out.back().segs.back().id != "FHS" && out.back().segs.back().id != "BHS" && false)) { out.push_back(DocMessage()); out.back().s.a = a; out.back().enc = enc; out.back().skippedLines = skipped; skipped = 0; }
                DocMessage& m = out.back(); SegSpan sg; sg.s = { a, b }; sg.id = id;
                std::vector<Span> parts; splitSpans(t, a, b, enc.fs, parts);
                if (hdr) { sg.fields.push_back(fieldSpan(t, a + 3, std::min(b, a + 4), enc, true)); if (parts.size() > 1) sg.fields.push_back(fieldSpan(t, parts[1].a, parts[1].b, enc, true)); for (size_t i = 2; i < parts.size(); i++) sg.fields.push_back(fieldSpan(t, parts[i].a, parts[i].b, enc, false)); }
                else for (size_t i = 1; i < parts.size(); i++) sg.fields.push_back(fieldSpan(t, parts[i].a, parts[i].b, enc, false));
                m.segs.push_back(sg); m.s.b = b;
            } else if (!out.empty()) out.back().skippedLines++; else skipped++;
        }
        if (e >= t.size()) break; p = e + 1;
    }
    return out;
}
int messageIndexAt(const std::vector<DocMessage>& msgs, size_t caret) { int best = -1; for (size_t i = 0; i < msgs.size(); i++) { if (msgs[i].s.a <= caret) best = (int)i; else break; } return best < 0 && !msgs.empty() ? 0 : best; }
bool resolvePath(const DocMessage& m, const std::string& path0, Span& out, std::string* norm) {
    std::string p = upper(trim(path0)); if (p.size() < 3) return false; std::string id = p.substr(0, 3); size_t i = 3; int nth = 1, field = 0, rep = 1, comp = 0, sub = 0; bool repGiven = false;
    auto num = [&](int& v) { size_t s = i; while (i < p.size() && isdigit((unsigned char)p[i])) i++; if (i == s) return false; v = atoi(p.substr(s, i - s).c_str()); return true; };
    if (i < p.size() && p[i] == '[') { i++; if (!num(nth) || i >= p.size() || p[i] != ']') return false; i++; }
    if (i < p.size() && (p[i] == '-' || p[i] == '.' || p[i] == ':')) { i++; if (!num(field)) return false; }
    if (i < p.size() && p[i] == '(') { i++; if (!num(rep) || i >= p.size() || p[i] != ')') return false; i++; repGiven = true; }
    if (i < p.size() && p[i] == '.') { i++; if (!num(comp)) return false; }
    if (i < p.size() && p[i] == '.') { i++; if (!num(sub)) return false; }
    if (i != p.size()) return false;
    int k = 0; const SegSpan* sg = nullptr; for (auto& s : m.segs) if (s.id == id && ++k == nth) { sg = &s; break; }
    if (!sg) return false;
    std::string n = id + (nth > 1 ? "[" + std::to_string(nth) + "]" : "");
    if (field == 0) { out = sg->s; if (norm) *norm = n; return true; }
    n += "-" + std::to_string(field);
    if (field > (int)sg->fields.size()) return false; const FieldSpan& f = sg->fields[field - 1];
    if (comp == 0 && !repGiven) { out = f.s; if (norm) *norm = n; return true; }
    if (f.reps.empty()) { if (comp <= 1 && rep == 1) { out = { f.s.a, f.s.a }; if (norm) *norm = n + (comp ? ".1" : ""); return true; } return false; }
    if (rep < 1 || rep > (int)f.reps.size()) return false; const RepSpan& r = f.reps[rep - 1]; if (repGiven) n += "(" + std::to_string(rep) + ")";
    if (comp == 0) { out = r.s; if (norm) *norm = n; return true; }
    if (comp > (int)r.comps.size()) return false; const CompSpan& c = r.comps[comp - 1]; n += "." + std::to_string(comp);
    if (sub == 0) { out = c.s; if (norm) *norm = n; return true; }
    if (sub > (int)c.subs.size()) return false; out = c.subs[sub - 1]; n += "." + std::to_string(sub); if (norm) *norm = n; return true;
}
std::vector<Span> occurrences(const std::vector<DocMessage>& msgs, const std::string& segId, int field, int comp, int sub) {
    std::vector<Span> out;
    for (auto& m : msgs) for (auto& s : m.segs) {
        if (s.id != segId) continue; if (field == 0) { out.push_back({ s.s.a, s.s.a + 3 }); continue; }
        if (field > (int)s.fields.size()) continue; const FieldSpan& f = s.fields[field - 1];
        if (comp == 0) { if (f.s.b > f.s.a) out.push_back(f.s); continue; }
        for (auto& r : f.reps) { if (comp > (int)r.comps.size()) continue; const CompSpan& c = r.comps[comp - 1]; if (sub == 0) { if (c.s.b > c.s.a) out.push_back(c.s); } else if (sub <= (int)c.subs.size() && c.subs[sub - 1].b > c.subs[sub - 1].a) out.push_back(c.subs[sub - 1]); }
    }
    return out;
}


// ------------------------------------------------------------------ compare two messages
static std::string repText(const Rep& r, const Enc& e) { std::vector<std::string> cs; for (auto& c : r) cs.push_back(join(c, std::string(1, e.sub))); while (!cs.empty() && cs.back().empty()) cs.pop_back(); return join(cs, std::string(1, e.comp)); }
static std::string compText(const Rep& r, size_t c, const Enc& e) { if (c >= r.size()) return ""; std::string t = join(r[c], std::string(1, e.sub)); while (!t.empty() && t.back() == e.sub) t.pop_back(); return t; }
static std::vector<std::string> occIds(const Message& m, const Message& other) { std::map<std::string, int> tot; for (auto& s : m.segments) tot[s.id]++; std::map<std::string, int> to; for (auto& s : other.segments) to[s.id]++;
    std::vector<std::string> v; std::map<std::string, int> n; for (auto& s : m.segments) { int k = ++n[s.id]; v.push_back(std::max(tot[s.id], to[s.id]) > 1 ? s.id + "[" + std::to_string(k) + "]" : s.id); } return v; }   // SEG[n] only when the segment repeats in either message
Diff diffMessages(const Message& A, const Message& B) {
    Diff D; hl7dict::VersionScope vs(A.version);
    std::vector<std::string> ia, ib; for (auto& s : A.segments) ia.push_back(s.id); for (auto& s : B.segments) ib.push_back(s.id);
    std::vector<std::string> oa = occIds(A, B), ob = occIds(B, A);
    size_t n = ia.size(), m = ib.size(); std::vector<std::vector<int>> L(n + 1, std::vector<int>(m + 1, 0));
    for (size_t i = n; i-- > 0;) for (size_t j = m; j-- > 0;) L[i][j] = ia[i] == ib[j] ? L[i + 1][j + 1] + 1 : std::max(L[i + 1][j], L[i][j + 1]);
    auto segRow = [&](const Message& M, size_t k, const std::string& occ) { DiffRow r; r.path = occ; r.name = orv(segDesc(M.segments[k].id), "Segment"); std::string raw = M.segments[k].raw; if (raw.size() > 160) raw = raw.substr(0, 157) + "..."; return std::make_pair(r, raw); };
    size_t i = 0, j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && ia[i] == ib[j]) {
            const Segment& sa = A.segments[i]; const Segment& sb = B.segments[j]; D.matched++;
            std::string occ = oa[i] == ob[j] ? oa[i] : oa[i] + "/" + ob[j]; std::string sid = sa.id; bool hdr = sid == "MSH" || sid == "FHS" || sid == "BHS";
            size_t nf = std::max(sa.fields.size(), sb.fields.size());
            for (size_t f = 1; f <= nf; f++) {
                if (hdr && f <= 2) continue;
                std::string va = f <= sa.fields.size() ? sa.fields[f - 1].value : "", vb = f <= sb.fields.size() ? sb.fields[f - 1].value : ""; if (va == vb) continue;
                std::string fpath = occ + "-" + std::to_string(f), fname = fieldName(sid, (int)f);
                bool expected = sid == "MSH" && (f == 7 || f == 10); auto& out = expected ? D.expected : D.changed;
                static const Field empty; const Field& fa = f <= sa.fields.size() ? sa.fields[f - 1] : empty; const Field& fb = f <= sb.fields.size() ? sb.fields[f - 1] : empty;
                size_t nr = std::max(fa.reps.size(), fb.reps.size()); bool showRep = nr > 1;
                for (size_t r = 0; r < nr; r++) {
                    static const Rep er; const Rep& ra = r < fa.reps.size() ? fa.reps[r] : er; const Rep& rb = r < fb.reps.size() ? fb.reps[r] : er;
                    std::string ta = repText(ra, A.enc), tb = repText(rb, B.enc); if (ta == tb) continue;
                    std::string rpath = fpath + (showRep ? "(" + std::to_string(r + 1) + ")" : "");
                    size_t nc = std::max(ra.size(), rb.size()); std::string ft = effectiveType(sid, (int)f, sid == "OBX" ? fieldValue(&sa, 2) : "");
                    if (nc <= 1 || ta.empty() || tb.empty()) { out.push_back({ rpath, fname, ta, tb }); continue; }
                    for (size_t c = 0; c < nc; c++) { std::string ca = compText(ra, c, A.enc), cb = compText(rb, c, B.enc); if (ca != cb) out.push_back({ rpath + "." + std::to_string(c + 1), fname + " > " + componentName(ft, (int)c + 1), ca, cb }); }
                }
            }
            i++; j++;
        } else if (j >= m || (i < n && L[i + 1][j] >= L[i][j + 1])) { auto p = segRow(A, i, oa[i]); p.first.a = p.second; D.onlyA.push_back(p.first); i++; }
        else { auto p = segRow(B, j, ob[j]); p.first.b = p.second; D.onlyB.push_back(p.first); j++; }
    }
    auto issues = [](const Message& M) { auto v = validateStructural(M); for (auto& x : versionFieldChecks(M)) v.push_back(x); return v; };
    auto va = issues(A), vb = issues(B); auto key = [](const Issue& x) { return x.level + "|" + x.pos + "|" + x.text; };
    std::set<std::string> ka, kb; for (auto& x : va) ka.insert(key(x)); for (auto& x : vb) kb.insert(key(x));
    for (auto& x : va) if (!kb.count(key(x))) D.issuesOnlyA.push_back(x);
    for (auto& x : vb) if (!ka.count(key(x))) D.issuesOnlyB.push_back(x);
    return D;
}
static std::string msgLine(const Message& m) {
    const Segment* h = findSeg(m, "MSH"); std::string t = fieldValue(h, 9, 1) + (fieldValue(h, 9, 2).empty() ? "" : "^" + fieldValue(h, 9, 2));
    return orv(t, "(no MSH-9)") + "   control ID " + orv(fieldValue(h, 10), "-") + "   version " + orv(m.version, "-") + "   " + std::to_string(m.segments.size()) + " segments";
}
std::string diffReport(const Message& A, const Message& B, const std::string& la, const std::string& lb) {
    Diff D = diffMessages(A, B); hl7dict::VersionScope vs(A.version); std::string o;
    auto clip = [](std::string s) { return s.size() > 70 ? s.substr(0, 67) + "..." : s; };
    o += "HL7Lens message comparison\n==========================\n";
    o += "A: " + la + "\n   " + msgLine(A) + "\nB: " + lb + "\n   " + msgLine(B) + "\n\n";
    if (!A.version.empty() && !B.version.empty() && A.version != B.version) o += "Note: the messages declare different versions (A " + A.version + ", B " + B.version + "). Field names below follow A.\n\n";
    o += "Summary: " + std::to_string(D.changed.size()) + " value difference(s), " + std::to_string(D.onlyA.size()) + " segment(s) only in A, " + std::to_string(D.onlyB.size()) + " segment(s) only in B, " + std::to_string(D.matched) + " segment(s) matched";
    o += D.changed.empty() && D.onlyA.empty() && D.onlyB.empty() ? ". The messages carry the same content.\n" : ".\n";
    auto table = [&](const std::vector<DiffRow>& rows) { for (auto& r : rows) { o += "  " + pad(r.path, 16) + clip(r.name) + "\n"; o += "      A: " + (r.a.empty() ? std::string("(empty)") : r.a) + "\n      B: " + (r.b.empty() ? std::string("(empty)") : r.b) + "\n"; } };
    if (!D.changed.empty()) { o += "\nDIFFERENT VALUES\n"; table(D.changed); }
    if (!D.onlyA.empty()) { o += "\nSEGMENTS ONLY IN A\n"; for (auto& r : D.onlyA) o += "  " + pad(r.path, 10) + pad(r.name, 30) + r.a + "\n"; }
    if (!D.onlyB.empty()) { o += "\nSEGMENTS ONLY IN B\n"; for (auto& r : D.onlyB) o += "  " + pad(r.path, 10) + pad(r.name, 30) + r.b + "\n"; }
    if (!D.issuesOnlyA.empty() || !D.issuesOnlyB.empty()) {
        o += "\nVALIDATION FINDINGS THAT DIFFER\n";
        for (auto& x : D.issuesOnlyA) o += "  only A  " + pad(x.level, 6) + pad(x.pos, 12) + x.text + "\n";
        for (auto& x : D.issuesOnlyB) o += "  only B  " + pad(x.level, 6) + pad(x.pos, 12) + x.text + "\n";
    }
    if (!D.expected.empty()) { o += "\nEXPECTED TO DIFFER (timestamp / control ID)\n"; table(D.expected); }
    return o;
}
std::vector<Issue> versionFieldChecks(const Message& m) {
    std::vector<Issue> out; hl7dict::VersionScope vs(m.version); std::string v = hl7dict::activeVersion(); if (v.empty()) return out;
    std::map<std::string, int> occ;
    for (auto& s : m.segments) {
        int k = ++occ[s.id]; std::string p = s.id + (k > 1 ? "[" + std::to_string(k) + "]" : "");
        if (hl7dict::isCustom(s.id)) continue;
        int nf = hl7dict::fieldCount(s.id);
        if (nf < 0) { if (s.id[0] != 'Z') out.push_back({ "warn", p, s.id + " is not a segment in HL7 v" + v + "." }); continue; }
        for (size_t f = 1; f <= s.fields.size(); f++) {
            const Field& F = s.fields[f - 1]; if (F.value.empty()) continue; std::string fp = s.id + "-" + std::to_string(f);
            if ((int)f > nf) { out.push_back({ "warn", fp, "HL7 v" + v + " defines " + std::to_string(nf) + " fields for " + s.id + "; field " + std::to_string(f) + " is not part of this version." }); continue; }
            int rep = hl7dict::fieldRepeat(s.id, (int)f); bool hdr = (s.id == "MSH" || s.id == "FHS" || s.id == "BHS") && f <= 2;
            if (!hdr && rep >= 1 && F.reps.size() > (size_t)rep) out.push_back({ "warn", fp, fieldName(s.id, (int)f) + " repeats " + std::to_string(F.reps.size()) + " times, but HL7 v" + v + (rep == 1 ? " does not allow repetitions here." : " allows at most " + std::to_string(rep) + ".") });
        }
    }
    return out;
}
} // namespace hl7
