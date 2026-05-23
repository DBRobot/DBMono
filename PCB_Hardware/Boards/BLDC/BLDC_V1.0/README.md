# DBMono BLDC V1.0

This BLDC driver is designed as a general purpose brushless motor controller for robotics. Its focus is industrial grade performance at as low a cost as possible. Design requirements for this controller:
### Power
- Working voltage from 12-72V main input with daisy-chainable connectors
- Min 12A continuous current without cooling @60C
- 100A peak phase current
- Able to safely send regen current back to the bus or to a brake resistor
- Hot swap controller for safe hot plug and inrush control
- 5V logic input
- USBC logic input power delivery
- Short circuit protection
- Bidirectional reverse polarity protection
- Can survive +/- 100V transients

### Communication
- Daisy chain CANFD as the main communication interface
- USBC for configuration and live monitoring
- Dual SPI for up to 2 external encoders
- IO to support I2C, UART, ADC, TIM, and GPIO peripherals

### Sensing
- Onboard 21bit absolute magnetic encoder
- Onboard temperature sensor
- Optional off-board temperature sensor
- 4 Pin fan connector for PWM control and tachometer feedback

### Control Modes
- Sensorless FOC
- Position, velocity, and torque control
- Trajectory control
- Impedance control
- Anti-cogging compensation 
- Friction compensation
- Time synchronized commands
- Auto-tune

### Driver Parameters
- Switching  frequency of 45kHz

### Other
- ORing inputs for safe handling of multiple inputs
- EEPROM for configuration settings




## Gate Driver Design
The TI DRV8353SRTAR was selected for its low cost to performance and flexibility, as well as the high operating voltage. 
### FET Selection

#### Calculating Max $Q_g$ 
From 9.2.1.2.1 of the datasheet, the driver only supports fets that fall into the range given by the following equations:

![[Pasted image 20260510133448.png]] 

The design spec is up to 45kHz switching speed, and $I_{\text{VCP/VGLS}}$ is 25mA for all VM > 15V. While this design is technically specked to operate down to 12V, the target for performance is 24 and 48V motors. VM is fed by the input, so for the sake of this calculation the <15V scenario is ignored. 

**Trapezoidal Commutation**
$$
\begin{align}
&Q_g < \frac{I_{\text{VCP/VGLS}}}{f_{\text{PWM}}} \\
\\
&I_{\text{VCP/VGLS}} = 25mA \\
&f_{\text{PWM}} = 45kHz \\
\\
&Q_g < \frac{25 \times 10^{-3}\;A}{45 \times 10^3\;Hz}\\
&Q_g < 555\;nC
\end{align}
$$
**Sinusoidal Commutation**

$$
\begin{align}
&Q_g < \frac{I_{\text{VCP/VGLS}}}{3 \times f_{\text{PWM}}} \\
\\
&I_{\text{VCP/VGLS}} = 25mA \\
&f_{\text{PWM}} = 45kHz \\
\\
&Q_g < \frac{25 \times 10^{-3}\;A}{3 \times 45 \times 10^3\;Hz}\\
&Q_g < 185\;nC
\end{align}
$$

#### Calculating Max $R_{DS(ON)}$ 
From 9.2.1.2.3 in the datasheet, the overcurrent configuration is set via the equation bellow:

$$V_{DS\_OCP} > I_{max} \times R_{DS(ON)max}$$
As shown on the table in page 16 of the datasheet, the max setting for $V_{DS\_OCP}$ = 2V. Therefor:

$$
\begin{gathered}
R_{DS(ON)max} < \frac{V_{DS\_OCP}}{I_{max}} \\
\\
R_{DS(ON)max} < \frac{2V}{100A} \\
\\
R_{DS(ON)max} < 20m \Omega
\end{gathered}
$$

#### Selected FET - TPH3R70APL
This part was selected primarily for its thermal performance. In order to survive with 12A continuous current without a heat sink or other cooling solution, thermal performance is the most important aspect of fet selection. 

##### Min Rise Time
From the datasheet of the fet:

$$
Q_{gd}=14nC
$$

Then from the datasheet of the  DRV8353:
$$
\begin{array}{c c}
I_{DRIVEP} = \frac{Q_{gd}}{t_r} && I_{DRIVEN} = \frac{Q_{gd}}{t_f} \\
t_r > \frac{Q_{gd}}{I_{DRIVEP}} && t_f > \frac{Q_{gd}}{I_{DRIVEN}} \\
t_r > \frac{14nC}{1A} && t_f > \frac{14nC}{2A}\\
t_r > 14ns && t_f > 7ns
\end{array}
$$
##### Thermal Performance 
These calculations are done with no consideration for fans or other thermal management. This is done to establish the 12A no cooling baseline.

