"""
Python backend.

Emits the names C cannot hand across an FFI boundary: register IDs, enum
value names, error names, struct field names. Behaviour and tables stay in
the C library -- nothing here decodes, converts, or mirrors reg_table.
"""

OUTPUTS = [
    ("py_template", "dbcan_reg_map.py"),
]

#  struct field dtype -> Python type. `enum` is int because struct fields
#  reference a shared value set rather than declaring their own, exactly as
#  c_field_type spells them uint{w}_t.
PY_FIELD_TYPE = {
    "bool":  "bool",
    "uint":  "int",
    "int":   "int",
    "float": "float",
    "enum":  "int",
}

#  engineering-value type of a converter, as Python spells it
PY_ENG_TYPE = {"int": "int", "float": "float"}


def py_ident(path):
    return path.replace(".", "_")


def py_class(path):
    return "".join(part.title() for part in py_ident(path).split("_"))


# ============================================================================
#  MARKER HANDLERS
#  One per generated block. Each returns the text that replaces its marker.
# ============================================================================

def emit_reg_enum(ir):
    return "".join(f"\t{py_ident(reg["path"].upper())} = {reg["num"]}\n"
                   for reg in ir.registers)

def emit_dtype_enum(ir):
    return "".join(f"\t{name.upper()} = {num + 1}\n"
                   for num, name in enumerate(ir.dtypes))

def emit_reg_val_enums(ir):
    out = []
    for reg in ir.registers:
        if reg.get("dtype") != "enum" or not reg["values_resolved"]:
            continue

        out.append(f"class {py_class(reg["path"])}(IntEnum):\n")
        for key, value in reg["values_resolved"].items():
            out.append(f"\t{key.upper()} = {value}\n")
        out.append("\n")
    return "".join(out)

def emit_reg_error_enums(ir):
    out = []
    for reg in ir.registers:
        errs = reg.get("errors_resolved")
        if errs is None:
            continue

        out.append(f"class {py_class(reg["path"])}Error(IntEnum):\n")
        for name, code in errs:
            out.append(f"\t{name.upper()} = {code}\n")
        out.append("\n")
    return "".join(out)

def emit_reg_structs(ir):
    out = []
    for reg in ir.registers:
        if reg.get("dtype") != "struct":
            continue

        out.append(f"class {py_class(reg["path"])}(NamedTuple):\n")
        for field in reg["values"]:
            out.append(f"\t{field["name"]}: {PY_FIELD_TYPE[field["dtype"]]}\n")
        out.append("\n")
    return "".join(out)


def emit_converters(ir):
    """Typed wrappers around the C converters.

    The arithmetic stays in C -- these exist so the call site is checkable.
    lib.* is Any to a type checker; these are not.
    """
    out = []
    for c in ir.converters:
        eng = PY_ENG_TYPE[c["eng_type"]]
        to_, from_ = f"{c["name"]}_to_{c["units"]}", f"{c["name"]}_from_{c["units"]}"

        out.append(f"def {to_}(raw: int) -> {eng}:\n"
                   f"\treturn lib.{to_}(raw)\n\n")
        out.append(f"def {from_}(eng: {eng}) -> int:\n"
                   f"\treturn lib.{from_}(eng)\n\n")
    return "".join(out)


# ============================================================================
#  MARKER DISPATCH
#  Maps each template placeholder to the handler that fills it.
# ============================================================================

MARKERS = {
    "# @@REGISTER_ENUM@@":          emit_reg_enum,
    "# @@DTYPE_ENUM@@":             emit_dtype_enum,
    "# @@REG_VALUE_ENUMS@@":        emit_reg_val_enums,
    "# @@REG_ERROR_ENUMS@@":        emit_reg_error_enums,
    "# @@REG_STRUCTS@@":            emit_reg_structs,
    "# @@CONVERTERS@@":             emit_converters,
}
