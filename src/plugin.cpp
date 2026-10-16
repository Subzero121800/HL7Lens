// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Joseph C McGinty Jr.
// HL7Lens - HL7 v2 message decoder for Notepad++.  Win32 shell around the portable core (hl7.cpp / dict.cpp).
// Plugins > HL7Lens: dockable decoder panel (message tree, live field readout, validation, version + ELR checks),
// field navigation, path search, highlighting, pretty print / JSON / ACK / de-identify / repair, escape tools,
// HL7 user-language installer.  Works offline on the text open in the editor.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include "PluginInterface.h"
#include "Docking.h"
#include "dockingResource.h"
#include "menuCmdID.h"
#include "hl7.h"
#include "dict.h"
#include "assets.h"
#include "resource.h"

static const wchar_t* PLUGIN_NAME = L"HL7Lens";
static const wchar_t* MODULE_NAME = L"HL7Lens.dll";
static const wchar_t* PLUGIN_VERSION = L"1.1.0";
static const int IND_FIELD = 17, IND_MARK = 18;
static const UINT WM_APP_EDITORFOCUS = WM_APP + 7;

static HINSTANCE g_hInst = nullptr;
static NppData g_npp;
static HWND g_panel = nullptr;
static bool g_panelVisible = false;
static std::wstring g_configDir, g_iniPath;
static bool g_follow = true, g_hideEmpty = true, g_highlight = true, g_autoOpen = false;
static std::wstring g_selfTest; static bool g_quiet = false;

// document model (current buffer)
static std::string g_doc; static UINT_PTR g_docBuffer = 0; static bool g_docStale = true; static UINT g_cp = CP_UTF8;
static std::vector<hl7::DocMessage> g_msgs; static int g_cur = -1;
struct Node { hl7::Span s; int depth; };
static std::vector<Node> g_nodes; static int g_treeMsg = -1; static bool g_syncing = false;
struct Row { std::string pos; };
static std::vector<Row> g_rows;
enum View { V_TREE = 0, V_VALIDATE, V_VERSION };
static const wchar_t* VIEW_NAMES[] = { L"Message tree", L"Validation", L"Version and ELR checks" };

// ------------------------------------------------------------------ helpers
static std::wstring toW(const std::string& s, UINT cp = CP_UTF8) { if (s.empty()) return L""; int n = MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), nullptr, 0); if (n <= 0 && cp != CP_ACP) return toW(s, CP_ACP); std::wstring w(n, 0); MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), &w[0], n); return w; }
static std::wstring dW(const std::string& s) { return toW(s, g_cp); }      // text that came from the document
static std::string toU8(const std::wstring& w, UINT cp = CP_UTF8) { if (w.empty()) return ""; int n = WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr); std::string s(n, 0); WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr); return s; }
static std::string replaceAll(std::string s, const std::string& a, const std::string& b) { if (a.empty()) return s; size_t p = 0; while ((p = s.find(a, p)) != std::string::npos) { s.replace(p, a.size(), b); p += b.size(); } return s; }
static std::wstring fmtW(const wchar_t* f, ...) { std::vector<wchar_t> b(1024); for (;;) { va_list ap; va_start(ap, f); int n = _vsnwprintf(b.data(), b.size(), f, ap); va_end(ap); if (n >= 0 && (size_t)n < b.size()) return std::wstring(b.data(), n); b.resize(b.size() * 2); } }
static std::string crlf(const std::string& s) { return replaceAll(replaceAll(replaceAll(s, "\r\n", "\n"), "\r", "\n"), "\n", "\r\n"); }
static std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
static bool fileExists(const std::wstring& p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
static bool writeFile(const std::wstring& p, const std::string& s) { HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr); if (h == INVALID_HANDLE_VALUE) return false; DWORD wr = 0; bool ok = WriteFile(h, s.data(), (DWORD)s.size(), &wr, nullptr) && wr == s.size(); CloseHandle(h); return ok; }
static std::wstring dirOf(const std::wstring& p) { size_t k = p.find_last_of(L"\\/"); return k == std::wstring::npos ? L"" : p.substr(0, k); }
static int msgBox(const std::wstring& text, UINT flags = MB_OK | MB_ICONINFORMATION) { if (g_quiet) return (flags & MB_YESNO) ? IDYES : IDOK; return MessageBoxW(g_npp._nppHandle, text.c_str(), PLUGIN_NAME, flags); }
static void setClipboard(const std::wstring& w) { if (!OpenClipboard(g_npp._nppHandle)) return; EmptyClipboard(); HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t)); if (h) { memcpy(GlobalLock(h), w.c_str(), (w.size() + 1) * sizeof(wchar_t)); GlobalUnlock(h); SetClipboardData(CF_UNICODETEXT, h); } CloseClipboard(); }
static std::wstring getClipboard() { std::wstring out; if (!OpenClipboard(g_npp._nppHandle)) return out; HANDLE h = GetClipboardData(CF_UNICODETEXT); if (h) { const wchar_t* p = (const wchar_t*)GlobalLock(h); if (p) out = p; GlobalUnlock(h); } CloseClipboard(); return out; }

// ------------------------------------------------------------------ settings
static void loadCustomSegments(); static bool iniChanged();
static void loadSettings() {
    g_follow = GetPrivateProfileIntW(L"HL7Lens", L"FollowCaret", 1, g_iniPath.c_str()) != 0;
    g_hideEmpty = GetPrivateProfileIntW(L"HL7Lens", L"HideEmptyFields", 1, g_iniPath.c_str()) != 0;
    g_highlight = GetPrivateProfileIntW(L"HL7Lens", L"HighlightFieldAtCaret", 1, g_iniPath.c_str()) != 0;
    g_autoOpen = GetPrivateProfileIntW(L"HL7Lens", L"OpenPanelForHL7Files", 0, g_iniPath.c_str()) != 0;
    loadCustomSegments(); iniChanged();
}
// [ZCT]                          one section per custom segment (3 characters, letters/digits)
// Description=Lab contact        segment description
// 1=Contact Role|CWE             field n = name|data type[|repeats]
static void loadCustomSegments() {
    hl7dict::clearCustom(); if (g_iniPath.empty()) return; std::vector<wchar_t> names(32768); DWORD n = GetPrivateProfileSectionNamesW(names.data(), (DWORD)names.size(), g_iniPath.c_str());
    for (const wchar_t* sec = names.data(); sec < names.data() + n && *sec; sec += wcslen(sec) + 1) {
        std::wstring id = sec; if (id.size() != 3 || !iswalpha(id[0])) continue; bool ok = true; for (auto c : id) if (!iswalnum(c)) ok = false; if (!ok) continue;
        wchar_t d[512] = { 0 }; GetPrivateProfileStringW(sec, L"Description", L"", d, 512, g_iniPath.c_str());
        std::vector<hl7dict::CustomField> fields;
        for (int k = 1; k <= 200; k++) {
            wchar_t v[512] = { 0 }; GetPrivateProfileStringW(sec, std::to_wstring(k).c_str(), L"\x01", v, 512, g_iniPath.c_str()); std::string t = toU8(v);
            if (t == "\x01") { bool more = false; for (int j = k + 1; j <= 200 && !more; j++) { wchar_t w[8] = { 0 }; GetPrivateProfileStringW(sec, std::to_wstring(j).c_str(), L"\x01", w, 8, g_iniPath.c_str()); more = w[0] != 1; } if (!more) break; fields.push_back({}); continue; }
            hl7dict::CustomField f; size_t a = t.find('|'); f.name = t.substr(0, a);
            if (a != std::string::npos) { size_t b = t.find('|', a + 1); f.type = t.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1); std::string r = b == std::string::npos ? "" : lower(t.substr(b + 1)); f.rep = r.find("rep") != std::string::npos || r == "y" || r == "*" ? 0 : 1; }
            while (!f.type.empty() && f.type.back() == ' ') f.type.pop_back(); while (!f.name.empty() && f.name.back() == ' ') f.name.pop_back(); fields.push_back(f);
        }
        hl7dict::defineCustom(toU8(id), toU8(d), fields);
    }
}
static FILETIME g_iniStamp = {};
static bool iniChanged() { WIN32_FILE_ATTRIBUTE_DATA a; if (!GetFileAttributesExW(g_iniPath.c_str(), GetFileExInfoStandard, &a)) return false; bool ch = CompareFileTime(&a.ftLastWriteTime, &g_iniStamp) != 0; g_iniStamp = a.ftLastWriteTime; return ch; }
static void saveSettings() {
    if (!fileExists(g_iniPath)) writeFile(g_iniPath, "; HL7Lens settings\r\n[HL7Lens]\r\n");
    WritePrivateProfileStringW(L"HL7Lens", L"FollowCaret", g_follow ? L"1" : L"0", g_iniPath.c_str());
    WritePrivateProfileStringW(L"HL7Lens", L"HideEmptyFields", g_hideEmpty ? L"1" : L"0", g_iniPath.c_str());
    WritePrivateProfileStringW(L"HL7Lens", L"HighlightFieldAtCaret", g_highlight ? L"1" : L"0", g_iniPath.c_str());
    WritePrivateProfileStringW(L"HL7Lens", L"OpenPanelForHL7Files", g_autoOpen ? L"1" : L"0", g_iniPath.c_str());
    iniChanged();
}

// ------------------------------------------------------------------ Notepad++ / Scintilla
static HWND curSci() { int which = 0; SendMessage(g_npp._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which); return which == 0 ? g_npp._scintillaMainHandle : g_npp._scintillaSecondHandle; }
static std::string docText(HWND sci) { LRESULT len = SendMessage(sci, SCI_GETLENGTH, 0, 0); std::string s((size_t)len + 1, '\0'); SendMessage(sci, SCI_GETTEXT, (WPARAM)(len + 1), (LPARAM)&s[0]); s.resize((size_t)len); return s; }
static std::string docEol(HWND sci) { switch (SendMessage(sci, SCI_GETEOLMODE, 0, 0)) { case SC_EOL_CR: return "\r"; case SC_EOL_LF: return "\n"; default: return "\r\n"; } }
static UINT_PTR curBuffer() { return (UINT_PTR)SendMessage(g_npp._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0); }
static size_t caretPos() { return (size_t)SendMessage(curSci(), SCI_GETCURRENTPOS, 0, 0); }
static std::wstring currentPath() { wchar_t b[MAX_PATH * 2] = { 0 }; SendMessage(g_npp._nppHandle, NPPM_GETFULLCURRENTPATH, MAX_PATH * 2, (LPARAM)b); return b; }
static void selectSpan(size_t a, size_t b, bool focus) {
    HWND sci = curSci(); int line = (int)SendMessage(sci, SCI_LINEFROMPOSITION, a, 0); SendMessage(sci, SCI_ENSUREVISIBLEENFORCEPOLICY, line, 0);
    SendMessage(sci, SCI_SETSEL, (WPARAM)b, (LPARAM)a); SendMessage(sci, SCI_SCROLLRANGE, (WPARAM)b, (LPARAM)a); if (focus) SetFocus(sci);
}
static void newTab(const std::string& text) {
    SendMessage(g_npp._nppHandle, NPPM_MENUCOMMAND, 0, IDM_FILE_NEW); HWND s = curSci();
    SendMessage(s, SCI_SETCODEPAGE, SC_CP_UTF8, 0); SendMessage(s, SCI_SETTEXT, 0, (LPARAM)text.c_str()); SendMessage(s, SCI_SETSAVEPOINT, 0, 0); SendMessage(s, SCI_GOTOPOS, 0, 0);
}
static bool isHl7Text(const std::string& t) { size_t i = 0; while (i < t.size() && (isspace((unsigned char)t[i]) || t[i] == '\x0b' || (unsigned char)t[i] >= 0xEF)) i++; return t.compare(i, 4, "MSH|") == 0 || t.compare(i, 4, "FHS|") == 0 || t.compare(i, 4, "BHS|") == 0 || (t.size() > i + 3 && t.compare(i, 3, "MSH") == 0); }
static bool isHl7Doc() {
    std::wstring p = currentPath(); for (auto& c : p) c = (wchar_t)towlower(c);
    if (p.size() > 4 && p.compare(p.size() - 4, 4, L".hl7") == 0) return true;
    HWND sci = curSci(); char b[64] = { 0 }; Sci_TextRangeFull tr; tr.chrg.cpMin = 0; tr.chrg.cpMax = std::min<Sci_Position>(60, (Sci_Position)SendMessage(sci, SCI_GETLENGTH, 0, 0)); tr.lpstrText = b; SendMessage(sci, SCI_GETTEXTRANGEFULL, 0, (LPARAM)&tr);
    return isHl7Text(b);
}
static void setupIndicators(HWND sci) {
    SendMessage(sci, SCI_INDICSETSTYLE, IND_FIELD, INDIC_ROUNDBOX); SendMessage(sci, SCI_INDICSETFORE, IND_FIELD, RGB(64, 160, 224)); SendMessage(sci, SCI_INDICSETALPHA, IND_FIELD, 70); SendMessage(sci, SCI_INDICSETOUTLINEALPHA, IND_FIELD, 160); SendMessage(sci, SCI_INDICSETUNDER, IND_FIELD, TRUE);
    SendMessage(sci, SCI_INDICSETSTYLE, IND_MARK, INDIC_ROUNDBOX); SendMessage(sci, SCI_INDICSETFORE, IND_MARK, RGB(255, 170, 0)); SendMessage(sci, SCI_INDICSETALPHA, IND_MARK, 90); SendMessage(sci, SCI_INDICSETOUTLINEALPHA, IND_MARK, 200); SendMessage(sci, SCI_INDICSETUNDER, IND_MARK, TRUE);
}
static void clearIndicator(int ind) { HWND sci = curSci(); SendMessage(sci, SCI_SETINDICATORCURRENT, ind, 0); SendMessage(sci, SCI_INDICATORCLEARRANGE, 0, SendMessage(sci, SCI_GETLENGTH, 0, 0)); }
static void fillIndicator(int ind, size_t a, size_t b) { if (b <= a) return; HWND sci = curSci(); SendMessage(sci, SCI_SETINDICATORCURRENT, ind, 0); SendMessage(sci, SCI_INDICATORFILLRANGE, a, b - a); }

