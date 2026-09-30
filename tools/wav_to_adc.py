#!/usr/bin/env python3
"""Convert a WAV recording to raw 12-bit ADC samples (8 kHz, uint16 LE) for test/host_decode.

    python3 tools/wav_to_adc.py recording.wav out.u16 [--amp 1500]
Needs numpy and scipy.
"""
import argparse
import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly
from math import gcd

ap = argparse.ArgumentParser()
ap.add_argument('wav'); ap.add_argument('out')
ap.add_argument('--amp', type=float, default=1500, help='full-scale input -> ADC counts')
a = ap.parse_args()
sr, d = wavfile.read(a.wav)
if d.ndim > 1:
    d = d[:, 0]
d = d.astype(float) / (np.iinfo(d.dtype).max if d.dtype.kind == 'i' else 1.0)
g = gcd(sr, 8000)
x = resample_poly(d, 8000 // g, sr // g)
np.clip(np.round(2048 + x * a.amp), 0, 4095).astype('<u2').tofile(a.out)
