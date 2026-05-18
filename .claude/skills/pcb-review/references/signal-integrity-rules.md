# Signal Integrity Rules Reference

## I2C Bus Design

### Pull-Up Resistor Calculation
The pull-up resistor value must satisfy two constraints:

**Minimum Rp (current sink limit)**:
```
Rp_min = (VCC - VOL_max) / IOL
```
- VOL_max = 0.4V (standard I2C)
- IOL = 3mA (standard mode), 6mA (fast mode)

| VCC   | Rp_min (Std Mode, 3mA) | Rp_min (Fast Mode, 6mA) |
|-------|------------------------|-------------------------|
| 3.3V  | 967 ohm                | 483 ohm                 |
| 5.0V  | 1.53k ohm              | 767 ohm                 |

**Maximum Rp (rise time limit)**:
```
Rp_max = tr / (0.8473 * Cb)
```
- tr = rise time: 1000ns (std), 300ns (fast), 120ns (fast+)
- Cb = total bus capacitance (device + trace + pull-up parasitic)

| Bus Capacitance | Rp_max (100kHz) | Rp_max (400kHz) | Rp_max (1MHz)  |
|-----------------|-----------------|-----------------|----------------|
| 50pF            | 23.6k ohm       | 7.1k ohm        | 2.8k ohm       |
| 100pF           | 11.8k ohm       | 3.5k ohm        | 1.4k ohm       |
| 200pF           | 5.9k ohm        | 1.8k ohm        | 708 ohm        |
| 400pF           | 2.9k ohm        | 885 ohm         | 354 ohm        |

### Common I2C Pull-Up Values
| VCC   | Bus Speed  | Typical Rp | Notes                           |
|-------|------------|------------|---------------------------------|
| 3.3V  | 100kHz     | 4.7k ohm   | Most common default             |
| 3.3V  | 400kHz     | 2.2k ohm   | Fast mode standard              |
| 3.3V  | 1MHz       | 1k ohm     | Check device IOL rating         |
| 5.0V  | 100kHz     | 4.7k ohm   | Standard                        |
| 5.0V  | 400kHz     | 2.2k ohm   | Fast mode standard              |

### I2C Design Checklist
- [ ] Pull-up resistors present on SDA and SCL
- [ ] Pull-up value appropriate for bus speed and capacitance
- [ ] Pull-ups connected to correct VCC rail (not mixed voltage levels without level shifter)
- [ ] No more than one set of pull-ups on the bus (avoid doubled pull-ups from multiple modules)
- [ ] Bus capacitance < 400pF (standard I2C limit)
- [ ] Address conflicts checked (no two devices at same address)

## SPI Bus Design

### Signal Roles
| Signal | Direction      | Notes                                    |
|--------|----------------|------------------------------------------|
| SCK    | Master -> Slave | Clock — must have clean edges            |
| MOSI   | Master -> Slave | Master Out, Slave In                     |
| MISO   | Slave -> Master | Master In, Slave Out — tri-state when CS high |
| CS/SS  | Master -> Slave | Active low chip select — one per slave   |

### SPI Design Rules
1. **Chip select**: Each slave needs its own CS line (no shared CS for different devices)
2. **MISO bus contention**: When multiple slaves share MISO, each must tri-state when CS is high
   - Verify each slave datasheet confirms tri-state behavior
   - If a device doesn't tri-state, add a buffer or use separate SPI bus
3. **Series resistors**: 33-100 ohm series on SCK and MOSI for EMI reduction at high speeds
4. **Maximum frequency**: Limited by slowest device on the bus
5. **Clock polarity/phase (CPOL/CPHA)**: Must match between master and all slaves
6. **Trace length**: Keep < 10cm for clock speeds > 10MHz

### SPI Speed vs Trace Considerations
| SPI Clock  | Max Trace Length | Series Resistor | Notes                    |
|------------|------------------|-----------------|--------------------------|
| < 1MHz     | 30cm+            | Optional        | Relaxed requirements     |
| 1-10MHz    | 15cm             | 33 ohm          | Good practice            |
| 10-50MHz   | 5cm              | 22-33 ohm       | Impedance matching helps |
| > 50MHz    | 2cm              | Matched         | Controlled impedance req |

## CAN / CAN FD Bus Design

### Termination
- **Standard termination**: 120 ohm at each end of bus (total 60 ohm between CANH and CANL)
- **Split termination**: 2x 60 ohm with 4.7nF capacitor to ground at center tap
  - Better common-mode noise rejection
  - Recommended for CAN FD

### CAN Termination Check
```
If node is at END of bus:     -> 120 ohm termination required
If node is MIDDLE of bus:     -> NO termination (high impedance)
If node is the ONLY device:   -> May need termination on both ends (240 ohm total)
```

