# SelCall for the Quansheng UV-K1

A CCIR 5-tone selective-call decoder that runs as an **overlay app** on the
[F4HWN custom firmware](https://github.com/armel/uv-k1-k5v3-firmware-custom) v6.0.0 (Labs
edition) for the Quansheng UV-K1. It shows each call as it's received, keeps a short
history, and passes the received audio through as narrow FM.

- Decodes CCIR selcall in software (Goertzel filters), with no hardware changes needed.
- Handles the extended first tone that many systems use, and the repeat tone (E).
- Calls of 3–10 digits are shown in large digits as they come in, with repeat counts.
- Audio follows the squelch; monitor mode keeps it open.
- Fits in the 4 KiB overlay-app limit (3.9 KiB).

## Install

1. Get `dist/SelCall.app`, or build it (see below).
2. Open **UV Studio** → Apps (Labs edition) and install `SelCall.app` into a free slot.
3. Tune the radio to the channel, press **F + 7**, pick the slot, press **M**.

| Key  | Action |
|------|--------|
| 3    | Monitor on/off (audio open regardless of squelch; remembered) |
| MENU | Clear the call history |
| EXIT | Quit |

## Build

The app builds inside the firmware tree:

```sh
git clone https://github.com/armel/uv-k1-k5v3-firmware-custom.git
cd uv-k1-k5v3-firmware-custom && git checkout v6.0.0
cp -r /path/to/this/repo/app/selcall App/apps/selcall
./compile-app.sh selcall          # or: cd App/apps/selcall && ./build.sh
```

The folder must be named `selcall`, because the build script takes the app name from it. This needs
`arm-none-eabi-gcc` and `python3`. The result is `App/apps/selcall/SelCall.app`.

## Test on a PC

The decoder has no hardware dependencies, so it compiles and runs on a PC:

```sh
test/run_tests.sh                                    # synthetic CCIR regression tests
python3 tools/gen_ccir.py sig.u16 86546 26546 --noise 0.2
cc -O2 -DHOST_TEST -o host_decode test/host_decode.c && ./host_decode sig.u16
python3 tools/wav_to_adc.py recording.wav rec.u16 && ./host_decode rec.u16   # needs numpy/scipy
```

## How it works

The radio chip (BK4829) has a built-in SelCall detector, but it only reports the first of
several back-to-back tones, so it can't decode CCIR calls. SelCall does the decoding in
software instead. The received audio also reaches processor pin PA4, which normally outputs
voice prompts and can also be read by the ADC. The app samples it at 8 kHz, and Goertzel
filters find the strongest CCIR tone in each 32 ms block. See [docs/how-it-works.md](docs/how-it-works.md)
for details, and [docs/bk4829-findings.md](docs/bk4829-findings.md) for what was learned about
the chip's own detector.

## Limitations

- **Tested on:** the UV-K1 with F4HWN v6.0.0 Labs. The UV-K5 V3 and other firmware versions
  are untested. The app relies on received audio reaching PA4.
- **Tone standards:** CCIR only. EEA uses the same frequencies with 40 ms tones and may partly
  work. ZVEI uses other frequencies and isn't supported.
- **Frequency tolerance:** about ±10 Hz per tone.

## Please be considerate

Selcall systems often belong to security or safety services, and the IDs relate to real
people. Rules on listening, and especially on sharing what you hear, vary by country.
Please don't publish decoded IDs or link them to people or places.

## License

Apache License 2.0, the same as the F4HWN firmware. See [LICENSE](LICENSE). SelCall uses the
firmware's overlay-app API and build system. Thanks to Armel F4HWN and all contributors of the
UV-K5/UV-K1 open firmware projects.
