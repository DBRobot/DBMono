"""
regc -- the DBcan register map compiler.

Subcommands do the work; `gen` is the only one implemented so far. The global
register map and the schema ship with the package, so a caller supplies only
which targets to build and where output should go.
"""

import argparse
import json
import re
import shutil
import sys
import tempfile
from dataclasses import dataclass
from importlib.metadata import version
from importlib.resources import files
from pathlib import Path

import yaml

from . import ir as reg_ir
from . import ffi
from .backends import c as backend_c
from .backends import python as backend_python


DATA = files("regc.data")


@dataclass(frozen=True)
class Target:
    backend:   object                 # module exposing TEMPLATE, OUTPUT, MARKERS
    help:      str
    needs:     tuple = ()             # targets that must be generated first
    build_ext: bool  = False          # also compile the cffi extension


#  Adding a language is one backend module plus one entry here.
TARGETS = {
    "c":  Target(backend_c,      "C header"),
    "py": Target(backend_python, "Python module and compiled binding",
                 needs=("c",), build_ext=True),
}


def load_map():
    return yaml.safe_load(DATA.joinpath("global_reg.yaml").read_text())


def load_schema():
    return json.loads(DATA.joinpath("register.json").read_text())


MARKER = re.compile(r"@@\w*@@?")


def render(template_name, output_path, markers, ir):
    """Copy a template through, replacing marker lines with generated text.

    A marker with no handler, or a handler no marker ever used, is a hard
    error: silently copying a placeholder into the output produces a file
    that still compiles but is missing a whole generated block.
    """
    template = DATA.joinpath("templates", template_name).read_text()
    used = set()

    with open(output_path, "w") as out:
        for lineno, line in enumerate(template.splitlines(keepends=True), 1):
            key = line.strip()
            handler = markers.get(key)
            if handler:
                used.add(key)
                out.write(handler(ir))
            else:
                if MARKER.search(line):
                    raise SystemExit(
                        f"regc: {template_name}:{lineno}: no handler for marker "
                        f"{key!r}")
                out.write(line)

    unused = set(markers) - used
    if unused:
        raise SystemExit(f"regc: {template_name}: handlers never matched a "
                         f"marker: {', '.join(sorted(unused))}")


def load_board(path):
    if path is None:
        return None
    p = Path(path)
    if not p.is_file():
        raise SystemExit(f"regc: no such board map: {p}")
    return yaml.safe_load(p.read_text())


def build(args):
    """Load the shipped global map, plus a board map if given, into the IR.

    Validation happens inside build_ir, which exits non-zero and reports every
    problem it found if either map is bad.
    """
    return reg_ir.build_ir(load_map(), load_schema(),
                           board=load_board(getattr(args, "board", None)))


def resolve(requested):
    """Requested targets plus everything they depend on."""
    needed, queue = set(), list(requested)
    while queue:
        name = queue.pop()
        if name in needed:
            continue
        needed.add(name)
        queue.extend(TARGETS[name].needs)
    return needed


# ============================================================================
#  SUBCOMMANDS
# ============================================================================

def cmd_gen(args):
    requested = set(TARGETS) if "all" in args.targets else set(args.targets)
    needed    = resolve(requested)

    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)

    ir = build(args)

    #  Everything is generated into staging, including dependencies that were
    #  not asked for -- only the requested targets' artifacts are copied out.
    with tempfile.TemporaryDirectory() as tmp:
        staging = Path(tmp)
        artifacts = []

        for name in needed:
            backend = TARGETS[name].backend
            render(backend.TEMPLATE, staging / backend.OUTPUT, backend.MARKERS, ir)
            if name in requested:
                artifacts.append(staging / backend.OUTPUT)

        if any(TARGETS[n].build_ext for n in requested):
            extension = ffi.build(ir, staging)
            if extension is not None:
                artifacts.append(extension)

        written = [shutil.copy2(a, out / a.name) for a in artifacts]

    print(f"regc: {len(ir.registers)} registers, hash 0x{ir.crc:08X}")
    for path in written:
        print(f"  {path}")
    return 0


def cmd_hash(args):
    print(f"0x{build(args).crc:08X}")
    return 0


def cmd_list(args):
    ir = build(args)
    width = max(len(r["path"]) for r in ir.registers)

    print(f"{'NUM':>5}  {'PATH':<{width}}  {'TYPE':<7} {'BYTES':>5}  PERM")
    for reg in ir.registers:
        dtype = reg.get("dtype") or "-"
        print(f"{reg['num']:>5}  {reg['path']:<{width}}  {dtype:<7} "
              f"{reg['width']:>5}  {reg['perm']}")
    return 0


def cmd_validate(args):
    ir = build(args)          # build_ir reports and exits non-zero on any problem
    print(f"regc: ok — {len(ir.registers)} registers, hash 0x{ir.crc:08X}")
    return 0


# ============================================================================
#  ENTRY POINT
# ============================================================================

def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="regc",
        description="Compile the DBcan register map into code.",
    )
    parser.add_argument("--version", action="version",
                        version=f"regc {version('regc')}")
    sub = parser.add_subparsers(dest="command", required=True, metavar="COMMAND")

    def board_arg(p):
        p.add_argument("-b", "--board", metavar="YAML",
                       help="a board's own registers, appended above BOARD_BASE")
        return p

    targets_help = "; ".join(f"{n} = {t.help}" for n, t in TARGETS.items())
    gen = board_arg(sub.add_parser("gen", help="generate code from the register map",
                                   epilog=f"targets: {targets_help}; all = everything"))
    gen.add_argument("targets", nargs="+", choices=[*TARGETS, "all"], metavar="TARGET",
                     help=f"one or more of: {', '.join([*TARGETS, 'all'])}")
    gen.add_argument("-o", "--out", default=".", metavar="DIR",
                     help="output directory (default: current directory)")
    gen.set_defaults(func=cmd_gen)

    board_arg(sub.add_parser("hash", help="print the register map hash")
              ).set_defaults(func=cmd_hash)

    board_arg(sub.add_parser("list", help="list every register with its number and type")
              ).set_defaults(func=cmd_list)

    board_arg(sub.add_parser("validate", help="check the map, write nothing, exit non-zero on error")
              ).set_defaults(func=cmd_validate)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
