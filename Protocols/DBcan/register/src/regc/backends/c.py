"""
C backend.

Every C-specific decision lives here: type spellings, permission tokens,
identifier style, and the marker set filled into templates/dbcan_reg_map.h.
Each handler takes the RegisterMap and returns the text for its marker.
"""

TEMPLATE = "c_template"
OUTPUT   = "dbcan_reg_map.h"

PERM_C = {
    "R":    "REG_R",
    "W":    "REG_W",
    "RW":   "REG_R | REG_W",
    "CMD":  "REG_CMD",
    "ANY":  "0"
}


def c_ident(path):
    return path.replace(".", "_")


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
#  MARKER HANDLERS
#  One per generated block. Each returns the text that replaces its marker.
# ============================================================================

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

        #  positional, not designated: a board register's number is >= BOARD_BASE
        #  and would index far past the end. table order is IR order; use
        #  reg_index() to go from a wire number to a slot.
        out.append(
            f"\t{{ .width_bytes = {reg['width']}, .dtype = {dtype}, "
            f".perms = {PERM_C[reg['perm']]}, "
            f".offset = {reg['byte_offset']}, .persist = {persist}, "
            f".min = {reg['min_raw']}, .max = {reg['max_raw']} }},"
            f"\t/* REG_{path} */\n"
        )
    return "".join(out)


def emit_register_enums(ir):
    out = []
    for reg in ir.registers:
        if reg.get("dtype") != "enum" or not reg["values_resolved"]:
            continue

        out.append("typedef enum {\n")
        for key, value in reg["values_resolved"].items():
            out.append(f"\t{reg['name'].upper()}_{key.upper()} = {value},\n")
        out.append("} " + f"{c_ident(reg['path'])}_t;\n\n")
    return "".join(out)


def emit_crc(ir):
    return f"#define DBCAN_REG_MAP_HASH 0x{ir.crc:08X}\n"


def emit_reg_store(ir):
    inits = []
    for reg in ir.registers:
        if "default_bytes" not in reg:                    # groups, commands, structs: no scalar default
            continue
        for j, b in enumerate(reg["default_bytes"]):
            if b:                                         # only non-zero bytes; C zero-fills the rest
                inits.append(f"[{reg['byte_offset'] + j}] = 0x{b:02X}")
    return (f"#define REG_STORE_BYTES {ir.store_bytes}\n"
            f"static uint8_t reg_store[REG_STORE_BYTES] = {{ {', '.join(inits)} }};\n")


def emit_reg_count(ir):
    return (f"#define REG_COUNT {len(ir.registers)}\n"
            f"#define GLOBAL_COUNT {ir.global_count}\n"
            f"#define BOARD_BASE {ir.board_base}\n")


def emit_register_structs(ir):
    out = []
    for reg in ir.registers:
        if reg.get("dtype") != "struct":
            continue
        name     = c_ident(reg["path"])
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
    for reg in ir.registers:
        errs = reg.get("errors_resolved")
        if errs is None:
            continue
        out.append("typedef enum {\n")
        for name, code in errs:
            out.append(f"\t{reg['name'].upper()}_ERROR_{name.upper()} = {code},\n")
        out.append("} " + c_ident(reg["path"]) + "_error_t;\n\n")
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
    "/* @@CRC_DEFINE@@*/":              emit_crc,
    "/* @@REG_STORE_DECLARE@@ */":      emit_reg_store,
    "/* @@REG_COUNT@@ */":              emit_reg_count,
    "/* @@REGISTER_ENUM_TYPES@@ */":    emit_register_enums,
    "/* @@REGISTER_STRUCT_TYPES@@ */":  emit_register_structs,
    "/* @@ERROR_ENUM_TYPES@@ */":       emit_error_enums,
    "/* @@CONVERSION_FUNCTIONS@@ */":   emit_conversion_funcs,
}
