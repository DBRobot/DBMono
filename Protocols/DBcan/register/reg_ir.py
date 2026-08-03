"""
Register map IR — the language-neutral middle stage.

Reads the YAML register map plus its JSON Schema and produces a RegisterMap:
a flat list of register dicts with paths, permissions, offsets and widths
resolved, alongside the vocab tables and the map hash. Nothing in this module
knows about C (or any other output language).
"""

import sys
import json
import zlib
import struct
import jsonschema
from dataclasses import dataclass, field


# ============================================================================
#  IR CONSTRUCTION
#  Walk the YAML register tree into one flat list of register dicts. Each entry
#  gets a dotted path, resolved permission, byte offset, and width. Leaves own
#  storage; groups span their children (offset = first child, width = the span).
# ============================================================================

def walk_map(node, path = "", perm = "ANY", reg_list = None, cursor = 0):
    if reg_list is None:
        reg_list = []
    name = node["name"]
    path = name if not path else f"{path}.{name}"
    children = node.get("children")

    entry = dict(node)
    entry["path"]   = path
    entry["perm"]   = node.get("permissions") or perm
    entry["byte_offset"] = cursor                        # storage offset (distinct from engineering `offset`)
    reg_list.append(entry)

    if children:
        for child in children:
            cursor = walk_map(child, path, node.get("permissions"), reg_list, cursor)
        entry["width"] = cursor - entry["byte_offset"]
    else:
        bits = width_of(entry)
        entry["width"] = (bits + 7) // 8
        cursor += entry["width"]

    return cursor


def width_of(reg):
    dtype = reg.get("dtype")
    array = reg.get("array_size") or 1
    if dtype is None:
        return 0
    if dtype == "bool":
        return 1 * array
    if dtype == "struct":
        return sum(f["bits"] for f in reg["values"]) * array
    return reg["bits"] * array


def apply_schema_defaults(reg, primitive, group):
    props = group if "children" in reg else primitive
    for key, spec in props.items():
        if "default" in spec and key not in reg:
            reg[key] = spec["default"]


# ============================================================================
#  NUMERIC DERIVATION
#  Storage class, raw<->engineering conversion, default encoding. Pure integer
#  and byte math — the same answers regardless of which backend consumes them.
# ============================================================================

def round_up_bits(bits):
    for w in (8, 16, 32, 64):
        if bits <= w:
            return w
    raise SystemExit(f"struct field width {bits} exceeds 64 bits")


def encode_default(reg):
    dt, w, val = reg["dtype"], reg["width"], reg["default"]
    if dt == "bool":
        return bytes([1 if val else 0])
    if dt == "enum":
        return int(val).to_bytes(w, "little")
    if dt in ("uint", "int"):
        raw = round((val - reg.get("offset", 0)) / reg["resolution"])
        return int(raw).to_bytes(w, "little", signed=(dt == "int"))
    if dt == "float":
        return struct.pack("<f" if w == 4 else "<d", float(val))
    if dt == "string":
        return str(val).encode("ascii")[:w].ljust(w, b"\0")
    return bytes(w)


def raw_bounds(reg):
    if reg.get("dtype") not in ("uint", "int"):
        return 1, 0
    has_min, has_max = "min_value" in reg, "max_value" in reg
    if not has_min and not has_max:
        return 1, 0
    res, off = reg["resolution"], reg.get("offset", 0)
    bits, dt = reg["bits"], reg["dtype"]
    lo = round((reg["min_value"] - off) / res) if has_min else (0 if dt == "uint" else -(1 << (bits - 1)))
    hi = round((reg["max_value"] - off) / res) if has_max else ((1 << bits) - 1 if dt == "uint" else (1 << (bits - 1)) - 1)
    if not (-(1 << 31) <= lo <= (1 << 31) - 1 and -(1 << 31) <= hi <= (1 << 31) - 1):
        raise SystemExit(f"{reg['path']}: raw bounds exceed int32 (widen descriptor min/max to int64)")
    return lo, hi


def scaled(item):
    if item.get("dtype") not in ("uint", "int"):
        return False
    if item.get("resolution", 1) == 1 and item.get("offset", 0) == 0:
        return False
    return item["bits"] <= 64


# ============================================================================
#  ERROR CODES
#  Command primitives get the fixed global success/failed pair; everything else
#  numbers its custom errors from 64 up (0-63 reserved for standard codes).
# ============================================================================

CMD_ERRORS = [
    {"name": "success", "code": 0},
    {"name": "failed",  "code": 1},
]


def resolve_errors(reg):
    """[(name, code), ...] for a register that declares errors, else None."""
    if reg.get("perm") == "CMD" and "children" not in reg:
        return [(e["name"], e["code"]) for e in CMD_ERRORS]
    arr = reg.get("errors")
    if not arr:
        return None
    return [(e["name"], 64 + i) for i, e in enumerate(arr)]


# ============================================================================
#  DERIVATION
#  Everything a backend would otherwise have to compute while emitting. Runs
#  once, after the hash, so these fields can never perturb it. A backend reads
#  the results and formats them; it never calls the functions above.
# ============================================================================

