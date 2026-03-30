/**
 * @file ad5940_eis.c
 * @brief Electrochemical Impedance Spectroscopy engine for AD5940.
 *
 * Implements RCAL calibration and frequency-swept impedance measurement
 * using the AD5940's on-chip waveform generator, HSTIA, and DFT engine.
 *
 * Measurement topology (for each frequency point):
 *
 *   HSDAC -> Waveform Gen (sinusoid) -> Excitation Amp -> [Switch Matrix] -> Sensor/RCAL
 *                                                                 |
 *   ADC <- DFT engine <- sinc3 filter <----- HSTIA <---- [Switch Matrix] <--+
 *
 * The DFT engine returns complex (real + imaginary) values.  The unknown
 * impedance is computed by comparing the sensor DFT result against the
 * RCAL calibration DFT result:
 *
 *       Z_unknown = RCAL * (DFT_rcal / DFT_sensor)
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <math.h>
#include <string.h>
#include <errno.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "ad5940_eis.h"
#include "ad5940_spi.h"
#include "ad5940_regs.h"

LOG_MODULE_REGISTER(ad5940_eis, CONFIG_LOG_DEFAULT_LEVEL);

/* ====================================================================
 * Internal state
 * ==================================================================== */

/* RCAL calibration DFT result (complex) – set by eis_calibrate_rcal() */
static float rcal_dft_real;
static float rcal_dft_imag;
static float rcal_ohms_stored;
static bool  rcal_valid = false;

/* Cached config for reconfiguring per-frequency */
static bool high_power_mode = false;

/* ====================================================================
 * Helper: sign-extend 18-bit DFT result to int32_t
 * ==================================================================== */
static int32_t sign_extend_18(uint32_t raw)
{
    if (raw & (1u << 17)) {
        return (int32_t)(raw | 0xFFFC0000u);
    }
    return (int32_t)raw;
}

/* ====================================================================
 * Helper: compute the Sine FCW for a given frequency.
 *
 * From the datasheet:
 *   f_out = f_ACLK * SINEFCW / 2^30
 *   => SINEFCW = f_out * 2^30 / f_ACLK
 *
 * f_ACLK = 16 MHz (low power) or 32 MHz (high power).
 * ==================================================================== */
static uint32_t freq_to_fcw(float freq_hz)
{
    float aclk = high_power_mode ? 32e6f : 16e6f;
    double fcw = (double)freq_hz * (double)(1UL << 30) / (double)aclk;
    return (uint32_t)(fcw + 0.5);
}

/* ====================================================================
 * Helper: determine if a frequency requires high-power mode (> 80 kHz)
 * ==================================================================== */
static bool needs_high_power(float freq_hz)
{
    return freq_hz > 80000.0f;
}

/* ====================================================================
 * Helper: configure the switch matrix using xSWFULLCON registers.
 *
 * This matches the ADI library's AD5940_SWMatrixCfgS() function.
 * The bitmask values come from the ADI library's SWD_/SWP_/SWN_/SWT_ defines.
 * ==================================================================== */

/* Switch bitmask defines (from ADI ad5940.h) */
#define SWD_RCAL0       (1u << 0)
#define SWD_CE0         (1u << 4)

#define SWP_RCAL0       (1u << 0)
#define SWP_RE0         (1u << 4)
#define SWP_CE0         (1u << 10)

#define SWN_RCAL1       (1u << 9)
#define SWN_SE0LOAD     (1u << 4)
#define SWN_SE0         (1u << 8)

#define SWT_RCAL1       (1u << 11)
#define SWT_SE0LOAD     (1u << 4)
#define SWT_TRTIA       (1u << 8)   /* T9: connect general RTIA */

