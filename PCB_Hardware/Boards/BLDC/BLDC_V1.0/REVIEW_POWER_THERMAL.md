# BLDC_V1.0 Power & Thermal Analysis

## 1. Power Architecture Summary

```
BATT_V (J3/J4, 12–72V)
  ├─ R6 (0.5mΩ 2512) → SENSE → Q2(d) → Q2(s)=Q13(s) → Q13(d) → +BATT (VM bus)
  ├─ D4 (SMCJ78A) → GND           (TVS clamp on input)
  ├─ R29 (49Ω) → D18 (BZG05C75) → U5(VIN)  (LM5069 IC supply, zener-clamped ~75V)
  └─ U5 (LM5069MM-2) hot-swap controller
       ├─ SENSE=1 → R6 sense
       ├─ GATE=10 → Q2+Q13 gates
       ├─ OUT=9   → +BATT (VM)
       ├─ TIMER=6 → C11 (33nF)
       ├─ UVLO=3  → R7(102k)/R8(30k)  → Von=5.5V, Voff=5.06V
       ├─ OVLO=4  → R9(113k)/R10(3.9k) → Vovlo=37.5V  *** SEE WARNING ***
       └─ PWR=7   → R11(3.9k)

VM bus (+BATT, 12–72V)
  ├─ U1 (DRV8353) VM+VDRAIN   (gate driver, 100V max)
  ├─ U4 (LM516x) VIN           (buck converter → +5V MAIN)
  ├─ Q3/Q5/Q7 drains           (high-side FETs, TPH3R70APL)
  ├─ D15–D17 (SMCJ90A)        (bidirectional TVS on phases)
  └─ Bulk: C15/C17/C22/C23/C25/C26/C28/C50 (330µF+4×2.2µF MLCC+more)

Buck: LM516x (SOIC-8-1EP)
  ├─ L1 = 68µH (SRN4018)
  ├─ R30 RT = 24.9k → fsw ~3MHz (verify part number)
  ├─ R39(453k)/R40(143k) FB → Vout = 5.0V
  └─ Output: +5V MAIN

+5V MAIN (5V bus)
  ├─ U11 (AMS1117-3.3, SOT-223, 1A) → +3.3V
  ├─ U6 (TLV70233, SOT-23-5, 300mA) → +3.3VA
  ├─ U12/U13 (LM66100, ideal diode OR-ing) ← 5V_USB
  └─ D21 (LED indicator)

Motor phase: U1 → Q3/Q4(A), Q5/Q6(B), Q7/Q8(C)
  ├─ Shunts: R12/R13/R14 = 1mΩ 2512 (each phase low-side)
  ├─ SPx → FET source (high side of shunt), SNx → GND
  └─ No external RC filter on SPx/SNx (relies on DRV8353 internal filter)
```

## 2. FET Power Loss (TPH3R70APL × 6, SOP Advance 5×6mm)

### Assumptions
- Vin = 48V (midpoint), 12A phase RMS, sinusoidal commutation
- fsw = 45kHz, IDRIVE = 0.25–1.0A (adjustable)
- Tj ~ 60°C → Rds(on) ≈ 3.7mΩ × 1.175 = **4.35mΩ**
- Duty cycle average ~0.5, each FET conducts ~120° electrical

### Conduction Loss (per FET)
```
Pcon = I_phase² × Rds(on) × ¼ = 144 × 0.00435 × 0.25 = 0.157W
Total 6 FETs: 0.94W
```

### Switching Loss (per FET)
```
tr/tf ≈ Qsw / IDRIVE = 21nC / 0.25A ≈ 84ns (moderate drive)
Psw = ½ × Vds × Id × (tr+tf) × fsw × (1/3)
    = 0.5 × 48 × 12 × 168e-9 × 45000 / 3
    = 0.726W per FET
Total 6 FETs: 4.36W

At IDRIVE = 1.0A (tr/tf = 21ns):
Psw = 0.5 × 48 × 12 × 42e-9 × 45000 / 3 = 0.181W per FET
Total 6 FETs: 1.09W
```

### Gate Drive Loss
```
Pgate = Qg × Vgs × fsw × (1/3) = 67nC × 11V × 45000 / 3 = 11mW per FET
Total 6 FETs: 66mW
```

### Total FET Dissipation
| IDRIVE | Per FET | Total 6 FETs |
|--------|---------|--------------|
| 0.25A  | ~0.9W   | ~5.4W        |
| 1.0A   | ~0.35W  | ~2.1W        |

### Junction Temperature
```
RθJA(SOP Advance) ≈ 40°C/W (estimated, 4-layer PCB with vias)
Tj = 60 + (5.4/6) × 40 = 60 + 36 = 96°C     (IDRIVE=0.25A)
Tj = 60 + (2.1/6) × 40 = 60 + 14 = 74°C     (IDRIVE=1.0A)
```
Max rated Tj = 175°C. Both OK with margin.

### 100A Peak Transient
```
Pcon_peak = 100² × 0.00435 × 0.25 = 10.9W per FET
ZθJC(1s) ≈ 2°C/W → Tj rise ≈ 22°C
Peak Tj ≈ 96 + 22 = 118°C  → OK for short transient
```

