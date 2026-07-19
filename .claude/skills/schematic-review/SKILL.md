---
name: schematic-review
description: Review KiCad schematics for design issues. Use when asked to review, check, audit, or validate a schematic, KiCad project, or circuit design. TRIGGER when user mentions design review, schematic review, or asks to check a circuit.
argument-hint: [kicad-project-path]
allowed-tools: Read, Grep, Glob, Write, Edit, Bash, Agent, WebSearch, WebFetch, AskUserQuestion
---

# Schematic Design Review

Orchestrates a comprehensive design review of a KiCad schematic using 10 domain-specialist agents that review in parallel and produce a consolidated checklist.

## Workflow Overview

```
Phase 1: Project Discovery → Find and confirm the KiCad project
Phase 2: Data Extraction   → Export netlist/BOM via kicad-cli or parse .kicad_sch directly
Phase 3: Requirements       → Functional Requirements agent asks user what the board should do
Phase 4: Parallel Review    → 10 specialist agents review simultaneously (ALL sims run, written to /tmp)
Phase 5: Consolidation      → Merge findings into report saved to /tmp/sch_review_report.md
Phase 6: Walkthrough        → Present issues one at a time, check them off the report as resolved
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

### Read existing documentation FIRST:
Before proceeding, search for and read any project documentation:
1. `Glob("**/README*")` — read every README found in the project tree
2. `Glob("**/*requirements*")` — read any requirements documents
3. `Glob("**/*spec*")` — read any specification documents
4. `Glob("**/*.pdf")` within the project directory — list any PDFs found

Extract all technical requirements, design intent, specifications, and constraints from these documents. Pass this information to the Functional Requirements agent so it can avoid re-asking questions already answered in the docs.

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

### Critical: All agents MUST use the netlist for connection tracing
The netlist XML (`/tmp/sch_review_netlist.xml`) is the **authoritative source for component interconnections**. Agents MUST reference it to determine what each pin connects to. Do NOT attempt to trace wire segments in `.kicad_sch` files — those are complex S-expression trees and agents frequently misread them. Use the netlist `<net>` entries to find all nodes on a given net, and `<comp>` entries for component details. The `.kicad_sch` files should only be used for component positions, property values, and net labels that supplement the netlist.

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

**Spawn all 10 remaining agents in a SINGLE message (parallel execution).**

For each agent, read its instruction file and spawn it with the Agent tool. Include the kicad-cli skill path (`.claude/skills/kicad-cli/SKILL.md`) in every agent prompt so they can do additional data extraction if needed.

### Mandatory Simulation Rule
Any agent whose instruction file references SPICE simulations (Analog Systems, Protection, Component Rating, Footprint Audit) **MUST run every simulation described in their instructions**. Do not skip, defer, or note "for future simulation." Run them now. Write each simulation to `/tmp/sch_review_sim_[agent]_[name].cir` and log results. If a sim fails due to missing models, note the missing model and provide a download link to the user, but still run every sim you can.

### Agent 1: Power & Thermal
- Read instructions: `agents/power-thermal.md`
- Include in prompt:
  - Full agent instructions
  - All schematic file paths
  - Netlist path: `/tmp/sch_review_netlist.xml` — **MUST use this for connection tracing**
  - Reference file path: `references/power-design-rules.md`
  - kicad-cli skill path: `.claude/skills/kicad-cli/SKILL.md`
  - Component inventory from Phase 2

### Agent 2: Protection
- Read instructions: `agents/protection.md`
- Include in prompt:
  - Full agent instructions
  - All schematic file paths
  - Netlist path: `/tmp/sch_review_netlist.xml` — **MUST use this for connection tracing**
  - Reference file path: `references/protection-guidelines.md`
  - kicad-cli skill path: `.claude/skills/kicad-cli/SKILL.md`
  - Connector inventory from Phase 2

### Agent 3: Signal Integrity
- Read instructions: `agents/signal-integrity.md`
- Include in prompt:
  - Full agent instructions
  - All schematic file paths
  - Netlist path: `/tmp/sch_review_netlist.xml` — **MUST use this for connection tracing**
  - Reference file path: `references/signal-integrity-rules.md`
  - kicad-cli skill path: `.claude/skills/kicad-cli/SKILL.md`
  - Net/bus inventory from Phase 2

### Agent 4: Pin Verification
- Read instructions: `agents/pin-verification.md`
- Include in prompt:
  - Full agent instructions
  - All schematic file paths
  - Netlist path: `/tmp/sch_review_netlist.xml` — **MUST use this for connection tracing**
  - Path to .ioc file (if exists)
  - kicad-cli skill path: `.claude/skills/kicad-cli/SKILL.md`
  - Full component inventory from Phase 2

### Agent 5: Analog Systems
- Read instructions: `agents/analog-systems.md`
- Include in prompt:
  - Full agent instructions
  - All schematic file paths
  - Netlist path: `/tmp/sch_review_netlist.xml` — **MUST use this for connection tracing**
  - Path to spice-sim skill: `.claude/skills/spice-sim/SKILL.md`
  - kicad-cli skill path: `.claude/skills/kicad-cli/SKILL.md`
  - Analog component inventory from Phase 2

### Agent 6: Component Rating
- Read instructions: `agents/component-rating.md`
- Include in prompt:
  - Full agent instructions
  - All schematic file paths
  - Netlist path: `/tmp/sch_review_netlist.xml` — **MUST use this for connection tracing**
  - kicad-cli skill path: `.claude/skills/kicad-cli/SKILL.md`
  - Full component inventory from Phase 2

### Agent 7: Memory Audit
- Read instructions: `agents/memory-audit.md`
- Include in prompt:
  - Full agent instructions
  - All schematic file paths
  - Netlist path: `/tmp/sch_review_netlist.xml` — **MUST use this for connection tracing**
  - Full component inventory from Phase 2
  - Ask user about firmware size, config data, and logging requirements

### Agent 8: Footprint Audit
- Read instructions: `agents/footprint-audit.md`
- Include in prompt:
  - Full agent instructions
  - All schematic file paths
  - Netlist path: `/tmp/sch_review_netlist.xml` — **MUST use this for connection tracing**
  - Full component inventory from Phase 2
  - Path to any `.kicad_pcb` file (if available)

### Agent 9: ERC Analysis
- Read instructions: `agents/erc-analysis.md`
- Include in prompt:
  - Full agent instructions
  - All schematic file paths
  - Netlist path: `/tmp/sch_review_netlist.xml` — **MUST use this for connection tracing**
  - Path to ERC report: `/tmp/sch_review_erc.json`
  - kicad-cli skill path: `.claude/skills/kicad-cli/SKILL.md`

### Agent 10: Common Gotchas (boundary-scan agent)
- Read instructions: `agents/common-gotchas.md`
- Include in prompt:
  - Full agent instructions
  - All schematic file paths
  - Netlist path: `/tmp/sch_review_netlist.xml` — **MUST use this for connection tracing**
  - Component inventory from Phase 2 (BOM / netlist)
  - kicad-cli skill path: `.claude/skills/kicad-cli/SKILL.md`

## Phase 5: Consolidation — Save Report to /tmp

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

SIMULATIONS:
- /tmp/sch_review_sim_[name].cir: [PASS/FAIL] — [summary]
```