static int set_switch_matrix(const eis_switch_cfg_t *sw)
{
    int ret;

    /* Write each full switch control register */
    ret = ad5940_write_reg(REG_DSWFULLCON, sw->d_mux);
    if (ret) return ret;
    ret = ad5940_write_reg(REG_PSWFULLCON, sw->p_mux);
    if (ret) return ret;
    ret = ad5940_write_reg(REG_NSWFULLCON, sw->n_mux);
    if (ret) return ret;
    ret = ad5940_write_reg(REG_TSWFULLCON, sw->t_mux);
    if (ret) return ret;

    /* Set SWSOURCESEL bit to activate full control registers */
    ret = ad5940_write_reg(REG_SWCON, SWCON_SWSOURCESEL);

    return ret;
}

/* ====================================================================
 * Helper: configure LP DAC for the DC sensor bias.
 *
 * VBIAS0 (12-bit) sets the counter/reference electrode voltage.
 * VZERO0 (6-bit) sets the sense electrode voltage via HSTIA.
 * Sensor bias = VBIAS0 - VZERO0.
 *
 * For impedance measurement we connect VZERO0 to the HSTIA positive
 * input (via LPDACSW0 SW0).
 * ==================================================================== */
static int configure_lp_dac(float bias_v)
{
    int ret;

    /* Power on the LP DAC, enable writes, use 2.5 V LP reference */
    uint32_t lpdaccon0 = LPDACCON0_RSTEN;  /* bit 0 = enable writes, bit 1 = 0 (powered on) */
    ret = ad5940_write_reg(REG_LPDACCON0, lpdaccon0);
    if (ret) return ret;

    /* Enable LP reference buffer */
    ret = ad5940_write_reg(REG_LPREFBUFCON, 0x00000000);
    if (ret) return ret;

    /*
     * Set VZERO0 to a mid-rail value (1.1 V) and VBIAS0 to VZERO0 + bias.
     *
     * 6-bit VZERO0:  voltage = 0.2 V + code * 34.375 mV
     *   code_6 = (voltage - 0.2) / 0.034375
     *   For 1.1 V: code_6 = (1.1 - 0.2) / 0.034375 ≈ 26 = 0x1A
     *
     * 12-bit VBIAS0: voltage = 0.2 V + code * 537.2 µV
     *   For 1.1 V + bias: code_12 = ((1.1 + bias) - 0.2) / 0.000537
     */
    float vzero_v = 1.1f;
    float vbias_v = vzero_v + bias_v;

    /* Clamp to valid range [0.2V, 2.4V] */
    if (vbias_v < 0.2f) vbias_v = 0.2f;
    if (vbias_v > 2.4f) vbias_v = 2.4f;
    if (vzero_v < 0.2f) vzero_v = 0.2f;
    if (vzero_v > 2.366f) vzero_v = 2.366f;

    uint32_t code_6  = (uint32_t)((vzero_v - 0.2f) / 0.034375f);
    uint32_t code_12 = (uint32_t)((vbias_v - 0.2f) / 0.000537f);

    if (code_6 > 0x3F) code_6 = 0x3F;
    if (code_12 > 0xFFF) code_12 = 0xFFF;

    /* Compensate for loading when 12-bit >= 6-bit (datasheet eq.7) */
    if (code_12 >= (code_6 * 64)) {
        if (code_12 > 0) code_12 -= 1;
    }

    uint32_t lpdacdat0 = (code_6 << 12) | code_12;
    ret = ad5940_write_reg(REG_LPDACDAT0, lpdacdat0);
    if (ret) return ret;

    /*
     * Configure LP DAC switches for impedance mode:
     *   SW0 = 1: Connect VZERO0 to HSTIA positive input
     *   SW3 = 1: Connect VBIAS0 to VBIAS0 pin (for potentiostat if needed)
     *   LPMODEDIS = 1: Individual switch control
     */
    uint32_t lpdacsw0 = LPDACSW0_LPMODEDIS | LPDACSW0_SW0 | LPDACSW0_SW3;
    ret = ad5940_write_reg(REG_LPDACSW0, lpdacsw0);

    LOG_INF("LP DAC: VZERO0=%.3fV (code6=%u), VBIAS0=%.3fV (code12=%u), bias=%.3fV",
            (double)vzero_v, code_6, (double)vbias_v, code_12, (double)bias_v);

    return ret;
}

