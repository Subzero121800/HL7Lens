// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Joseph C McGinty Jr.
// Runs the HL7 Inspector's original TypeScript core (compiled to ref/hl7core-js) and prints the same JSON the C++ CLI prints,
// so tests/test_hl7.py can prove the port is faithful.  Usage: node oracle.js <file> [stamp]
const fs = require('fs'); const path = require('path');
const R = path.join(__dirname, '..', 'ref', 'hl7core-js');
const P = require(R + '/parser.js'), D = require(R + '/dictionary.js'), F = require(R + '/format.js'), V = require(R + '/validate.js'), A = require(R + '/ack.js'), X = require(R + '/deident.js');
const text = fs.readFileSync(process.argv[2], 'utf8'); const stamp = process.argv[3] || '20260101120000';
function meaning(seg, n) {
  const f = seg.fields[n - 1]; const val = f ? f.value : '';
  if (seg.id === 'MSH' && (n === 1 || n === 2)) return '';
  const type = D.fieldType(seg.id, n); let m = '';
  if (D.isTsField(seg.id, n) && val) m = F.fmtTS(val) || '';
  else if (type && f) { const fr = F.friendly(type, f.reps); if (fr && fr !== val) m = fr; }
  return m;
}
const msgs = P.parseAll(text);
const out = { messages: msgs.map(m => ({
  version: m.version, enc: m.enc, notes: m.notes,
  segments: m.segments.map(s => ({ id: s.id, raw: s.raw, desc: D.segDesc(s.id), fields: s.fields.map((f, i) => ({ value: f.value, reps: f.reps, name: D.fieldName(s.id, i + 1), type: D.fieldType(s.id, i + 1), meaning: meaning(s, i + 1) })) })),
  issues: V.validateStructural(m), version_report: V.versionReport(m), elr: V.elrChecks(m),
})),
  split: P.splitMessages(text),
  ack: (() => { const a = A.buildAck(text, 'AA', 'OK'); return a.replace(/\d{14}/g, stamp); })(),
  deid: X.deidentify(text, { orderIds: true, freeText: true }),
};
process.stdout.write(JSON.stringify(out));