// ------------------------------------------------------------------ model
static void setDocStatus();
static void loadCustomSegments(); static bool iniChanged();
static void refreshDoc(bool force = false) {
    if (iniChanged()) { loadCustomSegments(); force = true; }   // custom segment definitions edited in settings.ini
    UINT_PTR buf = curBuffer();
    if (!force && !g_docStale && buf == g_docBuffer) return;
    HWND sci = curSci(); g_doc = docText(sci); g_docBuffer = buf; g_docStale = false; g_cp = SendMessage(sci, SCI_GETCODEPAGE, 0, 0) == SC_CP_UTF8 ? CP_UTF8 : CP_ACP;
    g_msgs = hl7::scanDocument(g_doc); g_treeMsg = -1; setDocStatus();
}
static std::string spanText(const hl7::Span& s) { return s.b > s.a && s.b <= g_doc.size() ? g_doc.substr(s.a, s.b - s.a) : ""; }
static std::string docMsgVersion(int mi) {
    if (mi < 0 || mi >= (int)g_msgs.size() || g_msgs[mi].segs.empty()) return ""; const hl7::SegSpan& h = g_msgs[mi].segs[0];
    if (h.id != "MSH" || h.fields.size() < 12) return ""; std::string v = spanText(h.fields[11].s); return v.substr(0, v.find(g_msgs[mi].enc.comp));
}
static std::string useVersionOf(int mi) { return hl7dict::setVersion(docMsgVersion(mi)); }   // field names and types follow the message's own MSH-12
static hl7::Pos locateAt(size_t caret) { int mi = hl7::messageIndexAt(g_msgs, caret); useVersionOf(mi); return mi >= 0 ? hl7::locate(g_doc, caret, g_msgs[mi].enc) : hl7::locate(g_doc, caret); }

