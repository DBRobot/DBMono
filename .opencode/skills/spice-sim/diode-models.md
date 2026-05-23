# Diode Model Generation

How to build ngspice diode models from datasheet parameters.

## Model Structure

```spice
.model <NAME> D (
+   Is=<saturation_current>
+   N=<ideality_factor>
+   Rs=<series_resistance>
+   Bv=<breakdown_voltage>
+   Ibv=<current_at_breakdown>
+   Cjo=<zero_bias_capacitance>
+   Vj=<junction_potential>
+   M=<grading_coefficient>
+   Tt=<transit_time>
+)
```

## Mapping Datasheet to Model Parameters

| Model Parameter | Datasheet Parameter | Typical Range | Notes |
|---|---|---|---|
| `Is` | Reverse leakage / I_R | 1e-14 to 100u | Extrapolate from V_F vs I_F curve at low current |
| `N` | — | 1.0 - 2.0 | 1.0 = ideal, 1.05 = Schottky, 1.5-2.0 = LED/recombination |
| `Rs` | Slope of V_F at high I_F | 1m - 10 | Bulk resistance, dominates at high forward current |
| `Bv` | V_R, V_RRM, V_BR | 5 - 1000 | Maximum reverse voltage |
| `Ibv` | — | 100u - 5m | Current flowing at breakdown, use 100u for most diodes |
| `Cjo` | C_T, C_J at V_R=0 | 1p - 1n | Junction capacitance at zero bias |
| `Vj` | — | 0.3 - 0.7 | Built-in potential: 0.3-0.4 Schottky, 0.6-0.7 silicon |
| `M` | — | 0.33 - 0.5 | Grading: 0.5 abrupt junction, 0.33 graded |
| `Tt` | t_rr | 5n - 10u | Transit time, controls reverse recovery speed |

## Choosing Is and N

The forward voltage is controlled by Is and N through the diode equation:

```
V_F = N * Vt * ln(I_F / Is) + I_F * Rs
```

where Vt = kT/q = 26mV at 25C.

To match a specific V_F at a given I_F:
1. Pick N based on diode type (see table above)
2. Solve for Is: `Is = I_F / exp(V_F / (N * 0.026))`
3. This ignores Rs — refine by subtracting the Rs voltage drop at high current

### Quick Is Estimates by Diode Type

| Diode Type | Typical V_F | Typical Is | Typical N |
|---|---|---|---|
| Standard silicon rectifier | 0.7-1.0V | 1n - 100n | 1.5 - 2.0 |
| Fast recovery rectifier | 0.8-1.2V | 0.1u - 10u | 1.5 |
| Schottky (low voltage) | 0.3-0.5V | 1u - 100u | 1.02 - 1.10 |
| Schottky (high voltage) | 0.5-0.7V | 1u - 50u | 1.05 - 1.15 |
| Signal / switching | 0.6-0.7V | 1n - 10n | 1.5 - 1.8 |
| Zener (reverse mode) | N/A | 1p | 1.5 |
| LED red | ~1.8V | 1e-22 | 1.5 |
| LED green | ~2.2V | 1e-24 | 1.8 |
| LED blue/white | ~3.2V | 1e-30 | 2.0 |

## Zener Diodes

Zener diodes are modeled using the reverse breakdown parameters:

```spice
.model ZENER_5V1 D (
+   Is=1p N=1.5 Rs=10
+   Bv=5.1 Ibv=5m
+   Cjo=100p Vj=0.7 M=0.33
+)
```

- `Bv` = Zener voltage (the regulation voltage)
- `Ibv` = test current at Bv (from datasheet I_ZT)
- `Rs` = dynamic impedance Z_ZT at the test current

## TVS Diodes

TVS diodes are modeled similarly to Zeners, with `Bv` set to the breakdown voltage (V_BR), not the working voltage:

```spice
.model TVS_48V D (
+   Is=1p N=1.1 Rs=10m
+   Bv=53.3 Ibv=1m
+   Cjo=500p Vj=0.7 M=0.5
+)
```

- `Bv` = V_BR (breakdown, typically 1.1x V_WM)
- The clamping voltage V_C at peak pulse current depends on Rs and the current magnitude
- `Cjo` matters for signal-line TVS — check datasheet for capacitance

## Application Selection

| Use Case | Key Parameters | What Matters |
|---|---|---|
| Power rectification | Is, N, Rs, Bv, Tt | V_F accuracy, reverse recovery (Tt) |
| Flyback / freewheeling | Bv, Tt, Rs | Speed (low Tt), voltage rating |
| Voltage clamping | Bv, Ibv, Rs | Precise Bv, low Rs for tight clamping |
| ESD / surge protection | Bv, Rs, Cjo | Low Cjo for signal lines, low Rs for clamping |
| Signal switching | Is, N, Cjo, Tt | Low capacitance, fast switching |
| LED indicator | Is, N, Rs | V_F accuracy at operating current |
| Body diode (MOSFET) | Is, N, Rs, Bv, Tt | Bv matches FET, Tt for reverse recovery loss |

## Body Diode for MOSFETs

