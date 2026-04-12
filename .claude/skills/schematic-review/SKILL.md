---
name: schematic-review
description: Review KiCad schematics for design issues. Use when asked to review, check, audit, or validate a schematic, KiCad project, or circuit design. TRIGGER when user mentions design review, schematic review, or asks to check a circuit.
argument-hint: [kicad-project-path]
allowed-tools: Read, Grep, Glob, Write, Edit, Bash, Agent, WebSearch, WebFetch, AskUserQuestion
---

# Schematic Design Review

Orchestrates a comprehensive design review of a KiCad schematic using 7 domain-specialist agents that review in parallel and produce a consolidated checklist.

## Workflow Overview

```
Phase 1: Project Discovery → Find and confirm the KiCad project
Phase 2: Data Extraction   → Export netlist/BOM via kicad-cli or parse .kicad_sch directly
Phase 3: Requirements       → Functional Requirements agent asks user what the board should do
Phase 4: Parallel Review    → 6 specialist agents review simultaneously
Phase 5: Consolidation      → Merge all findings into single severity-sorted checklist
```

## Phase 1: Project Discovery

### If user provided a path argument:
Use that path directly. Verify it points to a `.kicad_pro` or `.kicad_sch` file.

### If no path provided:
1. Search for `.kicad_pro` files: `Glob("**/*.kicad_pro")`
2. If multiple found, ask user which project to review using AskUserQuestion
3. If one found, ask user to confirm: "Found project at [path]. Review this project?"

### Identify schematic hierarchy:
1. Read the top-level `.kicad_sch` file (same name as `.kicad_pro`)
2. Find all `(sheet ...)` blocks to identify hierarchical sub-sheets
3. Build the complete list of `.kicad_sch` files to review
4. Report to user: "Found [N] schematic sheets: [list sheet names]"

## Phase 2: Data Extraction

Use the `kicad-cli` skill for all data extraction. Read the skill instructions at `.claude/skills/kicad-cli/SKILL.md` for the full command reference.

### Extract design data:
Follow the "Design Review Data Extraction" recipe from the kicad-cli skill:
1. Export netlist (XML format) to `/tmp/sch_review_netlist.xml`
2. Export BOM (CSV) to `/tmp/sch_review_bom.csv`
3. Run ERC (JSON format) to `/tmp/sch_review_erc.json`

If `kicad-cli` is not available, use the fallback S-expression parsing documented in the kicad-cli skill.

### Build data summary:
Create a structured inventory of:
- All components (ref, value, footprint, sheet)
- All nets and their connections
- All power rails
- All connectors

## Phase 3: Functional Requirements (Sequential)

**This phase runs FIRST and ALONE because it requires user interaction.**

1. Read the agent instructions: `Read("agents/functional-requirements.md")`
2. Spawn a single Agent with:
   - **prompt**: Include the full agent instructions from the file, plus:
     - List of all schematic file paths
     - Tell the agent to ask the user about requirements using AskUserQuestion
   - **description**: "Gather functional requirements"
3. Wait for this agent to complete
4. Capture the requirements list and verification results

## Phase 4: Specialist Review (Parallel)

**Spawn all 6 remaining agents in a SINGLE message (parallel execution).**

For each agent, read its instruction file and spawn it with the Agent tool. Include the kicad-cli skill path (`.claude/skills/kicad-cli/SKILL.md`) in every agent prompt so they can do additional data extraction if needed.

### Agent 1: Power & Thermal
- Read instructions: `agents/power-thermal.md`
- Include in prompt:
  - Full agent instructions
  - All schematic file paths
  - Reference file path: `references/power-design-rules.md`
  - kicad-cli skill path: `.claude/skills/kicad-cli/SKILL.md`
  - Component inventory from Phase 2

### Agent 2: Protection
- Read instructions: `agents/protection.md`
- Include in prompt:
  - Full agent instructions
  - All schematic file paths
  - Reference file path: `references/protection-guidelines.md`
  - kicad-cli skill path: `.claude/skills/kicad-cli/SKILL.md`
  - Connector inventory from Phase 2

### Agent 3: Signal Integrity
- Read instructions: `agents/signal-integrity.md`
- Include in prompt:
  - Full agent instructions
  - All schematic file paths
  - Reference file path: `references/signal-integrity-rules.md`
  - kicad-cli skill path: `.claude/skills/kicad-cli/SKILL.md`
  - Net/bus inventory from Phase 2

### Agent 4: Pin Verification
- Read instructions: `agents/pin-verification.md`
- Include in prompt:
  - Full agent instructions
  - All schematic file paths
  - Path to .ioc file (if exists)
  - kicad-cli skill path: `.claude/skills/kicad-cli/SKILL.md`
  - Full component inventory from Phase 2

