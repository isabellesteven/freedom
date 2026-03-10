#!/usr/bin/env sh
set -eu

python tools/cli.py build examples/gain_chain.dsl -o out.grph
./bin/disasm out.grph > out.disasm.txt
diff -u --strip-trailing-cr tests/golden/gain_chain.disasm.txt out.disasm.txt
