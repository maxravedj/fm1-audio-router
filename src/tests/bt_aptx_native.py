# SPDX-License-Identifier: MPL-2.0
"""Execute linked DSP helper listings with a deliberately narrow ISA model.

Checks native lane order, signed interpolation, accumulator bounds, memory
accesses and the C ABI against a full 96-tap integer convolution. Unknown
instructions fail closed. This is arithmetic/ABI evidence, not silicon timing.
"""
import random
import re
import struct

MASK = 0xffffffff


def signed(v, bits=32):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


def instructions(dis, name):
    body = dis.split(name + ':\n', 1)[1].split('\n\n', 1)[0]
    return [(int(m[1], 16), bytes.fromhex(m[2]), m[3])
            for line in body.splitlines()
            if (m := re.match(r'\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)', line))]


class Kernel:
    def __init__(self, listing):
        self.code = {pc: (raw, op) for pc, raw, op in listing}
        self.start = listing[0][0]
        self.mem = bytearray(65536)
        self.reg = [0] * 16
        self.sp = 0x9000
        self.carry = 0

    def load(self, addr, size=4):
        assert addr % size == 0 and 0 <= addr <= len(self.mem)-size
        return int.from_bytes(self.mem[addr:addr+size], 'little')

    def store(self, addr, value):
        assert addr % 4 == 0 and 0 <= addr <= len(self.mem)-4
        struct.pack_into('<I', self.mem, addr, value & MASK)

    def run(self):
        r = self.reg
        pc, steps = self.start, 0
        while True:
            raw, op = self.code[pc]
            pc += len(raw)
            steps += 1
            assert steps < 2000
            if op == 'rts':
                return
            if op == '[--sp] = {r15-r4}':
                self.sp -= 48
                for i in range(12): self.store(self.sp+4*i, r[4+i])
            elif op == '{r15-r4} = [sp++]':
                for i in range(12): r[4+i] = self.load(self.sp+4*i)
                self.sp += 48
            elif m := re.fullmatch(r'r(\d+) = \[sp\+(\d+)\]', op):
                r[int(m[1])] = self.load(self.sp+int(m[2]))
            elif m := re.fullmatch(r'r(\d+) = (h)?\[r(\d+)(?:\+(\d+)|\+\+=(\d+))\](?: \(([su])\))?', op):
                dst, size, base = int(m[1]), 2 if m[2] else 4, int(m[3])
                value = self.load(r[base]+int(m[4] or 0), size)
                if m[6] == 's': value = signed(value, 16)
                if m[5]: r[base] += int(m[5])
                r[dst] = value & MASK
            elif m := re.fullmatch(r'\[r(\d+)\+(\d+)\] = r(\d+)', op):
                self.store(r[int(m[1])]+int(m[2]), r[int(m[3])])
            elif m := re.fullmatch(r'r(\d+)_r(\d+) = unpackb\(r(\d+)\) \(u\)', op):
                hi, lo, value = int(m[1]), int(m[2]), r[int(m[3])]
                r[lo] = (value & 255) | ((value >> 8 & 255) << 16)
                r[hi] = (value >> 16 & 255) | ((value >> 24 & 255) << 16)
                assert raw == bytes.fromhex('08 e4 00 ed')
            elif m := re.fullmatch(r'r(\d+)_r(\d+) \+= r(\d+)\.h,r\3\.l \*\|\* r(\d+)\.l,r\4\.l \(ssat\)', op):
                hi, lo, value, c = int(m[1]), int(m[2]), r[int(m[3])], signed(r[int(m[4])], 16)
                for dst, sample in ((lo, signed(value, 16)), (hi, signed(value >> 16, 16))):
                    total = signed(r[dst]) + sample*c
                    assert -(1 << 31) <= total < (1 << 31), 'lane saturated'
                    r[dst] = total & MASK
                assert raw in (bytes.fromhex('71 e5 ca 4a'), bytes.fromhex('71 e5 ea 6a'))
            elif m := re.fullmatch(r'r(\d+)_r(\d+) = r(\d+) \* r(\d+) \(s\)', op):
                hi, lo, a, b = map(int, m.groups())
                value = signed(r[a]) * signed(r[b])
                r[lo], r[hi] = value & MASK, value >> 32 & MASK
            elif m := re.fullmatch(r'r(\d+) = uextra\(r(\d+), p:0, l:10\)', op):
                r[int(m[1])] = r[int(m[2])] & 1023
            elif m := re.fullmatch(r'r(\d+) = r(\d+) >>> (\d+)', op):
                r[int(m[1])] = signed(r[int(m[2])]) >> int(m[3]) & MASK
            elif m := re.fullmatch(r'r(\d+) = r(\d+) ([+-]) (r\d+|\d+)( \+ c)?', op):
                dst, a = int(m[1]), r[int(m[2])]
                b = r[int(m[4][1:])] if m[4].startswith('r') else int(m[4])
                value = a + b + (self.carry if m[5] else 0) if m[3] == '+' else a - b
                self.carry = int(value > MASK) if m[3] == '+' else int(value >= 0)
                r[dst] = value & MASK
            elif m := re.fullmatch(r'r(\d+) ([+*])= r(\d+)', op):
                dst, rhs = int(m[1]), r[int(m[3])]
                value = r[dst]+rhs if m[2] == '+' else r[dst]*rhs
                r[dst] = value & MASK
            elif m := re.fullmatch(r'r(\d+) = (\d+)', op):
                r[int(m[1])] = int(m[2])
            elif m := re.fullmatch(r'if \(--r(\d+) != 0\) goto -\d+ <[^>]+ : ([0-9a-f]+) >', op):
                dst = int(m[1]); r[dst] = r[dst]-1 & MASK
                if r[dst]: pc = int(m[2], 16)
            else:
                raise AssertionError('unmodelled native instruction: ' + op)


def check_native(dis, header):
    rng = random.Random(731)
    dot = Kernel(instructions(dis, 'aptx_dot2'))
    rails = [-(1 << 31), -(1 << 31)+1, -1, 0, 1, (1 << 31)-1]
    for trial in range(4000):
        args = [rng.choice(rails) if trial < 2000 else signed(rng.getrandbits(32)) for _ in range(4)]
        saved = [rng.getrandbits(32) for _ in range(12)]
        dot.reg = [x & MASK for x in args]+saved
        dot.run()
        assert dot.reg[0] | dot.reg[1] << 32 == (args[0]*args[1]+args[2]*args[3]) & ((1 << 64)-1)
        assert dot.reg[4:] == saved and dot.sp == 0x9000
    kernel = Kernel(instructions(dis, 'fm1_aptx_fir24_span'))
    tables = [list(map(int, re.findall(r'-?\d+', b))) for b in re.findall(r'BTR_FIR_\d+\[65\]\[96\]=\{(.*?)\n\};', header.read_text(), re.S)]
    assert len(tables) == 2 and all(len(t) == 65*96 for t in tables)
    # Bound every interpolation fraction without executing all the kernels.
    # Rounding contributes less than 1.5 coefficient units per tap; bound by
    # two. Convexity bounds the unrounded absolute sum by either endpoint.
    # This also bounds every prefix and every wrap-split subspan.
    for table in tables:
        assert all(-32766 <= c <= 32765 for c in table), 'interpolated coefficient must fit a signed lane'
        for phase in range(64):
            for offset in (3, 48):
                ends = [sum(abs(c) for c in table[(phase+row)*96+offset:(phase+row)*96+offset+45]) for row in (0, 1)]
                bound = max(ends)+2*45
                assert bound <= 50573
                assert bound*32768 < 1 << 31 and bound*255 < 1 << 31
    cases = 0
    for table in tables:
        # Every phase, every omitted tap and both signs of the worst-case sum.
        for phase in range(64):
            coeff = table[phase*96:(phase+2)*96]
            kernel.mem[0x6000:0x6180] = struct.pack('<192h', *coeff)
            assert all(coeff[row*96+k] == 0 for row in (0, 1) for k in (0, 1, 2, 93, 94, 95))
            for trial in range(10):
                frac = (0, 1, 511, 512, 1022, 1023)[trial % 6] if trial < 6 else rng.randrange(1024)
                cs = []
                for k in range(96):
                    delta = (coeff[k+96]-coeff[k])*frac+512
                    cs.append(coeff[k]+(abs(delta)//1024)*(-1 if delta < 0 else 1))
                assert all(-32768 <= c <= 32767 for c in cs)
                for off in (3, 48): assert sum(abs(c) for c in cs[off:off+45]) <= 50573
                at = (0, 1955, 1956, 1999, 2000, 2044, 2047)[trial % 7] if trial < 7 else rng.randrange(2048)
                expect = [0, 0]
                for k, c in enumerate(cs):
                    pos = (at+k) & 2047
                    high = [(-32768 if (c < 0) ^ bool(ch) else 32767) if trial < 6 else signed(rng.getrandbits(16), 16) for ch in range(2)]
                    low = [rng.randrange(256) for _ in range(2)]
                    struct.pack_into('<2h', kernel.mem, 0x1000+pos*4, *high)
                    kernel.mem[0x4000+pos*2:0x4002+pos*2] = bytes(low)
                    for ch in range(2): expect[ch] += (high[ch]*256+low[ch])*c
                result = [0, 0, 0, 0]
                for offset in (3, 48):
                    pos, done = (at+offset) & 2047, 0
                    while done < 45:
                        count = min(45-done, 2048-pos)
                        saved = [rng.getrandbits(32) for _ in range(12)]
                        kernel.reg = [0x1000+pos*4, 0x4000+pos*2, 0x6000+2*(offset+done), frac]+saved
                        kernel.store(kernel.sp, count); kernel.store(kernel.sp+4, 0x7000)
                        kernel.run()
                        assert kernel.sp == 0x9000 and kernel.reg[4:] == saved
                        part = struct.unpack_from('<4i', kernel.mem, 0x7000)
                        result = [a+b for a, b in zip(result, part)]
                        done += count; pos = 0
                assert [(result[ch]*256+result[ch+2]) >> 15 for ch in range(2)] == [s >> 15 for s in expect]
                cases += 1
    print('PASS: native aptX dot2 4,000 signed/rail cases; FIR', cases,
          'full-96-tap oracle cases, both rates/all phases/wrap/full-scale; C ABI and no lane saturation')