### Parse and merge
1. Extract all findings from all 10 agents
2. Tag each finding with its source agent/category
3. Sort by severity: CRITICAL first, then WARNING, INFO, SUGGESTION
4. Within each severity, group by category

### Write report to file
Write the complete report to `/tmp/sch_review_report.md`. This file survives context loss. The report format:

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
- [ ] [Power] ...
- [ ] [Pin] ...

## WARNING
- [ ] [Protection] ...
- [ ] [Signal] ...

## INFO
- [ ] [Analog] Current sense: Shunt power dissipation = 0.5mW at 20A — well within 0805 rating
- [ ] [Requirements] REQ-03 (CAN FD): Fully implemented via U5 (TCAN1044VDR) + FDCAN2 peripheral
- [ ] [Rating] All components within 80% of rated stress — no overstress conditions found
- [ ] [Memory] Firmware fits with 40% headroom — 256KB used of 512KB available

## SUGGESTION
- [ ] [Power] ...
- [ ] [Requirements] ...

## ERC
- Total violations: N | Real issues: X | False positives: Y | Needs clarification: Z | Excluded: W

## Simulations Run
- [file] — PASS/FAIL — [summary]
```

After writing, display the Summary table and first issue to the user to begin Phase 6.

## Severity Definitions

| Level      | Meaning                                           | Action Required |
|------------|---------------------------------------------------|-----------------|
| CRITICAL   | Will likely cause board failure or malfunction     | Must fix before fabrication |
| WARNING    | May cause issues under certain operating conditions | Should fix, evaluate risk |
| INFO       | Best practice deviation, not necessarily a problem  | Acknowledge and document |
| SUGGESTION | Optimization opportunity for cost, reliability, or performance | Consider for next revision |

## Phase 6: Walkthrough — One Issue at a Time

After the report is saved to `/tmp/sch_review_report.md`, present issues to the user **one at a time**, starting with CRITICAL items first.

For each issue:

1. **Read** the current report from `/tmp/sch_review_report.md`
2. **Present** the single issue to the user with context:
   - What the finding is
   - Which component(s) and sheet(s) are involved
   - What the datasheet says vs what the schematic shows
   - Any relevant simulation result
3. **Ask** the user: "Fix this issue, acknowledge it, or skip for now?"
4. **If user fixes it** — re-read the relevant schematic file to confirm the fix, then update `/tmp/sch_review_report.md` by changing `[ ]` to `[x]` for that item. If the fix introduces new concerns (e.g., changing a resistor value affects a simulation), re-run the relevant sim.
5. **If user acknowledges** — change `[ ]` to `[x]` with note `(acknowledged)`
6. **If user skips** — leave as `[ ]` and move on
7. **After each issue** — re-display the updated summary so the user sees progress:

```
Progress: [X] of [N] issues resolved
CRITICAL: [done/total] | WARNING: [done/total] | INFO: [done/total] | SUGGESTION: [done/total]
```

Continue until all issues have been visited or the user ends the session.

## Agent Rule: Requesting External Files
If ANY agent cannot retrieve a file itself (datasheet PDF, SPICE model, footprint file, etc.):
1. Search for a direct download link for the file
2. Provide the specific URL to the user: "I cannot download [file] directly. Please download it from [URL] and I'll analyze it."
3. Do NOT proceed with verification of that component until the file is provided
4. Flag it as `CRITICAL` in findings — verification is blocked pending the file

## Important Notes

- **Datasheets are the source of truth.** Every pin, value, and rating must be verified against the component datasheet. If a datasheet cannot be found, the Pin Verification agent will ask the user to provide it.
- **No guessing.** If an agent cannot verify something, it flags it rather than assuming it's correct.
- **SPICE simulations** are MANDATORY, not optional. Any agent with simulation instructions must run them. All simulations go to `/tmp/sch_review_sim_*.cir` — they are disposable and can be re-run if the schematic changes.
- **The report lives at `/tmp/sch_review_report.md`** — it persists across context windows. If the session resets, read this file to resume the walkthrough.
- **The review is schematic-only.** PCB layout concerns (trace routing, copper pours, stackup) are noted as INFO items but cannot be fully verified from the schematic alone.
