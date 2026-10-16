#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Joseph C McGinty Jr.
"""Proves the C++ core matches the Inspector's TypeScript core (oracle) on every fixture, then runs extra checks
(locate, pretty print, repair, escape round trip).  Usage: test_hl7.py <cli> [--oracle]  (needs node + ref/hl7core-js for --oracle)"""
import json, os, subprocess, sys, glob
cli = sys.argv[1]; here = os.path.dirname(os.path.abspath(__file__)); use_oracle = '--oracle' in sys.argv
fails = 0; checks = 0
def ok(c, msg):
    global fails, checks; checks += 1
    print(('ok   ' if c else 'FAIL ') + msg)
    if not c: fails += 1
def run(args, inp=None):
    r = subprocess.run([cli] + args, input=inp, capture_output=True, text=True)
    if r.returncode != 0: raise RuntimeError(r.stderr)
    return r.stdout
def deep_diff(a, b, path=''):
    if type(a) != type(b): return [path + ': type %s vs %s' % (type(a).__name__, type(b).__name__)]
    if isinstance(a, dict):
        out = []
        for k in sorted(set(a) | set(b)):
            if k not in a or k not in b: out.append(path + '.' + k + ': missing'); continue
            out += deep_diff(a[k], b[k], path + '.' + k)
        return out
    if isinstance(a, list):
        if len(a) != len(b): return [path + ': len %d vs %d' % (len(a), len(b))]
        out = []
        for i, (x, y) in enumerate(zip(a, b)): out += deep_diff(x, y, path + '[%d]' % i)
        return out
    return [] if a == b else [path + ': %r vs %r' % (a, b)]
fixtures = sorted(glob.glob(os.path.join(here, 'fixtures', '*.hl7')) + glob.glob(os.path.join(here, 'fixtures', '*.txt')))
for fx in fixtures:
    name = os.path.basename(fx)
    cpp = json.loads(run(['json', fx, '20260101120000']))
    ok(len(cpp['messages']) >= 1, '%s: C++ parses %d message(s)' % (name, len(cpp['messages'])))
    if use_oracle:
        js = json.loads(subprocess.run(['node', os.path.join(here, '..', 'tools', 'oracle.js'), fx, '20260101120000'], capture_output=True, text=True, check=True).stdout)
        d = deep_diff(js, cpp)
        ok(not d, '%s: C++ output identical to the TypeScript oracle' % name + ('' if not d else ' -- ' + '; '.join(d[:6])))
