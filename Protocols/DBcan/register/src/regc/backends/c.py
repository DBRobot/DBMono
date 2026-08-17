"""
C backend.

Every C-specific decision lives here: type spellings, permission tokens,
identifier style, and the marker set filled into templates/dbcan_reg_map.h.
Each handler takes the RegisterMap and returns the text for its marker.
"""

#  (template, output filename) pairs. reg_table and reg_store are defined in
#  the .c so there is exactly one copy of each; the header only declares them.
OUTPUTS = [
    ("c_protocol_template", "dbcan_protocol.h"),
    ("c_header_template",   "dbcan_reg_map.h"),
    ("c_source_template",   "dbcan_reg_map.c"),
]

def perm_c(bits, permissions):
    """reg_perm_t expression for a register's effective permission bits."""
    names = [f"REG_{n.upper()}" for n, bit in permissions.items() if bits & bit]
    return " | ".join(names) or "0"


def c_ident(path):
    return path.replace(".", "_")


def type_ident(reg):
    """Type name for a register. Copies of a pinned group share one type,
    named without the copy suffix."""
    return c_ident(reg.get("type_path", reg["path"]))


def c_field_type(field):
    dt = field["dtype"]
    if dt == "bool":            return "bool"
    if dt == "float":           return "float"
    w = field["storage_bits"]
    if dt == "uint":            return f"uint{w}_t"
    if dt == "int":             return f"int{w}_t"
    if dt == "enum":            return f"uint{w}_t"
    raise SystemExit(f"unsupported struct field dtype: {dt}")


# ============================================================================
#  THE STORE
#  Registers are fields of a generated struct rather than slices of a byte
#  array, so the compiler owns alignment and internal code can touch a register
#  by name. Descriptor offsets come from offsetof, so the two can never drift.
#  A group's descriptor width stays the packed sum of its leaves -- that is the
#  wire length, which is not sizeof the struct once padding exists.
# ============================================================================

def store_ident(reg):
    """Struct type for a group. Prefixed because unqualified group names
    collide with system types -- `id` alone would clash with POSIX id_t."""
    return f"reg_{type_ident(reg)}_t"


def member_path(reg):
    """Designator reaching this register inside reg_store_t. Copies of a pinned
    group live in an array, so can_0.settings.mode is can[0].settings.mode."""
    parts = reg["path"].split(".")
    if "type_path" in reg:
        head = reg["type_path"].split(".")[0]
        return ".".join([f"{head}[{reg['copy']}]"] + parts[1:])
    return reg["path"]


def leaf_decl(reg):
    dt   = reg.get("dtype")
    n    = reg.get("array_size") or 1
    name = reg["name"]

    if dt == "string":
        return f"char {name}[{reg['width']}];"
    if dt == "struct":
        base = f"{type_ident(reg)}_t"
    elif dt == "bool":
        base = "bool"
    elif dt == "float":
        base = "float" if reg["width"] // n == 4 else "double"
    else:
        bits = reg.get("storage_bits")
        if bits is None:                    # wider than any scalar type, e.g. a 96-bit uid
            return f"uint8_t {name}[{reg['width']}];"
        base = {"uint": f"uint{bits}_t", "int": f"int{bits}_t",
                "enum": f"uint{bits}_t"}[dt]

    return f"{base} {name}[{n}];" if n > 1 else f"{base} {name};"


def store_tree(ir):
    """(top-level registers, path -> direct children), in walk order."""
    roots, kids = [], {}
    for reg in ir.registers:
        if "." in reg["path"]:
            kids.setdefault(reg["path"].rsplit(".", 1)[0], []).append(reg)
        else:
            roots.append(reg)
    return roots, kids


