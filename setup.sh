#!/bin/bash

# KiCad template symlink
ln -sf "$(pwd)/PCB_Hardware/templates/KiCAD_template" ~/.local/share/kicad/10.0/template/KiCAD_template
echo "KiCad template linked"