## 3. DRV8353SRTAR Gate Driver Dissipation (QFN-40 6×6mm)

### Internal Power Breakdown
| Source | Calculation | Power |
|--------|-------------|-------|
| VCP charge pump (HS gate) | 3 × 67nC × 11V × 45kHz | 100mW |
| VGLS LDO (LS gate) | 3 × 67nC × 11V × 45kHz | 100mW |
| DVDD reg (3.3V) | ~10mA × 3.3V | 33mW |
| Bias + shunt amps | from datasheet typical | ~50mW |
| Buck reg (internal) | LM5008A type, ~50mA load | ~150mW |
| **Total** | | **~430mW** |

### Junction Temperature
```
RθJA ≈ 30°C/W (4-layer PCB, thermal vias under exposed pad)
Tj = 60 + 0.43 × 30 = 60 + 12.9 = 73°C
```
Max rated Tj = 125°C. OK with 52°C margin.

At 85°C ambient: Tj = 85 + 12.9 = 98°C. Still OK.

## 4. Shunt Resistors (3 × 1mΩ 2512)

### Continuous (12A)
```
Pshunt = I² × R = 144 × 0.001 = 0.144W per shunt → Total 0.43W
2512 rating: ~1–2W → OK
```

### Peak (100A)
```
Pshunt_peak = 100² × 0.001 = 10W per shunt
```
**WARNING:** 10W exceeds typical 2512 continuous rating. For short pulses (<10ms during SPIN cycle), pulse energy capability of the specific shunt must be verified. Ensure selected resistor has adequate pulsed-power rating.

## 5. Hot-Swap (LM5069 + IPT015N10N5 × 2)

### UVLO/OVLO Settings
```
UVLO on:  1.25 × (102k+30k)/30k = 5.5V
UVLO off: 1.15 × 4.4 = 5.06V     (hys=0.1V)

OVLO:     1.25 × (113k+3.9k)/3.9k = 37.5V
```

**CRITICAL:** OVLO set to 37.5V, but system nominal is 12–72V. This will trip during normal operation at >37.5V. Likely a design error — the OVLO divider needs recalculation. For 80V OVLO:
- R9/(R9+R10) = 1.25/80 → R9/R10 = 63 → R9 = 63 × 3.9k = 246k (use 243k standard)

### Current Limit
```
Ilimit = 55mV / 0.5mΩ = 110A
```

### Timer / Start-up
```
Ctimer = 33nF → ttimer ≈ 33nF × 8ms/µF = 264µs
Charge time for 330µF bulk to 48V at 110A:
  dV/dt = 110A / 330µF = 0.33V/µs
  t_charge = 48V / 0.33V/µs = 145µs < 264µs  ✓
```

### SOA During Start-up
```
Vds ≈ 48V, Id ≈ 110A for ~145µs
IPT015N10N5 SOA: >100A at 48V for >1ms → OK
```

### Steady-State Conduction
```
Pcon = 12² × 0.0015 = 0.216W per FET (two in series: 0.432W total)
RθJA(HSOF-8) = 62 K/W
Tj = 60 + 0.216 × 62 = 73.4°C  → OK
```

## 6. LDO Thermal Analysis

### AMS1117-3.3 (SOT-223, 1A, 5V→3.3V)
| I_load | Pdiss | RθJA | Tj at 60°C Ta | Status |
|--------|-------|------|---------------|--------|
| 300mA  | 0.51W | 90°C/W | 106°C | OK (margin 19°C) |
| 500mA  | 0.85W | 90°C/W | **136°C** | **EXCEEDS 125°C max** |
| 1A     | 1.70W | 90°C/W | **213°C** | **EXCEEDS** |

**CRITICAL:** If system load exceeds ~350mA, the AMS1117 will overheat at 60°C ambient. Recommend:
- Add copper pour/heatsinking to SOT-223 (improves RθJA to ~60°C/W)
- Use switching regulator instead (e.g., LMR16006)
- Or limit total 3.3V load to <350mA

### TLV70233 (SOT-23-5, 300mA, 5V→3.3VA)
| I_load | Pdiss | RθJA | Tj at 60°C Ta | Status |
|--------|-------|------|---------------|--------|
| 100mA  | 0.17W | 220°C/W | 97°C | OK |
| 200mA  | 0.34W | 220°C/W | **135°C** | **EXCEEDS 125°C max** |

**WARNING:** Keep TLV70233 load below ~120mA to stay within Tjmax at 60°C ambient.

## 7. Buck Converter (LM516x → 5V)

| Parameter | Value | Notes |
|-----------|-------|-------|
| Topology | LM516x (likely LM5161 for 100V input) | SOIC-8-1EP |
| Input | 12–72V (from VM) | |
| Output | 5.0V | FB: R39=453k, R40=143k → Vfb=1.2×(1+453/143)=5.0V |
| Inductor | 68µH (SRN4018) | Bourns, 4×4mm |
| RT resistor | 24.9kΩ | fsw calculation → see below |
| Bootstrap | C51 = 2.2nF | |
| SW node | R31 = 121k (snubber?) | |
| Load | ~300–800mA | AMS1117 + TLV70233 + peripherals |

