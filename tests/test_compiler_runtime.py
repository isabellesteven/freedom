#!/usr/bin/env python3
# Verifies the compiler/runtime path from DSL text through blob generation and static-memory execution.
# It also checks that invalid graphs are rejected with deterministic compile-time errors.
import argparse
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "tools"))

from compiler import compile_dsl_to_blob, compile_dsl_to_canonical_ir  # type: ignore  # noqa: E402


VALID_DSL = {
    "gain_chain_2": """\
graph "gain_chain_2"
  io input  mic : f32@48k block=48 ch=1
  io output spk : f32@48k block=48 ch=1

  node g1 : Gain(gain_db=-6.0)
  node g2 : Gain(gain_db=3.0)

  connect mic -> g1.in
  connect g1.out -> g2.in
  connect g2.out -> spk
end
""",
    "gain_chain_4": """\
graph "gain_chain_4"
  io input  mic : f32@48k block=48 ch=1
  io output spk : f32@48k block=48 ch=1

  node g1 : Gain(gain_db=-6.0)
  node g2 : Gain(gain_db=3.0)
  node g3 : Gain(gain_db=-3.0)
  node g4 : Gain(gain_db=6.0)

  connect mic -> g1.in
  connect g1.out -> g2.in
  connect g2.out -> g3.in
  connect g3.out -> g4.in
  connect g4.out -> spk
end
""",
    "split_two_gains": """\
graph "split_two_gains"
  io input  mic   : f32@48k block=48 ch=1
  io output left  : f32@48k block=48 ch=1
  io output right : f32@48k block=48 ch=1

  node g1 : Gain(gain_db=-6.0)
  node g2 : Gain(gain_db=6.0)

  connect mic -> g1.in
  connect mic -> g2.in
  connect g1.out -> left
  connect g2.out -> right
end
""",
    "dry_wet_mix": """\
graph "dry_wet_mix"
  io input  mic : f32@48k block=48 ch=1
  io output spk : f32@48k block=48 ch=1

  node wet : Gain(gain_db=-6.0)
  node mix : Sum2()

  connect mic -> wet.in
  connect mic -> mix.a
  connect wet.out -> mix.b
  connect mix.out -> spk
end
""",
    "sum_two_inputs": """\
graph "sum_two_inputs"
  io input  a   : f32@48k block=48 ch=1
  io input  b   : f32@48k block=48 ch=1
  io output out : f32@48k block=48 ch=1

  node mix : Sum2()

  connect a -> mix.a
  connect b -> mix.b
  connect mix.out -> out
end
""",
    "inplace_gain": """\
graph "inplace_gain"
  io input  mic : f32@48k block=48 ch=1
  io output spk : f32@48k block=48 ch=1

  node g1 : Gain(gain_db=6.0)

  connect mic -> g1.in
  connect g1.out -> spk
end
""",
    "heap_too_small": """\
graph "heap_too_small"
  io input  mic : f32@48k block=48 ch=1
  io output spk : f32@48k block=48 ch=1

  node g1 : Gain(gain_db=-6.0)
  node g2 : Gain(gain_db=3.0)

  connect mic -> g1.in
  connect g1.out -> g2.in
  connect g2.out -> spk
end
""",
}

INVALID_DSL = {
    "bad_cycle": (
        """\
graph "bad_cycle"
  io input  mic : f32@48k block=48 ch=1
  io output spk : f32@48k block=48 ch=1

  node a : Sum2()
  node b : Sum2()

  connect mic -> a.a
  connect mic -> b.a
  connect b.out -> a.b
  connect a.out -> b.b
  connect a.out -> spk
end
""",
        "cycle",
    ),
    "bad_missing_input": (
        """\
graph "bad_missing_input"
  io input  mic : f32@48k block=48 ch=1
  io output spk : f32@48k block=48 ch=1

  node mix : Sum2()

  connect mic -> mix.a
  connect mix.out -> spk
end
""",
        "missing input",
    ),
    "bad_unknown_module": (
        """\
graph "bad_unknown_module"
  io input  mic : f32@48k block=48 ch=1
  io output spk : f32@48k block=48 ch=1

  node w1 : UnknownThing()

  connect mic -> w1.in
  connect w1.out -> spk
end
""",
        "unsupported module",
    ),
}


def compile_blob(text: str) -> bytes:
    return compile_dsl_to_blob(text)


def run_valid_cases(runner: Path, disasm: Path) -> None:
    tmp = REPO_ROOT / "temp_validate" / "compiler_runtime"
    tmp.mkdir(parents=True, exist_ok=True)
    for path in tmp.glob("*.grph"):
        path.unlink()
    for name, dsl in VALID_DSL.items():
        blob = compile_blob(dsl)
        compiler_ir = compile_dsl_to_canonical_ir(dsl)
        blob_path = tmp / f"{name}.grph"
        blob_path.write_bytes(blob)
        completed = subprocess.run(
            [str(runner), name, str(blob_path)],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
        )
        if completed.returncode != 0:
            raise SystemExit(
                f"{name} runtime validation failed\nstdout:\n{completed.stdout}\n"
                f"stderr:\n{completed.stderr}"
            )
        ir_completed = subprocess.run(
            [str(disasm), "--canonical", str(blob_path)],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
        )
        if ir_completed.returncode != 0:
            raise SystemExit(
                f"{name} canonical disassembly failed\nstdout:\n{ir_completed.stdout}\n"
                f"stderr:\n{ir_completed.stderr}"
            )
        if compiler_ir != ir_completed.stdout:
            raise SystemExit(
                f"{name} canonical IR mismatch\n"
                f"--- compiler ---\n{compiler_ir}\n"
                f"--- disasm ---\n{ir_completed.stdout}\n"
            )


def run_invalid_cases() -> None:
    for name, (dsl, needle) in INVALID_DSL.items():
        try:
            compile_blob(dsl)
        except Exception as exc:
            if needle not in str(exc):
                raise SystemExit(f"{name} error mismatch: expected '{needle}', got '{exc}'")
        else:
            raise SystemExit(f"{name} unexpectedly compiled")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", required=True)
    parser.add_argument("--disasm", required=True)
    args = parser.parse_args()

    runner = Path(args.runner)
    disasm = Path(args.disasm)
    if not runner.exists():
        raise SystemExit(f"runner not found: {runner}")
    if not disasm.exists():
        raise SystemExit(f"disasm not found: {disasm}")

    run_valid_cases(runner, disasm)
    run_invalid_cases()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