/* ====================================================================
 * Helper: configure the ADC + filters for impedance measurement.
 * ==================================================================== */
static int configure_adc(const eis_config_t *cfg)
{
    int ret;

    /* ADC MUX: positive = HSTIA positive output, negative = HSTIA negative */
    uint32_t adccon = 0;
    adccon |= (ADCMUXP_HSTIA_P & 0x3F);
    adccon |= ((uint32_t)(ADCMUXN_HSTIA_N & 0x1F) << ADCCON_MUXSELN_SHIFT);
    adccon |= ((uint32_t)(cfg->pga_gain & 0x7) << ADCCON_GNPGA_SHIFT);

    ret = ad5940_write_reg(REG_ADCCON, adccon);
    if (ret) return ret;

    /*
     * ADC filter configuration for impedance:
     *   - ADC sample rate: 800 kHz (low power) or 1.6 MHz (high power)
     *   - Sinc3 OSR = 5 (low power) or 4 (high power)
     *   - Sinc3 NOT bypassed (for clean DFT input)
     *   - 50/60 Hz notch bypassed (not needed for impedance)
     *   - Sinc2 OSR = 22 (default)
     */
    uint32_t adcfilter = 0;
    if (high_power_mode) {
        /* 1.6 MHz ADC, sinc3 OSR = 4 -> DFT input rate = 400 kHz */
        adcfilter |= (SINC3OSR_4 << ADCFILTERCON_SINC3OSR_SHIFT);
        /* ADCSAMPLERATE bit 0 = 0 for 1.6 MHz */
    } else {
        /* 800 kHz ADC, sinc3 OSR = 5 -> DFT input rate = 160 kHz */
        adcfilter |= ADCFILTERCON_ADCSAMPLERATE;  /* bit 0 = 1 for 800 kHz */
        adcfilter |= (SINC3OSR_5 << ADCFILTERCON_SINC3OSR_SHIFT);
    }

    /* Bypass the 50/60 Hz notch filter for impedance measurements */
    adcfilter |= ADCFILTERCON_LPFBYPEN;

    ret = ad5940_write_reg(REG_ADCFILTERCON, adcfilter);
    return ret;
}

/* ====================================================================
 * Helper: configure HSTIA (gain resistor and positive input).
 * ==================================================================== */
static int configure_hstia(const eis_config_t *cfg)
{
    int ret;

    /* HSTIA positive input = VZERO0 from LP DAC */
    ret = ad5940_write_reg(REG_HSTIACON,
                           (HSTIA_VZERO0 << HSTIACON_VBIASSEL_SHIFT));
    if (ret) return ret;

    /* RTIA selection + feedback capacitor for loop stability.
     * Use 1 pF cap by default; increase for higher RTIA values. */
    uint8_t rtia = cfg->rtia_sel;
    if (rtia > HSRTIA_160K) rtia = HSRTIA_5K;  /* safe default */

    uint32_t ctiacon = 0;
    if (rtia >= HSRTIA_40K) {
        ctiacon = 0x08;  /* 8 pF for high RTIA */
    } else if (rtia >= HSRTIA_10K) {
        ctiacon = 0x02;  /* 2 pF */
    } else {
        ctiacon = 0x01;  /* 1 pF */
    }

    uint32_t hsrtiacon = (ctiacon << HSRTIACON_CTIACON_SHIFT) |
                         (rtia & HSRTIACON_RTIACON_MASK);
    ret = ad5940_write_reg(REG_HSRTIACON, hsrtiacon);

    LOG_INF("HSTIA RTIA = %u Ω, CTIA = 0x%02X", hsrtia_ohms[rtia], ctiacon);
    return ret;
}

/* ====================================================================
 * Helper: configure the DFT engine.
 * ==================================================================== */
