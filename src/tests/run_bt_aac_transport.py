#!/usr/bin/env python3
"""Independent ffmpeg LOAS/LATM -> RTP fixtures against the live AAC receiver."""
from pathlib import Path
import subprocess,os,struct,array,math,json,argparse
ROOT=Path(__file__).resolve().parents[2];MOD=ROOT/'src/bt/br/aac_dec'
parser=argparse.ArgumentParser();parser.add_argument('--out',type=Path,default=ROOT/'build/tests/aac-transport')
OUT=parser.parse_args().out;OUT.mkdir(parents=True,exist_ok=True)
def run(args):subprocess.run(list(map(str,args)),check=True)
exe=OUT/'transport'
flags=[os.environ.get('CC','cc'),'-std=c99','-O1','-g','-fwrapv','-fsanitize=address,undefined','-fno-sanitize=shift-base','-fno-sanitize-recover=all','-Wno-attributes','-include',MOD/'pvaac/pv_fm1.h','-I'+str(MOD)]
decoder=[MOD/'aac_dec.c',*[MOD/p for p in (MOD/'sources.txt').read_text().splitlines()]]
run([*flags,ROOT/'src/tests/bt_aac_transport.c',*decoder,'-o',exe])
debug=OUT/'debug';run([*flags,ROOT/'src/tests/bt_aac_debug.c',*decoder,'-o',debug]);run([debug])
worker=OUT/'cpu2';run([*flags,'-pthread',ROOT/'src/tests/bt_aac_cpu2.c',*decoder,'-o',worker])
both=OUT/'codecs';run([*flags,f'-I{ROOT}/src/bt/br/sbc_dec',ROOT/'src/tests/bt_codec_cpu2.c',*decoder,*sorted((ROOT/'src/bt/br/sbc_dec').glob('*.c')),'-o',both])
results=[]
for rate in (44100,48000):
 for channels in (1,2):
  for quality in ('128k','256k','vbr'):
   stem=f'{rate}-{channels}-{quality}';latm=OUT/(stem+'.latm');ref=OUT/(stem+'-ref.pcm')
   expr='0.3*sin(2*PI*997*t)'+('|0.15*sin(2*PI*2309*t)' if channels==2 else '')
   run(['ffmpeg','-v','error','-y','-f','lavfi','-i',f'aevalsrc={expr}:s={rate}:d=0.3','-c:a','aac','-profile:a','aac_low',*(['-q:a','2'] if quality=='vbr' else ['-b:a',quality]),'-f','latm',latm])
   run(['ffmpeg','-v','error','-y','-i',latm,*(['-af','pan=stereo|c0=c0|c1=c0'] if channels==1 else ['-ac','2']),'-f','s16le',ref])
   data=latm.read_bytes();at=0;payloads=[]
   while at<len(data):
    assert data[at]==0x56 and data[at+1]&0xe0==0xe0
    n=((data[at+1]&31)<<8)|data[at+2];payloads.append(data[at+3:at+3+n]);at+=3+n
   for mtu in (900,73):
    wire=bytearray([0x40,1 if rate==44100 else 0,(0 if rate==44100 else 128)|(8 if channels==1 else 4),128,0,0]);seq=65530
    for index,p in enumerate(payloads):
     for offset in range(0,len(p),mtu):
      part=p[offset:offset+mtu];last=offset+len(part)==len(p)
      rtp=struct.pack('>BBHII',0x80,96|(128 if last else 0),seq&65535,index*1024,17)+part;seq+=1
      wire+=struct.pack('<I',len(rtp))+rtp
    wire+=b'\0'*4;fixture=OUT/(stem+f'-{mtu}.rtp');fixture.write_bytes(wire);actual=fixture.with_suffix('.pcm')
    run([exe,fixture,actual]);a=array.array('h',actual.read_bytes());b=array.array('h',ref.read_bytes());assert len(a)==len(b)
    unmarked=OUT/(fixture.stem+'-unmarked.pcm')
    run([exe,fixture,unmarked,'--unmarked']);assert unmarked.read_bytes()==actual.read_bytes()
    run([worker,fixture])
    for ch in range(2):
     x=a[4096+ch:-4096:2];y=b[4096+ch:-4096:2];err=sum((u-v)**2 for u,v in zip(x,y));energy=sum(v*v for v in y)
     snr=10*math.log10(energy/max(err,1));assert snr>35,snr
    results.append(dict(format=stem,fragment_bytes=mtu,snr_db=snr,unmarked_pcm_bit_exact=True))
sbc=OUT/'transition.sbc'
run(['ffmpeg','-v','error','-y','-f','lavfi','-i','sine=frequency=997:sample_rate=44100:duration=0.1','-ac','2','-c:a','sbc','-b:a','328k',sbc])
run([both,sbc,OUT/'44100-2-256k-900.rtp'])
(OUT/'summary.json').write_text(json.dumps(results,indent=2)+'\n')
print('PASS: 24 marked plus 24 unmarked independent LATM/RTP fixtures, bit-exact marker fallback, PCM comparison and sanitizer tests')
