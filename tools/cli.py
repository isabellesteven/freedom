#!/usr/bin/env python3
import argparse
from pathlib import Path
import sys

from compiler import compile_dsl_to_blob


def build_cmd(args: argparse.Namespace) -> int:
    in_path = Path(args.dsl)
    out_path = Path(args.output)

    try:
        dsl_text = in_path.read_text(encoding="utf-8")
    except OSError as exc:
        print(f"error: failed to read {in_path}: {exc}", file=sys.stderr)
        return 1

    try:
        blob = compile_dsl_to_blob(dsl_text)
    except Exception as exc:  # explicit error text for CLI users
        print(f"error: compile failed: {exc}", file=sys.stderr)
        return 1

    try:
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_bytes(blob)
    except OSError as exc:
        print(f"error: failed to write {out_path}: {exc}", file=sys.stderr)
        return 1

    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="cli.py")
    sub = parser.add_subparsers(dest="command", required=True)

    build = sub.add_parser("build", help="compile DSL to v1 graph blob")
    build.add_argument("dsl", help="input DSL path")
    build.add_argument("-o", "--output", required=True, help="output blob path")
    build.set_defaults(func=build_cmd)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))