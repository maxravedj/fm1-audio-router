#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Inspect actual linked CPU2/SRAM bootstrap, stack depths and scratch budgets."""
from pathlib import Path
import argparse,json,re,subprocess,sys
src=Path(__file__).resolve().parents[1];root=src.parent
sys.path.insert(0,str(src/'tools'));import stack_depth,ramcode
p=argparse.ArgumentParser();p.add_argument('build',type=Path);a=p.parse_args();out=a.build
elf=out/'fm1audio.elf';dis=(out/'fm1audio.dis').read_text()
syms=subprocess.check_output([str(root/'tools/jieli-open/common/bin/objdump'),'-t',str(elf)],text=True)
sym=lambda name:ramcode.symbol(syms,name)
for fn in ('fm1_cpu1_entry','fm1_cpu1_main','cpu2_job','btr_aac_job','btr_sbc_job','btr_resample_job','btr_aptx_job','btr_ldac_job','fm1_ldac_dec_frame','fm1_ldac_dec_check','aptx_decode_sync','aptx_mul32','aptx_dot2','btr_fir24','aptx_fir24_half','fm1_aptx_fir24_span','OI_CODEC_SBC_DecodeFrame','aac_dec_decode','fx_process','fx_detector_reset'):
 assert sym('_rt_start')<=sym(fn)<sym('_rt_end'),fn
assert not any(line.rstrip().endswith(' '+fn) for line in syms.splitlines() for fn in ('aptx_encode','aptx_encode_finish','aptx_init','aptx_finish')), 'target must not link encoder/heap entry points'
assert sym('_sram_start')<=sym('fx_ceiling_q23')<sym('_sram_end')
assert sym('btr_pcm')%4==0,'packed stereo high words require word alignment'
assert 0x02000120<=sym('fm1_ram_prestart')<sym('_etext')<=0x02000220
assert dis.index('<fm1_ram_prestart :')<dis.index('<_rt_load :')
bootstrap=dis.split('fm1_ram_prestart:\n',1)[1].split('\n\n',1)[0]
assert not re.search(r'\bcall\b',bootstrap),'early stop must inline, before SRAM exists'
irs=[q.read_text() for q in out.rglob('*.opt.ll')];assert irs
# Generic fn(uint32_t) callbacks share a signature. cpu2_job can only dispatch
# btr_aac_job/btr_sbc_job in this app; the inferred self edge is unreachable, bounded to 1.
r=stack_depth.analyse(dis,stack_depth.elf_words(elf),irs,{'cpu2_job':1})
for name,limit in (('_start',8192),('fm1_cpu1_main',6144-256)):
 depth,path=r['depth'](name);print(name,depth,'bytes, margin requires',stack_depth.need(depth),'of',limit,path)
 assert depth and stack_depth.need(depth)<=limit
assert stack_depth.need(r['irq_total'])<=7936
assert not r['notes']['var'] and not r['notes']['unknown'] and not stack_depth.STRADDLE
assert not r['notes']['recursion'];assert set(r['notes']['indirect, all taken'])<={'_start'}
import importlib.util
spec=importlib.util.spec_from_file_location('gateway_build',src/'tools/build.py');build=importlib.util.module_from_spec(spec);spec.loader.exec_module(build)
assert not any('aptx' in fn for fn in build.mul64_near_loads(dis)), 'aptX multiply/load erratum'
assert not any('ldac' in fn for fn in build.mul64_near_loads(dis)), 'LDAC multiply/load erratum'
fir_body=dis.split('fm1_aptx_fir24_span:\n',1)[1].split('\n\n',1)[0]
fir_ins=[(int(m[1],16),m[2]) for line in fir_body.splitlines()
         if (m:=re.match(r'\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(.*)',line))]
tap_loops=[]
for addr,op in fir_ins:
 if (branch:=re.search(r'goto -\d+ <[^>]+ : ([0-9a-f]+) >',op)):
  body=[ins for pc,ins in fir_ins if int(branch[1],16)<=pc<=addr]
  if sum('*|*' in ins for ins in body)==2 and not any('goto -' in ins for ins in body[:-1]):tap_loops.append(body)
