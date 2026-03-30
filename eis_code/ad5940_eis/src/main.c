/**
 * @file main.c
 * @brief nRF52840 + AD5940 Electrochemical Impedance Spectroscopy application.
 *
 * Performs a frequency sweep from 100 Hz to 100 kHz and outputs impedance
 * data in CSV format suitable for Nyquist / Bode plotting.
 *
 * Hardware requirements:
 *   - nRF52840 DK (or compatible board)
 *   - AD5940/AD5941 evaluation board or custom PCB
 *   - Precision RCAL resistor between RCAL0 and RCAL1 pins (e.g. 200 Ω)
 *   - Biosensor connected to CE0 / RE0 / SE0 electrodes
 *
 * Wiring – see nrf52840dk_nrf52840.overlay for pin assignments.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ad5940_spi.h"
#include "ad5940_eis.h"
#include "ad5940_regs.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* ====================================================================
 * Configuration – adjust these for your setup
 * ==================================================================== */

static eis_config_t build_config(void)
{
    eis_config_t cfg = eis_default_config();

    /* ---- Frequency sweep ---- */
    cfg.freq_start_hz   = 100.0f;      /* 100 Hz */
    cfg.freq_stop_hz    = 100000.0f;   /* 100 kHz */
    cfg.num_points      = 5;          /* Reduced for debugging */

    /* ---- Excitation ---- */
    cfg.excit_amplitude = 33;          /* ~10 mV peak (small signal for bio) */
    cfg.sensor_bias_v   = 0.0f;       /* 0 V DC bias (adjust for your sensor) */

    /* ---- TIA ----
     * Choose RTIA based on expected impedance range:
     *   RTIA = 200 Ω   -> good for Z < 1 kΩ
     *   RTIA = 1 kΩ    -> good for Z ~ 1–5 kΩ
     *   RTIA = 5 kΩ    -> good for Z ~ 5–25 kΩ
     *   RTIA = 10 kΩ   -> good for Z ~ 10–50 kΩ
     *   RTIA = 20–160 kΩ -> for very high impedance
     *
     * Rule of thumb: RTIA ≈ Z_expected / 5
     */
    cfg.rtia_sel = HSRTIA_5K;

    /* ---- DFT ----
     * More DFT points = better SNR but slower.
     * DFTNUM_4096 is a good balance for biosensing. */
    cfg.dft_num = DFTNUM_4096;

    /* ---- PGA gain ---- */
    cfg.pga_gain = ADCPGA_1P5;

    /* ---- RCAL (external calibration resistor) ----
     * Use a precision (0.1%) resistor between RCAL0 and RCAL1.
     * The value here must match the physical resistor exactly. */
    cfg.rcal_ohms = 9910.0f;

    /* ---- Switch matrix ----
     * Preset for 4-wire biosensor on CE0/RE0/SE0.
     * See ad5940_eis.h for EIS_SWITCH_2WIRE / EIS_SWITCH_4WIRE presets.
     * Adjust the D/P/N/T mux values if your electrodes are on different pins.
     *
     * 4-wire (body impedance):
     *   Excitation out -> CE0 (D4), Feedback <- RE0 (P2),
     *   HSTIA <- SE0 (N1, T1)
     *
     * 2-wire (simpler skin impedance):
     *   Excitation out -> CE0 (D4), Feedback <- CE0 (P4),
     *   HSTIA <- SE0 (N1, T1)
     */
    cfg.sw_sensor = EIS_SWITCH_2WIRE;
    cfg.sw_rcal   = EIS_SWITCH_RCAL;

    return cfg;
}

/* ====================================================================
 * Main
 * ==================================================================== */

int main(void)
{
    int ret;

    printk("\n");
    printk("========================================\n");
    printk(" AD5940 EIS – Biosensing Impedance Sweep\n");
    printk(" nRF52840 + Zephyr RTOS\n");
    printk("========================================\n\n");

    /* ---- Step 1: Initialise SPI and GPIO ---- */
    ret = ad5940_spi_init();
    if (ret) {
        LOG_ERR("SPI init failed: %d", ret);
        return ret;
    }

    /* ---- Step 2: Wait for AD5940 power to stabilize ---- */
    printk("Waiting for AD5940 power-up...\n");
    k_msleep(200);

    /* ---- Step 3: Hardware reset the AD5940 ---- */
    ad5940_hw_reset();

    /* ---- Step 4: Run mandatory chip initialisation ---- */
    ret = ad5940_chip_init();
    if (ret) {
        LOG_ERR("Chip init failed: %d", ret);
        return ret;
    }

    /* ---- Step 5: Verify chip identity (retry up to 10 times) ---- */
    for (int attempt = 0; attempt < 10; attempt++) {
        ret = ad5940_verify_id();
        if (ret == 0) {
            break;
        }
        printk("  Retry %d/10...\n", attempt + 1);
        k_msleep(100);
        ad5940_hw_reset();
        ad5940_chip_init();
    }
    if (ret) {
        LOG_ERR("Chip ID verification failed – check SPI wiring!");
        return ret;
    }

    /* ---- Step 5: Build EIS configuration ---- */
    eis_config_t cfg = build_config();

    /* ---- Step 6: Initialise AFE for impedance measurement ---- */
    ret = eis_init(&cfg);
    if (ret) {
        LOG_ERR("EIS init failed: %d", ret);
        return ret;
    }

    /* ---- Step 7: Run the frequency sweep (RCAL calibrated per-frequency) ---- */
    static eis_result_t result;

    ret = eis_run_sweep(&cfg, &result);
    if (ret) {
        LOG_ERR("Sweep failed: %d", ret);
        /* Print partial results if available */
        if (result.count > 0) {
            eis_print_results(&result);
        }
        return ret;
    }

    /* ---- Step 9: Output results ---- */
    eis_print_results(&result);

    printk("========================================\n");
    printk(" Sweep complete. Copy CSV data above\n");
    printk(" and plot Zimag vs Zreal for Nyquist.\n");
    printk("========================================\n");

    /*
     * Optional: continuous sweep loop.
     * Uncomment to re-run the sweep every 5 seconds.
     */
    /*
    while (1) {
        k_msleep(5000);
        ret = eis_run_sweep(&cfg, &result);
        if (ret == 0) {
            eis_print_results(&result);
        }
    }
    */

    return 0;
}
