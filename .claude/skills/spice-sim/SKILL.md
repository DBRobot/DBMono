---
name: spice-sim
description: Build and run ngspice simulations from KiCad schematics. Use when asked to simulate circuits, create SPICE netlists, verify analog designs, or test subcircuit behavior (filters, power stages, amplifiers, current sense, etc).
argument-hint: [schematic-path] [analysis-type]
allowed-tools: Read, Grep, Glob, Write, Edit, Bash, WebSearch, WebFetch, AskUserQuestion
---

# ngspice Simulation from KiCad Schematics

Build ngspice simulations from KiCad schematic files. Handles extracting circuit topology, generating component models from datasheet parameters, assembling netlists, and running appropriate analyses.

## Overview

1. Read the schematic and extract circuit topology
2. Break the design into simulatable functional blocks
3. Gather or generate SPICE models for each component
4. Build the netlist with appropriate analysis
5. Run the simulation and interpret results

---

## Step 1: Read the Schematic

Use the `kicad-cli` skill (`.claude/skills/kicad-cli/SKILL.md`) to export a SPICE netlist if possible:

```bash
kicad-cli sch export netlist --format spice -o /tmp/sim_netlist.cir "<project>.kicad_sch"
```

Otherwise, read the `.kicad_sch` files directly and extract:
- Component references, values, and footprints
- Net connections between pins
- Power rail names and structure
- Hierarchical sheet organization

## Step 2: Identify Functional Blocks

Scan the schematic for independently simulatable subcircuits. Common blocks:

| Block Type | Identifying Components | Typical Goal |
|---|---|---|
| Switching power stage | Controller IC, inductors, FETs, feedback divider | Startup, load/line transient, regulation |
| LDO / linear regulator | LDO IC, input/output caps | Dropout, load transient, PSRR |
| Analog filter | Op-amp + RC/LC network | Frequency response, step response |
| Current sense | Sense resistor + amplifier IC | Gain accuracy, bandwidth |
| Voltage monitor | Resistor divider to ADC | Divider accuracy, filter cutoff |
| Temperature sense | NTC/PTC + bias resistor | Voltage vs temperature curve |
| Gate drive / bootstrap | Gate driver IC, bootstrap cap/diode | Bootstrap voltage hold-up at min duty |
| Comparator / threshold | Comparator IC, reference divider | Trip point, hysteresis |
| Oscillator / crystal | Crystal + load caps | Load capacitance match |
| Motor drive bridge | Half-bridge FETs, gate drivers | Switching waveforms, dead time |

Present the blocks found and ask the user which to simulate:

> Found these subcircuits:
> 1. **48V-12V buck** — LM5117 + sync FETs, LC output filter
> 2. **Phase current sense** — 1mOhm shunt + INA240
> 3. **ADC input filter** — RC low-pass on voltage monitor divider
>
> Which would you like to simulate?

## Step 3: Choose Analyses

For each block, recommend analyses based on its type and ask the user:

| Block Type | Primary | Secondary | Optional |
|---|---|---|---|
| Switching converter | `.tran` startup | `.tran` load step | `.dc` regulation |
| LDO | `.tran` load step | `.dc` dropout | `.ac` PSRR |
| Filter / amplifier | `.ac` bode plot | `.tran` step response | `.dc` transfer |
| Oscillator | `.tran` startup | `.four` harmonics | |
| Comparator | `.dc` threshold sweep | `.tran` propagation delay | |
| Voltage reference | `.dc` line regulation | `.tran` startup | |
| Current sense | `.dc` gain accuracy | `.ac` bandwidth | |
| Level shifter | `.tran` timing | `.dc` transfer | |
| Protection circuit | `.tran` trip response | `.dc` threshold | |
| LED / motor driver | `.tran` PWM response | `.dc` I-V characteristic | |

## Step 4: Acquire SPICE Models

For each component, determine what model is needed:

**Passives (R, L, C)**: Use ideal elements with parasitics where they matter:
- Capacitor ESR for power supply stability
- Inductor DCR for power loss / efficiency
- Inductor saturation (model as current-dependent if critical)

**Discrete semiconductors**: Build models from datasheet parameters. See reference files:
- `mosfet-models.md` — Level 1 MOSFET models from Vth, Rds(on), capacitances
- `diode-models.md` — Diode models from Vf, Ir, Cj, trr

**ICs (controllers, amplifiers, transceivers)**: Try to find manufacturer models:

1. Search the project for existing `.lib` or `.mod` files
2. Search online: `"<part_number>" SPICE model site:<manufacturer>.com`
3. Check LTspice libraries (compatible with ngspice)
4. Check distributor pages (Mouser, DigiKey sometimes host models)

