#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Check real pinned boot packaging, invalid inputs and unchanged app/OTA bytes."""
from pathlib import Path
import hashlib
import struct
import sys
import tempfile
import tomllib
from unittest.mock import patch

SRC=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(SRC/'tools'))
import fm1pkg_make as pack
APP=SRC/'apps/fm1-audio-gateway'
conf=tomllib.loads((APP/'fm1.toml').read_text())
path=APP/conf['bootloader']
boot=pack.read_bootloader(path,conf['bootloader_sha256'])
assert boot[16+0x50:16+0x70].split(b'\0')[0]==b'2.2-64f7c9c'

with tempfile.TemporaryDirectory() as tmp:
    bad=Path(tmp)/'bad.boot'
    def rejected(data,sha=None):
        bad.write_bytes(data)
        try:pack.read_bootloader(bad,sha)
        except SystemExit:return
        raise AssertionError('invalid bootloader accepted')
    rejected(boot,'0'*64)
    for data in (b'',boot[:15],boot[:-1],boot+b'\0'):
        rejected(data)
    for offset in (0,4,8,12,14,len(boot)-1):
        data=bytearray(boot);data[offset]^=1;rejected(data)
    data=bytearray(boot);data[16+0x48:16+0x50]=b'smk37\0\0\0'
    struct.pack_into('<H',data,12,pack.crc16(data[16:]))
    struct.pack_into('<H',data,14,pack.crc16(data[:14]))
    rejected(data)  # valid container, wrong board
    sdk={'uboot.boot':b'legacy stock fixture','cfg_tool.bin':b'c'*0xE00,'cfg/eq_cfg_hw.bin':b'e'*100}
    with patch.object(pack,'sdk_file',side_effect=lambda name:sdk[name]):
        app=bytes(range(256))*16
        legacy=pack.flash_image(app,pack.KEY)
        image=pack.flash_image(app,pack.KEY,boot=boot)
    assert image[0x4000:]==legacy[0x4000:], 'boot selection must not alter the app/regions'
    assert len(image)==0x93000
    def entry(at):
        raw=bytearray(image[at:at+32]);pack.enc(raw,0,32)
        assert pack.crc16(raw[2:])==struct.unpack_from('<H',raw)[0]
        return struct.unpack('<HHIIBBH16s',raw)
    e=entry(0x20);assert e[2]==0xA0 and e[3]==len(boot) and e[1]==pack.crc16(boot)
    stored=bytearray(image[0xA0:0xA0+len(boot)])
    pack.enc(stored,0,16);pack.enc(stored,16,len(stored)-16)
    assert bytes(stored)==boot
    e=entry(0x40);assert e[2:4]==(0x3FA0,96)
    cfg=image[e[2]:e[2]+e[3]]
    assert pack.crc16(cfg)==e[1] and pack.chipkey_decode(cfg)==pack.KEY
    assert pack.crc16(cfg[:32])==struct.unpack_from('<H',cfg,32)[0]
    fields={};at=34
    while at<len(cfg):
        n=cfg[at];at+=1;end=cfg.index(0,at);key=cfg[at:end].decode();at=end+1
        fields[key]=cfg[at:at+n];at+=n
    assert fields=={'OSC':b'OSC0','OSC_FREQ':struct.pack('<I',24000000),
                    'SYS_CLK':struct.pack('<I',360000000),'HSB_DIV':struct.pack('<I',1),'LSB_DIV':struct.pack('<I',2)}
    assert all(b==255 for b in image[0xA0+len(boot):0x3FA0])
    ota=b'unchanged update loader'
    package=pack.ufw(image,ota,'FM-1_73100')
    assert ota in package
print('PASS: pinned FM-1 m-bootloader, malformed/wrong-board/hash rejection, independent ROM ENC and directory/config CRC checks, 96-byte compatibility config; app/OTA unchanged')
