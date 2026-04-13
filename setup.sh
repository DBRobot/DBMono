#!/bin/bash

# KiCad template symlink
ln -sf "$(pwd)/PCB_Hardware/templates/KiCAD_template" ~/.local/share/kicad/10.0/template/KiCAD_template
echo "KiCad template linked"

# VS Code C block comment extension (/* + Enter auto-continues with *)
EXT_DIR="$HOME/.vscode/extensions/local.c-block-comment-rules-1.0.0"
ln -sfn "$(pwd)/tools/vscode-c-block-comment" "$EXT_DIR"
echo "VS Code block comment extension linked"
