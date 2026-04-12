# MOSFET Model Generation

How to build ngspice Level 1 MOSFET models from datasheet parameters.

## Level 1 Model Structure

```spice
.model <NAME> NMOS (
+   Level=1
+   Vto=<threshold>
+   Kp=<transconductance>
+   Lambda=0.01
+   Rd=<drain_resistance>
+   Rs=<source_resistance>
+   Cgso=<gate_source_cap>
+   Cgdo=<gate_drain_cap>
+   Cbd=<drain_body_cap>
+   Cbs=0
+   Is=1e-14
+   Pb=0.8
+)
```

For P-channel, use `PMOS` and negate `Vto`.

## Mapping Datasheet to Model Parameters

| Model Parameter | Datasheet Parameter | How to Get It |
|---|---|---|
| `Vto` | V_GS(th) | Use the typical value from the threshold voltage spec |
| `Kp` | Derived from I_D vs V_GS | See calculation below |
| `Rd` | R_DS(on) / 2 | Split total on-resistance evenly between drain and source |
| `Rs` | R_DS(on) / 2 | Same as above |
| `Cgso` | C_iss | Input capacitance (gate-source + gate-drain) |
| `Cgdo` | C_rss | Reverse transfer capacitance (Miller cap) |
| `Cbd` | C_oss - C_rss | Output capacitance minus Miller |
| `Lambda` | — | Use 0.01 as default (channel-length modulation) |
| `Is` | — | Body diode saturation current, use 1e-14 default |
| `Pb` | — | Built-in potential, use 0.8 default |

## Calculating Kp (Transconductance Parameter)

From the MOSFET square-law equation in saturation:

```
I_D = (Kp / 2) * (V_GS - Vto)^2
```

Rearranging:

```
Kp = 2 * I_D / (V_GS - Vto)^2
```

Pick a point from the datasheet's output characteristics (I_D vs V_DS curves) or transfer characteristics (I_D vs V_GS) in the saturation region.

### Example

Datasheet says I_D = 80A at V_GS = 10V, V_GS(th) = 2.0V:

```
Kp = 2 * 80 / (10 - 2)^2
   = 160 / 64
   = 2.5
```

Level 1 models are coarse. Adjust Kp empirically so the model matches I_D at your operating V_GS. Start with the calculated value and scale it until the simulated I_D at your operating point matches the datasheet curve.

## Body Diode

Power MOSFETs have an intrinsic body diode. For synchronous rectification or any circuit where the body diode conducts, add it explicitly:

```spice
M1 drain gate source source NFET_MODEL
D_body source drain BODYDIODE_MODEL

.model BODYDIODE_MODEL D (
+   Is=100n N=1.5 Rs=10m
+   Bv=<match MOSFET Vds_max>
+   Ibv=100u Tt=50n
+)
```

The body diode `Tt` (transit time) controls reverse recovery behavior. Use 50-100ns for standard MOSFETs, 20-30ns for fast-recovery types.

## Capacitance Notes

Datasheet capacitances (C_iss, C_oss, C_rss) are **voltage-dependent** — they increase significantly at low V_DS. The values listed are typically at V_DS = 25V or 50V.

Level 1 models use fixed capacitances, so:
- For switching loss analysis, use capacitance values at the actual V_DS operating point
- For gate charge analysis, use values at higher V_DS (where most switching occurs)
- For rough estimates, the datasheet typical values are adequate

## Thermal Derating

R_DS(on) increases with temperature — typically 1.5x at 125C vs 25C. If simulating at elevated temperature, multiply Rd and Rs by the derating factor:

```spice
* Room temperature model
.model NFET_25C NMOS (Level=1 Vto=2.0 Kp=200 Rd=2.8m Rs=2.8m ...)

* 125C model (1.5x Rds(on))
.model NFET_125C NMOS (Level=1 Vto=1.8 Kp=200 Rd=4.2m Rs=4.2m ...)
```

Note that Vto typically decreases ~2mV/C with temperature.

## Worked Examples by Category

### Power MOSFET (high current, low Rds(on))

Typical for motor drive bridges, synchronous buck FETs, load switches.

Datasheet givens for a 100V, 4mOhm NMOS in a PowerPAK package:
- V_GS(th) = 2.1V typ
- R_DS(on) = 4.1mOhm at V_GS = 10V
- I_D = 100A at V_GS = 10V (from transfer curve)
- C_iss = 2000pF, C_oss = 1050pF, C_rss = 50pF (at V_DS = 50V)

```spice
.model POWER_NFET NMOS (Level=1
+   Vto=2.1 Kp=150 Lambda=0.01
+   Rd=2.05m Rs=2.05m
+   Cgso=2n Cgdo=50p Cbd=1n Cbs=0
+   Is=1e-14 Pb=0.8)

* Kp derivation:
*   Kp = 2 * 100 / (10 - 2.1)^2 = 200/62.4 = 3.2
*   Scaled to ~150 to match I_D at operating point (Level 1 empirical fit)
```