### Agent 5: Analog Systems
- Read instructions: `agents/analog-systems.md`
- Include in prompt:
  - Full agent instructions
  - All schematic file paths
  - Path to spice-sim skill: `.claude/skills/spice-sim/SKILL.md`
  - kicad-cli skill path: `.claude/skills/kicad-cli/SKILL.md`
  - Analog component inventory from Phase 2

### Agent 6: ERC Analysis
- Read instructions: `agents/erc-analysis.md`
- Include in prompt:
  - Full agent instructions
  - All schematic file paths
  - Path to ERC report: `/tmp/sch_review_erc.json`
  - kicad-cli skill path: `.claude/skills/kicad-cli/SKILL.md`

## Phase 5: Consolidation

### Collect all agent outputs
Each agent returns findings in the format:
```
AGENT: [Name]
---
CRITICAL: ...
WARNING: ...
INFO: ...
SUGGESTION: ...
---
```

### Parse and merge
1. Extract all findings from all 7 agents
2. Tag each finding with its source agent/category
3. Sort by severity: CRITICAL first, then WARNING, INFO, SUGGESTION
4. Within each severity, group by category

### Generate consolidated checklist
Display the final report in the terminal:

```markdown
# Schematic Design Review: [Project Name]
**Project**: [path to .kicad_pro]
**Date**: [current date]
**Sheets reviewed**: [count] ([list])

## Summary
| Severity   | Count |
|------------|-------|
| CRITICAL   | X     |
| WARNING    | Y     |
| INFO       | Z     |
| SUGGESTION | W     |

## CRITICAL
- [ ] [Power] U3 (LMR38020): Output capacitor ESR may cause instability — Datasheet requires ESR < 50mohm, C12 (X5R 22uF/25V) ESR typical 3mohm at 100kHz ✓ but effective capacitance derates to ~10uF at 12V bias
- [ ] [Pin] U1 (STM32G474RET6): PA5 connected to net ENCODER_CS but .ioc configures PA5 as ADC2_IN13

## WARNING
- [ ] [Protection] J3 (CAN connector): No common-mode choke on CANH/CANL — recommended for industrial environment
- [ ] [Signal] R12 (4.7k): I2C pull-up may be too high for 400kHz — Rp_max at 200pF bus cap = 1.8k ohm

## INFO
- [ ] [Analog] Current sense: Shunt power dissipation = 0.5mW at 20A — well within 0805 rating
- [ ] [Requirements] REQ-03 (CAN FD): Fully implemented via U5 (TCAN1044VDR) + FDCAN2 peripheral

## SUGGESTION
- [ ] [Power] C5 (100uF electrolytic): Consider polymer cap for lower ESR and longer lifetime
- [ ] [Requirements] Fan control circuit present but not listed in requirements — intentional?

## ERC
- Total violations: 12 | Real issues: 2 | False positives: 8 | Needs clarification: 1 | Excluded: 1
- [ ] [ERC] Pin not connected: U7 pin 48 (PB9) — net label "I2C1_SDA" present but wire not connected to pin
- [ ] [ERC] Net with one connection: net "FAULT_N" on Motor_Driver sheet — label exists but no destination

## Simulations Run
- /tmp/sch_review_sim_bootstrap.cir: PASS — Bootstrap voltage holds >8V at 5% duty cycle
- /tmp/sch_review_sim_adc_filter.cir: WARNING — -3dB at 15kHz, ADC samples at 20kHz (marginal)
```

## Severity Definitions

| Level      | Meaning                                           | Action Required |
|------------|---------------------------------------------------|-----------------|
| CRITICAL   | Will likely cause board failure or malfunction     | Must fix before fabrication |
| WARNING    | May cause issues under certain operating conditions | Should fix, evaluate risk |
| INFO       | Best practice deviation, not necessarily a problem  | Acknowledge and document |
| SUGGESTION | Optimization opportunity for cost, reliability, or performance | Consider for next revision |

## Important Notes

- **Datasheets are the source of truth.** Every pin, value, and rating must be verified against the component datasheet. If a datasheet cannot be found, the Pin Verification agent will ask the user to provide it.
- **No guessing.** If an agent cannot verify something, it flags it rather than assuming it's correct.
- **SPICE simulations** are generated in `/tmp/` and are disposable. They verify analog circuit behavior using actual component values from the schematic.
- **The review is schematic-only.** PCB layout concerns (trace routing, copper pours, stackup) are noted as INFO items but cannot be fully verified from the schematic alone.