**Relevant Specs:**
$Q_g = 67nC < 185nC$
$T_{j\_max} = 175\degree C$ 
$R_{DS(ON)} \; @ \; T_{j\_max}= 9.25m\Omega$ 
$I_{peak} = I_D= 12 \times \sqrt 2 = 16.97A$
$t_r = 14ns$
$t_f = 19ns$

**Conduction Loss**
$$
\begin{gathered}
P_{cond} = I_{RMS}^2 \times R_{DS(ON)} \\
P_{cond} = (12A)^2 \times 0.00925 \Omega \\
P_{cond} = 1.33W
\end{gathered}
$$
**Switching Loss**
$$
\begin{gathered}
P_{sw} = \frac{1}{2} \times V_{BUS} \times I_D \times (t_r + t_f) \times f_{PWM} \\
P_{sw} = \frac{1}{2} \times 48V \times 16.97A \times  (14 \times 10^{-9} + 19 \times 10^{-9})s \times (45 \times 10^3) Hz \\
P_{sw} = 0.60W
\end{gathered}
$$

**Gate Charge Loss**
$$
\begin{gathered}
P_{gate} = Q_g \times V_{GS} \times f_{PWM} \\
P_{gate} = (67 \times 10^{-9})C \times 10V \times (45 \times 10^3)Hz \\
P_{gate} = 0.03W
\end{gathered}
$$

**Reverse Recovery Loss**

$$
\begin{gathered}
P_{rr} = Q_{rr} \times V_{BUS} \times f_{PWM} \\
P_{rr} = (74 \times 10^{-9})C \times 48V \times (45 \times 10^3)Hz \\
P_{rr} = 0.16W
\end{gathered}
$$
**Total Losses**
$$

P_{total} = 1.33 + 0.60 + 0.03 + 0.16 = 2.12W \\
$$
##### Copper Area Check
Using the total power loss of the FET, we can approximate the copper area needed to prevent the FET from failing. 

$$
\begin{gathered}
R_{\theta JA} < \frac{T_J - T_{ambient}}{P_{total}} \\
R_{\theta JA} < \frac{(175 - 60) \degree C}{2.12W} \\
R_{\theta JA} < 54.2 \degree C/W
\end{gathered}
$$

Application note Z8F80193346 from Infineon is used, as it is on a similar package, to sanity check the thermal limits given that there will be relatively little copper under each fet:
![[Pasted image 20260510212608.png]]

![[Pasted image 20260510212842.png]]
This implies that the board will need 12 thermal vias under the pad of the fet, with copper pour on both the top and bottom of them board. Single sided copper pour would be ill advised, but 2 layers of copper pour on the outer side of the board should be enough to keep thermals in check.

### Sense Amplifier Configuration
The DRV8353 uses shunt resistors to measure the current of each phase. To determine the resistance and gain values to use, the centered usable output voltage swing that the MCU can measure. Because a STM32G4 is used, $V_{VREF}=3.3V$.
$$
\begin{gathered}
V_O = (V_{VREF} - 0.25V) -\frac{V_{VREF}}{2} \\ \\
V_O = (3.3V-0.25V) - \frac{3.3V}{2} \\
V_O = 1.4V
\end{gathered}
$$

After calculating $V_O$, the resistance value $R$ can be calculated, where $A_V$ is the amplifier gain:

$$
\begin{aligned}
R &= \frac{V_O}{A_V \times I} & P_{SENSE} &> I_{RMS}^2 \times R 
\end{aligned}
$$

| | $A_V=5$ | $A_V=10$ | $A_V=20$ | $A_V=40$ |
|---|---|---|---|---|
| $R$ | $2.8m\Omega$ | $1.4m\Omega$ | $0.7m\Omega$ | $0.35m\Omega$ |
| $P_{SENSE}$ | $0.40W$ | $0.20W$ | $0.10W$ | $0.05W$ |
| $V_{DIFF}$ | $0.28V$ | $0.14V$ | $0.07V$ | $0.04V$ |

**Selected:** $A_V = 10$ with $R = 1m\Omega$. At $I_{max} = 100A$, $V_O = 1m\Omega \times 10 \times 100A = 1.0V$, keeping the amplifier output in the linear range with $0.4V$ of headroom in each direction. $1m\Omega$ is a standard value and $P_{SENSE} = 0.14W$ is easily handled by a 2512 package.

### DRV8353 Power Dissipation
Equations 26 and 27 from page 70 of the datasheet can be used to ensure that the gate driver itself does not overheat:

