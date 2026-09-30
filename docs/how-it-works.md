# How SelCall works

## Audio input
PA4 is the processor's DAC output for voice prompts, and it's also ADC input 4. On the UV-K1,
received audio reaches this pin. At start-up, the app:

- disables DAC channel 1, so PA4 only listens (voice prompts are disabled in this build);
- points ADC rank 1 at channel 4 (normally channel 8, the battery).

The battery reading is refreshed every ~12 s by briefly switching the ADC back to channel 8.
On exit, the ADC channel and sampling time and the DAC state are restored.

## Sampling
`acquire()` takes 256 samples at 8 kHz (32 ms), timed by the SysTick counter, which the
firmware already runs for its 10 ms tick. Blocks don't overlap. Between blocks there's a
short pause for analysis and, when something changed, a screen update.

## Tone detection
`analyse()` removes the average level from the block, then runs one Goertzel filter for each of
the 15 CCIR tones:

```c
s = x[i] + ((c * s1) >> 13) - s2;  s2 = s1;  s1 = s;            // per sample
p = s1*s1 + s2*s2 - ((c * s1) >> 13) * s2;                       // power, end of block
```

`c` is 2·cos(2π·f/8000) in Q13 fixed point. The processor (a Cortex-M0+) has no FPU, so all
maths is integer. That comes to about 60–70 k cycles per block, a few percent of the CPU.

The strongest tone counts if its **purity** (its share of the block's energy) is at least
50% and the signal is at least 40 ADC counts peak-to-peak. A pure tone scores ~100%; speech
and noise score low.

**Frequency response** (clean tone, worst case over phase):

| Offset | 0 | ±5 Hz | ±10 Hz | ±15 Hz | ±20 Hz |
|---|---|---|---|---|---|
| Purity | 99% | 91% | 70% | 43% | 19–20% |

A tone is accepted up to about ±13 Hz. The limit comes from the 32 ms block length:
frequency resolution is about 1/T ≈ 31 Hz.

## Call assembly
- A new tone adds a digit. E (2110 Hz) repeats the previous digit, and the same tone seen in
  consecutive blocks counts once.
- Three blocks without a valid tone (~100 ms) end the call. Calls of 3–10 digits are kept.
- A call identical to the newest one increases its repeat count (`xN`).
- Decoding only runs while the squelch is open, or monitor is on.

## Possible improvements
- **Continuous sampling:** timer-triggered ADC with DMA into a circular buffer, for
  overlapping windows and no gaps. This would help with EEA's 40 ms tones.
- **More tone sets:** ZVEI/EEA tables, or automatic format detection. The limit is code space
  (4 KiB), not CPU.
