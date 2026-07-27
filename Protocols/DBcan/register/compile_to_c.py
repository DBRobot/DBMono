import sys
import yaml
import json
import zlib
import struct
import jsonschema
import sys


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


# ============================================================================
#  CODE GENERATION
#  One emit_* per generated block. Each takes the open output file and writes
#  its slice of the C header. Dispatched by template marker (see MARKERS).
#  These read module-level globals (registers, dtypes, ...) populated by the
#  driver section below; they only run during the template render.
# ============================================================================

PERM_C = {
    "R":    "REG_R",
    "W":    "REG_W",
    "RW":   "REG_R | REG_W",
    "CMD":  "REG_CMD",
    "ANY":  "0"
}

CMD_ERRORS = [                                                                                                                                                  
    {"name": "success", "code": 0},                                                                                                                             
    {"name": "failed",  "code": 1},                                                                                                                             
] 

def round_up_bits(bits):
    for w in (8, 16, 32, 64):
        if bits <= w:
            return w
    raise SystemExit(f"struct field width {bits} exceeds 64 bits")

def c_field_type(field):
    dt = field["dtype"]
    if dt == "bool":            return "bool"
    if dt == "float":           return "float"
    w = round_up_bits(field["bits"])
    if dt == "uint":            return f"uint{w}_t"
    if dt == "int":             return f"int{w}_t"
    if dt == "enum":            return f"uint{w}_t"   
    raise SystemExit(f"unsupported struct field dtype: {dt}")

def apply_schema_defaults(reg):
    props = group if "children" in reg else primitive
    for key, spec in props.items():
        if "default" in spec and key not in reg:
            reg[key] = spec["default"]

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

def emit_dtype_enum(out):
    for num, name in enumerate(dtypes):
        out.write(f"\tDTYPE_{name.upper()} = {num + 1},\n")

def emit_value_dtype(out):
    for num, name in enumerate(field_dtypes):
        out.write(f"\tFIELD_DTYPE_{name.upper()} = {num + 1},\n")

def emit_reg_enum(out):
    for num, reg in enumerate(registers):
        path = reg["path"].upper().replace(".", "_")
        out.write(f"\tREG_{path} = {num},\n")

def emit_reg_table(out):
    for num, reg in enumerate(registers):
        path = reg["path"].upper().replace(".", "_")
        width = reg["width"]
        d = reg.get("dtype")
        dtype = f"DTYPE_{d.upper()}" if d else "DTYPE_NONE"
        perm_c = PERM_C[reg["perm"]]
        offset = reg["byte_offset"]
        lo, hi = raw_bounds(reg)

        out.write(
            f"\t[REG_{path}] = {{ .width_bytes = {width}, .dtype = {dtype}, .perms = {perm_c}, "
            f".offset = {offset}, .persist = {str(reg.get("persist", False)).lower()}, "
            f".min = {lo}, .max = {hi} }},\n"
            )

def emit_register_enums(out):
    for reg in registers:
        if reg.get("dtype") != "enum":
            continue
        vals = reg["values"]
        if isinstance(vals, str):                         
            vals = value_map.get(vals, FAULT_VALUES)

        out.write("typedef enum {\n")
        for key, value in vals.items():
            out.write(f"\t{reg["name"].upper()}_{key.upper()} = {value},\n")
        out.write("} " + f"{reg.get("path").replace(".", "_")}_t;\n\n")

def emit_crc(out):
    parts = []
    for num, reg in enumerate(registers):
        defn = {k: v for k, v in reg.items() if k not in ("byte_offset", "width", "children")}
        defn["num"] = num
        parts.append(json.dumps(defn, sort_keys=True, default=str))
    crc = zlib.crc32("\n".join(parts).encode())
    out.write(f"#define DBCAN_REG_MAP_HASH 0x{crc:08X}\n")

def emit_reg_store(out):
    inits = []
    for reg in registers:
        dt = reg.get("dtype")
        if "children" in reg or dt in (None, "struct"):   # groups, commands, structs: no scalar default
            continue
        for j, b in enumerate(encode_default(reg)):
            if b:                                         # only non-zero bytes; C zero-fills the rest
                inits.append(f"[{reg['byte_offset'] + j}] = 0x{b:02X}")
    out.write(f"#define REG_STORE_BYTES {cursor}\n")
    out.write(f"static uint8_t reg_store[REG_STORE_BYTES] = {{ {', '.join(inits)} }};\n")

def emit_reg_count(out):
    out.write(f"#define REG_COUNT {len(registers)}\n")