If a manufacturer model is PSpice format, it usually works in ngspice with `set ngbehavior=psa` in the `.control` block. See PSpice Compatibility below.

**When no model exists**, offer the user choices:
1. **Behavioral substitute** — simplified model capturing key behavior (gain, bandwidth, thresholds)
2. **Ideal source** — replace with fixed voltage/current source
3. **User provides** — point to a `.lib` file
4. **Skip** — simulate surrounding circuit without this part

### Behavioral Substitute Templates

**Ideal op-amp**:
```spice
.subckt OPAMP_IDEAL vp vn vout
E1 vout 0 VALUE = {1e6 * (V(vp) - V(vn))}
Rout vout 0 1MEG
.ends
```

**Op-amp with bandwidth and slew rate**:
```spice
* GBW and slew rate set by R1/C1 and R2/C2
.subckt OPAMP_BW vp vn vout vcc vee
E1 mid 0 VALUE = {1e5 * (V(vp) - V(vn))}
R1 mid filt 1k
C1 filt 0 {1/(6.28*1k*GBW)}   ; GBW in Hz
E2 buf 0 filt 0 1
R2 buf vout 1k
C2 vout 0 {SLEW_CAP}           ; Sets slew rate
B_clamp vout 0 V = {max(V(vee)+0.1, min(V(vcc)-0.1, V(vout)))}
.ends
```

**LDO (with dropout)**:
```spice
.subckt LDO_BEH vin vout gnd
.param VREG=3.3 VDROP=0.2
B1 vout gnd V = {min(VREG, max(0, V(vin,gnd) - VDROP))}
Rbleed vout gnd 1MEG
.ends
```

**Comparator with delay**:
```spice
.subckt COMP_BEH vp vn vout vcc gnd
B1 mid 0 V = {if(V(vp) > V(vn), V(vcc), 0)}
R1 mid vout 1k
C1 vout 0 100p
.ends
```

**Fixed-duty PWM (replace controller IC)**:
```spice
.param DUTY=0.25
.param FSW=500k
.param TPERIOD={1/FSW}
Vpwm gate 0 PULSE(0 10 0 10n 10n {DUTY*TPERIOD} {TPERIOD})
```

**Gate driver buffer**:
```spice
.subckt GATE_DRV in out vcc gnd
B1 out gnd V = {if(V(in,gnd) > 1.5, V(vcc,gnd), 0)}
Rdrv out gnd 10
.ends
```

**Current sense amplifier**:
```spice
.subckt CSAMP inp inn vout vcc gnd
.param GAIN=50
E1 vout gnd VALUE = {GAIN * (V(inp) - V(inn))}
.ends
```

## Step 5: Build the Netlist

Use this structure:

```spice
* <Title>
* <Description and operating conditions>
*
* ============================================
* Parameters
* ============================================
.param VIN=48
.param VOUT=12
.param RLOAD=6           ; 2A nominal
.param FSW=500k

* ============================================
* Models (generated or included)
* ============================================
.include <model_file>.lib

.model NFET NMOS (Level=1 Vto=2.0 Kp=200 Lambda=0.01
+   Rd=2.8m Rs=2.8m Cgso=1.2n Cgdo=30p Cbd=540p Cbs=0 Is=1e-14 Pb=0.8)

.model DFLYBACK D (Is=10u N=1.05 Rs=50m Bv=60 Ibv=100u
+   Cjo=50p Vj=0.4 M=0.5 Tt=5n)

* ============================================
* Circuit
* ============================================
Vin vin 0 DC {VIN}
<... netlist ...>
Rload vout 0 {RLOAD}

* ============================================
* Analysis
* ============================================
.OPTIONS RELTOL=0.003 VNTOL=1m ABSTOL=1u CHGTOL=1p
+ GMIN=1e-9 ITL1=1000 ITL2=1000 ITL4=500
+ METHOD=GEAR TRTOL=7 RSHUNT=1e8

.tran 100n 5m UIC

.control
  set ngbehavior=psa
  set filetype=ascii
  set wr_singlescale
  set wr_vecnames

  run

  wrdata /tmp/sim_output.csv V(vout) I(L1)

  meas tran vout_avg AVG V(vout) from=4m to=5m
  meas tran vout_ripple PP V(vout) from=4m to=5m

  echo "=== Done ==="
  quit
.endc

.end
```

### Choosing Analysis Parameters

**Transient (.tran)**:
- Timestep: 1/100 to 1/1000 of the fastest period (switching freq or signal bandwidth)
- Stop time: long enough to reach steady state (5-10x time constants)
- `UIC` — use initial conditions, skip DC operating point (required for most power circuits)