// ------------------------------------------------------------------ panel
static void setStatus(const std::wstring& s) { if (g_panel) SetDlgItemTextW(g_panel, IDC_STATUS, s.c_str()); }
static void setDocStatus() { setStatus(g_msgs.empty() ? std::wstring(L"No HL7 message in this document.") : fmtW(L"%d message(s) in this document.", (int)g_msgs.size())); }   // one detector: the same scan the tree uses
static int curView() { return g_panel ? (int)SendDlgItemMessage(g_panel, IDC_VIEW, CB_GETCURSEL, 0, 0) : 0; }
static HTREEITEM addTree(HWND tv, HTREEITEM parent, const std::wstring& text, const hl7::Span& s, int depth) {
    TVINSERTSTRUCTW is = {}; is.hParent = parent; is.hInsertAfter = TVI_LAST; is.item.mask = TVIF_TEXT | TVIF_PARAM; std::wstring t = text.size() > 1000 ? text.substr(0, 1000) + L"..." : text;
    is.item.pszText = (LPWSTR)t.c_str(); is.item.lParam = (LPARAM)g_nodes.size(); g_nodes.push_back({ s, depth }); return (HTREEITEM)SendMessage(tv, TVM_INSERTITEMW, 0, (LPARAM)&is);
}
static std::string compMeaning(const std::string& segId, int field, const std::string& ftype, int comp, const std::string& val) {
    if (val.empty()) return ""; std::string l = hl7dict::tableLabel(segId + "-" + std::to_string(field) + "." + std::to_string(comp), val);
    if (l.empty()) l = hl7dict::tableLabel(ftype + "." + std::to_string(comp), val);
    std::string ct = hl7dict::componentType(ftype, comp);
    if (l.empty() && (ct == "TS" || ct == "DTM" || ct == "DT")) l = hl7::fmtTS(val);
    if (l.empty() && (ct == "ID" || ct == "IS") && comp == 3) l = hl7dict::codesysLabel(val);
    return l;
}
static std::wstring filterText() { wchar_t b[256] = { 0 }; if (g_panel) GetDlgItemTextW(g_panel, IDC_FILTER, b, 256); std::wstring f = b; for (auto& c : f) c = (wchar_t)towlower(c); return f; }
static bool matches(const std::wstring& t, const std::wstring& f) { if (f.empty()) return true; std::wstring l = t; for (auto& c : l) c = (wchar_t)towlower(c); return l.find(f) != std::wstring::npos; }
static void buildTree() {
    if (!g_panel) return; HWND tv = GetDlgItem(g_panel, IDC_TREE); SendMessage(tv, WM_SETREDRAW, FALSE, 0); TreeView_DeleteAllItems(tv); g_nodes.clear(); g_treeMsg = g_cur;
    SetDlgItemTextW(g_panel, IDC_MSGLBL, g_msgs.empty() ? L"No HL7 message" : fmtW(L"Message %d of %d", g_cur + 1, (int)g_msgs.size()).c_str());
    if (g_cur < 0 || g_cur >= (int)g_msgs.size()) { SendMessage(tv, WM_SETREDRAW, TRUE, 0); return; }
    const hl7::DocMessage& dm = g_msgs[g_cur]; std::wstring flt = filterText(); bool filtering = !flt.empty(); std::string declared = docMsgVersion(g_cur), used = useVersionOf(g_cur);
    hl7::Message pm = hl7::parseMessage(g_doc.substr(dm.s.a, dm.s.b - dm.s.a));
    if (!filtering) {
        HTREEITEM sum = addTree(tv, TVI_ROOT, L"Summary", dm.s, 0);
        for (auto& kv : hl7::summary(pm)) addTree(tv, sum, toW(kv.first) + L": " + dW(kv.second), dm.s, 1);
        addTree(tv, sum, L"Field names: " + (used.empty() ? std::wstring(L"base dictionary (no version in MSH-12)") : L"HL7 v" + toW(used) + L" definitions" + (used != declared ? L" (message declares " + dW(declared) + L")" : L"")) + (hl7dict::nCustom() ? fmtW(L"; %d custom segment(s)", hl7dict::nCustom()) : L""), dm.s, 1);
        TreeView_Expand(tv, sum, TVE_EXPAND);
        std::vector<std::wstring> notes; for (auto& n : pm.notes) notes.push_back(toW(n.level) + L": " + dW(n.text));
        if (dm.skippedLines) notes.push_back(fmtW(L"warn: %d line(s) in this message are not HL7 segments and are not shown (Repair message can rejoin wrapped lines).", dm.skippedLines));
        if (!notes.empty()) { HTREEITEM nn = addTree(tv, TVI_ROOT, fmtW(L"Notes (%d)", (int)notes.size()), dm.s, 0); for (auto& n : notes) addTree(tv, nn, n, dm.s, 1); }
    }
    for (auto& sg : dm.segs) {
        hl7::Segment seg = hl7::parseSegment(spanText(sg.s), dm.enc); std::string desc = hl7dict::segDesc(sg.id);
        std::wstring segLabel = dW(sg.id) + L"  " + (desc.empty() ? (sg.id[0] == 'Z' ? L"Custom (Z) segment, not defined (see Define custom segments)" : L"Segment not in this version's dictionary") : toW(desc)) + (hl7dict::isCustom(sg.id) ? L"  (custom definition)" : L"");
        HTREEITEM hs = nullptr; bool any = false;
        if (!filtering || matches(segLabel, flt)) { hs = addTree(tv, TVI_ROOT, segLabel, sg.s, 0); any = true; }
        for (int n = 1; n <= (int)sg.fields.size(); n++) {
            const hl7::FieldSpan& fs = sg.fields[n - 1]; std::string val = spanText(fs.s); if (g_hideEmpty && val.empty()) continue;
            std::string ftype = hl7::effectiveType(sg.id, n, sg.id == "OBX" && sg.fields.size() >= 2 ? spanText(sg.fields[1].s) : ""), name = hl7dict::fieldName(sg.id, n), mean = n <= (int)seg.fields.size() ? hl7::fieldMeaning(seg, n) : "";
            char flag = hl7dict::fieldFlag(sg.id, n);
            std::wstring fl = dW(sg.id) + L"-" + std::to_wstring(n) + L"  " + toW(name) + (flag == 'B' ? L" (backward compatible)" : L"") + L" [" + toW(ftype) + L"]: " + dW(val) + (mean.empty() ? L"" : L"   = " + dW(mean)) + (fs.reps.size() > 1 ? fmtW(L"   [%d repetitions]", (int)fs.reps.size()) : L"");
            std::vector<std::wstring> childLabels; // decide filter on field or children
            bool fieldMatch = matches(fl, flt);
            bool literal = (sg.id == "MSH" || sg.id == "FHS" || sg.id == "BHS") && n <= 2;
            bool composite = hl7dict::compCount(ftype) > 1;
            bool showReps = fs.reps.size() > 1; bool showComps = !literal && !fs.reps.empty() && (fs.reps[0].comps.size() > 1 || (composite && !val.empty()));
            if (filtering && !fieldMatch) {   // child match?
                bool child = false; for (auto& r : fs.reps) for (size_t c = 0; c < r.comps.size(); c++) { std::string cv = spanText(r.comps[c].s); if (matches(toW(hl7dict::componentName(ftype, (int)c + 1)) + L" " + dW(cv) + L" " + dW(compMeaning(sg.id, n, ftype, (int)c + 1, cv)), flt)) child = true; }
                if (!child) continue;
            }
            if (!hs) { hs = addTree(tv, TVI_ROOT, segLabel, sg.s, 0); any = true; }
            HTREEITEM hf = addTree(tv, hs, fl, fs.s, 1);
            if (literal) continue;
            for (size_t r = 0; r < fs.reps.size(); r++) {
                const hl7::RepSpan& rs = fs.reps[r]; HTREEITEM parent = hf; std::wstring rp = showReps ? L"(" + std::to_wstring(r + 1) + L")" : L"";
                if (showReps) { std::string rv = spanText(rs.s); std::string rm = hl7::friendlyRep(ftype, n <= (int)seg.fields.size() && r < seg.fields[n - 1].reps.size() ? seg.fields[n - 1].reps[r] : hl7::Rep()); parent = addTree(tv, hf, L"Repetition " + std::to_wstring(r + 1) + L": " + dW(rv) + (rm.empty() || rm == rv ? L"" : L"   = " + dW(rm)), rs.s, 2); }
                if (!(showComps || (showReps && rs.comps.size() > 1))) continue;
                for (size_t c = 0; c < rs.comps.size(); c++) {
                    const hl7::CompSpan& cs = rs.comps[c]; std::string cv = spanText(cs.s); if (g_hideEmpty && cv.empty()) continue;
                    std::string cname = hl7dict::componentName(ftype, (int)c + 1), ctype = hl7dict::componentType(ftype, (int)c + 1), cm = compMeaning(sg.id, n, ftype, (int)c + 1, hl7::unescape(cv, dm.enc));
                    std::string un = hl7::unescape(cv, dm.enc);
                    HTREEITEM hc = addTree(tv, parent, dW(sg.id) + L"-" + std::to_wstring(n) + rp + L"." + std::to_wstring(c + 1) + L"  " + toW(cname) + L" [" + toW(ctype) + L"]: " + dW(cv) + (un != cv ? L"   (" + dW(replaceAll(un, "\n", " / ")) + L")" : L"") + (cm.empty() ? L"" : L"   = " + dW(cm)), cs.s, 3);
                    if (cs.subs.size() > 1) for (size_t k = 0; k < cs.subs.size(); k++) { std::string sv = spanText(cs.subs[k]); if (g_hideEmpty && sv.empty()) continue; addTree(tv, hc, dW(sg.id) + L"-" + std::to_wstring(n) + rp + L"." + std::to_wstring(c + 1) + L"." + std::to_wstring(k + 1) + L"  " + toW(hl7dict::componentName(ctype, (int)k + 1)) + L": " + dW(sv), cs.subs[k], 4); }
                }
            }
            if (filtering) TreeView_Expand(tv, hf, TVE_EXPAND);
        }
        if (hs && any) TreeView_Expand(tv, hs, TVE_EXPAND);
    }
    SendMessage(tv, WM_SETREDRAW, TRUE, 0); InvalidateRect(tv, nullptr, TRUE);
}
static void listColumns(HWND lv, const wchar_t* a, const wchar_t* b, const wchar_t* c) { LVCOLUMNW col = {}; col.mask = LVCF_TEXT; col.pszText = (LPWSTR)a; ListView_SetColumn(lv, 0, &col); col.pszText = (LPWSTR)b; ListView_SetColumn(lv, 1, &col); col.pszText = (LPWSTR)c; ListView_SetColumn(lv, 2, &col); }
static void listAdd(HWND lv, const std::wstring& a, const std::wstring& b, const std::wstring& c, const std::string& pos) {
    LVITEMW it = {}; it.mask = LVIF_TEXT; it.iItem = ListView_GetItemCount(lv); it.pszText = (LPWSTR)a.c_str(); int i = (int)SendMessage(lv, LVM_INSERTITEMW, 0, (LPARAM)&it);
    LVITEMW s = {}; s.iSubItem = 1; s.pszText = (LPWSTR)b.c_str(); SendMessage(lv, LVM_SETITEMTEXTW, i, (LPARAM)&s); s.iSubItem = 2; s.pszText = (LPWSTR)c.c_str(); SendMessage(lv, LVM_SETITEMTEXTW, i, (LPARAM)&s); g_rows.push_back({ pos });
}
static int g_lastIssueCount = 0;
static void buildList() {
    if (!g_panel) return; HWND lv = GetDlgItem(g_panel, IDC_LIST); ListView_DeleteAllItems(lv); g_rows.clear(); g_lastIssueCount = 0;
    if (g_cur < 0 || g_cur >= (int)g_msgs.size()) return; const hl7::DocMessage& dm = g_msgs[g_cur];
    hl7::Message pm = hl7::parseMessage(g_doc.substr(dm.s.a, dm.s.b - dm.s.a)); useVersionOf(g_cur);
    if (curView() == V_VALIDATE) {
        listColumns(lv, L"Level", L"Position", L"Problem");
        auto lvl = [](const std::string& l) { return l == "err" ? std::wstring(L"Error") : l == "warn" ? std::wstring(L"Warning") : std::wstring(L"Info"); };
        for (auto& n : pm.notes) listAdd(lv, lvl(n.level), L"", dW(n.text), "");
        for (auto& i : hl7::validateStructural(pm)) listAdd(lv, lvl(i.level), dW(i.pos), dW(i.text), i.pos);
        for (auto& i : hl7::versionReport(pm).notes) listAdd(lv, lvl(i.level), dW(i.pos), dW(i.text), i.pos);
        for (auto& i : hl7::versionFieldChecks(pm)) listAdd(lv, lvl(i.level), dW(i.pos), dW(i.text), i.pos);
        g_lastIssueCount = ListView_GetItemCount(lv);
        if (!g_lastIssueCount) listAdd(lv, L"OK", L"", L"No structural problems found in this message.", "");
    } else {
        listColumns(lv, L"Check", L"Result", L"Detail");
        hl7::VersionReport vr = hl7::versionReport(pm);
        listAdd(lv, L"Declared version (MSH-12)", dW(vr.declared.empty() ? "(none)" : vr.declared), toW(vr.style), "MSH-12");
        listAdd(lv, L"Content", toW(vr.content), L"", "");
        { std::string u = hl7dict::activeVersion(); listAdd(lv, L"Field names from", u.empty() ? L"base dictionary" : L"HL7 v" + toW(u), u.empty() ? L"no usable version in MSH-12" : (u != vr.declared ? L"closest defined version to " + dW(vr.declared) : L"exact match"), "MSH-12"); }
        for (auto& i : hl7::versionFieldChecks(pm)) listAdd(lv, L"Warning", dW(i.pos), dW(i.text), i.pos);
        for (auto& f : vr.feats) listAdd(lv, L"Uses feature", toW(f.first), L"since v" + toW(f.second), "");
        for (auto& u : vr.used) listAdd(lv, L"Backward-compatible field", toW(u), L"populated", u);
        for (auto& n : vr.notes) listAdd(lv, n.level == "warn" ? L"Warning" : L"Note", L"", dW(n.text), n.pos);
        for (auto& e : hl7::elrChecks(pm)) listAdd(lv, L"ELR readiness", e.ok ? L"Pass" : L"Missing", toW(e.text), "");
    }
    for (int c = 0; c < 3; c++) ListView_SetColumnWidth(lv, c, c == 2 ? LVSCW_AUTOSIZE_USEHEADER : LVSCW_AUTOSIZE);
}
static void showView() {
    if (!g_panel) return; int v = curView(); HWND tv = GetDlgItem(g_panel, IDC_TREE), lv = GetDlgItem(g_panel, IDC_LIST);
    ShowWindow(tv, v == V_TREE ? SW_SHOW : SW_HIDE); ShowWindow(lv, v == V_TREE ? SW_HIDE : SW_SHOW);
    EnableWindow(GetDlgItem(g_panel, IDC_FILTER), v == V_TREE); EnableWindow(GetDlgItem(g_panel, IDC_EXPAND), v == V_TREE); EnableWindow(GetDlgItem(g_panel, IDC_COLLAPSE), v == V_TREE);
    if (v == V_TREE) buildTree(); else buildList();
}
static void selectNodeFor(size_t caret) {
    if (!g_panel || curView() != V_TREE) return; HWND tv = GetDlgItem(g_panel, IDC_TREE);
    int best = -1; size_t bestLen = (size_t)-1; int bestDepth = -1;
    for (size_t i = 0; i < g_nodes.size(); i++) { const Node& n = g_nodes[i]; if (n.depth == 0 && n.s.a == g_msgs[g_cur].s.a && n.s.b == g_msgs[g_cur].s.b) continue; if (caret >= n.s.a && caret <= n.s.b) { size_t len = n.s.b - n.s.a; if (len < bestLen || (len == bestLen && n.depth > bestDepth)) { best = (int)i; bestLen = len; bestDepth = n.depth; } } }
    if (best < 0) return;
    // find the HTREEITEM with lParam == best (walk)
    std::vector<HTREEITEM> stack; HTREEITEM h = TreeView_GetRoot(tv);
    while (h) {
        TVITEMW it = {}; it.mask = TVIF_PARAM; it.hItem = h; SendMessage(tv, TVM_GETITEMW, 0, (LPARAM)&it);
        if ((int)it.lParam == best) { g_syncing = true; TreeView_SelectItem(tv, h); TreeView_EnsureVisible(tv, h); g_syncing = false; return; }
        HTREEITEM c = TreeView_GetChild(tv, h); if (c) { stack.push_back(h); h = c; continue; }
        HTREEITEM nx = TreeView_GetNextSibling(tv, h); while (!nx && !stack.empty()) { nx = TreeView_GetNextSibling(tv, stack.back()); stack.pop_back(); } h = nx;
    }
}
static hl7::Pos g_lastPos;
static void updateReadout(size_t caret) {
    hl7::Pos p = locateAt(caret); g_lastPos = p; if (!g_panel) return;
    std::wstring r;
    if (!p.ok) r = L"Caret is not on an HL7 segment.";
    else if (p.field == 0) r = dW(p.segId) + L"  " + toW(hl7dict::segDesc(p.segId).empty() ? std::string("Segment") : hl7dict::segDesc(p.segId));
    else {
        r = dW(p.path) + L"   " + toW(p.fieldName) + (p.compName.empty() ? L"" : L" > " + toW(p.compName)) + (p.subName.empty() ? L"" : L" > " + toW(p.subName)) + L"\r\n";
        { std::string v = hl7dict::isCustom(p.segId) ? std::string("custom") : hl7dict::activeVersion().empty() ? std::string("") : "v" + hl7dict::activeVersion(); r += L"Type: " + toW(p.fieldType) + (p.compType.empty() ? L"" : L" > " + toW(p.compType)) + (v.empty() ? L"" : L"   (" + toW(v) + L")") + L"     Line " + std::to_wstring(p.line + 1) + L"\r\n"; }
        std::string leaf = !p.subText.empty() && p.sub ? p.subText : (p.comp ? p.compText : p.fieldText);
        int mi = hl7::messageIndexAt(g_msgs, caret); hl7::Enc enc = mi >= 0 ? g_msgs[mi].enc : hl7::Enc(); std::string un = hl7::unescape(leaf, enc);
        r += L"Value: " + dW(replaceAll(un, "\n", " / ")) + (un != leaf ? L"   (raw " + dW(leaf) + L")" : L"") + L"\r\n";
        if (p.nreps > 1) r += fmtW(L"Repetition %d of %d", p.rep, p.nreps) + L"\r\n";
        if (p.comp || p.nreps > 1) r += L"Field: " + dW(p.fieldText) + L"\r\n";
        if (!p.meaning.empty()) r += L"Meaning: " + dW(p.meaning);
    }
    SetDlgItemTextW(g_panel, IDC_READOUT, r.c_str());
}
static void onCaret(bool rebuildIfNeeded = true) {
    if (!g_panel || !g_panelVisible) return; refreshDoc(); size_t caret = caretPos();
    int mi = hl7::messageIndexAt(g_msgs, caret); if (mi != g_cur || g_treeMsg != g_cur || g_nodes.empty()) { g_cur = mi; if (rebuildIfNeeded) showView(); }
    updateReadout(caret);
    if (g_highlight) { clearIndicator(IND_FIELD); if (g_lastPos.ok && g_lastPos.field > 0) { size_t a = g_lastPos.comp ? g_lastPos.compStart : g_lastPos.fieldStart, b = g_lastPos.comp ? g_lastPos.compEnd : g_lastPos.fieldEnd; if (g_lastPos.sub) { a = g_lastPos.subStart; b = g_lastPos.subEnd; } fillIndicator(IND_FIELD, a, b); } }
    selectNodeFor(caret);
}
static void layoutPanel(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc); int W = (int)rc.right, H = (int)rc.bottom;
    auto rectOf = [&](int id) { RECT r; GetWindowRect(GetDlgItem(hwnd, id), &r); MapWindowPoints(nullptr, hwnd, (POINT*)&r, 2); return r; };
    auto stretch = [&](int id, int h = -1) { RECT r = rectOf(id); MoveWindow(GetDlgItem(hwnd, id), (int)r.left, (int)r.top, std::max(40, W - 2 * (int)r.left), h < 0 ? (int)(r.bottom - r.top) : h, TRUE); };
    RECT st = rectOf(IDC_STATUS), tr = rectOf(IDC_TREE); int m = (int)tr.left, sh = (int)(st.bottom - st.top);
    stretch(IDC_MSGLBL); stretch(IDC_VIEW, 200); stretch(IDC_FILTER); stretch(IDC_READOUT);
    { const int rows[2][2] = { { IDC_COPYPATH, IDC_GOTO }, { IDC_EXPAND, IDC_COLLAPSE } };   // two buttons per row, each half the width
      for (auto& r : rows) { RECT b = rectOf(r[0]); int x = (int)b.left, bh = (int)(b.bottom - b.top), half = std::max(30, (W - 2 * x - 2) / 2); MoveWindow(GetDlgItem(hwnd, r[0]), x, (int)b.top, half, bh, TRUE); MoveWindow(GetDlgItem(hwnd, r[1]), x + half + 2, (int)b.top, std::max(30, W - x - (x + half + 2)), bh, TRUE); } }
    int top = (int)tr.top, bottom = H - sh - 6, w = std::max(40, W - 2 * m), h = std::max(40, bottom - top);
    MoveWindow(GetDlgItem(hwnd, IDC_TREE), m, top, w, h, TRUE); MoveWindow(GetDlgItem(hwnd, IDC_LIST), m, top, w, h, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_STATUS), m, H - sh - 3, w, sh, TRUE);
}
static void cmdGoTo(); static void cmdCopyPath(); static void gotoMessage(int delta);
static void expandAll(bool expand) { HWND tv = GetDlgItem(g_panel, IDC_TREE); SendMessage(tv, WM_SETREDRAW, FALSE, 0); std::vector<HTREEITEM> st; HTREEITEM h = TreeView_GetRoot(tv); while (h) { TreeView_Expand(tv, h, expand ? TVE_EXPAND : TVE_COLLAPSE); HTREEITEM c = TreeView_GetChild(tv, h); if (c) { st.push_back(h); h = c; continue; } HTREEITEM nx = TreeView_GetNextSibling(tv, h); while (!nx && !st.empty()) { nx = TreeView_GetNextSibling(tv, st.back()); st.pop_back(); } h = nx; } SendMessage(tv, WM_SETREDRAW, TRUE, 0); InvalidateRect(tv, nullptr, TRUE); }
static void showPanel(bool show);
static void cmdTreeAll(bool expand) { if (!g_panel || !g_panelVisible) showPanel(true); HWND v = GetDlgItem(g_panel, IDC_VIEW); if (SendMessage(v, CB_GETCURSEL, 0, 0) != V_TREE) { SendMessage(v, CB_SETCURSEL, V_TREE, 0); SendMessage(g_panel, WM_COMMAND, MAKEWPARAM(IDC_VIEW, CBN_SELCHANGE), (LPARAM)v); } expandAll(expand); }
static void cmdCollapseAll() { cmdTreeAll(false); }
static void cmdExpandAll() { cmdTreeAll(true); }
static bool goToPosText(const std::string& pos) {
    if (pos.empty() || g_cur < 0) return false; std::string p = pos; size_t sp = p.find(' '); if (sp != std::string::npos) p = p.substr(0, sp);
    hl7::Span s; if (!hl7::resolvePath(g_msgs[g_cur], p, s)) return false; selectSpan(s.a, s.b, true); return true;
}
static INT_PTR CALLBACK panelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        for (auto n : VIEW_NAMES) SendDlgItemMessageW(hwnd, IDC_VIEW, CB_ADDSTRING, 0, (LPARAM)n); SendDlgItemMessage(hwnd, IDC_VIEW, CB_SETCURSEL, 0, 0);
        CheckDlgButton(hwnd, IDC_FOLLOW, g_follow ? BST_CHECKED : BST_UNCHECKED); CheckDlgButton(hwnd, IDC_HIDEEMPTY, g_hideEmpty ? BST_CHECKED : BST_UNCHECKED);
        SendDlgItemMessageW(hwnd, IDC_FILTER, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter fields by name, value or meaning");
        TreeView_SetIndent(GetDlgItem(hwnd, IDC_TREE), 12); SendDlgItemMessageW(hwnd, IDC_EXPAND, BM_SETIMAGE, 0, 0); HWND lv = GetDlgItem(hwnd, IDC_LIST); ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP);
        LVCOLUMNW col = {}; col.mask = LVCF_TEXT | LVCF_WIDTH; col.cx = 80; col.pszText = (LPWSTR)L"Level"; ListView_InsertColumn(lv, 0, &col); col.pszText = (LPWSTR)L"Position"; ListView_InsertColumn(lv, 1, &col); col.cx = 400; col.pszText = (LPWSTR)L"Problem"; ListView_InsertColumn(lv, 2, &col);
        static HFONT mono = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas"); SendDlgItemMessage(hwnd, IDC_READOUT, WM_SETFONT, (WPARAM)mono, TRUE);
        SendMessage(g_npp._nppHandle, NPPM_DARKMODESUBCLASSANDTHEME, (WPARAM)NppDarkMode::dmfInit, (LPARAM)hwnd);
        layoutPanel(hwnd); return TRUE; }
    case WM_SIZE: layoutPanel(hwnd); return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_PREV: gotoMessage(-1); return TRUE;
        case IDC_NEXT: gotoMessage(1); return TRUE;
        case IDC_VIEW: if (HIWORD(wp) == CBN_SELCHANGE) { showView(); onCaret(false); } return TRUE;
        case IDC_FOLLOW: g_follow = IsDlgButtonChecked(hwnd, IDC_FOLLOW) == BST_CHECKED; saveSettings(); return TRUE;
        case IDC_HIDEEMPTY: g_hideEmpty = IsDlgButtonChecked(hwnd, IDC_HIDEEMPTY) == BST_CHECKED; saveSettings(); showView(); return TRUE;
        case IDC_FILTER: if (HIWORD(wp) == EN_CHANGE) SetTimer(hwnd, 3, 250, nullptr); return TRUE;
        case IDC_EXPAND: expandAll(true); return TRUE;
        case IDC_COLLAPSE: expandAll(false); return TRUE;
        case IDC_COPYPATH: cmdCopyPath(); return TRUE;
        case IDC_GOTO: cmdGoTo(); return TRUE;
        }
        return FALSE;
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lp;
        if (nm->code == DMN_CLOSE) { g_panelVisible = false; clearIndicator(IND_FIELD); return TRUE; }
        if (nm->idFrom == IDC_TREE && nm->code == TVN_SELCHANGEDW && !g_syncing) { NMTREEVIEWW* t = (NMTREEVIEWW*)lp; if (t->action != TVC_UNKNOWN) { int i = (int)t->itemNew.lParam; if (i >= 0 && i < (int)g_nodes.size() && g_nodes[i].depth > 0) { g_syncing = true; selectSpan(g_nodes[i].s.a, g_nodes[i].s.b, false); g_syncing = false; updateReadout(g_nodes[i].s.a); } } return TRUE; }
        if (nm->idFrom == IDC_TREE && (nm->code == NM_CLICK || nm->code == NM_DBLCLK)) { PostMessage(hwnd, WM_APP_EDITORFOCUS, 0, 0); return FALSE; }   // a click shows the field, then the arrow keys belong to the editor again
        if (nm->idFrom == IDC_LIST && (nm->code == NM_DBLCLK || nm->code == NM_RETURN)) { int sel = ListView_GetNextItem(nm->hwndFrom, -1, LVNI_SELECTED); if (sel >= 0 && sel < (int)g_rows.size()) { if (!goToPosText(g_rows[sel].pos)) setStatus(L"No position to go to for this row."); } return TRUE; }
        return FALSE; }
    case WM_APP_EDITORFOCUS: SetFocus(curSci()); return TRUE;
    case WM_TIMER:
        if (wp == 1) { KillTimer(hwnd, 1); if (g_follow) onCaret(); }
        if (wp == 2) { KillTimer(hwnd, 2); g_docStale = true; if (g_panelVisible) { refreshDoc(); g_treeMsg = -1; onCaret(); } }
        if (wp == 3) { KillTimer(hwnd, 3); if (curView() == V_TREE) { buildTree(); selectNodeFor(caretPos()); } }
        return TRUE;
    }
    return FALSE;
}
static FuncItem g_items[48]; static int g_itemCount = 0; static int g_idxPanel = 0, g_idxAutoOpen = 0, g_idxHighlight = 0;
static void ensurePanel() {
    if (g_panel) return; g_panel = CreateDialogParamW(g_hInst, MAKEINTRESOURCEW(IDD_LENS), g_npp._nppHandle, panelProc, 0);
    tTbData d = {}; d.hClient = g_panel; d.pszName = L"HL7Lens"; d.dlgID = g_idxPanel; d.uMask = DWS_DF_CONT_RIGHT; d.pszModuleName = MODULE_NAME; d.pszAddInfo = L"";
    SendMessage(g_npp._nppHandle, NPPM_DMMREGASDCKDLG, 0, (LPARAM)&d);
}
static void showPanel(bool show) {
    ensurePanel(); SendMessage(g_npp._nppHandle, show ? NPPM_DMMSHOW : NPPM_DMMHIDE, 0, (LPARAM)g_panel); g_panelVisible = show;
    SendMessage(g_npp._nppHandle, NPPM_SETMENUITEMCHECK, g_items[g_idxPanel]._cmdID, show);
    if (show) { setupIndicators(g_npp._scintillaMainHandle); setupIndicators(g_npp._scintillaSecondHandle); g_docStale = true; refreshDoc(); g_treeMsg = -1; onCaret(); setDocStatus(); }
    else clearIndicator(IND_FIELD);
}