static int configure_dft(const eis_config_t *cfg)
{
    /*
     * DFTCON:
     *   - Hanning window ON (recommended for impedance)
     *   - DFT number as configured
     *   - DFT input = sinc3 output after gain/offset correction
     */
    uint32_t dftcon = DFTCON_HANNINGEN;
    dftcon |= ((uint32_t)(cfg->dft_num & 0xF) << DFTCON_DFTNUM_SHIFT);
    dftcon |= ((uint32_t)DFTINSEL_SINC3 << DFTCON_DFTINSEL_SHIFT);

    return ad5940_write_reg(REG_DFTCON, dftcon);
}

/* ====================================================================
 * Helper: configure waveform generator for a specific frequency.
 * ==================================================================== */
static int set_excitation_freq(float freq_hz, uint16_t amplitude)
{
    int ret;

    /* Set sine FCW */
    uint32_t fcw = freq_to_fcw(freq_hz);
    ret = ad5940_write_reg(REG_WGFCW, fcw);
    if (ret) return ret;

    /* Set amplitude (11-bit unsigned) */
    ret = ad5940_write_reg(REG_WGAMPLITUDE, (uint32_t)(amplitude & 0x7FF));
    if (ret) return ret;

    /* Offset = 0 (no DC offset on excitation) */
    ret = ad5940_write_reg(REG_WGOFFSET, 0);
    if (ret) return ret;

    /* Phase = 0 */
    ret = ad5940_write_reg(REG_WGPHASE, 0);
    if (ret) return ret;

    /* Waveform type = Sinusoid, enable DAC offset+gain cal */
    ret = ad5940_write_reg(REG_WGCON,
                           WGCON_TYPE_SINE |
                           WGCON_DACGAINCAL |
                           WGCON_DACOFFSETCAL);
    return ret;
}

/* ====================================================================
 * Helper: configure HSDAC for impedance mode.
 * ==================================================================== */
static int configure_hsdac(void)
{
    /*
     * HSDACCON:
     *   - INAMPGNMDE = 0 -> excitation amplifier gain = 2
     *   - ATTENEN = 0 -> PGA gain = 1 (no attenuation)
     *   - Rate = recommended value for LP or HP mode
     *
     * This gives an excitation signal range of ±607 mV at full scale.
     * With amplitude code = 33, voltage ≈ 33/2048 * 607 mV ≈ 10 mV peak.
     */
    uint32_t rate = high_power_mode ? HSDACCON_RATE_HP : HSDACCON_RATE_LP;
    uint32_t hsdaccon = (rate << HSDACCON_RATE_SHIFT);
    /* Gain = 2 (INAMPGNMDE = 0), no attenuation (ATTENEN = 0) */

    return ad5940_write_reg(REG_HSDACCON, hsdaccon);
}

/* ====================================================================
 * Helper: configure power mode and bandwidth.
 * ==================================================================== */
static int configure_power_mode(bool hp)
{
    uint32_t pmbw = 0;
    if (hp) {
        pmbw |= PMBW_SYSHS;  /* High speed DAC + ADC */
        pmbw |= (0x3 << PMBW_SYSBW_SHIFT);  /* 250 kHz BW */
    } else {
        /* Low power: SYSHS = 0, auto BW */
        pmbw = 0;
    }
    high_power_mode = hp;
    return ad5940_write_reg(REG_PMBW, pmbw);
}

/* ====================================================================
 * Helper: power up the AFE blocks needed for impedance measurement.
 * ==================================================================== */
static int power_up_afe(void)
{
    /*
     * AFECON: Enable all blocks needed for impedance measurement.
     *
     * Bit 21: DACBUFEN – DC DAC buffer (needed for LP DAC -> excitation amp)
     * Bit 20: DACREFEN – HS DAC reference
     * Bit 19: Reserved – must be 1
     * Bit 15: DFTEN – DFT engine
     * Bit 14: WAVEGENEN – waveform generator
     * Bit 11: TIAEN – high speed TIA
     * Bit 10: INAMPEN – excitation instrumentation amplifier
     * Bit  9: EXBUFEN – excitation buffer
     * Bit  8: ADCCONVEN – ADC conversion start
     * Bit  7: ADCEN – ADC power
     * Bit  6: DACEN – HS DAC
     *
     * Bit 16: SINC2EN = 0 (disable for impedance, per datasheet)
     * Bit  5: HSREFDIS = 0 (HS reference enabled)
     */
    uint32_t afecon = AFECON_DACBUFEN   |
                      AFECON_DACREFEN   |
                      AFECON_RSVD19     |
                      AFECON_DFTEN      |
                      AFECON_WAVEGENEN  |
                      AFECON_TIAEN      |
                      AFECON_INAMPEN    |
                      AFECON_EXBUFEN    |
                      AFECON_ADCCONVEN  |
                      AFECON_ADCEN      |
                      AFECON_DACEN;

    return ad5940_write_reg(REG_AFECON, afecon);
}

