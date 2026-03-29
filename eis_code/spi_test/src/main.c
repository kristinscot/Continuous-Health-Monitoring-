/*
 * AD5940 Impedance Measurement on nRF52840 Dongle
 * Zephyr RTOS implementation
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <string.h>

/* Include AD5940 library headers */
#include "ad5940.h"
#include "Impedance.h"

LOG_MODULE_REGISTER(ad5940_main, LOG_LEVEL_INF);

/* Direct GPIO pin definitions matching devicetree overlay */
static const struct gpio_dt_spec reset_gpio = {
    .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)),
    .pin = 22,
    .dt_flags = GPIO_ACTIVE_LOW
};

static const struct gpio_dt_spec int_gpio = {
    .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)),
    .pin = 24,
    .dt_flags = GPIO_ACTIVE_HIGH
};

static const struct gpio_dt_spec cs_gpio = {
    .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)),
    .pin = 20,
    .dt_flags = GPIO_ACTIVE_LOW
};

/* SPI device */
static const struct device *spi_dev;
static struct spi_config spi_cfg;

/* Interrupt flag */
static volatile bool interrupt_flag = false;
static struct gpio_callback int_gpio_cb;

/* Application buffer used by AppIMPISR */
#define APPBUFF_SIZE 512
static uint32_t AppBuff[APPBUFF_SIZE];

/*
 * Sweep storage
 *
 * Your config below uses 101 sweep points, so we store one full sweep here and
 * only print when all points have been collected.
 */
#define SWEEP_POINT_COUNT 101
static fImpPol_Type sweep_results[SWEEP_POINT_COUNT];
static uint32_t sweep_result_count = 0;

/*******************************************************************************
 * Platform-specific functions required by AD5940 library
 ******************************************************************************/

/**
 * @brief Initialize SPI and GPIO pins
 */
static int ad5940_platform_init(void)
{
    int ret;

    /* Get SPI device from devicetree */
    spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi1));
    if (!device_is_ready(spi_dev)) {
        LOG_ERR("SPI device not ready");
        return -ENODEV;
    }

    /* Configure SPI - manual CS control */
    spi_cfg.frequency = 8000000;  /* 8 MHz */
    spi_cfg.operation = SPI_OP_MODE_MASTER |
                        SPI_TRANSFER_MSB |
                        SPI_WORD_SET(8) |
                        SPI_LINES_SINGLE;
    spi_cfg.slave = 0;
    spi_cfg.cs.gpio.port = NULL;  /* Disable automatic CS control */
    spi_cfg.cs.delay = 0;

    /* Initialize CS GPIO */
    if (!gpio_is_ready_dt(&cs_gpio)) {
        LOG_ERR("CS GPIO not ready");
        return -ENODEV;
    }
    ret = gpio_pin_configure_dt(&cs_gpio, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        LOG_ERR("Failed to configure CS pin");
        return ret;
    }

    /* Initialize RESET GPIO */
    if (!gpio_is_ready_dt(&reset_gpio)) {
        LOG_ERR("RESET GPIO not ready");
        return -ENODEV;
    }
    ret = gpio_pin_configure_dt(&reset_gpio, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Failed to configure RESET pin");
        return ret;
    }

    /* Initialize INT GPIO */
    if (!gpio_is_ready_dt(&int_gpio)) {
        LOG_ERR("INT GPIO not ready");
        return -ENODEV;
    }
    ret = gpio_pin_configure_dt(&int_gpio, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Failed to configure INT pin");
        return ret;
    }

    LOG_INF("AD5940 platform initialized");
    return 0;
}

/**
 * @brief Chip select control - Pull CS low
 */
void AD5940_CsClr(void)
{
    gpio_pin_set_dt(&cs_gpio, 1);  /* Active low */
}

/**
 * @brief Chip select control - Pull CS high
 */
void AD5940_CsSet(void)
{
    gpio_pin_set_dt(&cs_gpio, 0);  /* Inactive high */
}

/**
 * @brief Reset control - Pull RESET low
 */
