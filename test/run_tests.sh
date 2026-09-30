#!/usr/bin/env bash
# Decoder regression tests on synthetic CCIR signals. Needs a C compiler and python3.
set -euo pipefail
cd "$(dirname "$0")/.."
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cc -O2 -DHOST_TEST -o "$T/host_decode" test/host_decode.c

CALLS="86546 26546 88174 28174 26366 86155 12345 90210 7777 55555"
pass=0; fail=0
check() {                       # name, gen options, decode gap (ms)
    local name=$1 opts=$2 gap=$3
    python3 tools/gen_ccir.py "$T/s.u16" $CALLS $opts > "$T/want"
    "$T/host_decode" "$T/s.u16" "$gap" > "$T/got"
    if diff -q "$T/want" "$T/got" >/dev/null; then echo "PASS  $name"; pass=$((pass+1))
    else echo "FAIL  $name"; diff "$T/want" "$T/got" | sed 's/^/      /'; fail=$((fail+1)); fi
}
check "clean, 700 ms first tone"      ""                          0
check "no extended first tone"        "--first 100"               0
check "noise 20 %"                    "--noise 0.2"               0
check "tones 8 Hz off"                "--offset 8"                0
check "tones -8 Hz off"               "--offset -8"               0
check "low level (60 counts)"         "--amp 30"                  0
check "sampling gaps 10 ms/block"     "--noise 0.1"               10
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
