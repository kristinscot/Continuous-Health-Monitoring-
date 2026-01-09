#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
    !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

#define DT_SPEC_AND_COMMA(node_id, prop, idx) \
    ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

static const struct adc_dt_spec adc_channels[] = {
    DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels,
                         DT_SPEC_AND_COMMA)
};

int main(void)
{
    int err;
    int16_t sample;   /* SIGNED buffer */

    struct adc_sequence sequence = {
        .buffer = &sample,
        .buffer_size = sizeof(sample),
    };

    for (size_t i = 0; i < ARRAY_SIZE(adc_channels); i++) {
        if (!adc_is_ready_dt(&adc_channels[i])) {
            printk("ADC %s not ready\n", adc_channels[i].dev->name);
            return 0;
        }

        err = adc_channel_setup_dt(&adc_channels[i]);
        if (err) {
            printk("Channel setup failed (%d)\n", err);
            return 0;
        }
    }

    while (1) {
        for (size_t i = 0; i < ARRAY_SIZE(adc_channels); i++) {
            int32_t mv;

            adc_sequence_init_dt(&adc_channels[i], &sequence);
            err = adc_read_dt(&adc_channels[i], &sequence);
            if (err) {
                printk("adc_read failed (%d)\n", err);
                continue;
            }

            /* sample is already signed */
            mv = sample;

            printk("adc=%d ", sample);

            err = adc_raw_to_millivolts_dt(&adc_channels[i], &mv);
            if (err == 0) {
                printk("mv=%d\n", mv);
            } else {
                printk("(mV unavailable)\n");
            }
        }

        k_sleep(K_MSEC(250));
    }
}