// ------------------------------------------------------------------ commands
static void cmdTogglePanel() { showPanel(!g_panelVisible); }
static hl7::Pos posHere() { refreshDoc(); return locateAt(caretPos()); }
static void cmdCopyPath() { hl7::Pos p = posHere(); if (!p.ok) { msgBox(L"The caret is not on an HL7 segment."); return; } setClipboard(dW(p.path)); setStatus(L"Copied " + dW(p.path)); }
static void cmdCopyValue() {
    hl7::Pos p = posHere(); if (!p.ok || p.field == 0) { msgBox(L"The caret is not on an HL7 field."); return; }
    int mi = hl7::messageIndexAt(g_msgs, caretPos()); std::string leaf = p.sub ? p.subText : (p.comp ? p.compText : p.fieldText);
    setClipboard(dW(hl7::unescape(leaf, mi >= 0 ? g_msgs[mi].enc : hl7::Enc()))); setStatus(L"Copied the value of " + dW(p.path));
}
static void moveField(int dir) {
    refreshDoc(); size_t caret = caretPos(); std::vector<hl7::Span> fields; for (auto& m : g_msgs) for (auto& s : m.segs) for (auto& f : s.fields) { if (f.reps.size() > 1) for (auto& r : f.reps) fields.push_back(r.s); else fields.push_back(f.s); }   // a repeating field is one stop per repetition
    if (fields.empty()) return; int target = -1;
    if (dir > 0) { for (size_t i = 0; i < fields.size(); i++) if (fields[i].a > caret) { target = (int)i; break; } }
    else { for (int i = (int)fields.size() - 1; i >= 0; i--) if (fields[i].b < caret || (fields[i].a < caret && caret <= fields[i].b && i > 0 && false)) { target = i; break; } for (int i = (int)fields.size() - 1; i >= 0; i--) if (fields[i].a <= caret && caret <= fields[i].b) { target = i - 1; break; } }
    if (target < 0 || target >= (int)fields.size()) return; selectSpan(fields[target].a, fields[target].b, true); if (g_panelVisible) onCaret();
}
static void cmdNextField() { moveField(1); }
static void cmdPrevField() { moveField(-1); }
static void gotoMessage(int delta) {
    refreshDoc(); if (g_msgs.empty()) return; int mi = hl7::messageIndexAt(g_msgs, caretPos()) + delta; if (mi < 0 || mi >= (int)g_msgs.size()) { setStatus(delta > 0 ? L"This is the last message." : L"This is the first message."); return; }
    HWND sci = curSci(); SendMessage(sci, SCI_GOTOPOS, g_msgs[mi].s.a, 0); int line = (int)SendMessage(sci, SCI_LINEFROMPOSITION, g_msgs[mi].s.a, 0); SendMessage(sci, SCI_SETFIRSTVISIBLELINE, line, 0); SetFocus(sci); if (g_panelVisible) onCaret();
}
static void cmdNextMessage() { gotoMessage(1); }
static void cmdPrevMessage() { gotoMessage(-1); }
struct InputData { const wchar_t* label; std::wstring value; };
static INT_PTR CALLBACK inputProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    InputData* d = (InputData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_INITDIALOG: d = (InputData*)lp; SetWindowLongPtrW(hwnd, GWLP_USERDATA, lp); SetDlgItemTextW(hwnd, IDC_INLABEL, d->label); SetDlgItemTextW(hwnd, IDC_INEDIT, d->value.c_str()); SendMessage(g_npp._nppHandle, NPPM_DARKMODESUBCLASSANDTHEME, (WPARAM)NppDarkMode::dmfInit, (LPARAM)hwnd); return TRUE;
    case WM_COMMAND: if (LOWORD(wp) == IDOK) { wchar_t b[2048] = { 0 }; GetDlgItemTextW(hwnd, IDC_INEDIT, b, 2048); d->value = b; EndDialog(hwnd, 1); return TRUE; } if (LOWORD(wp) == IDCANCEL) { EndDialog(hwnd, 0); return TRUE; } return FALSE;
    case WM_CLOSE: EndDialog(hwnd, 0); return TRUE;
    }
    return FALSE;
}
static std::wstring g_testInput; static bool g_haveTestInput = false;
static bool askInput(const wchar_t* label, std::wstring& value) { if (g_quiet && g_haveTestInput) { value = g_testInput; g_haveTestInput = false; return true; } InputData d{ label, value }; if (DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_INPUT), g_npp._nppHandle, inputProc, (LPARAM)&d) == 1) { value = d.value; return true; } return false; }
static std::wstring g_lastPath = L"PID-3";
static bool goToPath(const std::string& path) {
    refreshDoc(); if (g_msgs.empty()) return false; int start = std::max(0, hl7::messageIndexAt(g_msgs, caretPos()));
    for (int k = 0; k < (int)g_msgs.size(); k++) { int mi = (start + k) % (int)g_msgs.size(); hl7::Span s; if (hl7::resolvePath(g_msgs[mi], path, s)) { selectSpan(s.a, s.b, true); if (g_panelVisible) onCaret(); return true; } }
    return false;
}
static void cmdGoTo() {
    std::wstring v = g_lastPath; if (!askInput(L"Go to field path, for example PID-3.1, OBX[2]-5, MSH-9.2 or PID-3(2).4:", v) || v.empty()) return; g_lastPath = v;
    if (!goToPath(toU8(v))) msgBox(L"Not found in any message: " + v, MB_OK | MB_ICONWARNING);
}
static void cmdMarkAll() {
    hl7::Pos p = posHere(); if (!p.ok) { msgBox(L"Put the caret on a field first; every occurrence of that field position is then marked."); return; }
    auto occ = hl7::occurrences(g_msgs, p.segId, p.field, p.comp, p.sub); clearIndicator(IND_MARK); setupIndicators(curSci()); for (auto& s : occ) fillIndicator(IND_MARK, s.a, s.b);
    std::string what = p.segId + (p.field ? "-" + std::to_string(p.field) + (p.comp ? "." + std::to_string(p.comp) + (p.sub ? "." + std::to_string(p.sub) : "") : "") : "");
    std::wstring m = fmtW(L"Marked %d occurrence(s) of %s.", (int)occ.size(), dW(what).c_str()); setStatus(m); if (!g_panelVisible) msgBox(m);
}
static void cmdClearMarks() { clearIndicator(IND_MARK); setStatus(L"Marks cleared."); }
static void cmdFindValues() {
    std::wstring v = g_lastPath; if (!askInput(L"List a field from every message, for example OBX-5, PID-3.1 or MSH-10:", v) || v.empty()) return; g_lastPath = v;
    refreshDoc(); std::string path = toU8(v); std::string out = "HL7Lens: values of " + path + " in " + std::to_string(g_msgs.size()) + " message(s)\r\n\r\nMsg\tControl ID\tPath\tValue\tMeaning\r\n"; int found = 0;
    for (size_t mi = 0; mi < g_msgs.size(); mi++) {
        const auto& m = g_msgs[mi]; std::string ctl; { hl7::Span cs; if (hl7::resolvePath(m, "MSH-10", cs)) ctl = spanText(cs); }
        // every segment occurrence of the base segment
        std::string id = path.substr(0, std::min<size_t>(3, path.size())); for (auto& c : id) c = (char)toupper((unsigned char)c); int count = 0; for (auto& s : m.segs) if (s.id == id) count++;
        std::string rest = path.size() > 3 ? path.substr(3) : ""; if (!rest.empty() && rest[0] == '[') { size_t e = rest.find(']'); rest = e == std::string::npos ? rest : rest.substr(e + 1); count = 1; }
        for (int k = 1; k <= count; k++) { std::string p = id + (k > 1 || path.find('[') != std::string::npos ? "[" + std::to_string(path.find('[') != std::string::npos ? atoi(path.substr(path.find('[') + 1).c_str()) : k) + "]" : "") + rest; hl7::Span s; std::string norm; if (!hl7::resolvePath(m, p, s, &norm)) continue; std::string val = hl7::unescape(spanText(s), m.enc); hl7::Pos lp = hl7::locate(g_doc, s.a, m.enc); out += std::to_string(mi + 1) + "\t" + ctl + "\t" + norm + "\t" + replaceAll(val, "\n", " / ") + "\t" + lp.meaning + "\r\n"; found++; }
    }
    out += "\r\n" + std::to_string(found) + " value(s).\r\n"; newTab(out);
}
static void cmdValidateReport() {
    refreshDoc(); if (g_msgs.empty()) { msgBox(L"No HL7 message in this document."); return; }
    std::string out = "HL7Lens validation report: " + std::to_string(g_msgs.size()) + " message(s)\r\n"; int errs = 0, warns = 0;
    for (size_t i = 0; i < g_msgs.size(); i++) {
        hl7::Message pm = hl7::parseMessage(g_doc.substr(g_msgs[i].s.a, g_msgs[i].s.b - g_msgs[i].s.a)); const hl7::Segment* msh = hl7::findSeg(pm, "MSH");
        out += "\r\nMessage " + std::to_string(i + 1) + ": " + hl7::fieldValue(msh, 9, 1) + "^" + hl7::fieldValue(msh, 9, 2) + "  control ID " + hl7::fieldValue(msh, 10) + "  version " + pm.version + "\r\n";
        int n = 0; auto put = [&](const std::string& l, const std::string& pos, const std::string& t) { out += "  " + std::string(l == "err" ? "ERROR  " : l == "warn" ? "WARN   " : "INFO   ") + (pos.empty() ? "" : pos + ": ") + t + "\r\n"; n++; if (l == "err") errs++; if (l == "warn") warns++; };
        hl7dict::VersionScope vs(pm.version); for (auto& x : pm.notes) put(x.level, "", x.text); for (auto& x : hl7::validateStructural(pm)) put(x.level, x.pos, x.text); for (auto& x : hl7::versionReport(pm).notes) put(x.level, x.pos, x.text); for (auto& x : hl7::versionFieldChecks(pm)) put(x.level, x.pos, x.text);
        if (!n) out += "  OK\r\n";
    }
    out += "\r\nTotal: " + std::to_string(errs) + " error(s), " + std::to_string(warns) + " warning(s).\r\n"; newTab(out);
}
static void cmdPretty() { refreshDoc(); auto ms = hl7::parseAll(g_doc); if (ms.empty()) { msgBox(L"No HL7 message in this document."); return; } std::string out; for (size_t i = 0; i < ms.size(); i++) { hl7dict::VersionScope vs(ms[i].version); out += "=== Message " + std::to_string(i + 1) + " of " + std::to_string(ms.size()) + " ===\r\n" + crlf(hl7::prettyPrint(ms[i], g_hideEmpty)); } newTab(out); }
static void cmdJson() { refreshDoc(); auto ms = hl7::parseAll(g_doc); if (ms.empty()) { msgBox(L"No HL7 message in this document."); return; } std::string out = "["; for (size_t i = 0; i < ms.size(); i++) { if (i) out += ",\r\n"; { hl7dict::VersionScope vs(ms[i].version); out += hl7::toJson(ms[i]); } } newTab(out + "]\r\n"); }
static void cmdDeid() { refreshDoc(); if (!isHl7Text(g_doc) && hl7::parseAll(g_doc).empty()) { msgBox(L"No HL7 message in this document."); return; } newTab(crlf(hl7::deidentify(g_doc, true, true)) + "\r\n"); setStatus(L"De-identified copy opened in a new tab (best effort: review before sharing)."); }
static void cmdAck() {
    refreshDoc(); int mi = hl7::messageIndexAt(g_msgs, caretPos()); if (mi < 0) { msgBox(L"No HL7 message at the caret."); return; }
    std::wstring v = L"AA"; if (!askInput(L"Acknowledgment code AA, AE or AR, optionally followed by a space and the text for MSA-3:", v) || v.empty()) return;
    std::string s = toU8(v); size_t sp = s.find(' '); std::string code = s.substr(0, sp), txt = sp == std::string::npos ? "" : s.substr(sp + 1); for (auto& c : code) c = (char)toupper((unsigned char)c);
    std::string ack = hl7::buildAck(g_doc.substr(g_msgs[mi].s.a, g_msgs[mi].s.b - g_msgs[mi].s.a), code, txt); if (ack.empty()) { msgBox(L"The message at the caret has no MSH segment."); return; } newTab(crlf(ack) + "\r\n");
}
static void cmdRepair() {
    HWND sci = curSci(); bool sel = SendMessage(sci, SCI_GETSELECTIONEMPTY, 0, 0) == 0; size_t a = sel ? (size_t)SendMessage(sci, SCI_GETSELECTIONSTART, 0, 0) : 0, b = sel ? (size_t)SendMessage(sci, SCI_GETSELECTIONEND, 0, 0) : (size_t)SendMessage(sci, SCI_GETLENGTH, 0, 0);
    std::string all = docText(sci); std::string part = all.substr(a, b - a); hl7::Repair r = hl7::repair(part, false, false);
    if (!r.changed) { msgBox(L"Nothing to repair: the text is already clean HL7."); return; }
    std::string fixed = replaceAll(r.repaired, "\r", docEol(sci)); SendMessage(sci, SCI_BEGINUNDOACTION, 0, 0); SendMessage(sci, SCI_SETTARGETRANGE, a, b); SendMessage(sci, SCI_REPLACETARGET, fixed.size(), (LPARAM)fixed.c_str()); SendMessage(sci, SCI_ENDUNDOACTION, 0, 0);
    std::wstring rep = L"Repaired (Ctrl+Z undoes it):\n"; for (auto& c : r.changes) rep += L"  - " + toW(c.second) + L"\n"; g_docStale = true; setStatus(fmtW(L"Repair: %d change(s).", (int)r.changes.size())); msgBox(rep);
}
static void convertEol(int mode) { HWND sci = curSci(); SendMessage(sci, SCI_CONVERTEOLS, mode, 0); SendMessage(sci, SCI_SETEOLMODE, mode, 0); g_docStale = true; }
static void cmdSegmentsOnLines() { convertEol(SC_EOL_CRLF); setStatus(L"Segment separators converted to CRLF: one segment per line."); }
static void cmdSegmentsCR() { convertEol(SC_EOL_CR); setStatus(L"Segment separators converted to CR only (the HL7 standard separator)."); }
static void transformSelection(bool escape) {
    HWND sci = curSci(); if (SendMessage(sci, SCI_GETSELECTIONEMPTY, 0, 0)) { msgBox(L"Select the text first."); return; }
    size_t a = SendMessage(sci, SCI_GETSELECTIONSTART, 0, 0), b = SendMessage(sci, SCI_GETSELECTIONEND, 0, 0); std::string t = docText(sci).substr(a, b - a);
    refreshDoc(); int mi = hl7::messageIndexAt(g_msgs, a); hl7::Enc enc = mi >= 0 ? g_msgs[mi].enc : hl7::Enc();
    std::string out = escape ? hl7::escapeText(t, enc) : replaceAll(hl7::unescape(t, enc), "\n", docEol(sci));
    SendMessage(sci, SCI_REPLACESEL, 0, (LPARAM)out.c_str()); SendMessage(sci, SCI_SETSEL, a, a + out.size()); g_docStale = true;
}
static void cmdEscape() { transformSelection(true); }
static void cmdUnescape() { transformSelection(false); }
static std::wstring settingsRoot() { std::wstring d = g_configDir; for (int i = 0; i < 3; i++) d = dirOf(d); return d; }
static int findUdlMenuId() {
    int udlID = 0; int n = (int)SendMessage(g_npp._nppHandle, NPPM_GETNBUSERLANG, 0, (LPARAM)&udlID); HMENU hm = (HMENU)SendMessage(g_npp._nppHandle, NPPM_GETMENUHANDLE, NPPMAINMENU, 0);
    for (int i = 0; i <= n + 1; i++) { wchar_t b[256] = { 0 }; GetMenuStringW(hm, udlID + i, b, 256, MF_BYCOMMAND); std::wstring s = b; if (s == L"HL7 v2" || s.find(L"HL7 v2") == 0) return udlID + i; }
    return 0;
}
static bool installUdl(std::wstring& rep) { std::wstring dir = settingsRoot() + L"\\userDefineLangs", f = dir + L"\\HL7_v2.xml"; CreateDirectoryW(dir.c_str(), nullptr); bool ok = writeFile(f, assets::UDL_XML); rep = ok ? L"HL7 v2 user language written to:\n" + f : L"Could not write " + f; return ok; }
static void cmdInstallUdl() { std::wstring rep; bool ok = installUdl(rep); msgBox(rep + (ok ? L"\n\nRestart Notepad++ once. Files ending in .hl7 then color automatically, and Language > HL7 v2 colors any other file." : L""), ok ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONWARNING); }
static void cmdSetLang() { int id = findUdlMenuId(); if (id) { SendMessage(g_npp._nppHandle, NPPM_MENUCOMMAND, 0, id); return; } if (msgBox(L"The HL7 v2 user language is not installed yet.\n\nInstall it now? Notepad++ must be restarted once afterwards.", MB_YESNO | MB_ICONQUESTION) == IDYES) cmdInstallUdl(); }
static void cmdToggleHighlight() { g_highlight = !g_highlight; saveSettings(); SendMessage(g_npp._nppHandle, NPPM_SETMENUITEMCHECK, g_items[g_idxHighlight]._cmdID, g_highlight); if (!g_highlight) clearIndicator(IND_FIELD); else if (g_panelVisible) onCaret(false); }
static void cmdToggleAutoOpen() { g_autoOpen = !g_autoOpen; saveSettings(); SendMessage(g_npp._nppHandle, NPPM_SETMENUITEMCHECK, g_items[g_idxAutoOpen]._cmdID, g_autoOpen); }
static void cmdSample() { newTab(crlf(assets::SAMPLE)); SendMessage(curSci(), SCI_SETEOLMODE, SC_EOL_CRLF, 0); int id = findUdlMenuId(); if (id) SendMessage(g_npp._nppHandle, NPPM_MENUCOMMAND, 0, id); if (!g_panelVisible) showPanel(true); else { g_docStale = true; onCaret(); } }
static void cmdSettings() { if (!fileExists(g_iniPath)) saveSettings(); SendMessage(g_npp._nppHandle, NPPM_DOOPEN, 0, (LPARAM)g_iniPath.c_str()); }