### Small-Signal N-Channel (logic-level switch)

Typical for level shifters, load enables, LED drives. SOT-23 package.

Datasheet givens for a 60V, 200mA NMOS:
- V_GS(th) = 1.3V typ
- R_DS(on) = 1 Ohm at V_GS = 4.5V
- C_iss = 30pF, C_rss = 8pF, C_oss = 15pF (at V_DS = 25V)

```spice
.model SMALL_NFET NMOS (Level=1
+   Vto=1.3 Kp=0.1 Lambda=0.02
+   Rd=0.5 Rs=0.5
+   Cgso=30p Cgdo=8p Cbd=7p Cbs=0
+   Is=1e-14 Pb=0.8)
```

### Small-Signal P-Channel (high-side switch, enable logic)

SOT-23 P-channel MOSFET. Note the negative Vto.

Datasheet givens for a -50V, -130mA PMOS:
- V_GS(th) = -1.2V typ
- R_DS(on) = 10 Ohm at V_GS = -4.5V
- C_iss = 28pF, C_rss = 7pF

```spice
.model SMALL_PFET PMOS (Level=1
+   Vto=-1.2 Kp=0.05 Lambda=0.02
+   Rd=5 Rs=5
+   Cgso=28p Cgdo=7p Cbd=10p Cbs=0
+   Is=1e-14 Pb=0.8)
```

### SOT-23 Pinout Warning

SOT-23 MOSFET pinouts vary between manufacturers. Always verify against the specific datasheet:
- Many N-channel SOT-23: pin 1 = Gate, pin 2 = Source, pin 3 = Drain
- Some P-channel SOT-23: pin 1 = Source, pin 2 = Gate, pin 3 = Drain
- Others swap Gate and Source

This affects the schematic symbol, not the SPICE model, but is a common source of errors.

---

## BJT Models

BJTs use the Gummel-Poon model. For most simulation purposes, a simplified parameter set works.

### BJT Model Structure

```spice
.model <NAME> NPN (
+   Is=<saturation_current>
+   Bf=<forward_current_gain>
+   Vaf=<early_voltage>
+   Rb=<base_resistance>
+   Rc=<collector_resistance>
+   Re=<emitter_resistance>
+   Cje=<base_emitter_cap>
+   Cjc=<base_collector_cap>
+   Tf=<forward_transit_time>
+   Tr=<reverse_transit_time>
+)
```

For PNP, use `PNP` and the same parameter names.

### Mapping Datasheet to BJT Parameters

| Model Parameter | Datasheet Parameter | Notes |
|---|---|---|
| `Is` | — | Saturation current, ~1e-14 for small signal |
| `Bf` | h_FE | Forward current gain at operating I_C |
| `Vaf` | V_A or Early voltage | ~100V for small signal, ~50V for power. Controls output impedance |
| `Rb` | — | Base spreading resistance, 10-100 Ohm |
| `Cje` | C_ib or C_be | Base-emitter capacitance, from datasheet or ~10-20pF |
| `Cjc` | C_ob or C_bc | Base-collector capacitance, from datasheet or ~2-5pF |
| `Tf` | f_T | Forward transit time = 1 / (2*pi*f_T) |

### Small-Signal NPN (general purpose switch/amplifier)

Typical SOT-23 NPN for biasing, level shifting, or open-collector outputs.

Datasheet givens:
- h_FE = 300 typ at I_C = 2mA
- V_CE(sat) = 0.1V at I_C = 10mA
- f_T = 300MHz
- C_ob = 2pF

```spice
.model NPN_SS NPN (
+   Is=1e-14 Bf=300 Vaf=100
+   Rb=10 Rc=1 Re=0.5
+   Cje=12p Cjc=2p
+   Tf=0.5n Tr=50n)

* Tf derivation: Tf = 1/(2*pi*300e6) = 0.53ns
```

### Small-Signal PNP

Same approach, use `PNP`:

```spice
.model PNP_SS PNP (
+   Is=1e-14 Bf=200 Vaf=80
+   Rb=20 Rc=2 Re=1
+   Cje=15p Cjc=5p
+   Tf=1n Tr=100n)
```

### When to Use a BJT vs Ideal Switch

For digital switching (saturated on/off), a BJT model is often overkill. Use a behavioral switch:

```spice
* Behavioral open-collector output
B_oc out 0 V = {if(V(base) > 0.7, 0.1, V(pullup))}
```

Use the full BJT model when you need:
- Accurate saturation voltage (V_CE(sat)) for power dissipation
- Frequency response / bandwidth
- Bias point analysis in linear amplifier circuits
- Turn-on/turn-off delays
