#!/bin/bash

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"

# KiCad template symlink
ln -sf "$REPO_ROOT/PCB_Hardware/templates/KiCAD_template" ~/.local/share/kicad/10.0/template/KiCAD_template
echo "KiCad template linked"

# Configure DBMONO_LIBS env var in every installed KiCad version's config so that
# fp-lib-table / sym-lib-table / 3D model paths using ${DBMONO_LIBS} resolve correctly.
DBMONO_LIBS="$REPO_ROOT/PCB_Hardware/libs"
for KICAD_CFG in "$HOME"/.config/kicad/*/kicad_common.json; do
  [ -f "$KICAD_CFG" ] || continue
  python3 - "$KICAD_CFG" "$DBMONO_LIBS" <<'PY'
import json, sys
path, libs = sys.argv[1], sys.argv[2]
with open(path) as f: d = json.load(f)
env = d.setdefault('environment', {})
vars = env.get('vars') or {}
vars['DBMONO_LIBS'] = libs
env['vars'] = vars
with open(path, 'w') as f: json.dump(d, f, indent=2)
print(f"  set DBMONO_LIBS={libs} in {path}")
PY
done

# VS Code C block comment extension (/* + Enter auto-continues with *)
EXT_DIR="$HOME/.vscode/extensions/local.c-block-comment-rules-1.0.0"
ln -sfn "$REPO_ROOT/tools/vscode-c-block-comment" "$EXT_DIR"
echo "VS Code block comment extension linked"