// ------------------------------------------------------------------ compare two messages
static std::wstring fileLabel() { std::wstring p = currentPath(); size_t k = p.find_last_of(L"\\/"); return k == std::wstring::npos ? p : p.substr(k + 1); }
static hl7::Message docMessage(const std::string& text, const hl7::DocMessage& m) { return hl7::parseMessage(text.substr(m.s.a, m.s.b - m.s.a)); }
static void showDiff(const hl7::Message& a, const hl7::Message& b, const std::wstring& la, const std::wstring& lb) { newTab(crlf(hl7::diffReport(a, b, toU8(la), toU8(lb)))); setStatus(L"Comparison opened in a new tab."); }
static void cmdCompareInFile() {
    refreshDoc(); int mi = hl7::messageIndexAt(g_msgs, caretPos()); if (mi < 0) { msgBox(L"Put the caret in the message to compare (message A) first."); return; }
    if (g_msgs.size() < 2) { msgBox(L"This document holds one message. Paste the second message below it, or use 'Compare with the clipboard' or 'Compare with the other view'."); return; }
    int def = mi + 1 < (int)g_msgs.size() ? mi + 2 : mi; std::wstring v = std::to_wstring(def);
    if (!askInput(fmtW(L"Compare message %d (A) with message number (1 to %d):", mi + 1, (int)g_msgs.size()).c_str(), v)) return;
    int other = _wtoi(v.c_str()) - 1; if (other < 0 || other >= (int)g_msgs.size() || other == mi) { msgBox(L"Enter the number of a different message in this document."); return; }
    std::wstring f = fileLabel(); showDiff(docMessage(g_doc, g_msgs[mi]), docMessage(g_doc, g_msgs[other]), fmtW(L"message %d of %d in ", mi + 1, (int)g_msgs.size()) + f, fmtW(L"message %d of %d in ", other + 1, (int)g_msgs.size()) + f);
}
static void cmdCompareClipboard() {
    refreshDoc(); int mi = hl7::messageIndexAt(g_msgs, caretPos()); if (mi < 0) { msgBox(L"Put the caret in the message to compare (message A) first."); return; }
    auto ms = hl7::parseAll(toU8(getClipboard())); if (ms.empty()) { msgBox(L"The clipboard does not hold an HL7 message. Copy message B first."); return; }
    showDiff(docMessage(g_doc, g_msgs[mi]), ms[0], fmtW(L"message %d in ", mi + 1) + fileLabel(), L"the clipboard" + (ms.size() > 1 ? fmtW(L" (first of %d messages)", (int)ms.size()) : std::wstring()));
}
static void cmdCompareOtherView() {
    refreshDoc(); int mi = hl7::messageIndexAt(g_msgs, caretPos()); if (mi < 0) { msgBox(L"Put the caret in the message to compare (message A) first."); return; }
    HWND other = curSci() == g_npp._scintillaMainHandle ? g_npp._scintillaSecondHandle : g_npp._scintillaMainHandle;
    if (!IsWindowVisible(other)) { msgBox(L"Only one view is open. Open message B, then right-click its tab > Move Document > Move to Other View, put the caret in message B and run this again."); return; }
    std::string t = docText(other); auto ms = hl7::scanDocument(t); int oi = hl7::messageIndexAt(ms, (size_t)SendMessage(other, SCI_GETCURRENTPOS, 0, 0)); if (oi < 0 && !ms.empty()) oi = 0;
    if (oi < 0) { msgBox(L"The other view does not hold an HL7 message."); return; }
    showDiff(docMessage(g_doc, g_msgs[mi]), docMessage(t, ms[oi]), fmtW(L"message %d in ", mi + 1) + fileLabel(), fmtW(L"message %d of %d in the other view", oi + 1, (int)ms.size()));
}