Always add an explicit body diode when simulating synchronous rectifiers or any circuit where the MOSFET body diode can conduct:

```spice
M1 drain gate source source NFET
D_body source drain BODY_D

.model BODY_D D (
+   Is=100n N=1.5 Rs=10m
+   Bv=60 Ibv=100u Tt=50n
+)
```

Set `Bv` to match the MOSFET's V_DS rating. Set `Tt` from the datasheet's body diode reverse recovery time (t_rr).

## Worked Examples

### Power Schottky Rectifier

For flyback diodes, buck converter freewheeling, reverse polarity protection.

Datasheet givens for a 10A, 150V Schottky:
- V_F = 0.85V at I_F = 10A
- I_R = 1mA at V_R = 150V
- C_T = 300pF at V_R = 4V
- t_rr = 15ns (Schottky, very fast)

```spice
.model SCHOTTKY_10A150V D (
+   Is=20u N=1.08 Rs=30m
+   Bv=150 Ibv=100u
+   Cjo=300p Vj=0.4 M=0.5 Tt=15n)

* Is derivation:
*   V_F = N*Vt*ln(I_F/Is) + I_F*Rs
*   0.85 = 1.08*0.026*ln(10/Is) + 10*0.03
*   0.55 = 0.028*ln(10/Is)
*   Is = 10/exp(0.55/0.028) = 10/exp(19.6) ~ 30u
*   Adjusted to 20u to match V_F at lower currents
```

### Small-Signal Switching Diode

For clamping, steering, detection circuits.

Datasheet givens for a common high-speed signal diode:
- V_F = 0.72V at I_F = 10mA
- I_R = 25nA at V_R = 20V
- C_T = 4pF at V_R = 0V
- t_rr = 12ns

```spice
.model SIGNAL_DIODE D (
+   Is=2.5n N=1.75 Rs=0.6
+   Bv=100 Ibv=100u
+   Cjo=4p Vj=0.5 M=0.44 Tt=12n)
```

### ESD Protection Diode (Unidirectional)

For protecting signal lines at connectors. Low capacitance is critical.

Datasheet givens for a 3.3V unidirectional ESD diode:
- V_WM = 3.3V (working voltage)
- V_BR = 6V min
- V_C = 12V at I_PP = 4A
- C = 0.4pF at V_R = 0V

```spice
.model ESD_3V3 D (
+   Is=1p N=1.1 Rs=1.5
+   Bv=6 Ibv=1m
+   Cjo=0.4p Vj=0.7 M=0.5)

* Rs derivation from clamping:
*   V_C = V_BR + I_PP * Rs
*   12 = 6 + 4*Rs => Rs = 1.5
```

### ESD Protection Diode (Bidirectional, CAN bus)

Two back-to-back zener-like structures. Model as two anti-series diodes:

```spice
* Bidirectional CAN ESD: V_WM = 24V, V_C = 40V
D_pos canbus mid ESD_CAN_UNI
D_neg mid canbus ESD_CAN_UNI

.model ESD_CAN_UNI D (
+   Is=1p N=1.1 Rs=0.5
+   Bv=27 Ibv=1m
+   Cjo=10p Vj=0.7 M=0.5)
```

### TVS for Power Rail (Unidirectional)

For clamping voltage transients on power input.

Datasheet givens for a 78V working voltage SMC TVS:
- V_WM = 78V
- V_BR = 86.7V min
- V_C = 134V at I_PP = 5.9A (for SMC package)
- C = 200pF

```spice
.model TVS_78V D (
+   Is=1p N=1.1 Rs=8m
+   Bv=86.7 Ibv=1m
+   Cjo=200p Vj=0.7 M=0.5)

* Rs from clamping: (134 - 86.7) / 5.9 = 8 Ohm
* This is approximate — TVS clamping is nonlinear
```

### LED (Indicator)

```spice
* Red: V_F ~ 1.8V at 10mA
.model LED_R D (Is=1e-22 N=1.5 Rs=2 Bv=5 Ibv=10u)

* Green: V_F ~ 2.2V at 10mA
.model LED_G D (Is=1e-24 N=1.8 Rs=3 Bv=5 Ibv=10u)

* Blue/White: V_F ~ 3.2V at 10mA
.model LED_BW D (Is=1e-30 N=2.0 Rs=5 Bv=5 Ibv=10u)
```

LED models are approximate — the exact Is needed depends on the specific forward voltage and emission wavelength. Adjust Is until `V_F` at your operating current matches the datasheet.

### Zener for Voltage Clamping

Datasheet givens for a 5.1V zener, SOD-323:
- V_Z = 5.1V at I_ZT = 5mA
- Z_ZT = 60 Ohm (dynamic impedance at test current)
- I_R = 2uA at V_R = 1V

```spice
.model ZENER_5V1 D (
+   Is=1p N=1.5 Rs=60
+   Bv=5.1 Ibv=5m
+   Cjo=80p Vj=0.7 M=0.33)

* Rs = Z_ZT sets how tightly the zener clamps
* Ibv = I_ZT ensures correct operating point
```