void AD5940_RstClr(void)
{
    gpio_pin_set_dt(&reset_gpio, 1);  /* Active low */
}

/**
 * @brief Reset control - Pull RESET high
 */
void AD5940_RstSet(void)
{
    gpio_pin_set_dt(&reset_gpio, 0);  /* Inactive high */
}

/**
 * @brief Delay function in 10us units
 * @param time: Number of 10us delays
 */
void AD5940_Delay10us(uint32_t time)
{
    k_usleep(time * 10U);
}

/**
 * @brief Read/Write N bytes over SPI
 * @param pSendBuffer: Pointer to data to send
 * @param pRecvBuff: Pointer to buffer for received data
 * @param length: Number of bytes to transfer
 */
void AD5940_ReadWriteNBytes(unsigned char *pSendBuffer,
                            unsigned char *pRecvBuff,
                            unsigned long length)
{
    struct spi_buf tx_buf = {
        .buf = pSendBuffer,
        .len = length
    };

    struct spi_buf rx_buf = {
        .buf = pRecvBuff,
        .len = length
    };

    struct spi_buf_set tx_bufs = {
        .buffers = &tx_buf,
        .count = 1
    };

    struct spi_buf_set rx_bufs = {
        .buffers = &rx_buf,
        .count = 1
    };

    (void)spi_transceive(spi_dev, &spi_cfg, &tx_bufs, &rx_bufs);
}

/**
 * @brief Get MCU interrupt flag status
 * @return true if interrupt occurred, false otherwise
 */
uint32_t AD5940_GetMCUIntFlag(void)
{
    return interrupt_flag ? 1U : 0U;
}

/**
 * @brief Clear MCU interrupt flag
 */
uint32_t AD5940_ClrMCUIntFlag(void)
{
    interrupt_flag = false;
    return 0U;
}

/**
 * @brief GPIO interrupt callback
 */
static void int_gpio_callback(const struct device *dev,
                              struct gpio_callback *cb,
                              uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    interrupt_flag = true;
}

/**
 * @brief Setup interrupt for AD5940
 */
static int ad5940_interrupt_init(void)
{
    int ret;

    /* Configure interrupt */
    ret = gpio_pin_interrupt_configure_dt(&int_gpio, GPIO_INT_EDGE_RISING);
    if (ret < 0) {
        LOG_ERR("Failed to configure interrupt");
        return ret;
    }

    /* Setup callback */
    gpio_init_callback(&int_gpio_cb, int_gpio_callback, BIT(int_gpio.pin));
    ret = gpio_add_callback(int_gpio.port, &int_gpio_cb);
    if (ret < 0) {
        LOG_ERR("Failed to add interrupt callback");
        return ret;
    }

    LOG_INF("AD5940 interrupt initialized");
    return 0;
}

/*******************************************************************************
 * Application helper functions
 ******************************************************************************/

/**
 * @brief Print one-line summary after full sweep is complete
 */
static void PrintSweepSummary(void)
{
    float freq = 0.0f;

    if (sweep_result_count == 0U) {
        LOG_INF("Sweep complete, but no points were stored");
        return;
    }

    /*
     * IMPCTRL_GETFREQ returns the current frequency state from the app.
     * At sweep completion, this is usually the latest frequency processed.
     */
    (void)AppIMPCtrl(IMPCTRL_GETFREQ, &freq);

    LOG_INF("Sweep complete: points=%u | first Mag=%.2f Ohm Phase=%.2f deg | "
            "last Mag=%.2f Ohm Phase=%.2f deg | current Freq=%.1f Hz",
            sweep_result_count,
            (double)sweep_results[0].Magnitude,
            (double)(sweep_results[0].Phase * 180.0f / MATH_PI),
            (double)sweep_results[sweep_result_count - 1U].Magnitude,
            (double)(sweep_results[sweep_result_count - 1U].Phase * 180.0f / MATH_PI),
            (double)freq);
}

/**
 * @brief Optionally print the full sweep
 *
 * Keep this disabled unless you really need it, because 101 log lines in one
 * burst can still overwhelm USB CDC logging on the nRF52840 dongle.
 */