// ------------------------------------------------------------------ custom segments, support
static const char* CUSTOM_EXAMPLE =
    "\r\n; ---- Custom segments ------------------------------------------------------------\r\n"
    "; One section per segment. Fields are numbered from 1:  n=Name|DATATYPE  (add |repeats if it repeats).\r\n"
    "; Save this file and HL7Lens picks the definitions up at once (tree, readout, validation, compare).\r\n"
    "; Remove the semicolons in front of the example to try it.\r\n"
    ";[ZCT]\r\n;Description=Lab contact (site-specific)\r\n;1=Set ID|SI\r\n;2=Contact Role|CWE\r\n;3=Contact Name|XPN|repeats\r\n;4=Contact Phone|XTN|repeats\r\n";
static void cmdDefineSegments() {
    saveSettings(); std::string t; { HANDLE h = CreateFileW(g_iniPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr); if (h != INVALID_HANDLE_VALUE) { char b[65536]; DWORD n = 0; ReadFile(h, b, sizeof b, &n, nullptr); t.assign(b, n); CloseHandle(h); } }
    if (t.find("Custom segments") == std::string::npos) { HANDLE h = CreateFileW(g_iniPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr); if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, CUSTOM_EXAMPLE, (DWORD)strlen(CUSTOM_EXAMPLE), &w, nullptr); CloseHandle(h); } }
    SendMessage(g_npp._nppHandle, NPPM_DOOPEN, 0, (LPARAM)g_iniPath.c_str()); SendMessage(curSci(), SCI_DOCUMENTEND, 0, 0);
}
static const wchar_t* SUPPORT_URL = L"https://buymeacoffee.com/jcinc";
static void cmdSupport() { ShellExecuteW(g_npp._nppHandle, L"open", SUPPORT_URL, nullptr, nullptr, SW_SHOWNORMAL); }
static const wchar_t* AUTHOR = L"Joseph C McGinty Jr.";
static std::wstring aboutVersion() { return fmtW(L"Version %s  (%s)   |   HL7 v2.1 to 2.8   |   Works offline", PLUGIN_VERSION, sizeof(void*) == 8 ? L"64-bit" : L"32-bit"); }
static std::wstring aboutText() {
    return fmtW(
        L"AUTHOR\r\n"
        L"Created, written and maintained by %s. HL7Lens is the author's own independent work.\r\n\r\n"
        L"WHAT IT DOES\r\n"
        L"A docked decoder panel with a message tree, a live readout of the field under the caret (path, name, data type, value, meaning), validation, and version and ELR-readiness checks. Field navigation, go to path, list a field across every message, mark every occurrence, pretty print, JSON, ACK builder, de-identified copy, repair, escape and unescape, and an HL7 v2 syntax-color installer.\r\n"
        L"Dictionary: %d segments, %d data types, %d code tables, %d message types.\r\n\r\n"
        L"PRIVACY\r\n"
        L"HL7Lens works entirely offline. It reads only the text open in the editor, sends nothing over any network, and stores no message content. De-identification is a best-effort aid, not a guarantee: always review a copy before sharing it.\r\n\r\n"
        L"INDEPENDENCE AND TRADEMARKS\r\n"
        L"HL7Lens is an independent tool. HL7 and Health Level Seven are registered trademarks of Health Level Seven International; HL7Lens is not affiliated with, endorsed by or sponsored by HL7 International or any software vendor. Segment, field and table names follow the published HL7 v2 standard and are shown for reference only. All other names are the property of their owners.\r\n"
        L"HL7Lens is an independent personal work of its author. It is not associated with, produced for or endorsed by any employer, agency, laboratory or institution.\r\n\r\n"
        L"NO WARRANTY\r\n"
        L"This software is provided \"as is\", without warranty of any kind, express or implied, including the warranties of merchantability, fitness for a particular purpose and non-infringement. Decoded values, names and checks are an aid to reading and do not replace the applicable specification or interface agreement. The user remains solely responsible for any message, data or configuration they create, change, send or rely on. In no event shall the author be liable for any claim, damages or other liability arising from, out of or in connection with this software or its use.\r\n\r\n"
        L"SUPPORT DEVELOPMENT\r\n"
        L"HL7Lens is free. If it saves you time, you can support its development by buying me a coffee: https://buymeacoffee.com/jcinc\r\n\r\n"
        L"CODE SIGNING\r\n"
        L"HL7Lens.dll is Authenticode-signed (SHA-256, timestamped) with the author's own code-signing certificate:\r\n"
        L"  Subject: CN = %s\r\n"
        L"  SHA-256 thumbprint: C3ED172F446450D6CDF58B7257483D58A7641B73423D578BEF8C0FE54C92455D\r\n"
        L"Check it in the DLL's Properties > Digital Signatures.\r\n\r\n"
        L"SETTINGS FILE\r\n%s\r\n\r\n"
        L"LICENSE\r\n"
        L"HL7Lens is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License, version 3 or (at your option) any later version. Source: https://github.com/Subzero121800/HL7Lens\r\n"
        L"Version-specific field definitions (HL7 v2.1 to v2.7.1) come from hl7-dictionary, MIT License, Copyright (c) 2015 Fernando Serrano.\r\n\r\n"
        L"Copyright (c) 2026 %s Licensed under the GNU GPL v3.",
        AUTHOR, hl7dict::N_SEGMENTS, hl7dict::N_DATATYPES, hl7dict::N_TABLES, hl7dict::N_MSGTYPES, AUTHOR, g_iniPath.c_str(), AUTHOR);
}
static HFONT g_abTitleFont = nullptr, g_abBoldFont = nullptr;
static INT_PTR CALLBACK aboutProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG: {
        HFONT base = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0); LOGFONTW lf = {}; GetObjectW(base, sizeof lf, &lf);
        LOGFONTW t = lf; t.lfWeight = FW_BOLD; t.lfHeight = lf.lfHeight * 7 / 4; if (!g_abTitleFont) g_abTitleFont = CreateFontIndirectW(&t);
        LOGFONTW b = lf; b.lfWeight = FW_BOLD; if (!g_abBoldFont) g_abBoldFont = CreateFontIndirectW(&b);
        SendDlgItemMessageW(hwnd, IDC_AB_TITLE, WM_SETFONT, (WPARAM)g_abTitleFont, FALSE); SendDlgItemMessageW(hwnd, IDC_AB_AUTHOR, WM_SETFONT, (WPARAM)g_abBoldFont, FALSE);
        SetDlgItemTextW(hwnd, IDC_AB_VER, aboutVersion().c_str());
        SetDlgItemTextW(hwnd, IDC_AB_AUTHOR, (std::wstring(L"Created by ") + AUTHOR).c_str());
        SetDlgItemTextW(hwnd, IDC_AB_COPY, (std::wstring(L"Copyright \u00A9 2026 ") + AUTHOR + L" Licensed under GPL v3.").c_str());
        SetDlgItemTextW(hwnd, IDC_AB_TEXT, aboutText().c_str());
        SendMessage(g_npp._nppHandle, NPPM_DARKMODESUBCLASSANDTHEME, (WPARAM)NppDarkMode::dmfInit, (LPARAM)hwnd);
        SetFocus(GetDlgItem(hwnd, IDOK)); return FALSE; }
    case WM_COMMAND: if (LOWORD(wp) == IDC_AB_COFFEE) { cmdSupport(); return TRUE; } if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) { EndDialog(hwnd, 1); return TRUE; } return FALSE;
    case WM_CLOSE: EndDialog(hwnd, 1); return TRUE;
    }
    return FALSE;
}
static void cmdAbout() { DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_ABOUT), g_npp._nppHandle, aboutProc, 0); }
static void cmdCollapseAll();
static void CALLBACK showAboutLater(HWND, UINT, UINT_PTR id, DWORD) { KillTimer(nullptr, id); wchar_t v[8] = { 0 }; GetEnvironmentVariableW(L"HL7LENS_SHOWABOUT", v, 8); if (v[0] == L'c') cmdCollapseAll();
    else cmdAbout(); }   // screenshot hooks (self-test hold mode only)

