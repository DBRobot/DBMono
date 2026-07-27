import yaml
import json
import zlib
import struct


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
    if dt == "enum":            return f"uint{w}_t"   # raw storage; named values live in the enum registry
    raise SystemExit(f"unsupported struct field dtype: {dt}")

def apply_schema_defaults(reg):
    # jsonschema doesn't inject declared defaults, so fill any the author omitted
    props = group if "children" in reg else primitive
    for key, spec in props.items():
        if "default" in spec and key not in reg:
            reg[key] = spec["default"]

def encode_default(reg):
    dt, w, val = reg["dtype"], reg["width"], reg["default"]
    if dt == "bool":
        return bytes([1 if val else 0])
    if dt == "enum":                                          # raw index, no scaling
        return int(val).to_bytes(w, "little")
    if dt in ("uint", "int"):
        raw = round((val - reg.get("offset", 0)) / reg["resolution"])   # engineering -> raw
        return int(raw).to_bytes(w, "little", signed=(dt == "int"))
    if dt == "float":
        return struct.pack("<f" if w == 4 else "<d", float(val))
    if dt == "string":
        return str(val).encode("ascii")[:w].ljust(w, b"\0")
    return bytes(w)

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

        out.write(
            f"\t[REG_{path}] = {{ .width_bytes = {width}, .dtype = {dtype}, .perms = {perm_c}, "
            f".offset = {offset}, .persist = {str(reg.get("persist", False)).lower()} }},\n"
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
    s = ""
    for num, reg in enumerate(registers):
        errs = ",".join(e["name"] for e in (reg.get("errors") or []))
        s += f"{num}:{reg['path']}:{reg.get('dtype')}:{width_of(reg)}:{reg['perm']}:{errs};"
    crc = zlib.crc32(s.encode())
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
        if dt in ("uint", "int") and scaled(reg):            # top-level scaled register
            emit_converter(out, reg["path"].replace(".", "_"), c_field_type(reg),
                           reg["resolution"], reg.get("offset", 0), reg.get("units", "eng"))
        elif dt == "struct":                                 # each scaled field of a struct
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
#  DRIVER
#  Load the map, build the IR, run semantic checks, load the schema
#  vocabularies, then render the template into the C header.
# ============================================================================


global_reg_map_file = open("global_reg.yaml")
global_reg_map = yaml.safe_load(global_reg_map_file)

registers = []
cursor = 0
for top in global_reg_map["children"]:
    cursor = walk_map(top, "", global_reg_map["permissions"], registers, cursor)


enum_names = [r["name"] for r in registers if r.get("dtype") == "enum"]
dupes = sorted({n for n in enum_names if enum_names.count(n) > 1})
if dupes:
    raise SystemExit(f"enum register names must be unique across the map; duplicates: {dupes}")


value_map = { r["path"].split(".")[-1]: r["values"]
              for r in registers
              if r.get("dtype") == "enum" and isinstance(r.get("values"), dict) }

FAULT_VALUES = {"no_fault": 0}                            


schema      = json.load(open("register.json"))
primitive   = schema["definitions"]["primitive"]["properties"]
group       = schema["definitions"]["group"]["properties"]

attributes  = list(primitive) + [k for k in group if k not in primitive]

dtypes      = primitive["dtype"]["enum"]                 
perms       = group["permissions"]["enum"]               

_values_forms = primitive["values"]["anyOf"]
_field_form   = next(f for f in _values_forms if f.get("type") == "array")
field_dtypes  = _field_form["items"]["properties"]["dtype"]["enum"]   # struct-field dtypes

# fill in every attribute the author omitted from the schema's declared defaults
for reg in registers:
    apply_schema_defaults(reg)

with open("templates/dbcan_reg_map.h") as tmpl, open("dbcan_reg_map.h", "w") as out:
    for line in tmpl:
        handler = MARKERS.get(line.strip())
        if handler:
            handler(out)
        else:
            out.write(line)
