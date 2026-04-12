---
name: kicad-cli
description: Extract data from KiCad projects using kicad-cli. Use when asked to export netlists, BOMs, Gerbers, run ERC/DRC, render PCBs, or extract any data from KiCad schematic or PCB files.
argument-hint: [command] [kicad-file-path]
allowed-tools: Read, Bash, Glob, Grep
---

# KiCad CLI Data Extraction

Use `kicad-cli` to export netlists, BOMs, run checks, generate fabrication outputs, and render PCB images from KiCad projects. This skill covers all available subcommands and their key options.

## Availability Check

```bash
which kicad-cli
```

If `kicad-cli` is not installed, fall back to direct `.kicad_sch` / `.kicad_pcb` S-expression parsing (see Fallback Parsing below).

## Project Discovery

To find KiCad projects in a directory:
```bash
# Find all KiCad project files
find <dir> -name "*.kicad_pro" 2>/dev/null
```

The top-level schematic shares the project name: `<project>.kicad_sch`. Identify hierarchical sub-sheets by reading the top-level `.kicad_sch` and finding `(sheet ...)` blocks.

---

## Schematic Commands

### Export Netlist

Structured representation of all components and their connections.

```bash
# KiCad XML format (best for structured parsing — includes component properties, nets, pin connections)
kicad-cli sch export netlist --format kicadxml -o /tmp/netlist.xml "<project>.kicad_sch"

# SPICE netlist (for simulation — automatically excludes components marked "exclude from sim")
kicad-cli sch export netlist --format spice -o /tmp/netlist.cir "<project>.kicad_sch"

# KiCad S-expression (default)
kicad-cli sch export netlist -o /tmp/netlist.net "<project>.kicad_sch"
```

**Available formats**: `kicadsexpr`, `kicadxml`, `cadstar`, `orcadpcb2`, `spice`, `spicemodel`, `pads`, `allegro`

**Variant support**: `--variant <name>` to export a specific variant. Use `${VARIANT}` in output path for multiple variants.

### Export BOM

Bill of Materials with full field control.

```bash
# Default BOM (Reference, Value, Footprint, Quantity, DNP)
kicad-cli sch export bom -o /tmp/bom.csv "<project>.kicad_sch"

# Custom fields — include manufacturer part number and description
kicad-cli sch export bom \
  --fields "Reference,Value,Footprint,${QUANTITY},MPN,Description" \
  --labels "Refs,Value,Footprint,Qty,MPN,Description" \
  --group-by "Value,Footprint,MPN" \
  --sort-field "Reference" \
  --exclude-dnp \
  -o /tmp/bom.csv "<project>.kicad_sch"
```

**Key options**:
- `--fields` / `--labels` — control which columns appear and their header names
- `--group-by` — group identical components (e.g., all 10k 0402 resistors on one line)
- `--exclude-dnp` — omit Do-Not-Populate components
- `--field-delimiter` — default `,` (CSV)
- `--string-delimiter` — default `"` (quote character)

**Generated fields** (use without `${}` delimiters in `--fields`):
- `QUANTITY` — component count after grouping
- `ITEM_NUMBER` — row number
- `DNP` — Do Not Populate flag
- `EXCLUDE_FROM_BOM` — excluded from BOM flag
- `EXCLUDE_FROM_BOARD` — excluded from board flag
- `EXCLUDE_FROM_SIM` — excluded from simulation flag

```bash
# BOM with simulation exclusion status
kicad-cli sch export bom \
  --fields "Reference,Value,Footprint,QUANTITY,EXCLUDE_FROM_SIM" \
  --labels "Ref,Value,Footprint,Qty,ExcludeSim" \
  -o /tmp/bom_sim.csv "<project>.kicad_sch"
```

### Run ERC (Electrical Rules Check)

```bash
# JSON format (machine-parseable)
kicad-cli sch erc --format json --severity-all -o /tmp/erc.json "<project>.kicad_sch"

# Human-readable report
kicad-cli sch erc --format report --severity-all -o /tmp/erc.rpt "<project>.kicad_sch"

# Return nonzero exit code if violations exist
kicad-cli sch erc --format json --severity-all --exit-code-violations -o /tmp/erc.json "<project>.kicad_sch"
```

**Severity filters** (combine as needed):
- `--severity-all` — include everything
- `--severity-error` — errors only
- `--severity-warning` — warnings only
- `--severity-exclusions` — include user-excluded violations

### Export Schematic PDF