// ------------------------------------------------------------------ self-test (HL7LENS_SELFTEST=<report file>)
static std::string treeItemText(HWND tv, HTREEITEM h) { wchar_t b[1100] = { 0 }; TVITEMW it = {}; it.mask = TVIF_TEXT; it.hItem = h; it.pszText = b; it.cchTextMax = 1100; SendMessage(tv, TVM_GETITEMW, 0, (LPARAM)&it); return toU8(b); }
static void setCaretAt(const std::string& needle, size_t extra = 0) { std::string t = docText(curSci()); size_t p = t.find(needle); if (p != std::string::npos) { SendMessage(curSci(), SCI_GOTOPOS, p + extra, 0); onCaret(); } }
static void selfTest() {
    std::string rep = "selftest start\n"; g_quiet = true; auto chk = [&](bool c, const std::string& m) { rep += std::string(c ? "ok   " : "FAIL ") + m + "\n"; writeFile(g_selfTest, rep); };
    try {
        chk(true, "plugin loaded, NPPN_READY received");
        newTab(crlf(assets::SAMPLE)); SendMessage(curSci(), SCI_SETEOLMODE, SC_EOL_CRLF, 0); UINT_PTR firstBuf = curBuffer(); (void)firstBuf;
        chk(isHl7Doc(), "sample document detected as HL7");
        showPanel(true); chk(g_panel && IsWindow(g_panel) && g_panelVisible, "decoder panel created and docked");
        chk(g_msgs.size() == 2, "document scanned: " + std::to_string(g_msgs.size()) + " messages");
        HWND tv = GetDlgItem(g_panel, IDC_TREE); int count = (int)TreeView_GetCount(tv); chk(count > 30, "message tree built: " + std::to_string(count) + " items");
        setCaretAt("JANE", 1); wchar_t rb[2048] = { 0 }; GetDlgItemTextW(g_panel, IDC_READOUT, rb, 2048); std::string ro = toU8(rb);
        chk(ro.find("PID-5.2") == 0 && ro.find("Given Name") != std::string::npos, "readout follows the caret: " + ro.substr(0, ro.find('\r')));
        HTREEITEM sel = TreeView_GetSelection(tv); std::string st = sel ? treeItemText(tv, sel) : ""; chk(st.find("PID-5.2") == 0, "tree selection follows the caret: " + st);
        setCaretAt("77^^^OTHER", 0); GetDlgItemTextW(g_panel, IDC_READOUT, rb, 2048); ro = toU8(rb); chk(ro.find("PID-3(2).1") == 0, "repetition path in readout: " + ro.substr(0, ro.find('\r')));
        setCaretAt("20260101080000", 2); GetDlgItemTextW(g_panel, IDC_READOUT, rb, 2048); ro = toU8(rb); chk(ro.find("OBR-7") == 0 && ro.find("2026-01-01 08:00") != std::string::npos, "timestamp decoded in readout");
        { Sci_Position a0 = 0; (void)a0; size_t before = caretPos(); cmdNextField(); size_t after = (size_t)SendMessage(curSci(), SCI_GETSELECTIONSTART, 0, 0); chk(after > before, "next field moves forward"); cmdPrevField(); chk((size_t)SendMessage(curSci(), SCI_GETSELECTIONSTART, 0, 0) < after, "previous field moves back"); }
        chk(goToPath("OBX[2]-5"), "go to OBX[2]-5"); { HWND s = curSci(); size_t a = SendMessage(s, SCI_GETSELECTIONSTART, 0, 0), b = SendMessage(s, SCI_GETSELECTIONEND, 0, 0); chk(docText(s).substr(a, b - a) == "140", "selection is the value 140"); }
        chk(goToPath("PID-5"), "go to PID-5 from message 1"); { gotoMessage(1); chk(g_cur == 1, "next message moves to message 2"); GetDlgItemTextW(g_panel, IDC_MSGLBL, rb, 2048); chk(toU8(rb) == "Message 2 of 2", "panel label: " + toU8(rb)); gotoMessage(-1); chk(g_cur == 0, "previous message back to message 1"); }
        setCaretAt("Glucose", 1); cmdMarkAll(); { HWND s = curSci(); int marked = 0; for (size_t i = 0, n = SendMessage(s, SCI_GETLENGTH, 0, 0); i < n; i++) if (SendMessage(s, SCI_INDICATORVALUEAT, IND_MARK, i)) { marked++; } chk(marked == (int)(strlen("Glucose") + strlen("Sodium")), "mark all OBX-3.2 marks Glucose and Sodium (" + std::to_string(marked) + " chars)"); cmdClearMarks(); }
        SendDlgItemMessage(g_panel, IDC_VIEW, CB_SETCURSEL, V_VALIDATE, 0); showView(); HWND lv = GetDlgItem(g_panel, IDC_LIST); chk(ListView_GetItemCount(lv) >= 1 && IsWindowVisible(lv), "validation view lists " + std::to_string(ListView_GetItemCount(lv)) + " row(s)");
        SendDlgItemMessage(g_panel, IDC_VIEW, CB_SETCURSEL, V_VERSION, 0); showView(); chk(ListView_GetItemCount(lv) >= 15, "version and ELR view lists " + std::to_string(ListView_GetItemCount(lv)) + " rows");
        SendDlgItemMessage(g_panel, IDC_VIEW, CB_SETCURSEL, V_TREE, 0); showView();
        SetDlgItemTextW(g_panel, IDC_FILTER, L"glucose"); buildTree(); int filtered = (int)TreeView_GetCount(tv); chk(filtered > 0 && filtered < count, "filter narrows the tree to " + std::to_string(filtered) + " items"); SetDlgItemTextW(g_panel, IDC_FILTER, L""); buildTree();
        UINT_PTR doc = curBuffer();
        cmdPretty(); { std::string t = docText(curSci()); chk(t.find("Patient Name") != std::string::npos && t.find("DOE, JANE M") != std::string::npos && t.find("=== Message 2 of 2") != std::string::npos, "pretty print opens a decoded report"); }
        LRESULT pos = SendMessage(g_npp._nppHandle, NPPM_GETPOSFROMBUFFERID, doc, 0); SendMessage(g_npp._nppHandle, NPPM_ACTIVATEDOC, (WPARAM)(pos >> 30), (LPARAM)(pos & 0x3FFFFFFF));
        cmdJson(); { std::string t = docText(curSci()); chk(t.size() > 10 && t[0] == '[' && t.find("\"PID-5 Patient Name\"") != std::string::npos, "JSON export"); }
        SendMessage(g_npp._nppHandle, NPPM_ACTIVATEDOC, (WPARAM)(pos >> 30), (LPARAM)(pos & 0x3FFFFFFF));
        setCaretAt("ORU^R01"); g_testInput = L"AE bad thing"; g_haveTestInput = true; cmdAck(); { std::string t = docText(curSci()); chk(t.compare(0, 4, "MSH|") == 0 && t.find("MSA|AE|MSG0001|bad thing") != std::string::npos, "ACK built for message 1"); }
        SendMessage(g_npp._nppHandle, NPPM_ACTIVATEDOC, (WPARAM)(pos >> 30), (LPARAM)(pos & 0x3FFFFFFF));
        cmdDeid(); { std::string t = docText(curSci()); chk(t.find("DOE") == std::string::npos && t.find("ROE") == std::string::npos && t.find("Glucose") != std::string::npos, "de-identified copy masks names, keeps results"); }
        SendMessage(g_npp._nppHandle, NPPM_ACTIVATEDOC, (WPARAM)(pos >> 30), (LPARAM)(pos & 0x3FFFFFFF));
        g_testInput = L"OBX-5"; g_haveTestInput = true; cmdFindValues(); { std::string t = docText(curSci()); chk(t.find("\tOBX-5\t99") != std::string::npos && t.find("OBX[2]-5\t140") != std::string::npos, "list OBX-5 across messages"); }
        SendMessage(g_npp._nppHandle, NPPM_ACTIVATEDOC, (WPARAM)(pos >> 30), (LPARAM)(pos & 0x3FFFFFFF));
        cmdValidateReport(); { std::string t = docText(curSci()); chk(t.find("Message 2:") != std::string::npos && t.find("Total:") != std::string::npos, "validation report for all messages"); }
        // escape / unescape on a scratch tab
        newTab("a|b^c"); SendMessage(curSci(), SCI_SETSEL, 0, 5); cmdEscape(); chk(docText(curSci()) == "a\\F\\b\\S\\c", "escape selection"); SendMessage(curSci(), SCI_SELECTALL, 0, 0); cmdUnescape(); chk(docText(curSci()) == "a|b^c", "unescape selection"); SendMessage(curSci(), SCI_SETSAVEPOINT, 0, 0);
        // repair in place
        newTab("MSH|^~\\&|A|B|C|D|2026||ORU^R01|1|P|2.5.1\r\n   PID|1||42||DOE^JOHN\r\n\r\nOBX|1|TX|X||line one\r\ncontinued\r\n"); cmdRepair(); { std::string t = docText(curSci()); chk(t.find("PID|1||42") != std::string::npos && t.find("line onecontinued") != std::string::npos && t.find("\r\n\r\n") == std::string::npos, "repair in place"); SendMessage(curSci(), SCI_UNDO, 0, 0); chk(docText(curSci()).find("   PID") != std::string::npos, "repair is undoable"); SendMessage(curSci(), SCI_SETSAVEPOINT, 0, 0); }
        // line ends
        newTab("MSH|^~\\&|A\rPID|1\r"); cmdSegmentsOnLines(); chk(docText(curSci()) == "MSH|^~\\&|A\r\nPID|1\r\n", "segments onto lines (CRLF)"); cmdSegmentsCR(); chk(docText(curSci()) == "MSH|^~\\&|A\rPID|1\r", "back to CR separators"); SendMessage(curSci(), SCI_SETSAVEPOINT, 0, 0);
        // UDL
        std::wstring ur; chk(installUdl(ur), "HL7 v2 user language written"); int udl = findUdlMenuId(); chk(true, std::string("UDL menu id ") + (udl ? "found (installed before this start)" : "not loaded yet (needs restart)"));
        if (udl) { SendMessage(g_npp._nppHandle, NPPM_ACTIVATEDOC, (WPARAM)(pos >> 30), (LPARAM)(pos & 0x3FFFFFFF)); SendMessage(g_npp._nppHandle, NPPM_MENUCOMMAND, 0, udl); int lang = -1; SendMessage(g_npp._nppHandle, NPPM_GETCURRENTLANGTYPE, 0, (LPARAM)&lang); chk(lang == L_USER, "Language > HL7 v2 applied"); }
        { refreshDoc(); cmdCollapseAll(); HWND tv = GetDlgItem(g_panel, IDC_TREE); int open = 0, roots = 0; for (HTREEITEM h = TreeView_GetRoot(tv); h; h = TreeView_GetNextSibling(tv, h)) { roots++; if (TreeView_GetItemState(tv, h, TVIS_EXPANDED) & TVIS_EXPANDED) open++; }
          chk(roots > 1 && open == 0, "Collapse all leaves " + std::to_string(roots) + " top-level rows, none expanded");
          cmdExpandAll(); open = 0; for (HTREEITEM h = TreeView_GetRoot(tv); h; h = TreeView_GetNextSibling(tv, h)) if (TreeView_GetItemState(tv, h, TVIS_EXPANDED) & TVIS_EXPANDED) open++; chk(open > 0, "Expand all reopens the tree"); cmdCollapseAll(); }
        { // arrow keys in the editor with the panel open and caret-follow on: the caret must stay where the key put it
          newTab(crlf(assets::SAMPLE)); SendMessage(curSci(), SCI_SETEOLMODE, SC_EOL_CRLF, 0); if (!g_panelVisible) showPanel(true); g_follow = true; CheckDlgButton(g_panel, IDC_FOLLOW, BST_CHECKED);
          HWND sci = curSci(); SetFocus(sci); SendMessage(sci, SCI_GOTOPOS, 0, 0); g_docStale = true; onCaret();
          auto pump = [](int ms) { DWORD end = GetTickCount() + ms; MSG m; while ((int)(end - GetTickCount()) > 0) { while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } Sleep(10); } };
          int lines = (int)SendMessage(sci, SCI_GETLINECOUNT, 0, 0); std::string seq; bool okDown = true;
          for (int k = 1; k < lines; k++) { PostMessageW(sci, WM_KEYDOWN, VK_DOWN, 0x00500001); PostMessageW(sci, WM_KEYUP, VK_DOWN, 0xC0500001); pump(400); int ln = (int)SendMessage(sci, SCI_LINEFROMPOSITION, caretPos(), 0); seq += std::to_string(ln) + " "; if (ln != k) okDown = false; }
          chk(okDown, "arrow Down walks every line with the panel following (" + seq + ")");
          std::string seq2; bool okUp = true; for (int k = lines - 2; k >= 0; k--) { PostMessageW(sci, WM_KEYDOWN, VK_UP, 0x00480001); PostMessageW(sci, WM_KEYUP, VK_UP, 0xC0480001); pump(400); int ln = (int)SendMessage(sci, SCI_LINEFROMPOSITION, caretPos(), 0); seq2 += std::to_string(ln) + " "; if (ln != k) okUp = false; }
          chk(okUp, "arrow Up walks every line back (" + seq2 + ")"); SendMessage(sci, SCI_SETSAVEPOINT, 0, 0);
          // click a field in the tree with the mouse, then press Down: the key must reach the editor and move one line
          { HWND tv = GetDlgItem(g_panel, IDC_TREE); SendMessage(sci, SCI_GOTOPOS, SendMessage(sci, SCI_POSITIONFROMLINE, 2, 0) + 6, 0); onCaret(); pump(300);
            HTREEITEM h = TreeView_GetSelection(tv); RECT r = {}; TreeView_GetItemRect(tv, h, &r, TRUE); TreeView_SelectItem(tv, TreeView_GetRoot(tv)); pump(100);
            LPARAM pt = MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2); PostMessage(tv, WM_LBUTTONDOWN, MK_LBUTTON, pt); PostMessage(tv, WM_LBUTTONUP, 0, pt); pump(500);
            int before = (int)SendMessage(sci, SCI_LINEFROMPOSITION, caretPos(), 0); HWND f = GetFocus(); PostMessageW(f, WM_KEYDOWN, VK_DOWN, 0x00500001); PostMessageW(f, WM_KEYUP, VK_DOWN, 0xC0500001); pump(400);
            int after = (int)SendMessage(sci, SCI_LINEFROMPOSITION, caretPos(), 0);
            chk(after == before + 1, "tree click then Down moves the editor one line (line " + std::to_string(before) + " -> " + std::to_string(after) + ", focus " + (f == sci ? "editor" : f == tv ? "tree" : "other") + ")"); SendMessage(sci, SCI_SETSAVEPOINT, 0, 0); }
        }
        { // ---- 1.1.0: versions, repetitions, custom segments, compare, status line
          auto obr = [](const std::string& v44) { std::string o = "OBR|1|ORD1|FILL1|85025^CBC^CPT"; for (int k = 0; k < 39; k++) o += "|"; return o + "|" + v44; };
          auto msg = [&](const std::string& ts, const std::string& cid, const std::string& ver, const std::string& given, const std::string& v44, const std::string& extra) {
              return "MSH|^~\\&|LIS|LAB|EHR|CLINIC|" + ts + "||ORU^R01|" + cid + "|P|" + ver + "\r\nPID|1||12345^^^HOSP^MR||DOE^" + given + "||19800101|F\r\n" + obr(v44) + "\r\nOBX|1|NM|718-7^Hemoglobin^LN||13.2|g/dL|12-16|N|||F\r\n" + extra; };
          // status line agrees with the panel after switching from an empty tab
          newTab(""); showPanel(true); onCaret(); wchar_t sb[256] = { 0 };
          newTab(msg("20261016101500", "MSG100", "2.3", "JANE", "85025~26543~89745", "")); g_docStale = true; onCaret(); GetDlgItemTextW(g_panel, IDC_STATUS, sb, 256); wchar_t lb[128] = { 0 }; GetDlgItemTextW(g_panel, IDC_MSGLBL, lb, 128);
          chk(std::wstring(sb).find(L"1 message(s)") != std::wstring::npos && std::wstring(lb) == L"Message 1 of 1", "status line follows the document: " + toU8(sb) + " / " + toU8(lb));
          setCaretAt("85025~", 8); wchar_t rb2[2048] = { 0 }; GetDlgItemTextW(g_panel, IDC_READOUT, rb2, 2048); std::wstring ro2 = rb2;
          chk(ro2.find(L"OBR-44(2)") == 0 && ro2.find(L"Field 44") != std::wstring::npos && ro2.find(L"(v2.3)") != std::wstring::npos && ro2.find(L"Repetition 2 of 3") != std::wstring::npos, "v2.3 message: OBR-44 not labelled from a later version, repetition 2 of 3");
          { HWND tv = GetDlgItem(g_panel, IDC_TREE); bool sumOk = false, repOk = false; for (int i = 0; i < (int)g_nodes.size(); i++) (void)i;
            std::vector<HTREEITEM> st; HTREEITEM h = TreeView_GetRoot(tv); while (h) { std::string tx = treeItemText(tv, h); if (tx.find("HL7 v2.3 definitions") != std::string::npos) sumOk = true; if (tx.find("OBR-44") == 0 && tx.find("[3 repetitions]") != std::string::npos) repOk = true; HTREEITEM c = TreeView_GetChild(tv, h); if (c) { st.push_back(h); h = c; continue; } HTREEITEM nx = TreeView_GetNextSibling(tv, h); while (!nx && !st.empty()) { nx = TreeView_GetNextSibling(tv, st.back()); st.pop_back(); } h = nx; }
            chk(sumOk && repOk, "tree: summary names the v2.3 definitions, OBR-44 shows [3 repetitions]"); }
          { HWND v = GetDlgItem(g_panel, IDC_VIEW); SendMessage(v, CB_SETCURSEL, V_VALIDATE, 0); showView(); HWND lv = GetDlgItem(g_panel, IDC_LIST); bool found = false; for (int i = 0; i < ListView_GetItemCount(lv); i++) { wchar_t b[512] = { 0 }; ListView_GetItemText(lv, i, 2, b, 512); if (std::wstring(b).find(L"not part of this version") != std::wstring::npos) found = true; }
            chk(found, "validation view: OBR-44 is not part of v2.3"); SendMessage(v, CB_SETCURSEL, V_TREE, 0); showView(); }
          // next field steps through repetitions
          setCaretAt("85025~", 0); cmdNextField(); { int a = (int)SendMessage(curSci(), SCI_GETSELECTIONSTART, 0, 0), b = (int)SendMessage(curSci(), SCI_GETSELECTIONEND, 0, 0); std::string d = docText(curSci()); chk(d.substr(a, b - a) == "26543", "next field stops at each repetition: " + d.substr(a, b - a)); }
          SendMessage(curSci(), SCI_SETSAVEPOINT, 0, 0);
          // v2.5.1 names
          newTab(msg("20261016101500", "MSG100", "2.5.1", "JANE", "85025~26543~89745", "")); g_docStale = true; setCaretAt("85025~", 8); GetDlgItemTextW(g_panel, IDC_READOUT, rb2, 2048); ro2 = rb2;
          chk(ro2.find(L"Procedure Code") != std::wstring::npos && ro2.find(L"(v2.5.1)") != std::wstring::npos, "v2.5.1 message: OBR-44 = Procedure Code");
          SendMessage(curSci(), SCI_SETSAVEPOINT, 0, 0);
          // custom segment from settings.ini
          WritePrivateProfileStringW(L"ZCT", L"Description", L"Lab contact (test)", g_iniPath.c_str()); WritePrivateProfileStringW(L"ZCT", L"1", L"Set ID|SI", g_iniPath.c_str()); WritePrivateProfileStringW(L"ZCT", L"2", L"Contact Role|CWE", g_iniPath.c_str()); WritePrivateProfileStringW(L"ZCT", L"3", L"Contact Name|XPN|repeats", g_iniPath.c_str());
          { FILETIME z = {}; g_iniStamp = z; }
          newTab(msg("20261016101500", "MSG100", "2.5.1", "JANE", "85025", "ZCT|1|PRIMARY^Primary^L|SMITH^ANN~JONES^BOB\r\n")); g_docStale = true; setCaretAt("JONES", 1); GetDlgItemTextW(g_panel, IDC_READOUT, rb2, 2048); ro2 = rb2;
          chk(hl7dict::isCustom("ZCT") && ro2.find(L"ZCT-3(2).1   Contact Name > Family Name") == 0 && ro2.find(L"(custom)") != std::wstring::npos, "custom ZCT from settings.ini: " + toU8(ro2.substr(0, ro2.find(L"\r"))));
          SendMessage(curSci(), SCI_SETSAVEPOINT, 0, 0); WritePrivateProfileStringW(L"ZCT", nullptr, nullptr, g_iniPath.c_str()); { FILETIME z = {}; g_iniStamp = z; } loadCustomSegments(); chk(!hl7dict::isCustom("ZCT"), "custom definition removed again");
          // compare two messages in one file, and with the clipboard
          std::string two = msg("20261016101500", "MSG100", "2.5.1", "JANE", "85025~26543~89745", "") + msg("20261016111500", "MSG101", "2.5.1", "JOHN", "85025", "NTE|1||Hemolyzed\r\n");
          newTab(two); g_docStale = true; setCaretAt("JANE", 1); g_testInput = L"2"; g_haveTestInput = true; cmdCompareInFile(); { std::string d = docText(curSci());
            chk(d.find("3 value difference(s)") != std::string::npos && d.find("A: JANE") != std::string::npos && d.find("NTE|1||Hemolyzed") != std::string::npos && d.find("EXPECTED TO DIFFER") != std::string::npos, "compare message 1 with message 2 in the same file"); SendMessage(curSci(), SCI_SETSAVEPOINT, 0, 0); }
          setClipboard(toW(msg("20261016101500", "MSG100", "2.5.1", "JANE", "85025~26543~89745", ""))); newTab(two); g_docStale = true; setCaretAt("JANE", 1); cmdCompareClipboard(); { std::string d = docText(curSci()); chk(d.find("same content") != std::string::npos, "compare with the clipboard (identical message)"); SendMessage(curSci(), SCI_SETSAVEPOINT, 0, 0); }
        }
        chk(hl7dict::N_SEGMENTS >= 25 && hl7dict::N_TABLES >= 40, "dictionary loaded");
        { HWND ab = CreateDialogParamW(g_hInst, MAKEINTRESOURCEW(IDD_ABOUT), g_npp._nppHandle, aboutProc, 0); wchar_t a1[256] = { 0 }, a2[256] = { 0 }; std::wstring body;
          if (ab) { GetDlgItemTextW(ab, IDC_AB_AUTHOR, a1, 256); GetDlgItemTextW(ab, IDC_AB_COPY, a2, 256); int n = GetWindowTextLengthW(GetDlgItem(ab, IDC_AB_TEXT)); body.resize(n + 1); GetDlgItemTextW(ab, IDC_AB_TEXT, &body[0], n + 1); body.resize(n);
            RECT cr; GetClientRect(ab, &cr); bool fits = true; std::string why; HDC dc = GetDC(ab);
            for (int id : { IDC_AB_TITLE, IDC_AB_VER, IDC_AB_AUTHOR, IDC_AB_COPY, IDC_AB_TAG, IDC_AB_TEXT, IDOK, IDC_AB_COFFEE }) { HWND c = GetDlgItem(ab, id); RECT r; GetWindowRect(c, &r); MapWindowPoints(nullptr, ab, (POINT*)&r, 2);
              if (r.left < 0 || r.top < 0 || r.right > cr.right || r.bottom > cr.bottom) { fits = false; why += " outside:" + std::to_string(id); }
              if (id != IDC_AB_TEXT) { wchar_t t[256] = { 0 }; GetWindowTextW(c, t, 256); HGDIOBJ o = SelectObject(dc, (HFONT)SendMessage(c, WM_GETFONT, 0, 0)); SIZE sz; GetTextExtentPoint32W(dc, t, lstrlenW(t), &sz); SelectObject(dc, o);
                if (sz.cx > r.right - r.left || sz.cy > r.bottom - r.top) { fits = false; why += " clipped:" + std::to_string(id) + "(" + std::to_string(sz.cx) + "x" + std::to_string(sz.cy) + " in " + std::to_string(r.right - r.left) + "x" + std::to_string(r.bottom - r.top) + ")"; } } }
            ReleaseDC(ab, dc); chk(fits, "About layout: every label fits its box, all controls inside the dialog" + why); DestroyWindow(ab); }
          chk(ab && std::wstring(a1) == L"Created by Joseph C McGinty Jr." && std::wstring(a2).find(L"2026 Joseph C McGinty Jr.") != std::wstring::npos, "About dialog: Created by + copyright lines");
          { std::wstring tail = L"2026 Joseph C McGinty Jr. Licensed under the GNU GPL v3."; chk(body.size() > 2100 && body.size() > tail.size() && body.compare(body.size() - tail.size(), tail.size(), tail) == 0, "About text complete (" + std::to_string(body.size()) + " chars, ends with the copyright line)"); } }
    } catch (...) { chk(false, "exception"); }
    rep += "selftest end\n"; writeFile(g_selfTest, rep);
    wchar_t hold[8] = { 0 }; GetEnvironmentVariableW(L"HL7LENS_SELFTEST_HOLD", hold, 8);
    // mark every scratch tab saved so Notepad++ can close without prompts
    for (int k = 0; k < 20; k++) { SendMessage(curSci(), SCI_SETSAVEPOINT, 0, 0); if (hold[0]) break; SendMessage(g_npp._nppHandle, NPPM_MENUCOMMAND, 0, IDM_FILE_CLOSE); }
    if (hold[0]) {
        SetWindowPos(g_npp._nppHandle, nullptr, 0, 0, 1260, 780, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
        newTab(crlf(assets::SAMPLE)); SendMessage(curSci(), SCI_SETEOLMODE, SC_EOL_CRLF, 0); int id = findUdlMenuId(); if (id) SendMessage(g_npp._nppHandle, NPPM_MENUCOMMAND, 0, id);
        SendMessage(curSci(), SCI_SETSAVEPOINT, 0, 0); g_docStale = true; g_quiet = false; setStatus(L"2 message(s) in this document."); setCaretAt("Glucose", 2); { wchar_t ab[8] = { 0 }; if (GetEnvironmentVariableW(L"HL7LENS_SHOWABOUT", ab, 8)) SetTimer(nullptr, 0, 1500, showAboutLater); } return;
    }
    g_quiet = false; PostMessage(g_npp._nppHandle, WM_CLOSE, 0, 0);
}