$$
\begin{aligned}
P_{VCP} &= I_{VCP} \times (V_{VM}+V_{VDRAIN}) & P_{VGLS} &= I_{VGLS} \times V_{VM} & P_{VM} &= I_{VM} \times V_{VM} \\
P_{VCP} &= 9mA \times (72V + 72V) & P_{VGLS} &= 9mA \times 72V & P_{VM} &= 15mA \times 72V \\
P_{VCP} &= 1.3W & P_{VGLS} &= 0.65W & P_{VM} &= 1.08W 
\end{aligned}
$$
$$
\begin{gathered}
P_{TOTAL} = 1.3W + 0.65W + 1.08W = 3.03W \\ 
R_{\theta JA} = 26.1 \\ \\
T_J = T_A + R_{\theta JA} \times P_{TOTAL} \\
T_J = 60 \degree C + (26.1 \times 3.03W) \\
T_J = 139 \degree C <150 \degree C
\end{gathered}
$$
### Input Bulk Capacitor

The DC link capacitor has two distinct jobs:

1. **Switching frequency bypass** (45kHz) — low-ESR MLCCs handle this trivially. Each 1206 MLCC handles $\sim 2A_{RMS}$ ripple. Only 3-4 are needed.
2. **Low-frequency energy storage** ($2\times$ electrical frequency, $\sim 100-1000$ Hz) — this drives the total capacitance. For a 12A motor at low speed, the DC link ripple current at $2\times f_e$ can be $4-6A_{RMS}$.

The DC bus current at 12A RMS phase current:

$$
I_{BUS} \approx \frac{3 \times V_{PHASE} \times I_{PHASE} \times \cos\phi}{V_{BUS} \times \eta}
\approx \frac{3 \times 26V \times 12A \times 0.9}{48V \times 0.9} \approx 20A
$$

Worst case switching-frequency ripple at $D=0.5$:

$$
C = \frac{I_{BUS} \times 0.25}{f_{PWM} \times \Delta V}
$$

| Condition | $I_{BUS}$ | $\Delta V=0.5V$ | $\Delta V=1V$ | $\Delta V=2V$ |
|---|---|---|---|---|
| 48V, 12A cont | 20A | $222\mu F$ | $111\mu F$ | $56\mu F$ |
| 72V, 12A cont | 14A | $156\mu F$ | $78\mu F$ | $39\mu F$ |
| 48V, 100A peak | 90A | $1000\mu F$ | $500\mu F$ | $250\mu F$ |

**Selected:**

| Part | Count | Value | Package | Purpose |
|------|-------|-------|---------|---------|
| C23 | 1 | $330\mu F$, 100V | Radial through-hole ($10\times 20$mm) | Bulk, handles $2\times f_e$ ripple |
| C25–C28 | 4 | $2.2\mu F$, 100V X7S | 1206 | Switching frequency bypass |
| CVM1 | 1 | $0.1\mu F$, 100V X7R | 0805 | Datasheet requirement at VM pin |

At 48V/12A: $\Delta V \approx 0.14V$ switching ripple, $\sim 3V$ peak at $2\times$ electrical frequency — well within DRV8353 headroom. At 100A peak transient: $\Delta V \approx 0.7V$. A single radial electrolytic is smaller than two 100V SMD equivalents, simpler to route, and handles the ripple current adequately since the battery sinks most of the low-frequency content and the MLCCs handle the 45kHz content.

### Hot-Swap Precharge (LM5069MM-2)

The LM5069MM-2 (U5) controls inrush via Rsns (R6 = 0.5mΩ, 2512) and Ctimer (C11 = 33nF, 0402). The pass FET is Q2 = IPT015N10N5 (100V/300A).

**Current limit** (Datasheet §6.5, V_sns_th = 50mV typical):

$$
I_{LIM} = \frac{V_{SNS\_TH}}{R_{SNS}} = \frac{50\text{mV}}{0.5\text{m}\Omega} = 100\text{A}
$$

**Inrush charge time** with C_load ≈ 340µF:

$$
t_{CHARGE} = \frac{C_{LOAD} \times V_{IN}}{I_{LIM}}
$$

| $V_{IN}$ | $t_{CHARGE}$ |
|----------|-------------|
| 48V | $340\mu F \times 48V / 100A = 163\mu s$ |
| 72V | $340\mu F \times 72V / 100A = 245\mu s$ |

**Fault timer** (Datasheet §7.3.2, I_timer = 85µA typical, V_timer_th = 1.25V):

$$
t_{FAULT} = \frac{C_{TIMER} \times V_{TIMER\_TH}}{I_{TIMER}} = \frac{33\text{nF} \times 1.25\text{V}}{85\mu\text{A}} = 485\mu\text{s}
$$

**Margin check** — t_fault > t_charge with >2× margin across all conditions:

| Condition | t_charge | t_fault | Margin |
|-----------|----------|---------|--------|
| 48V | 163µs | 485µs | 3.0× |
| 72V | 245µs | 485µs | 2.0× |
| 72V, I_timer max (110µA) | 245µs | 375µs | 1.5× |
