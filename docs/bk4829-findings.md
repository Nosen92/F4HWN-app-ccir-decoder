# The BK4829's built-in SelCall detector

These are notes from trying to use the radio chip's own detector before switching to software
decoding. They were tested on a UV-K1 against a real CCIR system (700 ms first tone, then
100 ms tones), and may help others working with the BK4819 or BK4829.

## Setup that works (for the first tone)
- `REG_09`: 16 programmable symbol coefficients, written as `(symbol << 12) | coef`.
  `coef = round(128 · cos(2π·f / Fs))` with Fs ≈ 8435 Hz. This reproduces the firmware's own
  DTMF table to within ±1. The CCIR values are
  `12,86,80,74,68,61,53,44,34,24,228,98,243,95,0` (0–9, A–E).
- `REG_24`: `(1<<15) | (thr<<7) | (1<<6) | (1<<5) | 14`, i.e. SelCall mode (bit 4 = 0),
  highest symbol 14, threshold 130.
- `REG_3F<11>` enables the interrupt. `REG_02<11>` flags a symbol, and `REG_0B<11:8>` holds
  its code.

## The limitation
- **One report per transmission.** The detector reports the first tone about 100–170 ms after
  the squelch opens, then nothing more, even though four more tones follow.
- **The flag latches.** With a BK GPIO set to output type 8 ("DTMF/5-tone symbol received",
  `REG_34`), the flag goes high on the first tone and stays high. On the UV-K1, the green LED
  is chip GPIO1, even though the firmware names that pin "GPIO6".
- **Only a real gap in the tone clears it.** A silent carrier from another radio does it.
  Static-y signals occasionally let a second digit through.
- The detector behaves like a DTMF decoder, which expects pauses between symbols. CCIR sends
  its tones back to back.

## What was tried and didn't fix it
- **Detector restarts and table changes:** switching `REG_24` off and on (once, or pulsed
  every 20–100 ms), and removing the detected tone from `REG_09`. Combinations got up to
  4 digits once, but weren't reliable.
- **Threshold dips:** lowering the `REG_24` threshold briefly.
- **Other register writes:** `REG_24` bits 15 and 6, writes to `REG_0B`, and single-bit changes
  to the undocumented `REG_21`–`REG_27`.
- **Muting the audio:** RX DSP off (`REG_30` bit 0); AF gain mute (`REG_48`, which also ramps
  slowly, ~500 ms); AF output mute (1–2 digits at most); RX front end off (`REG_30<13:10>`,
  which also drops the squelch).

## Documentation
- The Beken BK4819 register reference (REG-BK4819-E02) and application notes (V3; the Chinese
  edition is more explicit) don't describe detector timing. According to the application
  note, Beken's reference 5-tone setup only sets coefficients, threshold and the TX path.
- The BK4829 datasheet (DS-BK4829-E01 V1.0) specifies a 30 ms SELCALL response time over
  400–3000 Hz.
