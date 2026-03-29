# Offline File I/O Host

The offline file host is the current end-to-end integration harness for running
compiled blobs against WAV files on the PC. It exists to exercise:

- blob loading and validation
- runtime sizing and binding
- block-by-block `graph_process()` execution
- deterministic file output

The executable is built as
[`bin/file_host.exe`](/C:/Users/steve/Documents/Audyl/Software/freedom/bin/file_host.exe)
from
[`runtime/host/file_io_host.c`](/C:/Users/steve/Documents/Audyl/Software/freedom/runtime/host/file_io_host.c)
and
[`tools/file_host_main.c`](/C:/Users/steve/Documents/Audyl/Software/freedom/tools/file_host_main.c).

## Current Scope

This host intentionally follows the current runtime/compiler shape rather than
trying to solve future heterogeneous I/O now.

Current assumptions:

- one graph-wide sample rate and block size
- float32 graph I/O buffers only
- one WAV channel per exposed graph input buffer
- one WAV channel per exposed graph output buffer
- final partial blocks are zero-padded on input and truncated back to the
  original frame count on output

The host is intentionally narrow. It is a regression and experimentation tool,
not a generalized media framework.

## Usage

```powershell
bin/file_host.exe <graph.grph> <input.wav> <output.wav>
```

Example:

```powershell
python tools/cli.py build examples/gain_chain.dsl -o temp_validate/gain.grph
bin/file_host.exe temp_validate/gain.grph temp_validate/in.wav temp_validate/out.wav
```

## Roadmap Fit

This host is the intended place to add future offline runtime experiments such
as:

- deterministic parameter changes at specific block indices
- runtime state logging
- repeatable control-plane regression scenarios

Those features are deliberately out of scope for milestone 1. The current host
keeps blob/runtime coupling explicit so later extensions can stay reviewable.