def emit_register_structs(out):
    for reg in registers:
        if reg.get("dtype") != "struct":
            continue
        out.write("typedef struct {\n")
        for field in reg["values"]:
            out.write(f"\t{c_field_type(field)} {field['name']};\n")
        out.write("} " + reg["path"].replace(".", "_") + "_t;\n\n")

def emit_error_enums(out):
    for reg in registers:
        if reg.get("perm") == "CMD" and "children" not in reg:
            errs = [(e["name"], e["code"]) for e in CMD_ERRORS]
        else:
            arr = reg.get("errors")
            if not arr:
                continue
            errs = [(e["name"], 64 + i) for i, e in enumerate(arr)]

        out.write("typedef enum {\n")
        for name, code in errs:
            out.write(f"\t{reg['name'].upper()}_ERROR_{name.upper()} = {code},\n")
        out.write("} " + reg["path"].replace(".", "_") + "_error_t;\n\n")

def emit_converter(out, name, raw_t, res, off, units):
    if res == 1 and off == 0:
        return
    if float(res).is_integer() and float(off).is_integer():
        eng_t = "int32_t"
        res_l, off_l = int(res), int(off)
    else:
        eng_t = "float"
        res_l, off_l = f"{float(res)}f", f"{float(off)}f"

    out.write(f"static inline {eng_t} {name}_to_{units}({raw_t} raw) {{\n")
    out.write(f"\treturn ({eng_t})(raw * {res_l} + {off_l});\n")
    out.write("}\n\n")

    out.write(f"static inline {raw_t} {name}_from_{units}({eng_t} eng) {{\n")
    out.write(f"\treturn ({raw_t})((eng - {off_l}) / {res_l});\n")
    out.write("}\n\n")

def scaled(item):
    if item.get("dtype") not in ("uint", "int"):
        return False
    if item.get("resolution", 1) == 1 and item.get("offset", 0) == 0:
        return False
    return item["bits"] <= 64

def emit_conversion_funcs(out):
    for reg in registers:
        dt = reg.get("dtype")
        if dt in ("uint", "int") and scaled(reg):       
            emit_converter(out, reg["path"].replace(".", "_"), c_field_type(reg),
                           reg["resolution"], reg.get("offset", 0), reg.get("units", "eng"))
        elif dt == "struct":                               
            base = reg["path"].replace(".", "_")
            for field in reg["values"]:
                if scaled(field):
                    emit_converter(out, f"{base}_{field['name']}", c_field_type(field),
                                   field.get("resolution", 1), field.get("offset", 0),
                                   field.get("units", "eng"))



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
    "/* @@VALUE_DTYPE_ENUM*/":          emit_value_dtype,
    "/* @@REGISTER_ENUM_TYPES@@ */":    emit_register_enums,
    "/* @@REGISTER_STRUCT_TYPES@@ */":  emit_register_structs,
    "/* @@ERROR_ENUM_TYPES@@ */":       emit_error_enums,
    "/* @@CONVERSION_FUNCTIONS@@ */":   emit_conversion_funcs,
}


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
#  DRIVER
#  load -> structural validate -> walk -> apply defaults -> semantic validate
#  -> render the template into the C header.
# ============================================================================

global_reg_map_file = open("global_reg.yaml")
global_reg_map = yaml.safe_load(global_reg_map_file)

schema      = json.load(open("register.json"))
primitive   = schema["definitions"]["primitive"]["properties"]
group       = schema["definitions"]["group"]["properties"]

attributes  = list(primitive) + [k for k in group if k not in primitive]

dtypes      = primitive["dtype"]["enum"]                 
perms       = group["permissions"]["enum"]               

_values_forms = primitive["values"]["anyOf"]
_field_form   = next(f for f in _values_forms if f.get("type") == "array")
field_dtypes  = _field_form["items"]["properties"]["dtype"]["enum"]  

# structural validation, before we walk a possibly-malformed tree
validate_structural(schema, global_reg_map)

registers = []
cursor = 0
for top in global_reg_map["children"]:
    cursor = walk_map(top, "", global_reg_map["permissions"], registers, cursor)


value_map = { r["path"].split(".")[-1]: r["values"]
              for r in registers
              if r.get("dtype") == "enum" and isinstance(r.get("values"), dict) }

FAULT_VALUES = {"no_fault": 0}                            


 

# fill omitted attributes from the schema's declared defaults, then semantic-validate
for reg in registers:
    apply_schema_defaults(reg)

validate_map(registers)

with open("templates/dbcan_reg_map.h") as tmpl, open("dbcan_reg_map.h", "w") as out:
    for line in tmpl:
        handler = MARKERS.get(line.strip())
        if handler:
            handler(out)
        else:
            out.write(line)
