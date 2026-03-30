/**
 * @file ad5940_eis.h
 * @brief Electrochemical Impedance Spectroscopy (EIS) module for the AD5940.
 *
 * Provides a high-level API to configure and run a frequency sweep,
 * producing complex impedance data suitable for Nyquist / Bode plotting.
 */
#ifndef AD5940_EIS_H
#define AD5940_EIS_H

#include <stdint.h>
#include <stdbool.h>

/* Maximum number of frequency points in a single sweep. */
#define EIS_MAX_FREQ_POINTS  128

/* ---------- Configuration ------------------------------------------- */

/**
 * @brief Switch matrix configuration for the impedance measurement path.
 *
 * Describes which switches connect the excitation amplifier output,
 * feedback, and HSTIA inputs. Predefined presets are available for
 * RCAL calibration and typical biosensor electrode configurations.
 */
typedef struct {
    uint32_t d_mux;  /**< DSWFULLCON bitmask – excitation amp output */
    uint32_t p_mux;  /**< PSWFULLCON bitmask – excitation amp positive input */
    uint32_t n_mux;  /**< NSWFULLCON bitmask – excitation amp negative input */
    uint32_t t_mux;  /**< TSWFULLCON bitmask – HSTIA inverting input (include SWT_TRTIA!) */
} eis_switch_cfg_t;

/**
 * @brief Full EIS sweep configuration.
 */
typedef struct {
    /* Frequency sweep */
    float    freq_start_hz;     /**< Start frequency, e.g. 100 Hz */
    float    freq_stop_hz;      /**< Stop frequency, e.g. 100 kHz */
    uint16_t num_points;        /**< Number of frequency points (log-spaced) */

    /* Excitation */
    uint16_t excit_amplitude;   /**< Waveform generator amplitude code (11-bit, 0–2047).
                                 *   ~10 mV peak with gain=2 ≈ code 33. */
    float    sensor_bias_v;     /**< DC bias across sensor (VBIAS0 − VZERO0) in volts. */

    /* TIA */
    uint8_t  rtia_sel;          /**< HSRTIA_200 … HSRTIA_160K (auto-range if 0xFF) */

    /* DFT */
    uint8_t  dft_num;           /**< DFTNUM_4 … DFTNUM_16384 */

    /* ADC */
    uint8_t  pga_gain;          /**< ADCPGA_1 … ADCPGA_9 */

    /* Switch matrix for unknown impedance measurement */
    eis_switch_cfg_t sw_sensor;

    /* Switch matrix for RCAL calibration */
    eis_switch_cfg_t sw_rcal;

    /* RCAL value in ohms (external precision resistor on RCAL0/RCAL1) */
    float    rcal_ohms;
} eis_config_t;

/* ---------- Result -------------------------------------------------- */

/**
 * @brief Single-frequency impedance result.
 */
typedef struct {
    float freq_hz;
    float real_ohms;    /**< Zreal (Ω) */
    float imag_ohms;    /**< Zimag (Ω) – negative for capacitive */
    float mag_ohms;     /**< |Z| (Ω) */
    float phase_deg;    /**< Phase (degrees) */
} eis_point_t;

/**
 * @brief Complete sweep result set.
 */
typedef struct {
    uint16_t    count;
    eis_point_t points[EIS_MAX_FREQ_POINTS];
} eis_result_t;

/* ---------- Presets ------------------------------------------------- */

/**
 * @brief Preset switch config: RCAL calibration.
 *
 * Uses xSWFULLCON bitmasks (matching ADI library):
 * D = RCAL0 (SWD_RCAL0), P = RCAL0 (SWP_RCAL0),
 * N = RCAL1 (SWN_RCAL1), T = RCAL1 + TRTIA (SWT_RCAL1|SWT_TRTIA)
 */
#define EIS_SWITCH_RCAL \
    (eis_switch_cfg_t){ .d_mux = (1u<<0), .p_mux = (1u<<0), \
                        .n_mux = (1u<<9), .t_mux = (1u<<11)|(1u<<8) }

/**
 * @brief Preset switch config: 4-wire sensor on CE0/RE0/SE0.
 *
 * Matching ADI library: D=CE0, P=RE0, N=SE0, T=SE0LOAD+TRTIA
 */
#define EIS_SWITCH_4WIRE \
    (eis_switch_cfg_t){ .d_mux = (1u<<4), .p_mux = (1u<<4), \
                        .n_mux = (1u<<8), .t_mux = (1u<<4)|(1u<<8) }

/**
 * @brief Preset switch config: 2-wire sensor on CE0 and SE0.
 *
 * D=CE0, P=CE0 (feedback from output), N=SE0, T=SE0LOAD+TRTIA
 */
#define EIS_SWITCH_2WIRE \
    (eis_switch_cfg_t){ .d_mux = (1u<<4), .p_mux = (1u<<10), \
                        .n_mux = (1u<<8), .t_mux = (1u<<4)|(1u<<8) }

/**
 * @brief Return a default EIS configuration suitable for biosensing.
 *
 * Sweep: 100 Hz – 100 kHz, 40 points, 10 mV excitation, 0 V bias.
 */
eis_config_t eis_default_config(void);

/* ---------- API ----------------------------------------------------- */

/**
 * @brief One-time initialisation of the AFE for impedance measurement.
 *
 * Powers up all required blocks (ADC, DAC, TIA, excitation buffer,
 * waveform generator, DFT engine), configures the LP DAC for the
 * sensor bias, and sets ADC filter / DFT parameters.
 *
 * @param cfg  Pointer to the sweep configuration.
 * @return 0 on success, negative errno on failure.
 */
int eis_init(const eis_config_t *cfg);

/**
 * @brief Perform RCAL calibration measurement.
 *
 * Measures the impedance of the external RCAL resistor at
 * a mid-band frequency. Stores the DFT result used to compute
 * a calibration gain factor for subsequent sensor measurements.
 *
 * Must be called after eis_init() and before eis_run_sweep().
 *
 * @param cfg  Pointer to the sweep configuration.
 * @return 0 on success.
 */
int eis_calibrate_rcal(const eis_config_t *cfg);

/**
 * @brief Execute a full frequency sweep and return impedance data.
 *
 * @param cfg     Pointer to the sweep configuration.
 * @param result  Pointer to the result structure to fill.
 * @return 0 on success, negative errno on failure.
 */
int eis_run_sweep(const eis_config_t *cfg, eis_result_t *result);

/**
 * @brief Print the sweep results to the console (Nyquist-friendly format).
 *
 * Columns: freq_hz, Zreal, Zimag, |Z|, phase_deg
 *
 * @param result  Pointer to the result structure.
 */
void eis_print_results(const eis_result_t *result);

#endif /* AD5940_EIS_H */