# ---- extra checks on the first fixture style message
MSG = 'MSH|^~\\&|LIS|LAB|EHR|CLINIC|20260101120000||ORU^R01|MSG1|P|2.5.1\rPID|1||123456^^^HOSP^MR~77^^^OTHER^PI||DOE^JANE^M||19800101|F|||1 Main St^^Springfield^XX^00000\rOBR|1|P1|F1|CMP^Comprehensive Metabolic Panel^L\rOBX|1|NM|2345-7^Glucose^LN||99|mg/dL|70-99|N|||F\r'
loc = json.loads(run(['locate', '-', str(MSG.index('77^^^OTHER'))], MSG))
ok(loc['path'] == 'PID-3(2).1' and loc['fieldName'] == 'Patient Identifier List' and loc['compName'] == 'ID Number' and loc['subText'] == '77', 'locate: caret in a repetition gives PID-3(2).1 / ID Number: ' + loc['path'])
loc = json.loads(run(['locate', '-', str(MSG.index('JANE'))], MSG))
ok(loc['path'] == 'PID-5.2' and loc['compName'] == 'Given Name' and loc['fieldText'] == 'DOE^JANE^M', 'locate: PID-5.2 Given Name: ' + loc['path'])
loc = json.loads(run(['locate', '-', str(MSG.index('Glucose'))], MSG))
ok(loc['path'] == 'OBX-3.2' and loc['fieldName'] == 'Observation Identifier' and loc['compName'] == 'Text' and loc['meaning'].startswith('Glucose'), 'locate: OBX-3.2 Text with meaning: ' + loc['meaning'])
loc = json.loads(run(['locate', '-', str(MSG.index('ORU^R01'))], MSG))
ok(loc['path'] == 'MSH-9.1' and loc['fieldName'] == 'Message Type', 'locate: MSH numbering (MSH-9.1): ' + loc['path'])
loc = json.loads(run(['locate', '-', '5'], MSG))
ok(loc['path'] == 'MSH-2' and loc['fieldName'] == 'Encoding Characters', 'locate: MSH-2 encoding characters: ' + loc['path'])
loc = json.loads(run(['locate', '-', '1'], MSG))
ok(loc['path'] == 'MSH' and loc['field'] == 0, 'locate: caret on the segment id gives field 0')
crlf = MSG.replace('\r', '\r\n')
loc = json.loads(run(['locate', '-', str(crlf.index('Glucose'))], crlf))
ok(loc['path'] == 'OBX-3.2', 'locate: CRLF file gives the same path')
loc = json.loads(run(['locate', '-', str(MSG.index('|99|') + 1)], MSG))
ok(loc['path'] == 'OBX-5' and loc['fieldType'] == 'NM', 'locate: OBX-5 takes its data type from OBX-2 (%s)' % loc['fieldType'])
pp = run(['pretty', '-'], MSG)
ok('PID-5' in pp and 'Patient Name' in pp and 'DOE, JANE M' in pp and 'OBX-3.2' in pp and 'Glucose' in pp, 'pretty print lists fields with names, meanings and components')
rep = json.loads(run(['repair', '-'], 'MSH|^~\\&|A|B|C|D|2026||ORU^R01|1|P|2.5.1\n  PID|1||42||DOE^JOHN\n\nOBX|1|TX|X||line one\ncontinued here\n'))
ok('PID|1||42||DOE^JOHN' in rep['repaired'] and 'line onecontinued here' in rep['repaired'] and len(rep['changes']) >= 2, 'repair: trims, drops blank lines and rejoins wrapped lines (%d changes)' % len(rep['changes']))
esc = run(['escape', '-'], 'a|b^c&d~e\\f'); une = run(['unescape', '-'], esc)
ok(esc == 'a\\F\\b\\S\\c\\T\\d\\R\\e\\E\\f' and une == 'a|b^c&d~e\\f', 'escape / unescape round trip: ' + esc)
ack = run(['ack', '-', 'AE', 'bad', '20260101120000'], MSG).replace('\n', '\r')
ok(ack.startswith('MSH|^~\\&|EHR|CLINIC|LIS|LAB|20260101120000||ACK^R01|ACK20260101120000|P|2.5.1\rMSA|AE|MSG1|bad'), 'ack: swaps sender/receiver, echoes control id: ' + ack.replace('\r', ' / '))
deid = run(['deid', '-'], MSG)
ok('DOE' not in deid and 'XXX^XXXX^X' in deid and '000000^^^XXXX^XX' in deid and '2345-7^Glucose' in deid, 'deid: masks PID name/ids, keeps results')
summ = json.loads(run(['summary', '-'], MSG))
ok(any(k == 'Patient' and 'DOE, JANE M' in v for k, v in summ) and any(k == 'Message type' and 'ORU^R01' in v for k, v in summ), 'summary: patient + message type')
starts = json.loads(run(['messages', '-'], MSG + MSG))
ok(starts == [0, len(MSG)], 'messages: finds every MSH offset: %s' % starts)
DOC = MSG.replace('\r', '\r\n') + '\r\n\r\n' + MSG.replace('MSG1', 'MSG2').replace('OBX|1|NM|2345-7^Glucose^LN||99', 'OBX|1|NM|2345-7^Glucose^LN||99\rOBX|2|NM|2951-2^Sodium^LN||140')
sc = json.loads(run(['scan', '-'], DOC))
ok(len(sc) == 2 and [x[0] for x in sc[0]['segs']] == ['MSH', 'PID', 'OBR', 'OBX'] and [x[0] for x in sc[1]['segs']] == ['MSH', 'PID', 'OBR', 'OBX', 'OBX'], 'scan: 2 messages with mixed CRLF / CR separators')
ok(sc[0]['segs'][0][1] == 12, 'scan: MSH counts MSH-1 as a field (12 fields)')
r1 = json.loads(run(['resolve', '-', 'PID-5.2'], DOC)); ok(r1['ok'] and r1['text'] == 'JANE', 'resolve PID-5.2 = JANE')
r2 = json.loads(run(['resolve', '-', 'OBX[2]-5', '1'], DOC)); ok(r2['ok'] and r2['text'] == '140', 'resolve OBX[2]-5 in message 2 = 140')
r3 = json.loads(run(['resolve', '-', 'PID-3(2).4'], DOC)); ok(r3['ok'] and r3['text'] == 'OTHER' and r3['path'] == 'PID-3(2).4', 'resolve PID-3(2).4 = OTHER')
r4 = json.loads(run(['resolve', '-', 'msh-9.2'], DOC)); ok(r4['ok'] and r4['text'] == 'R01', 'resolve msh-9.2 (lower case) = R01')
r5 = json.loads(run(['resolve', '-', 'ZZZ-1'], DOC)); ok(not r5['ok'], 'resolve of an absent segment fails cleanly')
oc = json.loads(run(['occ', '-', 'OBX', '3', '2'], DOC)); ok(oc == ['Glucose', 'Glucose', 'Sodium'], 'occurrences of OBX-3.2 across both messages: %s' % oc)
oc2 = json.loads(run(['occ', '-', 'PID', '3', '1'], DOC)); ok(oc2 == ['123456', '77', '123456', '77'], 'occurrences include every repetition: %s' % oc2)