/* ====================================================================
 * Helper: read a single DFT measurement.
 *
 * Polls the INTCFLAG0 register for the DFT-result bit.
 * Assumes interrupt flags have already been cleared and the
 * measurement has already been started before calling this.
 * ==================================================================== */
static int read_dft_result(float *dft_real, float *dft_imag)
{
    int ret;
    uint32_t flags;
    int timeout_ms = 5000;
    int elapsed = 0;

    /* Poll INTCFLAG0 for DFT result bit */
    while (elapsed < timeout_ms) {
        ret = ad5940_read_reg(REG_INTCFLAG0, &flags);
        if (ret) return ret;

        if (flags & INTC_DFTRESULT) {
            break;
        }
        k_msleep(1);
        elapsed++;
    }

    if (elapsed >= timeout_ms) {
        LOG_ERR("DFT timeout (%d ms)", elapsed);
        return -ETIMEDOUT;
    }

    /* Read DFT results */
    uint32_t raw_real, raw_imag;
    ret = ad5940_read_reg(REG_DFTREAL, &raw_real);
    if (ret) return ret;
    ret = ad5940_read_reg(REG_DFTIMAG, &raw_imag);
    if (ret) return ret;

    /* Sign-extend 18-bit results */
    *dft_real = (float)sign_extend_18(raw_real & 0x3FFFF);
    *dft_imag = (float)sign_extend_18(raw_imag & 0x3FFFF);

    /* Clear the interrupt */
    ad5940_write_reg(REG_INTCCLR, INTC_DFTRESULT);

    return 0;
}

/* ====================================================================
 * Helper: perform one impedance measurement at a single frequency
 * with the currently configured switch matrix.
 *
 * Strategy: Leave the DFT running continuously. After changing
 * frequency, wait for TWO DFT completions — the first flushes
 * samples from the old frequency, the second is clean data.
 * ==================================================================== */
static int measure_one_freq(float freq_hz, const eis_config_t *cfg,
                            float *dft_r, float *dft_i)
{
    int ret;

    /* Check if we need to change power mode for this frequency */
    bool hp_needed = needs_high_power(freq_hz);
    if (hp_needed != high_power_mode) {
        ret = configure_power_mode(hp_needed);
        if (ret) return ret;
        ret = configure_hsdac();
        if (ret) return ret;
        ret = configure_adc(cfg);
        if (ret) return ret;

        /* Re-enable full AFE after power mode change */
        ret = power_up_afe();
        if (ret) return ret;
        k_msleep(5);
    }

    /* Set new excitation frequency */
    ret = set_excitation_freq(freq_hz, cfg->excit_amplitude);
    if (ret) return ret;

    /* Wait 1: Flush DFT that contains old-frequency samples */
    ret = ad5940_write_reg(REG_INTCCLR, 0xFFFFFFFF);
    if (ret) return ret;
    ret = ad5940_write_reg(REG_INTCSEL0, INTC_DFTRESULT);
    if (ret) return ret;

    ret = read_dft_result(dft_r, dft_i);  /* Discard this result */
    if (ret) return ret;

    /* Wait 2: This DFT contains clean samples at the new frequency */
    ret = ad5940_write_reg(REG_INTCCLR, INTC_DFTRESULT);
    if (ret) return ret;

    ret = read_dft_result(dft_r, dft_i);  /* Keep this result */
    return ret;
}

