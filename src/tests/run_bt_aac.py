#!/usr/bin/env python3
"""AAC-1 host proof: extract ADTS to raw AUs; ffmpeg independently decodes PCM."""
from pathlib import Path
import array, json, math, os, shlex, struct, subprocess
ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / 'src/bt/br/aac_dec'
BUILD = ROOT / 'build/tests/aac-host'
LOG = ROOT / 'logs/tests/aac-host.log'
BUILD.mkdir(parents=True, exist_ok=True)
LOG.parent.mkdir(parents=True, exist_ok=True)
def run(args, **kwargs):
    r = subprocess.run([str(a) for a in args], capture_output=True, **kwargs)
    with LOG.open('ab') as log:
        log.write(('COMMAND ' + shlex.join(str(a) for a in args) + '\n').encode()); log.write(r.stdout + r.stderr)
    r.check_returncode()
    return r

def compile_decoder(name, sanitize=False):
    sources = [MODULE / p for p in (MODULE / 'sources.txt').read_text().splitlines()]
    flags = ['-std=c99', '-O2', '-fwrapv', '-Wno-attributes', '-include', MODULE/'pvaac/pv_fm1.h', '-I'+str(MODULE), '-I'+str(MODULE/'pvaac')]
    # PacketVideo assumes two's-complement signed fixed-point left shifts.
    # Keep shift-exponent, bounds and every ASan check; exclude shift-base only.
    if sanitize: flags += ['-g', '-fsanitize=address,undefined', '-fno-sanitize=shift-base', '-fno-omit-frame-pointer', '-fno-sanitize-recover=all']
    exe = BUILD/name
    run([os.environ.get('CC','cc'), *flags, ROOT/'src/tests/bt_aac_decoder.c', MODULE/'aac_dec.c', *sources, '-o', exe])
    return exe

def extract(path):
    data = path.read_bytes(); result = bytearray(); at = count = 0; asc = None
    while at < len(data):
        h = data[at:at+7]; assert len(h)==7 and h[0]==255 and h[1]&0xf6==0xf0
        assert h[6]&3==0
        obj, rate, ch = (h[2]>>6)+1, (h[2]>>2)&15, ((h[2]&1)<<2)|(h[3]>>6)
        current = bytes([(obj<<3)|(rate>>1), ((rate&1)<<7)|(ch<<3)])
        if asc is None: asc=current; result+=asc
        assert asc==current
        length = ((h[3]&3)<<11)|(h[4]<<3)|(h[5]>>5); header=7 if h[1]&1 else 9
        au=data[at+header:at+length]; assert len(au)==length-header
        result+=struct.pack('<I',len(au))+au; at+=length; count+=1
    result+=b'\0\0\0\0'; target=path.with_suffix('.aus'); target.write_bytes(result)
    return target,count

def compare(actual, reference, channels):
    a=array.array('h',actual.read_bytes()); b=array.array('h',reference.read_bytes()); assert len(a)==len(b),(len(a),len(b)); metrics=[]
    for ch in range(channels):
        x,y=a[2048*channels+ch:-2048*channels:channels],b[2048*channels+ch:-2048*channels:channels]
        ex,ey=sum(v*v for v in x),sum(v*v for v in y)
        corr=sum(u*v for u,v in zip(x,y))/math.sqrt(ex*ey); err=sum((u-v)**2 for u,v in zip(x,y))
        snr=10*math.log10(ey/max(err,1)); db=10*math.log10(ex/ey)
        assert corr>.999 and snr>35 and abs(db)<.1,(corr,snr,db)
        metrics.append(dict(channel=ch,correlation=corr,snr_db=snr,level_delta_db=db))
    return metrics

def main():
    LOG.write_text('AAC-1 host evidence; no device test\n')
    normal=compile_decoder('decode'); sanitized=compile_decoder('decode-sanitized',True); summary=[]
    for rate in (44100,48000):
      for channels in (1,2):
       for bitrate in ('128k','192k','256k','vbr'):
        stem=f'lc-{rate}-{channels}-{bitrate}'; fixture=BUILD/(stem+'.adts')
        expr='0.3*sin(2*PI*997*t)'+('|0.15*sin(2*PI*2309*t)' if channels==2 else '')
        flags=['-q:a','2'] if bitrate=='vbr' else ['-b:a',bitrate]
        run(['ffmpeg','-v','error','-y','-f','lavfi','-i',f'aevalsrc={expr}:s={rate}:d=0.25','-c:a','aac','-profile:a','aac_low',*flags,'-f','adts',fixture])
        raw,count=extract(fixture); reference=BUILD/(stem+'-ffmpeg.pcm')
        run(['ffmpeg','-v','error','-y','-i',fixture,'-f','s16le','-acodec','pcm_s16le',reference])
        output=BUILD/(stem+'-pv.pcm');run([normal,raw,output]);metrics=compare(output,reference,channels)
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        run([sanitized,raw,BUILD/(stem+'-sanitized.pcm'),25],env=env,timeout=90)
        summary.append(dict(fixture=stem,raw_aus=count,metrics=metrics));print('PASS',stem,count,'AUs',flush=True)
    (BUILD/'summary.json').write_text(json.dumps(summary,indent=2)+'\n'); print('PASS AAC-1; evidence:',LOG.relative_to(ROOT))
if __name__=='__main__': main()