def emit_store_struct(ir):
    roots, kids = store_tree(ir)
    counts = {b["name"]: b["name"].upper() + "_COUNT" for b in ir.blocks}
    types, seen = [], set()

    def group(reg):
        if store_ident(reg) in seen:
            return
        seen.add(store_ident(reg))
        body = []
        for child in kids.get(reg["path"], []):
            if child["width"] == 0:         # commands own no storage
                continue
            if "children" in child:
                group(child)
                body.append(f"\t{store_ident(child)} {child['name']};\n")
            else:
                body.append(f"\t{leaf_decl(child)}\n")
        types.append("typedef struct {\n" + "".join(body) + "} " + store_ident(reg) + ";\n\n")

    body, placed = [], set()
    for reg in roots:
        if reg["width"] == 0:
            continue
        head = (reg.get("type_path") or reg["path"]).split(".")[0]
        if head in counts:                  # pinned group: one type, an array of copies
            if head in placed:
                continue
            placed.add(head)
            group(reg)
            body.append(f"\t{store_ident(reg)} {head}[{counts[head]}];\n")
        elif "children" in reg:
            group(reg)
            body.append(f"\t{store_ident(reg)} {reg['name']};\n")
        else:
            body.append(f"\t{leaf_decl(reg)}\n")

    return "".join(types) + "typedef struct {\n" + "".join(body) + "} reg_store_t;\n"


def emit_store_asserts(ir):
    """One per leaf: the field must occupy exactly the bytes the descriptor
    promises, or a wire read walks into padding or the next register."""
    out = []
    for reg in ir.types:
        if reg["width"] == 0 or "children" in reg:
            continue
        out.append(f"_Static_assert(sizeof(((reg_store_t *)0)->{member_path(reg)}) == {reg['width']}, "
                   f"\"{reg['path']}: storage must match descriptor width\");\n")
    return "".join(out)


# ============================================================================
#  MARKER HANDLERS
#  One per generated block. Each returns the text that replaces its marker.
# ============================================================================

def emit_perm_flags(ir):
    return "".join(f"\t{f'REG_{name.upper()}':<8}= {bit},\n"
                   for name, bit in ir.protocol["permissions"].items())


def emit_error_codes(ir):
    return "".join(f"\t{f'ERR_{name.upper()}':<20}= {code},\n"
                   for name, code in ir.protocol["errors"].items())


def emit_cmd_result(ir):
    return "".join(f"\t{f'CMD_{name.upper()}':<12}= {code},\n"
                   for name, code in ir.protocol["cmd_result"].items())


def emit_dtype_enum(ir):
    return "".join(f"\tDTYPE_{name.upper()} = {num + 1},\n"
                   for num, name in enumerate(ir.dtypes))


def emit_reg_enum(ir):
    return "".join(f"\tREG_{c_ident(reg['path']).upper()} = {reg['num']},\n"
                   for reg in ir.registers)


def emit_reg_table(ir):
    out = []
    for reg in ir.registers:
        path = c_ident(reg["path"]).upper()
        d = reg.get("dtype")
        dtype = f"DTYPE_{d.upper()}" if d else "DTYPE_NONE"
        persist = str(reg.get("persist", False)).lower()

        #  a zero-width register owns no storage, so it has no member to point at
        offset = (f"offsetof(reg_store_t, {member_path(reg)})" if reg["width"] else "0")

        #  positional, not designated: a board register's number is >= BOARD_BASE
        #  and would index far past the end. table order is IR order; use
        #  reg_index() to go from a wire number to a slot.
        out.append(
            f"\t{{ .width_bytes = {reg['width']}, .dtype = {dtype}, "
            f".perms = {perm_c(reg['perm_bits'], ir.protocol['permissions'])}, "
            f".offset = {offset}, .persist = {persist}, "
            f".min = {reg['min_raw']}, .max = {reg['max_raw']} }},"
            f"\t/* REG_{path} */\n"
        )
    return "".join(out)


def emit_protocol_enums(ir):
    """Value sets from protocol.yaml. These land in dbcan_protocol.h so a layer
    that is not the register map -- a transport port -- can use them without
    pulling in the store, the table, or the register numbers."""
    out = []
    for name, values in ir.protocol.get("enums", {}).items():
        out.append("typedef enum {\n")
        for key, value in values.items():
            out.append(f"\t{name.upper()}_{key.upper()} = {value},\n")
        out.append("} " + f"dbcan_{name}_t;\n\n")
    return "".join(out)


