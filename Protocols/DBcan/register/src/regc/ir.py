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


def raw_default(reg):
    """The default as it is stored: raw counts for scaled numbers, the integer
    for an enum, the text for a string. None where a register owns no scalar."""
    dt, val = reg.get("dtype"), reg.get("default")
    if dt == "bool":
        return 1 if val else 0
    if dt == "enum":
        return int(val)
    if dt in ("uint", "int"):
        return round((val - reg.get("offset", 0)) / reg["resolution"])
    if dt == "float":
        return float(val)
    if dt == "string":
        return str(val)
    return None


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


def eng_type(item):
    """Engineering-value type of a scaled item: 'int' if resolution and
    offset are both whole, else 'float'. None when nothing is scaled."""
    if not scaled(item):
        return None
    res, off = item.get("resolution", 1), item.get("offset", 0)
    return "int" if float(res).is_integer() and float(off).is_integer() else "float"


# ============================================================================
#  ERROR CODES
#  A register numbers its own errors from 64 up; 0-63 are reserved for the
#  standard codes, which includes the fixed command success/failed pair.
# ============================================================================

#  Named value sets a register may reference by name instead of declaring
#  inline. `faults` is declared here and deliberately empty -- there is no
#  fault registry yet, so a reference to it resolves to no values at all and
#  backends emit no type. Nothing is invented.
NAMED_VALUES = {
    "faults": {},
}


def resolve_values(vals, value_map):
    """A register's value set: inline dict, or a name referring to another."""
    if not isinstance(vals, str):
        return vals or {}
    if vals in value_map:
        return value_map[vals]
    if vals in NAMED_VALUES:
        return NAMED_VALUES[vals]
    raise SystemExit(f"unknown values reference: {vals!r}")


def resolve_errors(reg):
    """[(name, code), ...] for a register declaring its own errors, else None.

    Command primitives also carry the global success/failed result, but that
    pair is fixed for every command, so backends emit it once as a shared type
    rather than a duplicate per register. It is not returned here.
    """
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

def perm_bits(permissions):
    """Map a YAML permission string to bits.

    RW and ANY are composites rather than values of their own -- ANY means
    unrestricted, which is read plus write.
    """
    r, w, c = permissions["r"], permissions["w"], permissions["cmd"]
    return {"R": r, "W": w, "RW": r | w, "CMD": c, "ANY": r | w}


def effective_perms(registers, bits):
    """Fold each group's permission together with everything stored inside it.

    A group can be read or written as one blob, so it may only permit what
    every register contributing bytes to that blob permits -- writing
    node_state as a unit would otherwise write straight through
    node_state.current, which is read-only.

    Commands are skipped: they carry no storage, so they contribute nothing
    to the blob and should not veto access to the group holding them.
    """
    for reg in registers:
        reg["perm_bits"] = bits[reg["perm"]]

    for reg in registers:
        if "children" not in reg:
            continue
        prefix = reg["path"] + "."
        stored = [r for r in registers
                  if r["path"].startswith(prefix)
                  and "children" not in r
                  and r["width"] > 0]

        allowed = 0xFF                                   # identity for AND
        for leaf in stored:
            allowed &= bits[leaf["perm"]]

        reg["perm_bits"] = bits[reg["perm"]] & allowed


def derive(registers, value_map, bits):
    effective_perms(registers, bits)

    for reg in registers:
        dt   = reg.get("dtype")
        bits = reg.get("bits")

        if dt in ("uint", "int", "enum") and bits is not None and bits <= 64:
            reg["storage_bits"] = round_up_bits(bits)

        reg["min_raw"], reg["max_raw"] = raw_bounds(reg)
        reg["scaled"]   = scaled(reg)
        reg["eng_type"] = eng_type(reg)

        errs = resolve_errors(reg)
        if errs is not None:
            reg["errors_resolved"] = errs

        if dt == "enum":
            reg["values_resolved"] = resolve_values(reg["values"], value_map)

        if dt == "struct":
            cursor = 0                                   # bit position within the register
            for f in reg["values"]:
                fbits = f.get("bits")
                if f.get("dtype") in ("uint", "int", "enum") and fbits is not None and fbits <= 64:
                    f["storage_bits"] = round_up_bits(fbits)
                f["scaled"]   = scaled(f)
                f["eng_type"] = eng_type(f)

                if f.get("dtype") == "enum":
                    f["values_resolved"] = resolve_values(f.get("values"), value_map)

                # placement within the register, so a backend that cannot see a
                # C struct layout can still slice the payload
                f["bit_offset"]  = cursor
                f["byte_offset"] = cursor // 8
                cursor += fbits or 0

        # groups, commands and structs own no scalar default
        if "children" not in reg and dt not in (None, "struct"):
            reg["default_bytes"] = encode_default(reg)
            reg["default_raw"]   = raw_default(reg)


