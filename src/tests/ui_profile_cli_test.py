#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Exercise the actual remote command without MIDI or sleeping."""
from pathlib import Path
import sys,importlib.util
src=Path(__file__).resolve().parents[1];sys.path.insert(0,str(src/'tools'))
spec=importlib.util.spec_from_file_location('remote',src/'tools/fm1_remote.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
m.time.sleep=lambda _:None
class Fake(m.FM1):
    def __init__(self):self.sent=[];self.listening=False;self.replies=0
    def sysex(self,data):self.sent.append(data)
    def frames(self,head,secs,until=None):
        assert head==m.LP;self.replies+=1
        return [b'started'] if self.replies==1 else [b'version 0.0.42 ms 10000 flags 15',b'end']
f=Fake();f.run(['profile','10','no-lcd','capture-off','idle-slow'])
assert f.sent==[m.LP+[1,10,15],m.LP+[2]]
for n in ('0','31','oops'):
    f=Fake()
    try:f.run(['profile',n]);raise AssertionError('invalid duration accepted')
    except SystemExit:assert not f.sent
class Old(Fake):
    def frames(self,*args,**kwargs):return []
try:Old().run(['profile','1']);raise AssertionError('silent old firmware accepted')
except SystemExit:pass
class Interrupted(Fake):
    def frames(self,*a,**k):
        self.replies+=1
        return [b'started'] if self.replies==1 else [b'end']
def interrupt(_):raise KeyboardInterrupt
m.time.sleep=interrupt;f=Interrupted()
try:f.run(['profile','1']);raise AssertionError('not interrupted')
except KeyboardInterrupt:assert f.sent[-1]==m.LP+[2]
print('PASS: profile CLI flags, duration validation, old firmware and interrupt cleanup; no MIDI/device access')
