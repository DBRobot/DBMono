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

##### Thermal Performance Calculations
These calculations are done with no consideration for fans or other thermal management. This is done to establish the 12A no cooling baseline.

**Relevant Specs:**
$Q_g = 67nC < 185nC$
$T_{j\_max} = 175\degree C$ 
$R_{DS(ON)} \; @ \; T_{j\_max}= 9.25m\Omega$ 
$I_{peak} = I_D= 12 \times \sqrt 2 = 16.97A$
$t_r = 10ns$
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
P_{sw} = \frac{1}{2} \times 48V \times 16.97A \times  (10 \times 10^{-9} + 19 \times 10^{-9})s \times (45 \times 10^3) Hz \\
P_{sw} = 0.53W
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

P_{total} = 1.33 + 0.53 + 0.03 + 0.16 = 2.05W \\
$$
##### Copper Area Check
Using the total power loss of the FET, we can approximate the copper area needed to prevent the FET from failing. 

$$
\begin{gathered}
R_{\theta JA} < \frac{T_J - T_{ambient}}{P_{total}} \\
R_{\theta JA} < \frac{(175 - 60) \degree C}{2.05W} \\
R_{\theta JA} < 73.2 \degree C/W
\end{gathered}
$$

Application note Z8F80193346 from Infineon is used, as it is on a similar package, to sanity check the thermal limits given that there will be relatively little copper under each fet:
![[Pasted image 20260510212608.png]]

![[Pasted image 20260510212842.png]]

This implies that the board will need 12 thermal vias under the pad of the fet, with copper pour on both the top and bottom of them board. Single sided copper pour would be ill advised, but 2 layers of copper pour on the outer side of the board should be enough to keep thermals in check.