def emit_register_enums(ir):
    out = []
    for reg in ir.types:
        if reg.get("dtype") != "enum" or not reg["values_resolved"]:
            continue
        if reg.get("shared_enum"):                    # defined once in dbcan_protocol.h
            continue

        out.append("typedef enum {\n")
        for key, value in reg["values_resolved"].items():
            out.append(f"\t{reg['name'].upper()}_{key.upper()} = {value},\n")
        out.append("} " + f"{type_ident(reg)}_t;\n\n")
    return "".join(out)


def emit_crc(ir):
    return f"#define DBCAN_REG_MAP_HASH 0x{ir.crc:08X}\n"


def c_literal(reg, value):
    dt = reg["dtype"]
    if dt == "string":
        escaped = value.replace("\\", "\\\\").replace('"', '\\"')
        return f'"{escaped}"'
    if dt == "bool":
        return "true"
    if dt == "float":
        return f"{value}f" if reg["width"] == 4 else f"{value}"
    return str(value)


def emit_reg_store_init(ir):
    """Designated initializers, one per register with a non-zero default. C
    zero-fills everything else, padding included, so the image is deterministic."""
    inits = []
    for reg in ir.registers:
        value = reg.get("default_raw")
        if value is None or not value:                    # 0, False and "" are already the zero fill
            continue
        n = reg.get("array_size") or 1
        literal = c_literal(reg, value)
        if n > 1 and reg["dtype"] != "string":
            literal = "{ " + ", ".join([literal] * n) + " }"
        inits.append(f"\t.{member_path(reg)} = {literal},\n")
    return "".join(inits)


def emit_reg_count(ir):
    return (f"#define REG_COUNT {len(ir.registers)}\n"
            f"#define GLOBAL_COUNT {ir.global_count}\n"
            f"#define AUTO_COUNT {ir.auto_count}\n"
            f"#define BOARD_BASE {ir.board_base}\n"
            f"#define PINNED_BLOCKS {len(ir.blocks)}\n"
            f"#define REG_WIRE_BYTES {ir.store_bytes}\n"
            f"#define REG_MAX_WIDTH {max(r['width'] for r in ir.registers)}\n")


def emit_pinned_constants(ir):
    """Per pinned group: how many copies and how far apart, so a caller can
    reach copy i as REG_<GROUP>_0_<FIELD> + i*<GROUP>_STRIDE."""
    out = []
    for b in ir.blocks:
        g = b["name"].upper()
        out.append(f"#define {g}_COUNT {b['count']}\n")
        out.append(f"#define {g}_STRIDE {b['stride']}\n")
    return "".join(out)


def emit_pinned_blocks(ir):
    """One row per pinned region, for reg_index to walk."""
    if not ir.blocks:
        return "\t{ 0, 0, 0, 0 },\t/* none */\n"
    return "".join(
        f"\t{{ {b['base']}, {b['count']}, {b['stride']}, {b['slot']} }},\n"
        for b in ir.blocks)


def emit_register_structs(ir):
    out = []
    for reg in ir.types:
        if reg.get("dtype") != "struct":
            continue
        name     = type_ident(reg)
        per_elem = reg["width"] // (reg.get("array_size") or 1)

        # packed: the descriptor's width is the sum of the declared field bits,
        # so the compiler must not insert padding or sizeof drifts from it
        out.append("typedef struct __attribute__((packed)) {\n")
        for field in reg["values"]:
            out.append(f"\t{c_field_type(field)} {field['name']};\n")
        out.append("} " + name + "_t;\n")
        out.append(f"_Static_assert(sizeof({name}_t) == {per_elem}, "
                   f"\"{reg['path']}: struct layout must match descriptor width\");\n\n")
    return "".join(out)


def emit_error_enums(ir):
    out = []
    for reg in ir.types:
        errs = reg.get("errors_resolved")
        if errs is None:
            continue
        out.append("typedef enum {\n")
        for name, code in errs:
            out.append(f"\t{reg['name'].upper()}_ERROR_{name.upper()} = {code},\n")
        out.append("} " + type_ident(reg) + "_error_t;\n\n")
    return "".join(out)


C_ENG_TYPE = {"int": "int32_t", "float": "float"}


