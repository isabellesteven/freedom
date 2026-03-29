# Compiler Module Catalog

The compiler keeps supported node/module metadata in
[`tools/compiler/module_catalog.py`](/C:/Users/steve/Documents/Audyl/Software/freedom/tools/compiler/module_catalog.py).
This is the compiler-side source of truth for:

- supported module names
- `module_id` values
- per-pin semantic properties
- state sizing metadata used during lowering

## Parser vs Compiler

[`tools/compiler/dsl_parser.py`](/C:/Users/steve/Documents/Audyl/Software/freedom/tools/compiler/dsl_parser.py)
is syntax-only. It parses graph structure, endpoint spelling, and literal
values, but it does not decide whether:

- a module is supported
- a pin exists on a module
- a connection is type-compatible
- a connection is channel-compatible

Those semantic checks happen in
[`tools/compiler/compiler.py`](/C:/Users/steve/Documents/Audyl/Software/freedom/tools/compiler/compiler.py)
before lowering builds schedules and buffers.

## Module Specs

Each module entry declares ordered input and output pins with explicit semantic
properties. A pin is described by:

- `name`
- `signal_type`
- `channels`

Pin properties are per-pin, not module-wide. This allows modules whose pins
have different meanings, such as an audio input and a control input with
different channel counts or signal types.

## Adding a New Compiler-Supported Module

1. Add a `ModuleSpec` entry to
   [`tools/compiler/module_catalog.py`](/C:/Users/steve/Documents/Audyl/Software/freedom/tools/compiler/module_catalog.py).
2. Declare each input and output pin with the correct `signal_type` and
   `channels`.
3. If the module has compiler-side init/default encoding rules, add that logic
   in
   [`tools/compiler/compiler.py`](/C:/Users/steve/Documents/Audyl/Software/freedom/tools/compiler/compiler.py)
   near `_module_init_and_defaults(...)`.
4. Add tests for valid connections and expected semantic failures.

## Connection Checking

During compilation, endpoint resolution determines both the pin identity and the
pin properties on each side of every edge. The compiler rejects connections when
either of these differ:

- `signal_type`
- `channels`

There are no implicit casts and no implicit channel adapters. Type or channel
mismatches are compile-time semantic errors.

## Current Limits

- Graph IO still uses one graph-wide sample rate and block size.
- The current lowering path still assumes fixed block frames for all buffers.
- Runtime execution support is still limited to the runtime's implemented
  modules and formats; the catalog may define additional compiler-only modules
  for semantic testing.