**WARNING:** RT resistor 24.9kΩ computes to fsw ≈ 3MHz for LM5160 formula (fsw = 76000/RRT(kΩ)). This exceeds typical 1MHz max. May be:
- Incorrect resistor value (should be ~150k for 500kHz)
- Different part number with different formula
- Verify LM516x exact MPN before fabrication

**INFO:** Pin 6 (PGOOD) is unconnected (no-connect in netlist). Should be pulled up or monitored for fault detection.

## 8. TVS Clamp Analysis

| Ref | Part | Location | Vrwm | Vbr | Vclamp | Issue |
|-----|------|----------|------|-----|--------|-------|
| D4 | SMCJ78A | BATT_V rail | 78V | 86.7–95.8V | ~105V @ Ipp | |
| D15–D17 | SMCJ90A | Phase A/B/C | 90V | 100–111V | ~120V @ Ipp | |
| D18 | BZG05C75 | U5 VIN reg | 75V zener | | | |

### Transient Protection Margin
```
System: 72V nominal, ±100V survival
DRV8353 VM max: 100V (abs max)
FET Vds max: 100V

SMCJ78A at 100V transient:
  - Breakdown starts at ~87–96V
  - Clamp voltage at 100A pulse: ~105–120V
  - May NOT protect 100V-rated parts during >100V surge
```

**CRITICAL:** During a +100V transient, SMCJ78A clamp voltage (~105–130V) may exceed the 100V absolute maximum of both the DRV8353 and FETs. Consider:
- Higher voltage TVS (e.g., SMCJ85A or SMCJ100A) with tighter margin
- Additional input LC filter to slow transient rise time
- Verify transient energy and TVS clamping with actual surge testing

## 9. Open Items / Placeholders

| Item | Location | Issue |
|------|----------|-------|
| C47, C48, C49 | Motor_Driver sheet | Value = "?" (DNP? need value) |
| U4 (LM516x) | Power sheet | No MPN specified; LM5161 (100V) vs LM5160 (65V) critical for 72V operation |
| R30 (24.9k) | LM516x RT resistor | Switching frequency may be out of recommended range |
| R9/R10 (OVLO) | LM5069 | 37.5V threshold incompatible with 72V system |
| Shunt pulse rating | R12/R13/R14 | 10W peak power at 100A must be within pulse rating of selected 2512 resistor |
| LDO heatsinking | U11 (AMS1117) | May need thermal relief / copper pour on SOT-223 pad |

## 10. Recommendations

### CRITICAL (must fix before fab)
1. **Fix OVLO divider** — recalculate R9/R10 for 80V OVLO threshold: R9 ≈ 243k
2. **Select LM5161** (100V variant) for the buck converter, not LM5160 (65V)
3. **Verify TVS protection margin** — SMCJ78A clamping may not protect 100V-rated FETs/DRV8353 during +100V surge
4. **Assign C47–C49 values** or confirm DNP
5. **Verify AMS1117 thermal budget** — total 3.3V load must be <350mA or add heatsinking / use switching reg

### WARNING (review before fab)
6. **LM516x RT resistor** — verify correct frequency; 24.9k may be wrong
7. **TLV70233 load** — keep <120mA at 60°C ambient to avoid Tj >125°C
8. **Shunt pulse rating** — verify 1mΩ 2512 can handle 10W pulses
9. **DRV8353 IDRIVE setting** — select appropriate resistor for ~0.5A to balance switching loss vs EMI
10. **PGOOD** (U4 pin 6) is floating — should be pulled up for fault monitoring

### INFO / SUGGESTION
11. Hot-swap timer (264µs) > charge time (145µs) — design OK for 330µF bulk
12. FET Tj ~96°C at 12A with moderate gate drive — acceptable, 79°C margin to 175°C
13. DRV8353 Tj ~73°C — comfortable margin
14. Shunt CSA has no external RC filter — relies on DRV8353 internal filter; verify noise immunity at 45kHz PWM
15. Add TVS clamp voltage derating analysis for 85°C operation (Vbr increases with temp)

## 11. Thermal Math Summary

| Component | Dissipation | RθJA | Tj @ 60°C Ta | Limit | Margin |
|-----------|------------|------|-------------|-------|--------|
| TPH3R70APL × 6 | 0.9W each (IDRIVE=0.25A) | 40°C/W | 96°C | 175°C | 79°C |
| DRV8353 | 0.43W | 30°C/W | 73°C | 125°C | 52°C |
| IPT015N10N5 × 2 | 0.22W each | 62 K/W | 73°C | 175°C | 102°C |
| AMS1117 @ 500mA | 0.85W | 90°C/W | **136°C** | 125°C | **–11°C** |
| TLV70233 @ 200mA | 0.34W | 220°C/W | **135°C** | 125°C | **–10°C** |
| Shunt 1mΩ @ 12A | 0.144W | — | — | 1–2W cont | OK |
| Shunt 1mΩ @ 100A | 10W peak | — | — | pulse dep. | **Verify** |