def collect_converters(registers):
    """Every raw<->engineering conversion the map calls for.

    One list both backends walk, so the C functions and the Python wrappers
    that call them are guaranteed to agree on names and types.
    """
    out = []

    def entry(name, item):
        return {
            "name":       name,
            "units":      item.get("units", "eng"),
            "eng_type":   item["eng_type"],
            "resolution": item.get("resolution", 1),
            "offset":     item.get("offset", 0),
            "raw_dtype":  item["dtype"],
            "raw_bits":   item["storage_bits"],
        }

    for reg in distinct_types(registers):
        base = reg.get("type_path", reg["path"]).replace(".", "_")
        if reg.get("dtype") in ("uint", "int") and reg["scaled"]:
            out.append(entry(base, reg))
        elif reg.get("dtype") == "struct":
            for f in reg["values"]:
                if f["scaled"]:
                    out.append(entry(f"{base}_{f['name']}", f))
    return out


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
HASH_EXCLUDE = {"children", "description", "base", "count", "overrides"}

#  computed by walk_map, not declared in the schema, but part of identity:
#  path names the register, perm is resolved from its parents
HASH_EXTRA = {"path", "perm"}


def hashed_keys(primitive, group):
    return (set(primitive) | set(group) | HASH_EXTRA) - HASH_EXCLUDE


def compute_crc(registers, keys):
    parts = []
    for reg in registers:
        defn = {k: reg[k] for k in keys if k in reg}
        defn["num"] = reg["num"]
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

    #  Not expressible in JSON Schema: uniqueItems compares whole objects, so
    #  it cannot require one property to be unique across siblings.
    paths = [r["path"] for r in registers]
    for dup in sorted({p for p in paths if paths.count(p) > 1}):
        errs.append(f"duplicate register path '{dup}' — sibling names must be unique")

    enum_names = [r["name"] for r in registers
                  if r.get("dtype") == "enum" and r.get("copy", 0) == 0]
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

    report(errs)


def report(errs):
    if errs:
        print("MAP VALIDATION FAILED:", file=sys.stderr)
        for e in errs:
            print("  " + e, file=sys.stderr)
        raise SystemExit(1)


def validate_derived(registers):
    """Checks that need resolved values, so they run after derive().

    An enum value wider than its register truncates on the wire with nothing
    downstream to catch it -- neither compiler complains, it is simply the
    wrong number on the bus.
    """
    errs = []

    def check_values(what, bits, values):
        for name, value in values.items():
            if value < 0:
                errs.append(f"{what}: value '{name}' = {value} is negative; "
                            f"enum storage is unsigned")
            elif value >= (1 << bits):
                errs.append(f"{what}: value '{name}' = {value} does not fit in "
                            f"{bits} bits (max {(1 << bits) - 1})")

    for reg in registers:
        if reg.get("dtype") == "enum":
            check_values(reg["path"], reg["bits"], reg["values_resolved"])
        elif reg.get("dtype") == "struct":
            for f in reg["values"]:
                if f.get("dtype") == "enum":
                    check_values(f"{reg['path']}.{f['name']}", f["bits"],
                                 f["values_resolved"])

    report(errs)


# ============================================================================
#  THE IR
#  Everything a backend is allowed to read. Backends take one of these and
#  nothing else — no module globals, no reaching back into the schema.
# ============================================================================

def distinct_types(registers):
    """Registers that define a type, one per type rather than one per copy.

    Copies of a pinned group are the same register repeated, so they share a
    type_path -- emitting per register would produce N identical typedefs.
    """
    seen, out = set(), []
    for reg in registers:
        key = reg.get("type_path", reg["path"])
        if key in seen:
            continue
        seen.add(key)
        out.append(reg)
    return out


@dataclass
class RegisterMap:
    registers:    list          # flat, in table order; wire id is reg["num"]
    types:        list          # subset of registers, deduplicated by type_path
    store_bytes:  int           # total size of the register store blob
    global_count: int           # how many of the registers come from the global map
    auto_count:   int           # of those, how many are auto-numbered (block 1)
    blocks:       list          # pinned regions: {base, count, slot, stride}
    board_base:   int           # first wire number available to a board
    dtypes:       list          # dtype vocab, in schema order; number == index + 1
    perms:        list          # permission vocab
    protocol:     dict          # protocol constants: errors, permissions, cmd_result
    converters:   list          # every raw<->eng conversion; see collect_converters
    crc:          int


#  Wire numbers below this belong to the protocol's global map; a board's own
#  registers start here. See README, "Register Map & Numbering".
BOARD_BASE = 8192

#  A pinned group may not repeat past this.
PINNED_MAX_COPIES = 128


def apply_overrides(source, board):
    """Let a board adjust a pinned group's copy count in the global map.

    Only count, only on groups that pin a base. Anything else would change
    the global map's shape, which every peer's numbering depends on.
    """
    over = (board or {}).get("overrides")
    if not over:
        return

    by_path = {}

    def index(node, path=""):
        path = node["name"] if not path else f"{path}.{node['name']}"
        by_path[path] = node
        for child in node.get("children", ()) or ():
            index(child, path)

    for top in source["children"]:
        index(top)

    errs = []
    for path, changes in over.items():
        target = by_path.get(path)
        if target is None:
            errs.append(f"override '{path}': no such register")
        elif "children" not in target:
            errs.append(f"override '{path}': not a group")
        elif "base" not in target:
            errs.append(f"override '{path}': group does not pin a base, so its "
                        f"count cannot vary without renumbering the map")
        elif changes["count"] > PINNED_MAX_COPIES:
            errs.append(f"override '{path}': count {changes['count']} exceeds "
                        f"{PINNED_MAX_COPIES}")
        else:
            target["count"] = changes["count"]

    report(errs)