```bash
# Full schematic PDF (all pages)
kicad-cli sch export pdf -o /tmp/schematic.pdf "<project>.kicad_sch"

# Black and white, no drawing sheet border
kicad-cli sch export pdf --black-and-white --exclude-drawing-sheet -o /tmp/schematic_bw.pdf "<project>.kicad_sch"

# Specific pages only
kicad-cli sch export pdf --pages "1,3,5" -o /tmp/schematic_partial.pdf "<project>.kicad_sch"
```

### Export Schematic SVG

```bash
# SVG output (one file per page, output is a directory)
kicad-cli sch export svg -o /tmp/sch_svg/ "<project>.kicad_sch"
```

---

## PCB Commands

### Run DRC (Design Rules Check)

```bash
# JSON format (machine-parseable)
kicad-cli pcb drc --format json --severity-all -o /tmp/drc.json "<project>.kicad_pcb"

# With zone refill (recommended for accurate results)
kicad-cli pcb drc --format json --severity-all --refill-zones -o /tmp/drc.json "<project>.kicad_pcb"

# Include schematic parity check
kicad-cli pcb drc --format json --severity-all --schematic-parity -o /tmp/drc.json "<project>.kicad_pcb"

# Return nonzero exit code if violations exist
kicad-cli pcb drc --format json --severity-all --exit-code-violations -o /tmp/drc.json "<project>.kicad_pcb"
```

### Export Gerbers

```bash
# All layers using board plot settings
kicad-cli pcb export gerbers --board-plot-params -o /tmp/gerbers/ "<project>.kicad_pcb"

# Specific layers
kicad-cli pcb export gerbers \
  -l "F.Cu,B.Cu,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts" \
  -o /tmp/gerbers/ "<project>.kicad_pcb"

# With drill origin and zone check
kicad-cli pcb export gerbers \
  --board-plot-params --use-drill-file-origin --check-zones \
  -o /tmp/gerbers/ "<project>.kicad_pcb"
```

### Export Drill Files

```bash
# Excellon format (standard)
kicad-cli pcb export drill --format excellon -o /tmp/gerbers/ "<project>.kicad_pcb"

# Separate PTH and NPTH files (many fabs prefer this)
kicad-cli pcb export drill --format excellon --excellon-separate-th -o /tmp/gerbers/ "<project>.kicad_pcb"

# With drill map
kicad-cli pcb export drill --format excellon --generate-map --map-format pdf -o /tmp/gerbers/ "<project>.kicad_pcb"
```

### Export Position (Pick and Place) Files

```bash
# CSV format, both sides, metric
kicad-cli pcb export pos --format csv --side both --units mm -o /tmp/pos.csv "<project>.kicad_pcb"

# SMD only, exclude DNP
kicad-cli pcb export pos --format csv --side both --smd-only --exclude-dnp -o /tmp/pos.csv "<project>.kicad_pcb"
```

### Export STEP (3D Model)

```bash
# Full board with components
kicad-cli pcb export step -o /tmp/board.step "<project>.kicad_pcb"

# Board only, no components
kicad-cli pcb export step --board-only -o /tmp/board_only.step "<project>.kicad_pcb"

# Exclude DNP components
kicad-cli pcb export step --no-dnp -o /tmp/board.step "<project>.kicad_pcb"

# Include copper and soldermask for visualization
kicad-cli pcb export step --include-tracks --include-pads --include-zones --include-soldermask -o /tmp/board_full.step "<project>.kicad_pcb"
```

### Export PCB PDF

```bash
# Front copper + silkscreen
kicad-cli pcb export pdf -l "F.Cu,F.SilkS" --mode-single -o /tmp/pcb_front.pdf "<project>.kicad_pcb"

# All layers as multipage PDF
kicad-cli pcb export pdf -l "F.Cu,B.Cu,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts" --mode-multipage -o /tmp/pcb_all.pdf "<project>.kicad_pcb"

# Separate files per layer
kicad-cli pcb export pdf -l "F.Cu,B.Cu" --mode-separate -o /tmp/pcb_layers/ "<project>.kicad_pcb"
```

### Render 3D Image

```bash
# Top-down PNG render
kicad-cli pcb render --side top --width 1920 --height 1080 -o /tmp/board_top.png "<project>.kicad_pcb"

# Bottom view
kicad-cli pcb render --side bottom -o /tmp/board_bottom.png "<project>.kicad_pcb"

# Isometric perspective view
kicad-cli pcb render --side top --perspective --rotate "-45,0,45" -o /tmp/board_iso.png "<project>.kicad_pcb"

# High quality with floor and shadows
kicad-cli pcb render --side top --quality high --floor --perspective -o /tmp/board_hq.png "<project>.kicad_pcb"

# Transparent background (PNG only)
kicad-cli pcb render --side top --background transparent -o /tmp/board_transparent.png "<project>.kicad_pcb"
```

### Board Statistics

