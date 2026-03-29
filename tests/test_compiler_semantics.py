#!/usr/bin/env python3
"""Exercises compiler-side module semantics and per-pin compatibility checks."""

import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "tools"))

from compiler import compile_dsl_to_blob, compile_dsl_to_canonical_ir  # type: ignore  # noqa: E402


def expect_compile_ok(name: str, dsl: str) -> None:
    try:
        blob = compile_dsl_to_blob(dsl)
    except Exception as exc:  # pragma: no cover - test failure path
        raise SystemExit(f"{name} unexpectedly failed: {exc}")
    if not blob:
        raise SystemExit(f"{name} produced an empty blob")


def expect_canonical_ok(name: str, dsl: str, needle: str) -> None:
    try:
        ir = compile_dsl_to_canonical_ir(dsl)
    except Exception as exc:  # pragma: no cover - test failure path
        raise SystemExit(f"{name} unexpectedly failed: {exc}")
    if needle not in ir:
        raise SystemExit(f"{name} canonical IR missing expected text: {needle}")


def expect_compile_error(name: str, dsl: str, needle: str) -> None:
    try:
        compile_dsl_to_blob(dsl)
    except Exception as exc:
        if needle not in str(exc):
            raise SystemExit(f"{name} error mismatch: expected '{needle}', got '{exc}'")
        return
    raise SystemExit(f"{name} unexpectedly compiled")


def main() -> int:
    expect_compile_ok(
        "gain_ok",
        """\
graph "gain_ok"
  io input  mic : f32@48k block=48 ch=1
  io output spk : f32@48k block=48 ch=1

  node g1 : Gain(gain_db=-6.0)

  connect mic -> g1.in
  connect g1.out -> spk
end
""",
    )

    expect_compile_ok(
        "typed_ok",
        """\
graph "typed_ok"
  io input  mic : f32@48k block=48 ch=2
  io output spk : f32@48k block=48 ch=2

  node ctrl : ConstFixed()
  node mix  : StereoControlMix()

  connect mic -> mix.audio_in
  connect ctrl.out -> mix.control_in
  connect mix.audio_out -> spk
end
""",
    )

    expect_canonical_ok(
        "typed_ir",
        """\
graph "typed_ir"
  io input  mic : f32@48k block=48 ch=2
  io output spk : f32@48k block=48 ch=2

  node ctrl : ConstFixed()
  node mix  : StereoControlMix()

  connect mic -> mix.audio_in
  connect ctrl.out -> mix.control_in
  connect mix.audio_out -> spk
end
""",
        "fmt=FIXED32",
    )

    expect_compile_error(
        "node_node_type_mismatch",
        """\
graph "node_node_type_mismatch"
  io input  mic : f32@48k block=48 ch=1
  io output spk : f32@48k block=48 ch=1

  node ctrl : ConstFixed()
  node mono : Gain()

  connect ctrl.out -> mono.in
  connect mono.out -> spk
end
""",
        "type mismatch: ctrl.out (fixed32) -> mono.in (float32)",
    )

    expect_compile_error(
        "node_node_channel_mismatch",
        """\
graph "node_node_channel_mismatch"
  io input  mic : f32@48k block=48 ch=1
  io output spk : f32@48k block=48 ch=1

  node wide : StereoSource()
  node mono : Gain()

  connect wide.out -> mono.in
  connect mono.out -> spk
end
""",
        "channel mismatch: wide.out (2 ch) -> mono.in (1 ch)",
    )

    expect_compile_error(
        "graph_input_mismatch",
        """\
graph "graph_input_mismatch"
  io input  mic : f32@48k block=48 ch=2
  io output spk : f32@48k block=48 ch=2

  node mix : StereoControlMix()

  connect mic -> mix.control_in
  connect mic -> mix.audio_in
  connect mix.audio_out -> spk
end
""",
        "type mismatch: graph input mic (float32) -> mix.control_in (fixed32)",
    )

    expect_compile_error(
        "graph_output_mismatch",
        """\
graph "graph_output_mismatch"
  io input  mic : f32@48k block=48 ch=2
  io output spk : f32@48k block=48 ch=2

  node tap : StereoToMonoTap()

  connect mic -> tap.in
  connect tap.out -> spk
end
""",
        "channel mismatch: tap.out (1 ch) -> graph output spk (2 ch)",
    )

    expect_compile_error(
        "unknown_module",
        """\
graph "unknown_module"
  io input  mic : f32@48k block=48 ch=1
  io output spk : f32@48k block=48 ch=1

  node nope : UnknownThing()

  connect mic -> nope.in
  connect nope.out -> spk
end
""",
        "unsupported module: UnknownThing",
    )

    expect_compile_error(
        "unknown_pin",
        """\
graph "unknown_pin"
  io input  mic : f32@48k block=48 ch=2
  io output spk : f32@48k block=48 ch=2

  node mix : StereoControlMix()

  connect mic -> mix.wrong
  connect mix.audio_out -> spk
end
""",
        "invalid input pin mix.wrong",
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