/* ====================================================================
 * Public API
 * ==================================================================== */

eis_config_t eis_default_config(void)
{
    eis_config_t cfg = {
        .freq_start_hz   = 100.0f,
        .freq_stop_hz    = 100000.0f,
        .num_points      = 40,
        .excit_amplitude = 33,      /* ~10 mV peak with gain=2 */
        .sensor_bias_v   = 0.0f,    /* No DC bias */
        .rtia_sel        = HSRTIA_5K,
        .dft_num         = DFTNUM_4096,
        .pga_gain        = ADCPGA_1P5,
        .sw_sensor       = EIS_SWITCH_4WIRE,
        .sw_rcal         = EIS_SWITCH_RCAL,
        .rcal_ohms       = 200.0f,  /* 200 Ω precision resistor */
    };
    return cfg;
}

int eis_init(const eis_config_t *cfg)
{
    int ret;

    LOG_INF("=== EIS Init ===");

    /* Start in low-power mode; we'll switch to HP per-frequency as needed */
    ret = configure_power_mode(false);
    if (ret) return ret;

    /* Configure LP DAC for sensor bias */
    ret = configure_lp_dac(cfg->sensor_bias_v);
    if (ret) return ret;

    /* Configure HSDAC */
    ret = configure_hsdac();
    if (ret) return ret;

    /* Configure HSTIA (gain resistor) */
    ret = configure_hstia(cfg);
    if (ret) return ret;

    /* Configure ADC */
    ret = configure_adc(cfg);
    if (ret) return ret;

    /* Configure DFT engine */
    ret = configure_dft(cfg);
    if (ret) return ret;

    /* Configure interrupt: positive edge on GP0 for active-low interrupt */
    ret = ad5940_write_reg(REG_INTCPOL, 0x00000000);  /* Negative edge */
    if (ret) return ret;

    /* Power up the AFE */
    ret = power_up_afe();
    if (ret) return ret;

    /* Allow blocks to settle */
    k_msleep(20);

    LOG_INF("EIS init complete");
    return 0;
}

int eis_calibrate_rcal(const eis_config_t *cfg)
{
    int ret;

    LOG_INF("=== RCAL Calibration ===");

    /* Configure switches for RCAL path */
    ret = set_switch_matrix(&cfg->sw_rcal);
    if (ret) return ret;

    k_msleep(5);  /* Allow switches to settle */

    /* Measure at a mid-band frequency (e.g. 10 kHz) */
    float cal_freq = 10000.0f;
    if (cal_freq > cfg->freq_stop_hz) cal_freq = cfg->freq_stop_hz;
    if (cal_freq < cfg->freq_start_hz) cal_freq = cfg->freq_start_hz;

    ret = measure_one_freq(cal_freq, cfg, &rcal_dft_real, &rcal_dft_imag);
    if (ret) {
        LOG_ERR("RCAL measurement failed");
        return ret;
    }

    rcal_ohms_stored = cfg->rcal_ohms;
    rcal_valid = true;

    float mag = sqrtf(rcal_dft_real * rcal_dft_real +
                      rcal_dft_imag * rcal_dft_imag);
    LOG_INF("RCAL DFT: real=%.1f, imag=%.1f, |DFT|=%.1f",
            (double)rcal_dft_real, (double)rcal_dft_imag, (double)mag);

    return 0;
}