assert tap_loops,'24-bit FIR tap loop missing'
tap_loop=min(tap_loops,key=len)
assert len(tap_loop)<=17,'24-bit FIR stereo lane loop regressed'
assert not any('[sp' in ins or 'call ' in ins for ins in tap_loop),'24-bit FIR per-tap stack traffic/call'
print('PASS: 24-bit FIR loop',len(tap_loop),'instructions per stereo tap, no per-tap stack traffic or calls')
dot=dis.split('aptx_dot2:\n',1)[1].split('\n\n',1)[0]
assert len(re.findall(r'r\d+_r\d+ = r\d+ \* r\d+',dot))==2
assert not re.search(r'\br\d+ = [hb]?\[|\bcall\b',dot),'paired multiplier must consume only register arguments'
assert '[sp' not in dot and '[--sp' not in dot,'paired multiplier must not spill'
sys.path.insert(0,str(src/'tests'))
from bt_aptx_native import check_native
check_native(dis,src/'bt/br/bt_receiver_fir.h')
from bt_ldac_native import check_native as check_ldac_native
check_ldac_native(dis)
print('IRQ',r['irq_total'],'bytes; assembly _start indirect calls conservatively include all taken targets')
sizes=json.loads((out/'sizes.json').read_text());free=sym('_sram_end')-sym('_sram_start')-sum(sizes[k] for k in ('ram','pool','ramtext'))
ir=(out/'unity.opt.ll').read_text()
alloc=re.search(r'@plat_work_alloc\(i32(?: \w+)* (\d+)\)[^\n]*\n[^\n]*store ptr [^\n]*@btr_aac',ir)
assert alloc,'AAC context allocation must be identifiable in target IR'
context=int(alloc[1]);aac=context+48768
sbc_alloc=re.search(r'@plat_work_alloc_top\(i32(?: \w+)* (\d+)\)',ir)
assert sbc_alloc,'SBC high-end allocation must be identifiable in target IR'
sbc_context=int(sbc_alloc[1]);assert 4248<=sbc_context<=4280
sbc=(sbc_context+7)&~7
aptx_ir=next(q.read_text() for q in out.rglob('*.opt.ll') if re.search(r'define[^\n]*@aptx_context_size',q.read_text()))
aptx_context=int(re.search(r'define[^\n]*@aptx_context_size[^}]+ret i32 (\d+)',aptx_ir)[1])
aptx_alloc=re.search(r'@aptx_context_size\(\)[^\n]*\n[^\n]*add i32 [^,]+, (\d+)',ir)
assert aptx_alloc
aptx=(aptx_context+int(aptx_alloc[1])+7)&~7
ldac_ir=next(q.read_text() for q in out.rglob('*.opt.ll') if re.search(r'define[^\n]*@fm1_ldac_dec_size',q.read_text()))
ldac_workspace=int(re.search(r'define[^\n]*@fm1_ldac_dec_size[^}]+ret i32 (\d+)',ldac_ir)[1])
ldac_alloc=re.search(r'@fm1_ldac_dec_size\(\)[^\n]*\n[^\n]*add i32 [^,]+, (\d+)',ir)
assert ldac_alloc,'LDAC high-end allocation must be identifiable in target IR'
ldac=(ldac_workspace+int(ldac_alloc[1])+7)&~7
# Verify both graph operands against the actual target allocation IR. Capture
# keeps two full FFT windows; PCM expands backwards into its FFT workspace.
graph_ir=re.search(r'define[^\n]*@gateway_acquire_display\([^}]+}',ir)[0]
assert '[ 40968,' in graph_ir and '[ 32768,' in graph_ir
assert '[ 3656,' in graph_ir and '[ 57344,' in graph_ir
graph=40968+32768;wave=3656+57344
# This includes compressed queue, low-byte PCM, decoder state and alignment.
# FIR/output preparation has no path from the CPU0 mixer. Only a nonblocking
# mailbox kick crosses that boundary; the audio IRQ cannot join the worker.
functions=stack_depth.parse(dis);seen=set();todo=['btr_pcm_mix','btr_decode_poll_irq']
while todo:
 fn=todo.pop()
 if fn in seen or fn not in functions:continue
 seen.add(fn);todo.extend(c[0] for c in functions[fn]['calls'])