static void PrintFullSweep(void)
{
    for (uint32_t i = 0; i < sweep_result_count; i++) {
        LOG_INF("[%03u] Mag=%.2f Ohm | Phase=%.2f deg",
                i,
                (double)sweep_results[i].Magnitude,
                (double)(sweep_results[i].Phase * 180.0f / MATH_PI));
    }
}

/**
 * @brief Append newly returned impedance points into the sweep buffer
 *
 * This assumes the AD5940 impedance app returns fImpPol_Type results in AppBuff
 * and that 'count' is the number of returned impedance points, which matches
 * how your earlier code used AppIMPISR(AppBuff, &temp) and then passed 'temp'
 * directly to the display function.
 */
static void StoreSweepChunk(uint32_t *pData, uint32_t count)
{
    fImpPol_Type *pImp = (fImpPol_Type *)pData;

    for (uint32_t i = 0; i < count; i++) {
        if (sweep_result_count < SWEEP_POINT_COUNT) {
            sweep_results[sweep_result_count++] = pImp[i];
        } else {
            /* Ignore any extra points beyond one sweep */
            break;
        }
    }
}

/**
 * @brief Configure AD5940 platform settings
 */
static int32_t AD5940PlatformCfg(void)
{
    CLKCfg_Type clk_cfg;
    FIFOCfg_Type fifo_cfg;
    AGPIOCfg_Type gpio_cfg;

    /* Use hardware reset */
    AD5940_HWReset();
    AD5940_Initialize();

    /* Step1. Configure clock */
    clk_cfg.ADCClkDiv = ADCCLKDIV_1;
    clk_cfg.ADCCLkSrc = ADCCLKSRC_HFOSC;
    clk_cfg.SysClkDiv = SYSCLKDIV_1;
    clk_cfg.SysClkSrc = SYSCLKSRC_HFOSC;
    clk_cfg.HfOSC32MHzMode = bFALSE;
    clk_cfg.HFOSCEn = bTRUE;
    clk_cfg.HFXTALEn = bFALSE;
    clk_cfg.LFOSCEn = bTRUE;
    AD5940_CLKCfg(&clk_cfg);

    /* Step2. Configure FIFO and Sequencer */
    fifo_cfg.FIFOEn = bFALSE;
    fifo_cfg.FIFOMode = FIFOMODE_FIFO;
    fifo_cfg.FIFOSize = FIFOSIZE_4KB;
    fifo_cfg.FIFOSrc = FIFOSRC_DFT;
    fifo_cfg.FIFOThresh = 4;
    AD5940_FIFOCfg(&fifo_cfg);

    fifo_cfg.FIFOEn = bTRUE;
    AD5940_FIFOCfg(&fifo_cfg);

    /* Step3. Interrupt controller */
    AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, bTRUE);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bTRUE);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);

    /* Step4: Reconfigure GPIO */
    gpio_cfg.FuncSet = GP0_INT | GP1_SLEEP | GP2_SYNC;
    gpio_cfg.InputEnSet = 0;
    gpio_cfg.OutputEnSet = AGPIO_Pin0 | AGPIO_Pin1 | AGPIO_Pin2;
    gpio_cfg.OutVal = 0;
    gpio_cfg.PullEnSet = 0;
    AD5940_AGPIOCfg(&gpio_cfg);
    AD5940_SleepKeyCtrlS(SLPKEY_UNLOCK);

    LOG_INF("AD5940 platform configured");
    return 0;
}

/**
 * @brief Initialize impedance measurement structure
 */
