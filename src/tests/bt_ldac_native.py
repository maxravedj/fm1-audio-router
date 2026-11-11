# SPDX-License-Identifier: MPL-2.0
"""Narrow arithmetic/ABI model of LDAC's linked multiply/round helpers.

Unknown instructions fail closed. This cannot simulate silicon timing/faults.
"""
import random
import re
from bt_aptx_native import MASK, instructions, signed

def evaluate(listing, args, saved):
    regs = [v & MASK for v in args]+[0]+saved[:]
    stack = []
    carry = 0
    pending = set()
    def value(s):
        return regs[int(s[1:])] if s.startswith('r') else int(s,0)
    for _, raw, text in listing:
        op = re.sub(r' <[A-Za-z_.$][^>]* >$', '', text)
        if op == 'rts':
            assert not pending, 'wide product returned before consumption'
            assert not stack and regs[4:] == saved, 'callee-register/stack ABI'
            return signed(regs[0])
        if m := re.fullmatch(r'\[--sp\] = \{r(\d+)-r(\d+)\}',op):
            hi,lo = map(int,m.groups());stack.append((lo,hi,regs[lo:hi+1]))
        elif m := re.fullmatch(r'\{r(\d+)-r(\d+)\} = \[sp\+\+\]',op):
            assert not pending, 'load before wide-product consumption'
            hi,lo = map(int,m.groups());sl,sh,values = stack.pop();assert (sl,sh)==(lo,hi)
            regs[lo:hi+1] = values
        elif m := re.fullmatch(r'r(\d+)_r(\d+) = r(\d+) \* r(\d+) \(([su])\)',op):
            hi,lo,a,b = map(int,m.groups()[:4]);assert not pending
            a,b = (signed(regs[a]),signed(regs[b])) if m[5]=='s' else (regs[a],regs[b])
            product = a*b;regs[lo]=product&MASK;regs[hi]=product>>32&MASK
            pending={lo,hi}
        elif m := re.fullmatch(r'r(\d+)_r(\d+) (<<|>>>|>>)= r(\d+)',op):
            hi,lo = int(m[1]),int(m[2]);shift = regs[int(m[4])];assert shift<64
            wide=regs[lo]|regs[hi]<<32
            if m[3]=='<<':wide <<= shift
            elif m[3]=='>>>':wide = signed(wide,64)>>shift
            else:wide >>= shift
            regs[lo]=wide&MASK;regs[hi]=wide>>32&MASK
            pending.discard(lo);pending.discard(hi)
        elif m := re.fullmatch(r'r(\d+) = (r\d+|0x[\da-fA-F]+|-?\d+) ([+-]) (r\d+|0x[\da-fA-F]+|-?\d+)( \+ c)?',op):
            a,b=value(m[2]),value(m[4]);v=a+b+(carry if m[5] else 0) if m[3]=='+' else a-b
            carry=int(v>MASK) if m[3]=='+' else int(v>=0)
            for operand in (m[2],m[4]):
                if operand.startswith('r'):pending.discard(int(operand[1:]))
            regs[int(m[1])]=v&MASK
        elif m := re.fullmatch(r'r(\d+) = (r\d+|0x[\da-fA-F]+|-?\d+)',op):
            if m[2].startswith('r'):pending.discard(int(m[2][1:]))
            regs[int(m[1])]=value(m[2])&MASK
        else:
            raise AssertionError('unmodelled native LDAC instruction: '+op)
    raise AssertionError('missing return')

def check_native(dis):
    rng=random.Random(731)
    rail=[-(1<<31),-(1<<31)+1,-1,0,1,(1<<31)-1]
    for name in ('ldac_fm1_mul_rsft','ldac_fm1_mul_lsft'):
        listing=instructions(dis,name)
        assert len(listing)<=20 and sum(' * ' in op for _,_,op in listing)==1
        for trial in range(4000):
            a,b=[rng.choice(rail) if trial<2000 else signed(rng.getrandbits(32)) for _ in range(2)]
            saved=[rng.getrandbits(32) for _ in range(12)]
            if name.endswith('rsft'):
                shift=(1,7,15,23,31,32,33,47,62,63)[trial%10] if trial<2000 else rng.randrange(1,64)
                expected=signed(signed(a*b+(1<<(shift-1)),64)>>shift)
            else:
                shift=-(trial%32) if trial<2000 else -rng.randrange(64)
                expected=signed(a*b<<(-shift))
            assert evaluate(listing,[a,b,shift],saved)==expected,(name,a,b,shift)
    # The old raw-product return does not implement the silicon workaround.
    try:
        evaluate([(0,b'', 'r1_r0 = r1 * r0 (s)'),(4,b'','rts')],[3,4,0],[0]*12)
    except AssertionError as e:
        assert 'before consumption' in str(e)
    else:
        raise AssertionError('raw-product-return regression not rejected')
    print('PASS: native LDAC fused multiplies: 8,000 signed/rail/shift cases, C ABI and product consumed before loads/return')