assert not seen.intersection({'btr_resample_fill','btr_fir24','aptx_fir24_half','fm1_aptx_fir24_span','btr_aac_job','btr_sbc_job','plat_cpu2_wait','fm1_cpu1_wait','plat_work_alloc','plat_work_alloc_top','plat_work_free','aac_dec_decode','OI_CODEC_SBC_DecodeFrame','btr_aptx_job','aptx_decode_sync','btr_ldac_job','fm1_ldac_dec_frame','fm1_ldac_dec_check','fm1_ldac_dec_init','fm1_ldac_dec_reset','fm1_ldac_dec_destroy'}),seen
assert 'plat_bt_receiver_service' not in seen,'UI RX service must remain foreground-only'
print('PASS: linked CPU0 audio/tick decode pump has no FIR/decoder/wait/allocator call path')
fx_seen=set();fx_todo=['fx_process']
while fx_todo:
 fn=fx_todo.pop()
 if fn in fx_seen or fn not in functions:continue
 fx_seen.add(fn);fx_todo.extend(c[0] for c in functions[fn]['calls'])
assert not fx_seen.intersection({'plat_work_alloc','plat_work_alloc_top','plat_work_free','malloc','calloc','realloc','free','plat_cpu2_wait','fm1_cpu1_wait'}),fx_seen
print('PASS: linked limiter callback has no allocation/free/worker-wait path; DSP and exact ceiling table are in SRAM')
for name,size in [('btr_pcm',8192),('btr_output',16384),('st_buf',256),('gateway_fx_presets_saved',456),('gateway_eq_work',384),('gateway_lim_work',64)]:
 line=next(x for x in syms.splitlines() if x.rstrip().endswith(' '+name))
 assert int(line.split()[-2],16)==size,(name,line)
fx_line=next(x for x in syms.splitlines() if x.rstrip().endswith(' gateway_fx'))
fx_bytes=int(fx_line.split()[-2],16)
record=(2048+fx_bytes+7)&~7
assert free-8192>=graph+record+sbc and free-8192>=aac+record
assert free-8192>=graph+record+aptx
assert free-8192>=ldac+record
loop=dis.split('fm1_cpu1_main:\n',1)[1].split('\n\n',1)[0]
if 'ui_profile_idle_spins' in loop:
 assert re.search(r'@ui_profile_idle_spins = .*global i32 64,',ir)
 assert 'i32 4096' in ir,'diagnostic slow-idle bound must be compiled'
 for fn in ('plat_profile_start','plat_profile_stop','plat_profile_end'):
  assert sym('_rt_start')<=sym(fn)<sym('_rt_end'),fn
 for fn in ('plat_profile_add','plat_profile_count','plat_profile_start','plat_profile_stop'):
  body=dis.split(fn+':\n',1)[1].split('\n\n',1)[0]
  assert '= icfg' in body and 'sti' in body,fn+' must read and restore the actual IRQ enable state'
  assert re.search(r'uextra\(.*p:9|&.*0x200|&.*512',body),fn+' must test ICFG bit 9'
 print('PASS: linked profiler probes/session boundaries preserve ICFG interrupt-enable state')
 print('PASS: diagnostic idle backoff is linked, defaults to 64, slow probe 4096; profiler in SRAM')
else:
 assert re.search(r'0x40|64',loop),'idle backoff must survive target optimization'
report={'ram_free':free,'aac_context_bytes':context,'aac_workspace_bytes':48768,'aac_dynamic_bytes':aac,
 'aptx_context_bytes':aptx_context,'aptx_dynamic_bytes':aptx,'usb_record_aptx_spectrum_margin':free-8192-graph-record-aptx,
 'spectrum_workspace_bytes':graph,'waveform_workspace_bytes':wave,'ldac_workspace_bytes':ldac_workspace,'ldac_dynamic_bytes':ldac,
 'usb_record_ldac_margin':free-8192-ldac-record,'usb_record_ldac_waveform_margin':free-8192-ldac-record-wave,
 'usb_record_ldac_spectrum_margin':free-8192-ldac-record-graph,
 'sbc_dynamic_bytes':sbc,'sbc_context_bytes':sbc_context,'fx_engine_bytes':fx_bytes,'usb_record_max_workspace_bytes':record,
 'usb_record_spectrum_margin':free-8192-graph-record-sbc,'usb_record_aac_margin':free-8192-aac-record,
 'cpu0_main_stack_bytes':8192,'cpu1_job_stack_bytes':6144,'cpu0_static_stack_depth':r['main'][0],
 'cpu1_static_stack_depth':r['depth']('fm1_cpu1_main')[0],'irq_static_stack_depth':r['irq_total']}
(out/'cpu2-budget.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: early CPU1 stop, SRAM entry/worker/decoder, guarded stacks and SBC+spectrum/recording and AAC/recording budgets',report)
