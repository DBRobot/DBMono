# Power Design Rules Reference

## Decoupling Strategy

### Per-Pin Decoupling
- **100nF ceramic (X7R or X5R)** on every VDD/VCC pin, placed as close to pin as possible
- **1uF ceramic** per power rail entry point to IC
- **10uF bulk** per voltage rail (at regulator output)
- **100uF+ electrolytic or polymer** at power input for energy storage

### Ceramic Capacitor Voltage Derating
Ceramic capacitors lose effective capacitance under DC bias. Apply these derating factors:

| Rated Voltage | Applied Voltage (% of rated) | Effective Capacitance (X5R) | Effective Capacitance (X7R) |
|---------------|------------------------------|-----------------------------|-----------------------------|
| 6.3V          | 50% (3.15V)                  | ~60% of nominal             | ~75% of nominal             |
| 10V           | 50% (5V)                     | ~55% of nominal             | ~70% of nominal             |
| 16V           | 50% (8V)                     | ~50% of nominal             | ~65% of nominal             |
| 25V           | 50% (12.5V)                  | ~45% of nominal             | ~60% of nominal             |
| 50V           | 50% (25V)                    | ~40% of nominal             | ~55% of nominal             |

**Rule of thumb**: Use capacitors rated at least 2x the applied voltage for X5R, 1.5x for X7R.

### Capacitor Dielectric Selection
| Dielectric | Temp Range    | Tolerance | Use Case                        |
|------------|---------------|-----------|----------------------------------|
| C0G/NP0    | -55 to +125C  | +/-5%     | Timing, filters, precision       |
| X7R        | -55 to +125C  | +/-15%    | General decoupling, bulk         |
| X5R        | -55 to +85C   | +/-15%    | Space-constrained decoupling     |
| Y5V        | -30 to +85C   | +22/-82%  | **Avoid** — massive derating     |

## Buck Converter Design Checklist

### Inductor Selection
1. **Inductance value**: L = (Vin_max - Vout) * Vout / (Vin_max * fsw * delta_IL)
   - delta_IL = target ripple current, typically 20-40% of Iout_max
2. **Saturation current (Isat)**: Must exceed Iout_max + delta_IL/2
   - Include margin: Isat > 1.2 * (Iout_max + delta_IL/2)
3. **DC resistance (DCR)**: Lower is better. Power loss = Iout_rms^2 * DCR
4. **Core material**: Ferrite for high frequency (>200kHz), powdered iron for high current

### Output Capacitor
1. **Capacitance**: C_out >= delta_IL / (8 * fsw * delta_Vout)
   - delta_Vout = target output ripple voltage
2. **ESR requirement**: ESR < delta_Vout / delta_IL
   - Low-ESR ceramic caps preferred for high-frequency converters
   - Electrolytic may be needed for bulk energy storage
3. **Voltage rating**: >= 1.5x Vout (accounting for derating)
4. **RMS ripple current rating**: Must exceed delta_IL / sqrt(12)

### Input Capacitor
1. **Capacitance**: C_in >= Iout * D * (1-D) / (fsw * delta_Vin)
   - D = duty cycle = Vout / Vin
2. **RMS ripple current**: Iin_rms = Iout * sqrt(D * (1-D))
   - This is often the limiting factor — check cap RMS current rating
3. **Voltage rating**: >= 1.5x Vin_max

### Feedback Resistor Divider
1. **Output voltage**: Vout = Vref * (1 + R_top / R_bottom)
2. **Current through divider**: I_div = Vout / (R_top + R_bottom)
   - Keep between 10uA and 1mA (too low = noise susceptible, too high = wasted power)
3. **Tolerance**: Use 1% resistors minimum for voltage accuracy
4. **Feedforward capacitor**: May be needed for compensation — check datasheet app note

### Compensation Network
- **Type II**: Most common for voltage-mode buck converters
- **Type III**: For current-mode or when additional phase boost needed
- **Always verify**: Check the regulator datasheet for recommended compensation values
- **Stability criteria**: Phase margin > 45 degrees, gain margin > 10dB

## LDO Design Rules

### Dropout Voltage
- Pdiss = (Vin - Vout) * Iload
- **Thermal check**: Tj = Ta + Pdiss * theta_JA
  - Tj must be < Tj_max (typically 125C or 150C)

### Common Package Thermal Resistance
| Package     | Theta_JA (C/W) | Max Pdiss at 25C (Tj=125C) |
|-------------|-----------------|----------------------------|
| SOT-23-5    | 200-250         | 0.4-0.5W                   |
| SOT-223     | 80-120          | 0.8-1.25W                  |
| DPAK        | 40-60           | 1.7-2.5W                   |
| TO-220      | 25-40           | 2.5-4.0W                   |

### LDO Stability
- **Output capacitor ESR**: Many LDOs require specific ESR range for stability
  - Ceramic-cap-stable LDOs: ESR < 100 mohm OK
  - Older LDOs: May require ESR between 0.1 and 1 ohm (use tantalum or electrolytic)
- **Minimum load**: Some LDOs oscillate with no load — check datasheet
- **Input capacitor**: Typically 1uF minimum, placed close to LDO input pin

## FET Thermal Analysis

### Power Dissipation Calculation
- **Conduction loss**: P_cond = Iload^2 * Rds(on) at operating Tj
  - Rds(on) increases with temperature: typically 1.5x at 125C vs 25C
- **Switching loss**: P_sw = 0.5 * Vin * Iload * (t_rise + t_fall) * fsw
- **Gate drive loss**: P_gate = Qg * Vgs * fsw
- **Total**: P_total = P_cond + P_sw + P_gate

### Thermal Check
- Tj = Ta + P_total * theta_JA
- For PCB-mounted FETs, theta_JA depends heavily on copper area
- **Minimum copper area**: Refer to datasheet thermal pad recommendations

## Copper Current Capacity (IPC-2221)

### External Layers (1oz copper, 10C rise)
| Trace Width (mil) | Max Current (A) |
|--------------------|-----------------|
| 10                 | 1.0             |
| 20                 | 1.5             |
| 50                 | 2.5             |
| 100                | 4.0             |
| 200                | 6.5             |

### Internal Layers (1oz copper, 10C rise)
| Trace Width (mil) | Max Current (A) |
|--------------------|-----------------|
| 10                 | 0.6             |
| 20                 | 1.0             |
| 50                 | 1.7             |
| 100                | 2.8             |
| 200                | 4.5             |

**Note**: For 2oz copper, multiply current capacity by ~1.4x.

## Power Sequencing Guidelines

### Common Requirements
1. **MCU**: Core voltage before I/O voltage (if separate rails)
2. **ADC**: Analog supply (VDDA) should come up with or before digital supply
3. **Communication ICs**: Typically require VCC stable before enable
4. **Gate drivers**: Logic supply before high-voltage supply
5. **Always check**: Datasheet "Recommended Operating Conditions" and "Power Sequencing" sections

### Sequencing Methods
- **RC delay on enable pin**: Simple, low cost, imprecise
- **Power-good chaining**: Output PG of one regulator enables the next
- **Sequencer IC**: For complex multi-rail systems