```bash
# JSON format
kicad-cli pcb export stats --format json -o /tmp/stats.json "<project>.kicad_pcb"

# Human-readable report
kicad-cli pcb export stats --format report -o /tmp/stats.rpt "<project>.kicad_pcb"
```

---

## Common Recipes

### Design Review Data Extraction

Extract everything needed for a schematic design review:

```bash
PROJECT="<project>.kicad_sch"
OUTDIR="/tmp/sch_review"
mkdir -p "$OUTDIR"

# Netlist (XML for structured parsing)
kicad-cli sch export netlist --format kicadxml -o "$OUTDIR/netlist.xml" "$PROJECT"

# BOM
kicad-cli sch export bom -o "$OUTDIR/bom.csv" "$PROJECT"

# ERC
kicad-cli sch erc --format json --severity-all -o "$OUTDIR/erc.json" "$PROJECT"
```

### Fabrication Output Package

Generate everything a PCB fab house needs:

```bash
PROJECT="<project>.kicad_pcb"
OUTDIR="/tmp/fab_output"
mkdir -p "$OUTDIR"

# Gerbers (using board settings)
kicad-cli pcb export gerbers --board-plot-params --check-zones -o "$OUTDIR/" "$PROJECT"

# Drill files (separate PTH/NPTH)
kicad-cli pcb export drill --format excellon --excellon-separate-th --generate-map --map-format gerberx2 -o "$OUTDIR/" "$PROJECT"

# Position file for assembly
kicad-cli pcb export pos --format csv --side both --units mm --exclude-dnp -o "$OUTDIR/pos.csv" "$PROJECT"

# BOM for assembly
kicad-cli sch export bom --exclude-dnp -o "$OUTDIR/bom.csv" "${PROJECT%.kicad_pcb}.kicad_sch"
```

---

## Parsing Exported Data

### XML Netlist Structure

The `kicadxml` netlist contains:
- `<components>` — all components with properties (ref, value, footprint, fields)
- `<nets>` — all nets with pin connections (component ref + pin number)
- `<libparts>` — library part definitions with pin lists

Key XPath queries:
```
Components: //comp
  Reference:  //comp/@ref
  Value:      //comp/value
  Footprint:  //comp/footprint
  Fields:     //comp/fields/field[@name="..."]

Nets:       //net
  Net name:   //net/@name
  Pins:       //net/node (@ref + @pin)
```

### ERC/DRC JSON Structure

Both ERC and DRC JSON reports follow the same structure:
```
{
  "source": "<file>",
  "date": "<timestamp>",
  "kicad_version": "<version>",
  "violations": [
    {
      "type": "<rule_id>",
      "description": "<text>",
      "severity": "error" | "warning",
      "items": [
        {
          "description": "<item detail>",
          "pos": { "x": <mm>, "y": <mm> }
        }
      ]
    }
  ],
  "unconnected_items": [...],
  "schematic_parity": [...]
}
```

---

## Fallback: Direct S-Expression Parsing

When `kicad-cli` is not available, parse `.kicad_sch` files directly. These are S-expression (LISP-like) text files.

### Extract Components
Look for `(symbol ...)` blocks (not inside `(lib_symbols)`):
```
(symbol (lib_id "Device:R") (at X Y angle) (unit 1)
  (in_bom yes)
  (on_board yes)
  (exclude_from_sim no)
  (property "Reference" "R1" ...)
  (property "Value" "10k" ...)
  (property "Footprint" "Resistor_SMD:R_0402_1005Metric" ...)
  (pin "1" (uuid ...))
  (pin "2" (uuid ...))
)
```

### Component Flags
Each symbol has three exclusion flags:
- `(in_bom yes/no)` — include in Bill of Materials
- `(on_board yes/no)` — include on PCB
- `(exclude_from_sim yes/no)` — exclude from SPICE simulation

To find components excluded from simulation:
```bash
grep -B5 "exclude_from_sim yes" *.kicad_sch
```

### Extract Wires and Labels
```
(wire (pts (xy X1 Y1) (xy X2 Y2)) ...)
(label "NET_NAME" (at X Y angle) ...)
(global_label "NET_NAME" (at X Y angle) ...)
(hierarchical_label "NET_NAME" ...)
```

### Extract Power Symbols
Power symbols are regular symbols with `(lib_id "power:...")`:
```
(symbol (lib_id "power:+3V3") ...)
(symbol (lib_id "power:GND") ...)
```

### Find Hierarchical Sheets
```
(sheet (at X Y) (size W H)
  (property "Sheetname" "Power" ...)
  (property "Sheetfile" "Power.kicad_sch" ...)
  (pin "NET_NAME" input/output (at X Y) ...)
)
```
