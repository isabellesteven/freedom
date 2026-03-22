# PC User Manual

This manual covers the current PC-side workflow for ad hoc testing:

1. Write a graph in the DSL
2. Compile it into a `.grph` blob
3. Verify that the generated blob looks correct

This document does not cover runtime execution, host IO, DMA, or embedded integration.

## Prerequisites

You need:

- Python 3
- A Windows shell such as PowerShell
- The project checked out locally

Optional:

- A C compiler and `make` if you need to rebuild the `disasm` tool

## Repository Layout

The main files used for this workflow are:

- `tools/cli.py`
  - compiles DSL into a blob
- `examples/gain_chain.dsl`
  - reference DSL example
- `bin/disasm.exe`
  - prints a human-readable view of a blob
- `tests/golden/gain_chain.disasm.txt`
  - known-good disassembly output for the example graph

## Step 1: Write a DSL File

Start from the existing example:

```txt
graph "gain_chain"
  io input  mic : f32@48k block=48 ch=1
  io output spk : f32@48k block=48 ch=1

  node g1 : Gain(gain_db=-6.0)

  connect mic -> g1.in
  connect g1.out -> spk
end
```

### DSL Structure

- `graph "name"`
  - names the graph
- `io input ...`
  - declares a graph input
- `io output ...`
  - declares a graph output
- `node ... : Gain(...)`
  - declares a processing node
- `connect a -> b`
  - wires endpoints together
- `end`
  - closes the graph

### Current Safe Starting Point

For ad hoc testing on the PC, the safest starting point is a single-node Gain graph modeled on `examples/gain_chain.dsl`.

Recommended constraints for now:

- `f32@48k`
- `block=48`
- `ch=1`
- one `Gain` node

Create your own file, for example:

`temp_validate/my_test.dsl`

## Step 2: Compile the DSL

Run:

```powershell
python tools/cli.py build temp_validate/my_test.dsl -o temp_validate/my_test.grph
```

Or compile the provided example:

```powershell
python tools/cli.py build examples/gain_chain.dsl -o out.grph
```

### Expected Result

If compilation succeeds:

- the command exits successfully
- the output `.grph` file is created

If compilation fails:

- the tool prints an error beginning with `error: compile failed:`

That usually means the DSL has a syntax problem or uses something the compiler does not support yet.

## Step 3: Disassemble the Blob

To verify what was compiled, use the disassembler.

If `bin/disasm.exe` already exists, run:

```powershell
bin/disasm.exe temp_validate/my_test.grph
```

To capture the output:

```powershell
bin/disasm.exe temp_validate/my_test.grph > temp_validate/my_test.disasm.txt
```

For the example graph:

```powershell
bin/disasm.exe out.grph > out.disasm.txt
```

## Step 4: Verify the Output

There are two practical ways to verify correctness.

### Option A: Compare Against the Golden Example

For `examples/gain_chain.dsl`, compare:

- generated: `out.disasm.txt`
- expected: `tests/golden/gain_chain.disasm.txt`

What should match:

- module list includes `Gain`
- graph config shows:
  - `sample_rate_hz=48000`
  - `block_multiple_N=1`
- there are two buffers
- there is one node
- the schedule contains one call:
  - `CALL node=10 in=[1] out=[2]`
- the footer says:
  - `CRC32: ok`

### Option B: Manually Inspect the Disassembly

A correct single-node Gain graph should look broadly like this:

```txt
[REQUIRES]
  module_count=1
  - module_id=0x00001001 version=1.0

[GRAPH_CONFIG]
  sample_rate_hz=48000
  block_multiple_N=1

[NODES]
  node_count=1
  - node_id=10 module=0x00001001 state_heap=2 ...

[SCHEDULE]
  op_count=1
  0: CALL node=10 in=[1] out=[2]

CRC32: ok
```

The exact formatting may include more fields, but these are the key items to check.

## Rebuilding the Disassembler

If `bin/disasm.exe` is missing or stale, rebuild it from the repo root:

```powershell
make disasm
```

This should produce:

- `bin/disasm.exe`

After that, rerun the disassembly step.

## Recommended Ad Hoc Workflow

Use this loop:

1. Edit a DSL file
2. Compile it with `python tools/cli.py build ...`
3. Disassemble the blob with `bin/disasm.exe`
4. Inspect the output for the expected nodes, buffers, schedule, and `CRC32: ok`

Example:

```powershell
python tools/cli.py build examples/gain_chain.dsl -o out.grph
bin/disasm.exe out.grph > out.disasm.txt
```

Then open `out.disasm.txt` and check that it matches the graph you intended to describe.

## Common Problems

### Compile Error

Symptom:

```txt
error: compile failed: ...
```

Meaning:

- the DSL is malformed, or
- the graph uses features the compiler does not support yet

Action:

- start from `examples/gain_chain.dsl`
- make one small change at a time

### Disassembler Missing

Symptom:

- `bin/disasm.exe` does not exist

Action:

```powershell
make disasm
```

### Blob Looks Wrong

Symptoms:

- missing node
- wrong module id
- wrong schedule
- `CRC32: ok` is not present

Action:

- confirm the DSL connections are correct
- recompile from a clean output file
- compare against `tests/golden/gain_chain.disasm.txt`

## Current Scope Limits

This manual intentionally stays within the current PC validation path.

Not covered yet:

- runtime execution
- audio device integration
- host buffering policy
- parameter protocol
- hardware IO

For now, the main goal is to confirm:

- the DSL is accepted
- the compiler produces a blob
- the disassembly matches the intended graph structure