def assign_numbers(registers, blocks):
    """Wire ids per block. List position stays the table index; the number is
    a separate field from here on.

    auto     numbered 0.. in walk order
    pinned   base + copy*stride, stride being the group's own register count,
             so a differing copy count moves nothing outside the block
    board    from BOARD_BASE
    """
    for reg in registers:
        reg["num"] = None

    n = 0
    for reg in registers:
        if reg["_block"] == "auto":
            reg["num"] = n
            n += 1

    for b in blocks:
        for copy in range(b["count"]):
            for offset in range(b["stride"]):
                registers[b["slot"] + copy*b["stride"] + offset]["num"] = \
                    b["base"] + copy*b["stride"] + offset

    n = BOARD_BASE
    for reg in registers:
        if reg["_block"] == "board":
            reg["num"] = n
            n += 1


def check_collisions(registers, global_count):
    """A board may not reuse a path the global map already defines."""
    reserved = {r["path"] for r in registers[:global_count]}
    clashes  = sorted({r["path"] for r in registers[global_count:]
                       if r["path"] in reserved})
    if clashes:
        print("BOARD MAP CONFLICTS WITH THE GLOBAL MAP:", file=sys.stderr)
        for p in clashes:
            print(f"  {p}: already defined by the protocol", file=sys.stderr)
        raise SystemExit(1)


def build_ir(source, schema, protocol, board=None):
    primitive = schema["definitions"]["primitive"]["properties"]
    group     = schema["definitions"]["group"]["properties"]

    validate_structural(schema, source)
    if board is not None:
        validate_structural(schema, board)
    apply_overrides(source, board)

    #  Pinned groups are walked after the auto-numbered ones so table slots run
    #  in block order -- auto, then pinned, then board -- which is what lets a
    #  wire number map to a slot with arithmetic instead of a search.
    registers = []
    cursor    = 0
    blocks    = []

    for top in source["children"]:
        if "base" not in top:
            cursor = walk_map(top, "", source["permissions"], registers, cursor)
    auto_count = len(registers)

    for top in source["children"]:
        if "base" not in top:
            continue
        slot   = len(registers)
        copies = top.get("count", 1)
        for i in range(copies):
            first = len(registers)
            copy = dict(top)
            copy["name"] = f"{top['name']}_{i}"
            cursor = walk_map(copy, "", source["permissions"], registers, cursor)
            for reg in registers[first:]:
                #  Every copy is the same register repeated, so types and names
                #  are generated once, from copy 0, under the unsuffixed path.
                reg["copy"] = i
                reg["type_path"] = top["name"] + reg["path"][len(copy["name"]):]
        blocks.append({
            "name":   top["name"],
            "base":   top["base"],
            "count":  copies,
            "slot":   slot,
            "stride": (len(registers) - slot) // copies,
        })

    global_count = len(registers)

    if board is not None:
        for top in board["children"]:
            cursor = walk_map(top, "", board["permissions"], registers, cursor)
        check_collisions(registers, global_count)

    for i, reg in enumerate(registers):
        reg["_block"] = ("board" if i >= global_count
                         else "pinned" if i >= auto_count
                         else "auto")

    assign_numbers(registers, blocks)

    shared = protocol.get("enums", {})
    value_map = { r["path"].split(".")[-1]: r["values"]
                  for r in registers
                  if r.get("dtype") == "enum" and isinstance(r.get("values"), dict) }
    clash = sorted(set(shared) & set(value_map))
    if clash:
        report([f"'{n}': declared inline by a register and in protocol.yaml; "
                f"reference it by name instead" for n in clash])
    value_map.update(shared)

    for reg in registers:
        if isinstance(reg.get("values"), str) and reg["values"] in shared:
            reg["shared_enum"] = reg["values"]

    # fill omitted attributes from the schema's declared defaults, then semantic-validate
    for reg in registers:
        apply_schema_defaults(reg, primitive, group)

    validate_map(registers)

    crc = compute_crc(registers, hashed_keys(primitive, group))

    derive(registers, value_map, perm_bits(protocol["permissions"]))
    validate_derived(registers)

    return RegisterMap(
        registers    = registers,
        types        = distinct_types(registers),
        store_bytes  = cursor,
        global_count = global_count,
        auto_count   = auto_count,
        blocks       = blocks,
        board_base   = BOARD_BASE,
        dtypes       = primitive["dtype"]["enum"],
        perms        = group["permissions"]["enum"],
        protocol     = protocol,
        converters   = collect_converters(registers),
        crc          = crc,
    )