def converter(conv):
    name, units = conv["name"], conv["units"]
    raw_t = c_field_type({"dtype": conv["raw_dtype"], "storage_bits": conv["raw_bits"]})
    eng_t = C_ENG_TYPE[conv["eng_type"]]
    res, off = conv["resolution"], conv["offset"]

    if conv["eng_type"] == "int":
        res_l, off_l = int(res), int(off)
    else:
        res_l, off_l = f"{float(res)}f", f"{float(off)}f"

    return (f"static inline {eng_t} {name}_to_{units}({raw_t} raw) {{\n"
            f"\treturn ({eng_t})(raw * {res_l} + {off_l});\n"
            "}\n\n"
            f"static inline {raw_t} {name}_from_{units}({eng_t} eng) {{\n"
            f"\treturn ({raw_t})((eng - {off_l}) / {res_l});\n"
            "}\n\n")


def commands(ir):
    """Leaf registers that are commands. A group of commands is not itself one."""
    return [(slot, reg) for slot, reg in enumerate(ir.registers)
            if "children" not in reg and reg["perm_bits"] & 4]


def emit_cmd_prototypes(ir):
    return "".join(f"reg_err_t cmd_{c_ident(reg['path'])}(void);\n"
                   for _, reg in commands(ir))


def emit_cmd_defaults(ir):
    """A weak body for every command, so an unimplemented one answers honestly.

    An ordinary definition of the same name anywhere in the link overrides it,
    which is how the library claims the protocol commands and a board claims
    its own -- neither edits generated code.
    """
    out = []
    for _, reg in commands(ir):
        out.append(f"__attribute__((weak)) reg_err_t cmd_{c_ident(reg['path'])}(void)\n"
                   f"{{\n\treturn ERR_NOT_IMPLEMENTED;\n}}\n\n")
    return "".join(out)


def emit_cmd_table(ir):
    #  indexed by table slot, not wire number: reg_index() already mapped it,
    #  and a slot is always < REG_COUNT even when the number is above BOARD_BASE
    return "".join(f"\t[{slot}] = cmd_{c_ident(reg['path'])},\n"
                   for slot, reg in commands(ir))


def emit_conversion_funcs(ir):
    return "".join(converter(c) for c in ir.converters)


# ============================================================================
#  MARKER DISPATCH
#  Maps each template placeholder to the handler that fills it.
# ============================================================================

MARKERS = {
    "/* @@REGISTER_ENUM@@ */":          emit_reg_enum,
    "/* @@REGISTER_TABLE@@ */":         emit_reg_table,
    "/* @@DTYPE_ENUM@@ */":             emit_dtype_enum,
    "/* @@PERM_FLAGS@@ */":             emit_perm_flags,
    "/* @@ERROR_CODES@@ */":            emit_error_codes,
    "/* @@CMD_RESULT@@ */":             emit_cmd_result,
    "/* @@CRC_DEFINE@@*/":              emit_crc,
    "/* @@REG_STORE_INIT@@ */":         emit_reg_store_init,
    "/* @@REG_COUNT@@ */":              emit_reg_count,
    "/* @@PINNED_BLOCKS@@ */":          emit_pinned_blocks,
    "/* @@PINNED_CONSTANTS@@ */":       emit_pinned_constants,
    "/* @@PROTOCOL_ENUM_TYPES@@ */":    emit_protocol_enums,
    "/* @@REGISTER_ENUM_TYPES@@ */":    emit_register_enums,
    "/* @@REGISTER_STRUCT_TYPES@@ */":  emit_register_structs,
    "/* @@REG_STORE_STRUCT@@ */":       emit_store_struct,
    "/* @@REG_STORE_ASSERTS@@ */":      emit_store_asserts,
    "/* @@ERROR_ENUM_TYPES@@ */":       emit_error_enums,
    "/* @@CONVERSION_FUNCTIONS@@ */":   emit_conversion_funcs,
    "/* @@CMD_PROTOTYPES@@ */":         emit_cmd_prototypes,
    "/* @@CMD_DEFAULTS@@ */":           emit_cmd_defaults,
    "/* @@CMD_TABLE@@ */":              emit_cmd_table,
}