int eis_run_sweep(const eis_config_t *cfg, eis_result_t *result)
{
    int ret;

    uint16_t npts = cfg->num_points;
    if (npts > EIS_MAX_FREQ_POINTS) npts = EIS_MAX_FREQ_POINTS;

    result->count = npts;

    printk("=== EIS Sweep: %.1f Hz - %.1f Hz, %u points ===\n",
            (double)cfg->freq_start_hz, (double)cfg->freq_stop_hz, npts);

    float log_ratio = logf(cfg->freq_stop_hz / cfg->freq_start_hz);

    for (uint16_t i = 0; i < npts; i++) {
        float frac = (npts > 1) ? (float)i / (float)(npts - 1) : 0.0f;
        float freq = cfg->freq_start_hz * expf(frac * log_ratio);

        /* ---- Step A: Measure RCAL at this frequency ---- */
        ret = set_switch_matrix(&cfg->sw_rcal);
        if (ret) return ret;
        k_msleep(2);

        float rcal_r, rcal_i;
        ret = measure_one_freq(freq, cfg, &rcal_r, &rcal_i);
        if (ret) {
            LOG_ERR("RCAL measurement failed at %.1f Hz", (double)freq);
            result->count = i;
            return ret;
        }

        /* ---- Step B: Measure sensor at the same frequency ---- */
        ret = set_switch_matrix(&cfg->sw_sensor);
        if (ret) return ret;
        k_msleep(2);

        float sens_r, sens_i;
        ret = measure_one_freq(freq, cfg, &sens_r, &sens_i);
        if (ret) {
            LOG_ERR("Sensor measurement failed at %.1f Hz", (double)freq);
            result->count = i;
            return ret;
        }

        /*
         * Step C: Compute impedance using polar form (matching ADI library).
         * ADI library uses atan2(-Imag, Real) — note negated imaginary.
         *
         * Z_mag   = |DFT_rcal| / |DFT_sensor| * RCAL_ohms
         * Z_phase = phase_rcal - phase_sensor
         */
        float rcal_mag = sqrtf(rcal_r * rcal_r + rcal_i * rcal_i);
        float sens_mag = sqrtf(sens_r * sens_r + sens_i * sens_i);

        if (sens_mag < 1.0f) {
            printk("[%2u] %.1f Hz: sensor DFT too small\n", i, (double)freq);
            result->points[i].freq_hz   = freq;
            result->points[i].real_ohms = 0;
            result->points[i].imag_ohms = 0;
            result->points[i].mag_ohms  = 0;
            result->points[i].phase_deg = 0;
            continue;
        }

        float rcal_phase = atan2f(-rcal_i, rcal_r);
        float sens_phase = atan2f(-sens_i, sens_r);

        float z_mag   = (rcal_mag / sens_mag) * cfg->rcal_ohms;
        float z_phase = rcal_phase - sens_phase;

        /* Wrap phase to [-pi, pi] */
        while (z_phase > (float)M_PI)  z_phase -= 2.0f * (float)M_PI;
        while (z_phase < -(float)M_PI) z_phase += 2.0f * (float)M_PI;

        float z_phase_deg = z_phase * (180.0f / (float)M_PI);
        float z_real = z_mag * cosf(z_phase);
        float z_imag = z_mag * sinf(z_phase);

        result->points[i].freq_hz   = freq;
        result->points[i].real_ohms = z_real;
        result->points[i].imag_ohms = z_imag;
        result->points[i].mag_ohms  = z_mag;
        result->points[i].phase_deg = z_phase_deg;

        printk("[%2u] %8.1f Hz: |Z|=%.1f  ph=%.1f  (rcal=%.0f,%.0f sens=%.0f,%.0f)\n",
               i, (double)freq, (double)z_mag, (double)z_phase_deg,
               (double)rcal_r, (double)rcal_i, (double)sens_r, (double)sens_i);
    }

    printk("Sweep complete: %u points\n", npts);
    return 0;
}

void eis_print_results(const eis_result_t *result)
{
    printk("\n");
    printk("# EIS Sweep Results (%u points)\n", result->count);
    printk("# freq_hz, Zreal_ohm, Zimag_ohm, Zmag_ohm, phase_deg\n");

    for (uint16_t i = 0; i < result->count; i++) {
        const eis_point_t *p = &result->points[i];
        printk("%.2f, %.4f, %.4f, %.4f, %.2f\n",
               (double)p->freq_hz,
               (double)p->real_ohms,
               (double)p->imag_ohms,
               (double)p->mag_ohms,
               (double)p->phase_deg);
    }
    printk("# END\n\n");
}
