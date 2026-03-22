Here is a **compact reload briefing (~250–300 tokens)** designed specifically so you can paste it at the start of a **new chat** and immediately restore the full project context.

It contains only the information needed for the model to reason correctly about your system.

---

# DSP Framework Reload Briefing

Project: **modular DSP framework (Audio Weaver alternative)**

Goal:
Simple, portable audio DSP framework where signal processing pipelines are compiled to a binary graph blob executed by a lightweight runtime.

Architecture:

```
DSL → IR → blob → runtime
```

Components:

**DSL**
Text pipeline description:

```
graph "name"
io input mic : f32@48k block=48 ch=1
node g1 : Gain(gain_db=-6)
connect mic -> g1.in
connect g1.out -> spk
end
```

**Compiler (Python)**
Implemented.

Pipeline:

```
dsl_parser.py
compiler.py
cli.py
```

Produces **Graph Blob v1**.

**Blob format**

Sectioned binary container:

```
FileHeader
Sections
CRC32
```

Sections:

```
REQUIRES
HEAPS
BUFFERS
NODES
SCHEDULE
PARAM_DEFAULTS
GRAPH_CONFIG
```

GRAPH_CONFIG contains:

```
sample_rate_hz
block_multiple_N
```

Block model:

```
B = base block size (defined by host integration / DMA)
N = block_multiple_N (from blob)

graph_block_frames = B × N
```

Runtime executes graph once per `N` base blocks.

---

**Runtime (C)**

Subsystems:

```
blob loader
module registry
graph instantiation
scheduler
IO adapter
```

Current status:

Implemented:

```
DSL parser
graph compiler
CLI
blob loader
blob disassembler
module ABI
reference Gain module
```

Not implemented yet:

```
runtime graph instantiation
scheduler execution
IO adapter
parameter protocol
```

---

**Module ABI**

Modules implement:

```
init()
process()
set_param()
get_param()
deinit()
```

Process signature:

```
process(state, inputs[], outputs[], ctx)
```

Process context:

```
sample_rate_hz
block_frames = B × N
block_index
```

Buffers are **heap-offset views** and may alias.

---

**Current development stage**

Compiler and blob system working.
Next step: **align compiler + loader with GRAPH_CONFIG and block model, then implement runtime graph execution.**



