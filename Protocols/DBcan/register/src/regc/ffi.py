"""
Builds the CPython extension that binds the DBcan C library.

cffi API mode: cdef() lists what Python may reach, set_source() is real C
that gets compiled. Because the generated shim #includes the header, this
reaches the `static inline` converters and the `static const reg_table[]`
that a dlsym-based binding (ctypes, cffi ABI mode) cannot see.

Called by compile.py after the header is written, so the extension can never
be stale with respect to the map.
"""

import shutil
import tempfile
from pathlib import Path

from .backends import c as backend_c

#  Declarations only -- no #include, no #ifdef, no macro bodies.
#  "..." means "the C compiler fills in the rest": enum values, struct layout
#  and padding, macro constants. That is what keeps this in step with the
#  header rather than restating it.
BASE_CDEF = """
    typedef enum { DTYPE_NONE, ... } dtype_t;
    typedef enum { REG_R, REG_W, REG_CMD, ... } reg_perm_t;

    typedef struct {
        uint16_t width_bytes;
        uint8_t  dtype;
        uint8_t  perms;
        uint16_t offset;
        bool     persist;
        int32_t  min;
        int32_t  max;
        ...;
    } reg_desc_t;

    static const reg_desc_t reg_table[];

    int reg_index(uint16_t n);

    #define REG_COUNT ...
    #define GLOBAL_COUNT ...
    #define BOARD_BASE ...
    #define REG_WIRE_BYTES ...
    #define REG_MAX_WIDTH ...
    #define DBCAN_REG_MAP_HASH ...
"""

#  --- uncomment once core/src/reg_server.c exists -------------------------
#
#  SERVER_CDEF = """
#      typedef enum { ERR_OK, ERR_NO_REGISTER, ERR_NOT_READABLE,
#                     ERR_NOT_WRITABLE, ERR_NOT_COMMAND, ERR_BAD_LENGTH,
#                     ERR_OUT_OF_RANGE, ... } reg_err_t;
#
#      reg_err_t reg_read(uint16_t n, uint8_t *out, uint16_t *out_len);
#      reg_err_t reg_write(uint16_t n, const uint8_t *val, uint16_t len);
#      reg_err_t reg_command(uint16_t n);
#  """
#
#  and add to set_source():
#      sources      = [".../core/src/reg_server.c"],
#      include_dirs = [str(build_dir), ".../core/inc"],
#  -------------------------------------------------------------------------


def converter_cdef(ir):
    """Prototypes for the generated converters.

    Their names come from register paths, so this cannot be hand-written --
    it changes with the map. Spelled by backend_c so the declarations match
    the definitions in the header exactly.
    """
    lines = []
    for c in ir.converters:
        raw_t = backend_c.c_field_type({"dtype": c["raw_dtype"], "storage_bits": c["raw_bits"]})
        eng_t = backend_c.C_ENG_TYPE[c["eng_type"]]
        lines.append(f"    {eng_t} {c['name']}_to_{c['units']}({raw_t} raw);")
        lines.append(f"    {raw_t} {c['name']}_from_{c['units']}({eng_t} eng);")
    return "\n".join(lines)


def build(ir, out_dir, verbose=False):
    """Compile _dbcan against the header just written to out_dir.

    Only the finished extension lands in out_dir; the generated C and the
    object file are intermediates and are built in a temporary directory.
    """
    try:
        from cffi import FFI
    except ImportError:
        print("regc: cffi not installed, skipping the extension "
              "(pip install cffi)")
        return None

    ffibuilder = FFI()
    ffibuilder.cdef(BASE_CDEF + "\n" + converter_cdef(ir))
    ffibuilder.set_source(
        "_dbcan",
        '#include "dbcan_reg_map.h"',
        sources=[str(Path(out_dir) / "dbcan_reg_map.c")],
        include_dirs=[str(out_dir)],
        #  reg_store is static in the header and unused by this shim -- that
        #  is expected here, and the firmware build still warns about it
        extra_compile_args=["-Wno-unused-variable"],
    )

    with tempfile.TemporaryDirectory() as tmp:
        built = Path(ffibuilder.compile(tmpdir=tmp, verbose=verbose))
        final = Path(out_dir) / built.name
        shutil.copy2(built, final)

    return final