### CAN Bus Design Rules
1. **Transceiver**: Must be CAN FD capable if using CAN FD protocol
2. **Common-mode choke**: Recommended for industrial/automotive — place between transceiver and connector
3. **ESD protection**: PESD1CAN or equivalent on CANH and CANL at connector
4. **Standby pin**: Pull high for normal operation, or connect to MCU for sleep mode control
5. **VIO pin** (if present): Connect to MCU I/O voltage level (3.3V or 5V)
6. **Bus stub length**: Keep < 30cm at 500kbps, < 10cm at 1Mbps, < 3cm at 5Mbps (CAN FD)

### CAN Transceiver Decoupling
- 100nF ceramic on VCC, as close to pin as possible
- 4.7uF bulk if transceiver is far from main supply
- VIO pin (if present): 100nF ceramic

## USB Design

### USB 2.0 Signal Requirements
| Parameter               | Full Speed (12Mbps) | High Speed (480Mbps) |
|-------------------------|---------------------|----------------------|
| Differential impedance  | 90 ohm +/- 15%     | 90 ohm +/- 15%      |
| Single-ended impedance  | 45 ohm +/- 15%     | 45 ohm +/- 15%      |
| Series resistors        | Optional 22-33 ohm | 45 ohm (HS only)    |
| Max trace length        | 15cm                | 5cm                  |
| D+/D- matching          | +/- 2mm             | +/- 0.5mm            |

### USB Type-C Specific
1. **CC pins**: Must have proper pull-down resistors for device mode
   - 5.1k ohm to ground on each CC pin (for device/UFP role)
2. **VBUS decoupling**: 4.7-10uF ceramic on VBUS
3. **ESD protection**: On D+, D-, CC1, CC2, and VBUS
4. **Shield grounding**: Connect shield to chassis ground through RC filter (1M + 4.7nF) or direct

### USB Design Checklist
- [ ] D+ and D- routed as differential pair
- [ ] Series resistors present (22-33 ohm for FS)
- [ ] CC pull-down resistors (5.1k for device mode)
- [ ] VBUS protection (TVS diode)
- [ ] D+/D- ESD protection
- [ ] Proper decoupling on VBUS and VCC

## General Signal Rules

### Reset and Enable Pins
| Pin Type      | Required Termination       | Value          | Notes                         |
|---------------|----------------------------|----------------|-------------------------------|
| Active-low reset (/RST) | Pull-up to VCC    | 10k-100k ohm  | + 100nF cap for noise immunity |
| Active-high enable (EN) | Pull-up or pull-down | 10k-100k ohm | Match desired default state    |
| Active-low enable (/EN) | Pull-up to VCC    | 10k-100k ohm  | Ensures device ON at power-up  |
| Chip select (/CS)       | Pull-up to VCC    | 10k ohm        | Keeps device deselected        |

### Unused IC Pins
**Always check the datasheet** for each unused pin. Common recommendations:
| Pin Type              | Typical Handling                | Danger if Wrong          |
|-----------------------|---------------------------------|--------------------------|
| Unused input          | Tie to VCC or GND via resistor  | Floating = oscillation   |
| Unused output         | Leave unconnected               | Usually safe             |
| Unused analog input   | Tie to GND or AGND              | Noise injection          |
| Unused open-drain     | Leave unconnected or pull-up    | Usually safe             |
| NC (no connect)       | Check datasheet!                | Some NC pins ARE connected internally |

### Analog/Digital Separation
1. **Ground planes**: Star ground or split ground between analog and digital sections
2. **Power rails**: Separate AVDD and DVDD with ferrite bead or inductor
3. **Signal routing**: Do not route digital signals under/over analog components
4. **ADC inputs**: Low-pass filter before ADC pin (RC, fc < Nyquist/2)
5. **Reference voltages**: Decoupled with 100nF + 10uF, short traces

### Common Pull-Up/Pull-Down Values by Protocol
| Protocol / Signal | Pull Direction | Typical Value | Notes                    |
|-------------------|----------------|---------------|--------------------------|
| I2C SDA/SCL       | Pull-up        | 2.2k-4.7k     | Speed dependent          |
| SPI CS            | Pull-up        | 10k            | Keeps deselected         |
| CAN STB           | Pull-down      | 10k            | Normal mode default      |
| UART RX           | Pull-up        | 10k            | Prevents false start bit |
| Reset             | Pull-up        | 10k-100k       | + filter cap             |
| Boot config       | Pull-down      | 10k            | Default boot mode        |
| Interrupt (act-low)| Pull-up       | 10k            | Default inactive         |
| LED drive         | Series resistor | (V-Vf)/If     | Typically 330-1k         |

### Maximum Bus Capacitance
| Bus Type   | Max Capacitance | Notes                                |
|------------|-----------------|--------------------------------------|
| I2C        | 400pF           | Standard; 550pF with Fast-mode Plus  |
| SPI        | ~100pF          | Practical limit for clean edges      |
| CAN        | ~N/A            | Transceiver handles, but stub < spec |
| UART       | ~2500pF         | At 115200 baud                       |