# ---- version-aware dictionary, version checks, message compare
import os as _os
FX = _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), 'fixtures')
V23, V251, VAR = [_os.path.join(FX, f) for f in ('08_v23_obr44.hl7', '09_v251_obr44.hl7', '10_v251_variant.hl7')]
t23 = open(V23, newline='').read(); p44 = str(t23.index('85025~') + 8)
l23 = run(['vlocate', V23, p44]).strip().split('\t'); l251 = run(['vlocate', V251, p44]).strip().split('\t')
ok(l23[0] == 'OBR-44(2)' and l23[1] == 'Field 44' and l23[6] == '2.3', 'v2.3 message: OBR-44 is not invented from a later version: %s' % l23[:2])
ok(l251[0] == 'OBR-44(2).1' and l251[1] == 'Procedure Code' and l251[2] == 'CE' and l251[5] == '3' and l251[6] == '2.5.1', 'v2.5.1 message: OBR-44 = Procedure Code [CE], repetition 2 of 3: %s' % l251)
c23 = run(['vcheck', V23]); c251 = run(['vcheck', V251])
ok('field 44 is not part of this version' in c23, 'version check: v2.3 defines 43 OBR fields')
ok('repeats 3 times' in c251 and 'does not allow repetitions' in c251, 'version check: OBR-44 repeating in v2.5.1 is flagged')
t271 = t23.replace('|2.7.1\r', '|').replace('|P|2.3\r', '|P|2.7.1\r'); l271 = run(['vlocate', '-', str(t271.index('85025^CBC') + 1)], t271).strip().split('\t')
ok(l271[2] == 'CWE' and l271[6] == '2.7.1', 'v2.7.1 message: OBR-4 is CWE (2.3 / 2.5.1 say CE): %s' % l271[2])
t28 = t23.replace('|P|2.3\r', '|P|2.8.2\r'); ok(run(['vlocate', '-', p44], t28).strip().split('\t')[6] == '2.7.1', 'v2.8.2 message uses the newest definitions (2.7.1)')
d = run(['diff', V251, VAR])
ok('3 value difference(s)' in d and '1 segment(s) only in B' in d, 'diff: summary counts')
ok('PID-5.2' in d and 'A: JANE' in d and 'B: JOHN' in d, 'diff: component-level difference with both values')
ok('OBR-44(3)' in d and 'A: 89745' in d, 'diff: per-repetition difference')
ok('SEGMENTS ONLY IN B' in d and 'NTE|1||Hemolyzed' in d, 'diff: inserted segment found without shifting the rest')
ok(d.index('EXPECTED TO DIFFER') > d.index('DIFFERENT VALUES') and 'MSH-10' in d.split('EXPECTED TO DIFFER')[1], 'diff: MSH-7 / MSH-10 reported as expected differences')
ok('only A' in d and 'repeats 3 times' in d, 'diff: validation findings present in only one message')
ok('same content' in run(['diff', V251, V251]), 'diff of a message with itself: same content')
print('\n%d checks, %d failed' % (checks, fails)); print('ALL PASSED' if not fails else '%d FAILED' % fails); sys.exit(1 if fails else 0)