static void AD5940ImpedanceStructInit(void)
{
    AppIMPCfg_Type *pImpedanceCfg;

    AppIMPGetCfg(&pImpedanceCfg);

    /* Configure initialization sequence */
    pImpedanceCfg->SeqStartAddr = 0;
    pImpedanceCfg->MaxSeqLen = 512;

    pImpedanceCfg->RcalVal = 9970.0f;
    pImpedanceCfg->SinFreq = 60000.0f;
    pImpedanceCfg->FifoThresh = 4;

    /* Set switch matrix for onboard sensor */
    pImpedanceCfg->DswitchSel = SWD_CE0;
    pImpedanceCfg->PswitchSel = SWP_RE0;
    pImpedanceCfg->NswitchSel = SWN_SE0;
    pImpedanceCfg->TswitchSel = SWT_SE0LOAD;
    pImpedanceCfg->HstiaRtiaSel = HSTIARTIA_5K;

    /* Configure sweep function */
    pImpedanceCfg->SweepCfg.SweepEn = bTRUE;
    pImpedanceCfg->SweepCfg.SweepStart = 100.0f;    /* Start from 100Hz */
    pImpedanceCfg->SweepCfg.SweepStop = 100e3f;     /* Stop at 100kHz */
    pImpedanceCfg->SweepCfg.SweepPoints = SWEEP_POINT_COUNT;
    pImpedanceCfg->SweepCfg.SweepLog = bTRUE;

    /* Configure power mode - use HP for freq > 80kHz */
    pImpedanceCfg->PwrMod = AFEPWR_HP;

    /* Configure filters */
    pImpedanceCfg->ADCSinc3Osr = ADCSINC3OSR_2;     /* 400kSPS */
    pImpedanceCfg->DftNum = DFTNUM_16384;
    pImpedanceCfg->DftSrc = DFTSRC_SINC3;

    LOG_INF("Impedance measurement configured");
}

/*******************************************************************************
 * Main application
 ******************************************************************************/

int main(void)
{
    int ret;
    uint32_t temp;

    LOG_INF("AD5940 Impedance Analyzer Starting...");

    /* Initialize platform */
    ret = ad5940_platform_init();
    if (ret < 0) {
        LOG_ERR("Platform initialization failed: %d", ret);
        return ret;
    }

    /* Initialize interrupt */
    ret = ad5940_interrupt_init();
    if (ret < 0) {
        LOG_ERR("Interrupt initialization failed: %d", ret);
        return ret;
    }

    /* Configure AD5940 */
    ret = AD5940PlatformCfg();
    if (ret < 0) {
        LOG_ERR("AD5940 configuration failed: %d", ret);
        return ret;
    }

    /* Initialize impedance measurement */
    AD5940ImpedanceStructInit();

    ret = AppIMPInit(AppBuff, APPBUFF_SIZE);
    if (ret < 0) {
        LOG_ERR("Impedance app initialization failed: %d", ret);
        return ret;
    }

    /* Clear any old sweep data */
    memset(sweep_results, 0, sizeof(sweep_results));
    sweep_result_count = 0U;

    /* Start impedance measurement */
    ret = AppIMPCtrl(IMPCTRL_START, 0);
    if (ret < 0) {
        LOG_ERR("Failed to start impedance measurement: %d", ret);
        return ret;
    }

    LOG_INF("Impedance measurement started - sweep from 100Hz to 100kHz");

    /* Main loop */
    while (1) {
        if (AD5940_GetMCUIntFlag()) {
            AD5940_ClrMCUIntFlag();

            temp = APPBUFF_SIZE;
            ret = AppIMPISR(AppBuff, &temp);

            if ((ret == 0) && (temp > 0U)) {
                StoreSweepChunk(AppBuff, temp);

                if (sweep_result_count >= SWEEP_POINT_COUNT) {
                    /*
                     * Safe default: print just one summary line per sweep.
                     * This avoids flooding USB logs.
                     */
                    PrintSweepSummary();

                    /*
                     * Uncomment only if you really want every point printed.
                     * Be aware this can still overwhelm USB CDC logging.
                     */
                    /* PrintFullSweep(); */

                    /* Prepare for next sweep */
                    sweep_result_count = 0U;
                    memset(sweep_results, 0, sizeof(sweep_results));
                }
            } else if (ret < 0) {
                LOG_ERR("AppIMPISR failed: %d", ret);
            }
        }

        /*
         * Service the AD5940 frequently.
         * Do not sleep for 1000 ms here or you will service interrupts far too slowly.
         */
        k_msleep(1);
    }

    return 0;
}