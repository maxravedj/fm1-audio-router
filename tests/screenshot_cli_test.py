#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Screenshot command transport, retries, PNG pixels, timestamp and cleanup."""
import contextlib
from datetime import datetime
import io
import os
from pathlib import Path
import re
import struct
import sys
import tempfile
import zlib
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from fm1_remote import FM1
HEAD=[0x7d,0x4c,0x49]
class Fake(FM1):
    def __init__(self,fail=False,abort=False):self.sent=[];self.fail=fail;self.abort=abort;self.attempts={};self.frozen=False
    def sysex(self,data):
        self.sent.append(data)
        if data==HEAD+[2]:self.frozen=True
        if data==HEAD+[0]:self.frozen=False
    def frames(self,head,secs,until=None):
        assert head==HEAD
        p=self.sent[-1]
        if p==HEAD+[2]:return [] if self.fail else [b"\x7f\x02\x01"]
        assert self.frozen
        y=p[4]+128*p[5];seq=p[6];self.attempts[y]=self.attempts.get(y,0)+1
        if self.abort and y==10:return [b"\x7f\x01\x00"]
        if self.fail:return []
        frames=[]
        for c in range(8):
            body=[]
            for x in range(c*30,c*30+30):
                v=((x*31//239)<<11)|((y*63//239)<<5)|((x+y)%32)
                body.extend((v>>12,(v>>8)&15,(v>>4)&15,v&15))
            frames.append(bytes([seq,y&127,y>>7,c]+body))
        if y==7 and self.attempts[y]==1:return frames[:-1]  # recover missing chunk
        return [b'bad',bytes([seq,0,0,0]+[16]*120)]+list(reversed(frames))+frames[:1]
with tempfile.TemporaryDirectory() as tmp:
    prev=os.getcwd();os.chdir(tmp)
    try:
        f=Fake()
        with contextlib.redirect_stdout(io.StringIO()):
            f.run(['screenshot'])
            f.screenshot()
        files=sorted(Path('.').glob('*.png'));assert len(files)==2 and f.attempts[7]>=2
        assert all(re.fullmatch(r'fm1-screenshot-\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2}_\d{6}\.png',p.name) for p in files)
        stamp=datetime.strptime(files[0].stem.removeprefix('fm1-screenshot-'),'%Y-%m-%d_%H-%M-%S_%f')
        assert abs((datetime.now()-stamp).total_seconds())<30
        data=files[0].read_bytes();assert data[:8]==b'\x89PNG\r\n\x1a\n'
        chunks={};p=8
        while p<len(data):
            n=struct.unpack('>I',data[p:p+4])[0];tag=data[p+4:p+8];body=data[p+8:p+8+n]
            assert struct.unpack('>I',data[p+8+n:p+12+n])[0]==zlib.crc32(tag+body)
            chunks[tag]=body;p+=12+n
        assert struct.unpack('>II',chunks[b'IHDR'][:8])==(240,240)
        raw=zlib.decompress(chunks[b'IDAT']);assert len(raw)==240*721
        for y in range(240):
            assert raw[y*721]==0
            for x in range(240):
                v=((x*31//239)<<11)|((y*63//239)<<5)|((x+y)%32)
                expected=bytes(((v>>11)*255//31,((v>>5)&63)*255//63,(v&31)*255//31))
                assert raw[y*721+1+x*3:y*721+4+x*3]==expected
        bad=Fake(True)
        try:bad.screenshot();assert False
        except SystemExit as e:assert '0.0.54' in str(e)
        assert bad.sent[-1]==HEAD+[0] and len(list(Path('.').glob('*.png')))==2
        assert f.sent[-1]==HEAD+[0] and not f.frozen and not bad.frozen
        interrupted=Fake(abort=True)
        try:interrupted.screenshot();assert False
        except SystemExit as e:assert 'interrupted' in str(e)
        assert not interrupted.frozen and len(list(Path('.').glob('*.png')))==2
        class Interrupted(Fake):
            def frames(self,*args,**kwargs):
                if len(self.sent[-1])==7 and self.sent[-1][4]==10:raise KeyboardInterrupt
                return super().frames(*args,**kwargs)
        stopped=Interrupted()
        try:stopped.screenshot();assert False
        except KeyboardInterrupt:pass
        assert not stopped.frozen and stopped.sent[-1]==HEAD+[0]
        assert len(list(Path('.').glob('*.png')))==2
    finally:os.chdir(prev)
print('PASS: screenshot CLI full RGB565 PNG oracle, current-folder timestamp, unique names, malformed/stale/duplicate/out-of-order packets, freeze handshake, retry, abort and failure unfreeze')
