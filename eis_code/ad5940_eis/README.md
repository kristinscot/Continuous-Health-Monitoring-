# AD5940 EIS – Biosensing Impedance Spectroscopy

nRF52840 + AD5940/AD5941 Electrochemical Impedance Spectroscopy (EIS) application
built on Zephyr RTOS. Performs a logarithmic frequency sweep and outputs complex
impedance data for Nyquist / Bode plotting.

## Project Structure

```
ad5940_eis/
├── CMakeLists.txt                     # Zephyr build system
├── prj.conf                          # Kernel + driver config
├── nrf52840dk_nrf52840.overlay        # Pin assignments (SPI, CS, RESET, GP0)
├── include/
│   ├── ad5940_regs.h                  # Full register map from Rev.G datasheet
│   ├── ad5940_spi.h                   # SPI driver API
│   └── ad5940_eis.h                   # EIS measurement API + config structs
└── src/
    ├── ad5940_spi.c                   # SPI protocol (2-transaction register access)
    ├── ad5940_eis.c                   # EIS engine (calibration, sweep, impedance calc)
    └── main.c                         # Application entry point
```

## Hardware Wiring (nRF52840-DK)

| AD5940 Pin | nRF52840 Pin | Function         |
|------------|-------------|------------------|
| SCLK       | P0.27       | SPI clock        |
| MOSI       | P0.26       | SPI data in      |
| MISO       | P1.15       | SPI data out     |
| CS         | P1.12       | Chip select      |
| RESET      | P1.11       | Hardware reset   |
| GP0        | P1.10       | Interrupt output |

Modify `nrf52840dk_nrf52840.overlay` to match your actual wiring.

### External Components

- **RCAL**: Connect a precision resistor (e.g. 200 Ω, 0.1%) between the
  RCAL0 and RCAL1 pins. Update `cfg.rcal_ohms` in `main.c` to match.
- **Sensor**: Connect your biosensor electrodes to CE0, RE0, SE0 (and
  optionally DE0). The switch matrix preset in `main.c` assumes a 4-wire
  configuration; change to `EIS_SWITCH_2WIRE` for 2-electrode setups.

## Building

```bash
# Set up Zephyr environment
source ~/zephyrproject/zephyr/zephyr-env.sh

# Build for nRF52840 DK
west build -b nrf52840dk/nrf52840 ad5940_eis

# Flash
west flash
```

## Configuration

All sweep parameters are set in `build_config()` in `main.c`:

| Parameter          | Default     | Description                           |
|-------------------|-------------|---------------------------------------|
| `freq_start_hz`   | 100 Hz      | Sweep start frequency                 |
| `freq_stop_hz`    | 100 kHz     | Sweep stop frequency                  |
| `num_points`      | 40          | Number of log-spaced frequency points |
| `excit_amplitude` | 33 (~10 mV) | Excitation voltage (11-bit code)      |
| `sensor_bias_v`   | 0.0 V       | DC bias across sensor                 |
| `rtia_sel`        | 5 kΩ        | TIA feedback resistor                 |
| `dft_num`         | 4096 pts    | DFT samples (more = better SNR)       |
| `rcal_ohms`       | 200 Ω       | External calibration resistor         |

### Choosing RTIA

The TIA feedback resistor (RTIA) determines the measurement range:

| RTIA     | Best for Z range   |
|----------|--------------------|
| 200 Ω    | < 1 kΩ             |
| 1 kΩ     | 1 – 5 kΩ           |
| 5 kΩ     | 5 – 25 kΩ          |
| 10 kΩ    | 10 – 50 kΩ         |
| 20 kΩ+   | Very high impedance |

Rule of thumb: RTIA ≈ Z_expected / 5.

### Switch Matrix Presets

Three presets are provided in `ad5940_eis.h`:

- **`EIS_SWITCH_RCAL`** – routes through the external calibration resistor
- **`EIS_SWITCH_4WIRE`** – 4-electrode biosensor (CE0/RE0/SE0)
- **`EIS_SWITCH_2WIRE`** – 2-electrode biosensor (CE0/SE0)

Modify the `.d_mux`, `.p_mux`, `.n_mux`, `.t_mux` fields if your electrodes
connect to different AD5940 pins.

## Output Format

Data is printed as CSV to the UART console:

```
# EIS Sweep Results (40 points)
# freq_hz, Zreal_ohm, Zimag_ohm, Zmag_ohm, phase_deg
100.00, 985.2300, -124.5600, 993.0700, -7.21
158.49, 982.1400, -198.3200, 1002.0200, -11.42
...
```

- Copy this into a CSV file
- Plot `-Zimag` vs `Zreal` for a **Nyquist plot**
- Plot `Zmag` and `phase_deg` vs `freq_hz` (log scale) for a **Bode plot**

## Architecture Notes

### SPI Protocol

The AD5940 uses a two-transaction SPI protocol (datasheet § "SPI Interface"):

1. **Set Address**: `[0x20] [ADDR_HI] [ADDR_LO]`  (CS toggles)
2. **Write**: `[0x2D] [D31..D0]`  or  **Read**: `[0x6D] [dummy] [D31..D0]`

### Impedance Calculation

Impedance is computed by comparing the DFT result of the unknown sensor
against the RCAL calibration result:

```
Z_unknown = R_cal × (DFT_rcal / DFT_sensor)
```

This is a complex division that yields both the real and imaginary components
of the impedance, accounting for phase shifts introduced by the measurement
chain.

### Power Mode Switching

Frequencies ≤ 80 kHz use low-power mode (16 MHz ACLK, 800 kSPS ADC).
Frequencies > 80 kHz automatically switch to high-power mode (32 MHz ACLK,
1.6 MSPS ADC). This happens transparently during the sweep.