**AC sweep (.ac)**:
- `dec 100 <fstart> <fstop>` — 100 points per decade, log sweep
- Start 1-2 decades below band of interest
- Stop 1-2 decades above

**DC sweep (.dc)**:
- `<source> <start> <stop> <step>`
- Step size small enough to capture transitions

**Measurements**: Use `.meas` for automated extraction of key values (average, peak-peak, rise time, delay, etc.)

## Step 6: Run and Verify

```bash
# Create output directory
mkdir -p <project>/simulation

# Write .spiceinit for PSpice compat
echo "set ngbehavior=psa" > <project>/simulation/.spiceinit

# Run in batch mode
ngspice -b <circuit>.cir 2>&1 | tee <circuit>.log
```

Check the log for:
- Convergence warnings ("timestep too small", "iteration limit")
- Measurement results
- Any error messages

### Optional: Generate Plots

```bash
gnuplot <<'EOF'
set terminal png size 1400,900
set output 'plot.png'
set datafile separator whitespace
set xlabel "Time (s)"
set ylabel "Voltage (V)"
plot 'sim_output.csv' using 1:2 with lines title "V(out)"
EOF
```

---

## PSpice Compatibility

Many manufacturer models are in PSpice format. ngspice handles most of them with:

```spice
.control
  set ngbehavior=psa
.endc
```

Or in `.spiceinit`:
```
set ngbehavior=psa
```

### Common Issues and Fixes

**SR latch initialization** — Cross-coupled NOR gates need different initial conditions on each side to break symmetry. Create two subcircuit variants:
```spice
* Variant that starts LOW
.subckt NOR2_LO a b out
B1 mid 0 VALUE = {if(V(a) > 1.5 | V(b) > 1.5, 0.3, 3.5)}
R1 mid out 1
C1 out 0 10n IC=0.3
.ends

* Variant that starts HIGH
.subckt NOR2_HI a b out
B1 mid 0 VALUE = {if(V(a) > 1.5 | V(b) > 1.5, 0.3, 3.5)}
R1 mid out 1
C1 out 0 10n IC=3.5
.ends
```

**Behavioral source discontinuities** — Abrupt if/then transitions cause convergence failures. Add RC smoothing:
```spice
.subckt SMOOTH_SWITCH in out
B1 mid 0 VALUE = {if(V(in) > 1.5, 3.5, 0.3)}
R1 mid out 10
C1 out 0 50p
.ends
```

**Function name case** — PSpice `IF()` becomes `if()` in ngspice. `LIMIT()` works with `psa` mode. `TABLE()` may need conversion to `PWL`.

---

## Convergence Troubleshooting

### "timestep too small"
1. Check for uninitialized SR latches (see above)
2. Add RC filtering to behavioral voltage/current sources
3. Increase `RSHUNT` (try 1e8 to 1e10) — adds high-value resistors from every node to ground
4. Relax `RELTOL` (try 0.01, default 0.001)
5. Reduce simulation timestep by 10x

### Operating point fails / "no convergence"
1. Use `.tran ... UIC` to bypass DC operating point
2. Add `.ic V(node)=value` for critical nodes
3. Check for floating nodes — add high-value bleed resistors (1MEG to ground)
4. Check for zero-impedance voltage source loops

### Controller IC stays off
1. Verify UVLO divider puts the enable pin above the threshold
2. Check soft-start cap initial voltage — may hold enable low too long
3. Bootstrap cap may need initial charge: `.ic V(boot)=10`
4. Increase simulation time — some controllers have long startup delays

### Output doesn't regulate
1. Recalculate feedback divider: `Vout = Vref * (1 + Rtop/Rbottom)`
2. Check compensation network values against datasheet application circuit
3. Verify the feedback loop is actually closed (trace the net from output sense to FB pin)

---

## File Organization

```
<project>/simulation/
  <name>.cir              # Simulation netlist
  <name>.log              # Simulation output log
  <name>_output.csv       # Exported waveform data
  run_sim.sh              # Batch runner script
  .spiceinit              # ngspice config (set ngbehavior=psa)
  models/                 # Manufacturer .lib files (if any)
```

### run_sim.sh template

```bash
#!/bin/bash
set -e
cd "$(dirname "$0")"

if ! command -v ngspice &>/dev/null; then
    echo "ngspice not installed. Install: sudo apt install ngspice"
    exit 1
fi

[ -f .spiceinit ] || echo "set ngbehavior=psa" > .spiceinit

for cir in "$@"; do
    echo "--- Running $cir ---"
    ngspice -b "$cir" 2>&1 | tee "${cir%.cir}.log"
done

echo "Done."
```