def derive(registers, value_map):
    for reg in registers:
        dt   = reg.get("dtype")
        bits = reg.get("bits")

        if dt in ("uint", "int", "enum") and bits is not None and bits <= 64:
            reg["storage_bits"] = round_up_bits(bits)

        reg["min_raw"], reg["max_raw"] = raw_bounds(reg)
        reg["scaled"] = scaled(reg)

        errs = resolve_errors(reg)
        if errs is not None:
            reg["errors_resolved"] = errs

        if dt == "enum":
            vals = reg["values"]
            reg["values_resolved"] = (value_map.get(vals, FAULT_VALUES)
                                      if isinstance(vals, str) else vals)

        if dt == "struct":
            for f in reg["values"]:
                fbits = f.get("bits")
                if f.get("dtype") in ("uint", "int", "enum") and fbits is not None and fbits <= 64:
                    f["storage_bits"] = round_up_bits(fbits)
                f["scaled"] = scaled(f)

        # groups, commands and structs own no scalar default
        if "children" not in reg and dt not in (None, "struct"):
            reg["default_bytes"] = encode_default(reg)


# ============================================================================
#  HASH
#  CRC32 over the canonical form of every register definition. Must be computed
#  here, never in a backend: every language has to agree on this number.
#
#  The hashed field set is taken from the schema, so a new attribute added to
#  register.json counts toward map identity automatically. Derived fields are
#  never schema properties and so can never leak in — which also means it does
#  not matter whether derive() has already run.
# ============================================================================

#  children: each child is hashed as its own entry
#  description: documentation edits must not invalidate a shipped map
HASH_EXCLUDE = {"children", "description"}

#  computed by walk_map, not declared in the schema, but part of identity:
#  path names the register, perm is resolved from its parents
HASH_EXTRA = {"path", "perm"}


def hashed_keys(primitive, group):
    return (set(primitive) | set(group) | HASH_EXTRA) - HASH_EXCLUDE


def compute_crc(registers, keys):
    parts = []
    for num, reg in enumerate(registers):
        defn = {k: reg[k] for k in keys if k in reg}
        defn["num"] = num
        parts.append(json.dumps(defn, sort_keys=True, default=str))
    return zlib.crc32("\n".join(parts).encode())


# ============================================================================
#  VALIDATION
#  Run before emitting anything. Structural (jsonschema vs register.json) catches
#  malformed registers; semantic catches the cross-field rules the schema can't
#  express. Each collects every error and reports together, then exits non-zero.
# ============================================================================

def validate_structural(schema, data):
    errs = sorted(jsonschema.Draft7Validator(schema).iter_errors(data),
                  key=lambda e: list(e.path))
    if errs:
        for e in errs:
            loc = "/".join(str(p) for p in e.path) or "(root)"
            print(f"SCHEMA ERROR at {loc}: {e.message}", file=sys.stderr)
        raise SystemExit(1)


def validate_map(registers):
    errs = []

    enum_names = [r["name"] for r in registers if r.get("dtype") == "enum"]
    for dup in sorted({n for n in enum_names if enum_names.count(n) > 1}):
        errs.append(f"enum register name '{dup}' must be unique across the map")

    for reg in registers:
        p = reg["path"]
        if len(p) > 48:
            errs.append(f"{p}: path is {len(p)} chars (max 48)")
        if reg.get("dtype") in ("uint", "int"):
            res, off, default = reg["resolution"], reg.get("offset", 0), reg["default"]
            r = (default - off) / res
            if abs(r - round(r)) > 1e-9:                       # must land on a whole raw value
                errs.append(f"{p}: default {default} not a multiple of resolution {res}")
            lo, hi = reg.get("min_value"), reg.get("max_value")
            if lo is not None and hi is not None and lo > hi:
                errs.append(f"{p}: min_value {lo} > max_value {hi}")
            if lo is not None and default < lo:
                errs.append(f"{p}: default {default} < min_value {lo}")
            if hi is not None and default > hi:
                errs.append(f"{p}: default {default} > max_value {hi}")

    if errs:
        print("MAP VALIDATION FAILED:", file=sys.stderr)
        for e in errs:
            print("  " + e, file=sys.stderr)
        raise SystemExit(1)


# ============================================================================
#  THE IR
#  Everything a backend is allowed to read. Backends take one of these and
#  nothing else — no module globals, no reaching back into the schema.
# ============================================================================

@dataclass
class RegisterMap:
    registers:    list          # flat; list index == wire ID
    store_bytes:  int           # total size of the register store blob
    dtypes:       list          # dtype vocab, in schema order; number == index + 1
    field_dtypes: list          # struct-field dtype vocab, same numbering
    perms:        list          # permission vocab
    crc:          int


FAULT_VALUES = {"no_fault": 0}


def build_ir(source, schema):
    primitive = schema["definitions"]["primitive"]["properties"]
    group     = schema["definitions"]["group"]["properties"]

    _values_forms = primitive["values"]["anyOf"]
    _field_form   = next(f for f in _values_forms if f.get("type") == "array")

    validate_structural(schema, source)

    registers = []
    cursor = 0
    for top in source["children"]:
        cursor = walk_map(top, "", source["permissions"], registers, cursor)

    value_map = { r["path"].split(".")[-1]: r["values"]
                  for r in registers
                  if r.get("dtype") == "enum" and isinstance(r.get("values"), dict) }

    # fill omitted attributes from the schema's declared defaults, then semantic-validate
    for reg in registers:
        apply_schema_defaults(reg, primitive, group)

    validate_map(registers)

    crc = compute_crc(registers, hashed_keys(primitive, group))

    derive(registers, value_map)

    return RegisterMap(
        registers    = registers,
        store_bytes  = cursor,
        dtypes       = primitive["dtype"]["enum"],
        field_dtypes = _field_form["items"]["properties"]["dtype"]["enum"],
        perms        = group["permissions"]["enum"],
        crc          = crc,
    )
