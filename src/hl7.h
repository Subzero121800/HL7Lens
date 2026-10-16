// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Joseph C McGinty Jr.
// HL7 v2 core - portable C++17 port of the HL7 Inspector's parser / format / validate / ack / deident modules,
// plus repair, locate (caret -> field path), pretty print and JSON. No platform dependencies.
#pragma once
#include <string>
#include <vector>
#include <utility>

namespace hl7 {

struct Enc { char fs = '|', comp = '^', rep = '~', sub = '&', esc = '\\'; };
typedef std::vector<std::vector<std::string>> Rep;            // component -> subcomponents (unescaped)
struct Field { std::string value; std::vector<Rep> reps; };
struct Segment { std::string id; std::vector<Field> fields; std::string raw; };
struct Note { std::string level, text; };                     // err | warn | info
struct Message { std::string raw; Enc enc; std::string version; std::vector<Segment> segments; std::vector<Note> notes; };

std::string normalize(const std::string& text);
Enc encFromHeader(const std::string& line);
std::string unescape(const std::string& s, const Enc& enc);
std::string escapeText(const std::string& s, const Enc& enc);   // inverse for plain text (separators + esc + CR/LF)
std::vector<std::string> splitMessages(const std::string& text);   // one raw string per message, segments joined by '\n'
Message parseMessage(const std::string& text);
Segment parseSegment(const std::string& line, const Enc& enc);
std::vector<Message> parseAll(const std::string& text);
std::string fieldValue(const Segment* seg, int n, int c = 1, int s = 1);
const Segment* findSeg(const Message& m, const std::string& id, int nth = 0);

// formatting
std::string fmtTS(const std::string& v);          // "" when not a valid HL7 TS
std::string ageFrom(const std::string& v);
std::string fmtPhone(const std::string& ln);
std::string friendlyRep(const std::string& type, const Rep& rep);
std::string friendly(const std::string& type, const std::vector<Rep>& reps);
std::string describeCode(const std::vector<std::string>& tableKeys, const std::string& code);
std::string fieldMeaning(const Segment& seg, int n);
std::string effectiveType(const std::string& segId, int field, const std::string& obx2);   // OBX-5 takes its type from OBX-2           // what the Inspector shows in its "meaning" column

// validation
struct Issue { std::string level, pos, text; };
std::vector<Issue> validateStructural(const Message& m);
struct VersionReport { std::string declared; std::vector<int> inferred; std::vector<std::pair<std::string, std::string>> feats; std::vector<std::string> used; std::vector<Issue> notes; std::string style, content; };
VersionReport versionReport(const Message& m);
struct ElrCheck { bool ok; std::string text; };
std::vector<ElrCheck> elrChecks(const Message& m);

// ack / deident / repair
std::string buildAck(const std::string& msgText, const std::string& code, const std::string& msaText, const std::string& stamp = "");
std::string deidentify(const std::string& text, bool orderIds = false, bool freeText = false);
struct Repair { std::string repaired; std::vector<std::pair<std::string, std::string>> changes; bool changed = false; };
Repair repair(const std::string& text, bool blankZeroDates = false, bool trimTrailingEmpty = false);

// summary + rendering
std::vector<std::pair<std::string, std::string>> summary(const Message& m);
std::string prettyPrint(const Message& m, bool hideEmpty = true);
std::string toJson(const Message& m);
std::string jsonEscape(const std::string& s);

// caret -> field path on the ORIGINAL document text (no normalization, offsets stay valid)
struct Pos {
    bool ok = false;
    std::string segId;              // "" when the line is not a segment
    int line = 0;                   // 0-based line in text
    int field = 0, rep = 0, comp = 0, sub = 0;   // 1-based; field 0 = on the segment id; rep/comp/sub 0 = n/a
    size_t segStart = 0, segEnd = 0, fieldStart = 0, fieldEnd = 0, repStart = 0, repEnd = 0, compStart = 0, compEnd = 0, subStart = 0, subEnd = 0;
    std::string path;               // "PID-3.1" / "OBX-5(2).1.2"  (rep shown only when the field repeats)
    std::string fieldName, fieldType, compName, compType, subName, label, fieldText, compText, subText, meaning;
    int nreps = 0;                  // repetitions in the field at the caret
};
Enc docEnc(const std::string& text);                          // from the first MSH/FHS/BHS line
Pos locate(const std::string& text, size_t caret);
Pos locate(const std::string& text, size_t caret, const Enc& enc);
std::pair<size_t, size_t> messageBounds(const std::string& text, size_t caret);   // [start,end) of the message around caret (MSH to next MSH)
std::vector<size_t> messageStarts(const std::string& text);   // offsets of every MSH/FHS/BHS line


// document scan with exact offsets (for the tree, navigation and highlighting). Offsets are byte offsets in the
// ORIGINAL text; each message keeps the separators declared by its own MSH/FHS/BHS.
struct Span { size_t a = 0, b = 0; };
struct CompSpan { Span s; std::vector<Span> subs; };
struct RepSpan { Span s; std::vector<CompSpan> comps; };
struct FieldSpan { Span s; std::vector<RepSpan> reps; };    // fields[0] = field 1
struct SegSpan { Span s; std::string id; std::vector<FieldSpan> fields; };
struct DocMessage { Span s; Enc enc; std::vector<SegSpan> segs; int skippedLines = 0; };
std::vector<DocMessage> scanDocument(const std::string& text);
int messageIndexAt(const std::vector<DocMessage>& msgs, size_t caret);       // -1 when none
// resolve "PID-3", "PID-3.1", "PID-3(2).1.2", "OBX[2]-5", "OBX-5" (first) inside one message; returns false when absent
bool resolvePath(const DocMessage& m, const std::string& path, Span& out, std::string* normalized = nullptr);
// every occurrence of the same field position (SEG-n[.c[.s]]) in the given messages
std::vector<Span> occurrences(const std::vector<DocMessage>& msgs, const std::string& segId, int field, int comp, int sub);


// compare two messages: segments aligned by ID (longest common subsequence), then field / repetition / component
struct DiffRow { std::string path, name, a, b; };
struct Diff { std::vector<DiffRow> changed, expected; std::vector<DiffRow> onlyA, onlyB; std::vector<Issue> issuesOnlyA, issuesOnlyB; int matched = 0; };
Diff diffMessages(const Message& a, const Message& b);
std::string diffReport(const Message& a, const Message& b, const std::string& labelA, const std::string& labelB);
// fields the message's own version does not define, or repeats where that version allows one (uses hl7dict context)
std::vector<Issue> versionFieldChecks(const Message& m);

} // namespace hl7
