#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/sys/printk.h>
#include <hal/nrf_saadc.h>   // ← needed for NRF_SAADC_INPUT_AINx

#define ADC_NODE        DT_NODELABEL(adc)
#define ADC_RESOLUTION  12
#define ADC_GAIN        ADC_GAIN_1_6
#define ADC_REFERENCE   ADC_REF_INTERNAL

/* Define a *value* for acquisition time, don't redefine the macro name */
#define ADC_ACQ_TIME_VALUE  ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 10)

/* For the dongle: use AIN0 = P0.02 on the edge connector */
#define ADC_CHANNEL_ID      0
#define ADC_CHANNEL_INPUT   NRF_SAADC_INPUT_AIN0

static const struct device *adc_dev;
static int16_t sample_buffer;

/* Channel configuration */
static const struct adc_channel_cfg channel_cfg = {
    .gain             = ADC_GAIN,
    .reference        = ADC_REFERENCE,
    .acquisition_time = ADC_ACQ_TIME_VALUE,
    .channel_id       = ADC_CHANNEL_ID,
#if defined(CONFIG_ADC_CONFIGURABLE_INPUTS)
    .input_positive   = ADC_CHANNEL_INPUT,
#endif
};

void main(void)
{
    int err;

    adc_dev = DEVICE_DT_GET(ADC_NODE);
    if (!device_is_ready(adc_dev)) {
        printk("ADC device not ready\n");
        return;
    }

    err = adc_channel_setup(adc_dev, &channel_cfg);
    if (err) {
        printk("adc_channel_setup failed (%d)\n", err);
        return;
    }

    printk("nRF52840 Dongle: SAADC AIN0 -> USB serial\n");

    struct adc_sequence sequence = {
        .channels    = BIT(ADC_CHANNEL_ID),
        .buffer      = &sample_buffer,
        .buffer_size = sizeof(sample_buffer),
        .resolution  = ADC_RESOLUTION,
    };

    while (1) {
        err = adc_read(adc_dev, &sequence);
        if (err) {
            printk("adc_read failed (%d)\n", err);
        } else {
            int32_t raw = sample_buffer;

            /* Vref 0.6 V, gain 1/6 → FS ≈ 3.6 V, 12-bit (0..4095) */
            int32_t mv = (raw * 3600) / 4095;

            printk("Raw: %4d  |  Voltage: %4d mV\n", (int)raw, (int)mv);
        }

        k_sleep(K_MSEC(50));
    }
}
