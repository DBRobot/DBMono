# Protection Guidelines Reference

## ESD Protection Levels by Interface

### Recommended Protection Levels
| Interface      | Standard        | Contact Discharge | Air Discharge | Notes                              |
|----------------|-----------------|-------------------|---------------|------------------------------------|
| USB             | IEC 61000-4-2   | +/-8kV            | +/-15kV       | Required for certification         |
| CAN / CAN FD    | ISO 7637        | +/-8kV HBM        | —             | Automotive/industrial standard     |
| Ethernet        | IEC 61000-4-2   | +/-8kV            | +/-15kV       | Often with magnetics isolation     |
| SPI (external)  | IEC 61000-4-2   | +/-4kV            | +/-8kV        | If connector-exposed               |
| I2C (external)  | IEC 61000-4-2   | +/-4kV            | +/-8kV        | If connector-exposed               |
| GPIO (external) | —               | +/-2kV HBM        | +/-4kV        | Minimum for board-to-board         |
| Encoder         | IEC 61000-4-2   | +/-4kV            | +/-8kV        | Exposed cable = ESD risk           |
| Power input     | —               | N/A               | N/A           | Use TVS, not ESD diodes            |

### Internal-Only Interfaces
- Board-internal SPI/I2C/UART between ICs on the same PCB typically do not need ESD protection
- Exception: long flex cable connections or board-to-board connectors

## TVS Diode Selection

### Selection Criteria
1. **Working voltage (Vwm)**: Vwm >= max operating voltage of the protected rail
   - Include tolerance: Vwm >= Vnom * 1.1
2. **Breakdown voltage (Vbr)**: Vbr > Vwm (typically Vbr = 1.1 * Vwm)
3. **Clamping voltage (Vc)**: Vc < Vabs_max of the protected IC
   - **Critical rule**: Vc at peak pulse current must be BELOW the absolute maximum rating of every IC on the protected net
4. **Peak pulse current (Ipp)**: Must handle expected surge current
5. **Capacitance**: Low capacitance (<1pF) for high-speed data lines (USB, CAN)
   - Higher capacitance OK for power rails

### TVS Selection Table (Common Rails)
| Protected Rail | Vwm (min) | Vbr (typ) | Vc (max) | Example Part     |
|----------------|-----------|-----------|----------|------------------|
| 3.3V           | 3.6V      | 4.0V      | 7.0V     | PESD3V3S1UL      |
| 5.0V           | 5.5V      | 6.1V      | 10.5V    | PESD5V0S1UL      |
| 12V            | 13.3V     | 14.7V     | 23.5V    | SMAJ12A          |
| 24V            | 26.7V     | 29.5V     | 42.1V    | SMAJ24A          |
| 48V            | 53.3V     | 58.9V     | 77.4V    | SMAJ48A          |
| USB D+/D-      | 5.0V      | 6.0V      | 12V      | TPD2E2U06        |
| CAN H/L        | 24V       | 26.7V     | 38.9V    | PESD1CAN         |

### Bidirectional vs Unidirectional
- **Unidirectional**: For DC power rails (anode to ground, cathode to protected rail)
- **Bidirectional**: For signal lines that swing above and below ground (CAN, RS-485)

## Fuse and Polyfuse Selection

### Fuse Sizing Rules
1. **Rated current**: I_rated >= I_normal * 1.25 (25% margin above normal operating current)
2. **Blow current**: I_blow should trip at < 2x I_rated for safety
3. **Voltage rating**: V_rated >= max system voltage
4. **I2t rating**: Must handle inrush current without nuisance tripping
   - I2t_fuse > I2t_inrush
5. **Breaking capacity**: Must safely interrupt maximum fault current

### Polyfuse (PTC Resettable Fuse) Selection
1. **Hold current (Ihold)**: Ihold >= I_normal at max ambient temperature
   - Derate: Ihold decreases ~5% per 10C above 25C
2. **Trip current (Itrip)**: Itrip < I_fault_min (must trip before damage occurs)
3. **Max voltage**: V_max >= circuit voltage
4. **Time to trip**: Check at expected fault current — may be seconds to minutes
5. **Resistance**: R_min to R_max — adds series resistance to the circuit

### When to Use What
| Scenario                     | Recommendation          | Why                            |
|------------------------------|-------------------------|--------------------------------|
| Main power input             | Fuse (non-resettable)   | One-time protection, low R     |
| USB port power               | Polyfuse                | User may repeatedly plug in    |
| Motor power                  | Fuse + electronic trip  | High current, fast response    |
| Sensor/peripheral power      | Polyfuse                | Self-recovering, low current   |
| Battery charging             | Fuse + BMS              | Safety critical                |

## Reverse Polarity Protection

### Methods Comparison
| Method                    | Voltage Drop | Current Limit | Cost  | Complexity |
|---------------------------|-------------|---------------|-------|------------|
| Series Schottky diode     | 0.3-0.5V    | By diode      | Low   | Simple     |
| P-channel MOSFET          | I*Rds(on)   | By FET        | Med   | Simple     |
| Ideal diode controller    | ~20mV       | By FET        | Med   | Medium     |
| Full bridge rectifier     | 0.6-1.0V    | By diode      | Low   | Simple     |

### P-FET Reverse Polarity Circuit
- Source to input, drain to load, gate to ground (through the load ground)
- **Vgs rating**: Must exceed max input voltage
- **Rds(on)**: Size for acceptable voltage drop at max current
- **Body diode**: Conducts during reverse polarity, blocking current flow

### Ideal Diode Controller (e.g., LM66100)
- Ultra-low forward voltage drop (~20mV)
- Fast reverse blocking (< 1us)
- Good for battery-powered or efficiency-critical designs

## Hot-Swap / Inrush Current Limiting

### Inrush Current Sources
1. **Bulk capacitors**: I_inrush = C * dV/dt (can be 10-100A peak)
2. **DC-DC converter input**: Soft-start helps but initial cap charging is still high
3. **Motor windings**: Low DC resistance = high inrush

### Hot-Swap Controller (e.g., LM5069)
- **Sense resistor**: R_sense = V_sense_threshold / I_limit
- **Gate capacitor**: Controls dV/dt slew rate
  - C_gate = I_limit * t_ramp / V_gate_drive
- **Undervoltage lockout (UVLO)**: Set below minimum operating voltage
- **Overvoltage protection (OVP)**: Set above maximum operating voltage
- **Power good delay**: Ensures output is stable before downstream enable

### Design Checks
1. FET SOA (Safe Operating Area): During inrush, FET operates in linear region
   - Power dissipation during ramp: P = Vin * I_inrush / 2 (triangle approximation)
   - Duration: t_ramp
   - Energy: E = P * t_ramp
   - Verify against FET SOA curve at operating Vds
2. Sense resistor power: P_sense = I_max^2 * R_sense (continuous)

## Protection Component Placement

### General Rules
1. **TVS/ESD diodes**: Place as close to the connector as possible
   - Signal path: Connector -> TVS -> series resistor -> IC
   - Minimize trace length between connector and TVS
2. **Fuses**: First component after power connector
   - Power path: Connector -> Fuse -> TVS -> Regulator
3. **Common-mode chokes**: After connector, before TVS (for differential interfaces)
4. **Ferrite beads**: After TVS, before IC (for power filtering)

### Power Input Protection Order
```
Connector -> Fuse -> Reverse polarity FET -> TVS -> Hot-swap controller -> Bulk cap -> Regulator
```

### Signal Protection Order
```
Connector -> ESD diode -> Series resistor (optional) -> IC pin
```