// ------------------------------------------------------------------ plugin exports
static ShortcutKey g_skPanel = { true, true, false, 'H' }, g_skGoto = { true, true, false, 'G' }, g_skNext = { true, true, false, VK_RIGHT }, g_skPrev = { true, true, false, VK_LEFT }, g_skNextMsg = { true, true, false, VK_NEXT }, g_skPrevMsg = { true, true, false, VK_PRIOR };
static void addItem(const wchar_t* name, PFUNCPLUGINCMD f, ShortcutKey* sk = nullptr, bool checked = false) { FuncItem& it = g_items[g_itemCount++]; lstrcpynW(it._itemName, name, menuItemSize); it._pFunc = f; it._pShKey = sk; it._init2Check = checked; }
BOOL APIENTRY DllMain(HINSTANCE h, DWORD reason, LPVOID) { if (reason == DLL_PROCESS_ATTACH) { g_hInst = h; INITCOMMONCONTROLSEX ic = { sizeof ic, ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_STANDARD_CLASSES }; InitCommonControlsEx(&ic); } return TRUE; }
extern "C" __declspec(dllexport) void setInfo(NppData d) {
    g_npp = d; wchar_t b[MAX_PATH * 2] = { 0 }; SendMessage(g_npp._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH * 2, (LPARAM)b);
    g_configDir = std::wstring(b) + L"\\HL7Lens"; CreateDirectoryW(g_configDir.c_str(), nullptr); g_iniPath = g_configDir + L"\\settings.ini"; loadSettings();
    wchar_t env[MAX_PATH * 2] = { 0 }; if (GetEnvironmentVariableW(L"HL7LENS_SELFTEST", env, MAX_PATH * 2)) g_selfTest = env;
    g_itemCount = 0;
    g_idxPanel = g_itemCount; addItem(L"HL7Lens panel", cmdTogglePanel, &g_skPanel);
    addItem(L"Go to field path...", cmdGoTo, &g_skGoto);
    addItem(L"Next field", cmdNextField, &g_skNext);
    addItem(L"Previous field", cmdPrevField, &g_skPrev);
    addItem(L"Next message", cmdNextMessage, &g_skNextMsg);
    addItem(L"Previous message", cmdPrevMessage, &g_skPrevMsg);
    addItem(L"Copy field path at caret", cmdCopyPath);
    addItem(L"Copy field value at caret (unescaped)", cmdCopyValue);
    addItem(L"Collapse all (panel tree)", cmdCollapseAll);
    addItem(L"Expand all (panel tree)", cmdExpandAll);
    addItem(L"-", nullptr);
    addItem(L"Mark this field in every segment", cmdMarkAll);
    addItem(L"Clear marks", cmdClearMarks);
    addItem(L"List a field from every message...", cmdFindValues);
    addItem(L"Validation report (all messages)", cmdValidateReport);
    addItem(L"-", nullptr);
    addItem(L"Compare message at caret with another message in this file...", cmdCompareInFile);
    addItem(L"Compare message at caret with the clipboard", cmdCompareClipboard);
    addItem(L"Compare message at caret with the other view", cmdCompareOtherView);
    addItem(L"-", nullptr);
    addItem(L"Decoded report (pretty print)", cmdPretty);
    addItem(L"Export as JSON", cmdJson);
    addItem(L"Build ACK for the message at caret...", cmdAck);
    addItem(L"De-identified copy", cmdDeid);
    addItem(L"Repair message (selection or document)", cmdRepair);
    addItem(L"Escape selection", cmdEscape);
    addItem(L"Unescape selection", cmdUnescape);
    addItem(L"Segments onto separate lines (CRLF)", cmdSegmentsOnLines);
    addItem(L"Segment separators to CR only", cmdSegmentsCR);
    addItem(L"-", nullptr);
    addItem(L"Set language to HL7 v2", cmdSetLang);
    addItem(L"Install HL7 v2 syntax colors", cmdInstallUdl);
    g_idxHighlight = g_itemCount; addItem(L"Highlight the field at caret", cmdToggleHighlight, nullptr, g_highlight);
    g_idxAutoOpen = g_itemCount; addItem(L"Open panel automatically for HL7 files", cmdToggleAutoOpen, nullptr, g_autoOpen);
    addItem(L"Open a sample message", cmdSample);
    addItem(L"Define custom segments (Z-segments)...", cmdDefineSegments);
    addItem(L"Settings (settings.ini)", cmdSettings);
    addItem(L"About HL7Lens", cmdAbout);
    addItem(L"Support development (buy me a coffee)", cmdSupport);
}
extern "C" __declspec(dllexport) const wchar_t* getName() { return PLUGIN_NAME; }
extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* n) { *n = g_itemCount; return g_items; }
extern "C" __declspec(dllexport) void beNotified(SCNotification* n) {
    switch (n->nmhdr.code) {
    case NPPN_READY: setupIndicators(g_npp._scintillaMainHandle); setupIndicators(g_npp._scintillaSecondHandle); if (!g_selfTest.empty()) selfTest(); break;
    case NPPN_SHUTDOWN: saveSettings(); break;
    case NPPN_BUFFERACTIVATED: g_docStale = true; if (g_autoOpen && !g_panelVisible && isHl7Doc()) showPanel(true); else if (g_panel && g_panelVisible) SetTimer(g_panel, 2, 150, nullptr); break;
    case SCN_MODIFIED: if (n->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) { g_docStale = true; if (g_panel && g_panelVisible) SetTimer(g_panel, 2, 500, nullptr); } break;
    case SCN_UPDATEUI: if ((n->updated & SC_UPDATE_SELECTION) && g_panel && g_panelVisible && !g_syncing && (HWND)n->nmhdr.hwndFrom == curSci()) SetTimer(g_panel, 1, 120, nullptr); break;
    }
}
extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }
extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }
