#!/usr/bin/env python3
"""Generate a synthetic CCIR selcall test signal as raw 12-bit ADC samples (8 kHz, uint16 LE).

Each call: an extended first tone, then 100 ms tones, then a pause. Repeated digits are sent
as the E tone (2110 Hz), as CCIR requires. Noise, level, tone accuracy and first-tone length
can be varied to stress the decoder.

    python3 tools/gen_ccir.py out.u16 86546 26546 88174 --first 700 --noise 0.05 --offset 5
Writes the calls that should be decoded to stdout, one per line.
"""
import argparse, math, random, struct

FREQ = {'0': 1981, '1': 1124, '2': 1197, '3': 1275, '4': 1358, '5': 1446, '6': 1540,
        '7': 1640, '8': 1747, '9': 1860, 'A': 2400, 'B': 930, 'C': 2247, 'D': 991, 'E': 2110}
FS = 8000

def tones(call):
    """Digits to tones: a digit equal to the previous tone is sent as E (7777 -> 7E7E)."""
    out = []
    for d in call:
        out.append('E' if out and d == out[-1] else d)
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('calls', nargs='+')
    ap.add_argument('--first', type=int, default=700, help='first tone length, ms')
    ap.add_argument('--tone', type=int, default=100, help='other tones, ms')
    ap.add_argument('--gap', type=int, default=320, help='pause between calls, ms')
    ap.add_argument('--amp', type=float, default=300, help='amplitude, ADC counts')
    ap.add_argument('--noise', type=float, default=0.0, help='noise, fraction of amp (rms)')
    ap.add_argument('--offset', type=float, default=0.0, help='frequency error, Hz')
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    random.seed(a.seed)

    samples, phase = [], 0.0
    def emit(freq, ms):
        nonlocal phase
        for _ in range(FS * ms // 1000):
            v = a.amp * math.sin(phase) if freq else 0.0
            v += random.gauss(0, a.noise * a.amp)
            phase += 2 * math.pi * (freq or 0) / FS
            samples.append(max(0, min(4095, round(2048 + v))))

    emit(0, a.gap)
    for call in a.calls:
        for i, t in enumerate(tones(call)):
            emit(FREQ[t] + a.offset, a.first if i == 0 else a.tone)
        emit(0, a.gap)
        print(call)
    with open(a.out, 'wb') as f:
        f.write(struct.pack('<%dH' % len(samples), *samples))

if __name__ == '__main__':
    main()
