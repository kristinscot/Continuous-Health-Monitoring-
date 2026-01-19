#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>   /* ADDED */
#include <zephyr/drivers/pwm.h>    /* ADDED */

/* -------------------- YOUR ADC DT CHECK (kept) -------------------- */
#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
    !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

#define DT_SPEC_AND_COMMA(node_id, prop, idx) \
    ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

static const struct adc_dt_spec adc_channels[] = {
    DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels, DT_SPEC_AND_COMMA)
};

/* -------------------- ADDED: LED + PWM DT specs -------------------- */
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
#define LED2_NODE DT_ALIAS(led2)
#define PWM_OUT_NODE DT_ALIAS(pwmout)

#if !DT_NODE_HAS_STATUS(LED0_NODE, okay) || \
    !DT_NODE_HAS_STATUS(LED1_NODE, okay) || \
    !DT_NODE_HAS_STATUS(LED2_NODE, okay)
#error "GPIO LED aliases missing (led0/led1/led2)"
#endif

#if !DT_NODE_HAS_STATUS(PWM_OUT_NODE, okay)
#error "PWM alias pwm_out missing"
#endif

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(LED2_NODE, gpios);

static const struct pwm_dt_spec pwm_out = PWM_DT_SPEC_GET(PWM_OUT_NODE);

/* ADDED: LED helpers */
static int init_led(const struct gpio_dt_spec *led)
{
    if (!device_is_ready(led->port)) {
        return -1;
    }
    return gpio_pin_configure_dt(led, GPIO_OUTPUT_INACTIVE);
}

static void all_off(void)
{
    gpio_pin_set_dt(&led0, 0);
    gpio_pin_set_dt(&led1, 0);
    gpio_pin_set_dt(&led2, 0);
}

int main(void)
{
    int err;

    /* -------------------- YOUR ADC SETUP (kept) -------------------- */
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

    /* -------------------- ADDED: LED init -------------------- */
    if (init_led(&led0) || init_led(&led1) || init_led(&led2)) {
        printk("LED init failed\n");
        return 0;
    }

    /* -------------------- ADDED: PWM init + start (hardcoded) -------------------- */
    if (!device_is_ready(pwm_out.dev)) {
        printk("PWM device not ready\n");
        return 0;
    }

    /* Hardcoded PWM settings (edit these) */
    uint32_t period_ns = 20000; /* 50 kHz */
    uint32_t duty_pct  = 50;    /* 0..100 */
    uint32_t pulse_ns  = (uint32_t)(((uint64_t)period_ns * duty_pct) / 100ULL);

    err = pwm_set_dt(&pwm_out, pulse_ns, period_ns);
    if (err) {
        printk("pwm_set_dt failed (%d)\n", err);
        return 0;
    }

    /* -------------------- MERGED LOOP: chaser + ADC every 250 ms -------------------- */
    const struct gpio_dt_spec *leds[3] = { &led0, &led1, &led2 };
    int idx = 0;

    int64_t next_adc_ms = k_uptime_get() + 250; /* matches your k_sleep(250ms) */

    while (1) {
        /* LED chaser step */
        all_off();
        gpio_pin_set_dt(leds[idx], 1);
        idx = (idx + 1) % 3;

        /* Your ADC read/print, on the same 250 ms cadence */
        int64_t now = k_uptime_get();
        if (now >= next_adc_ms) {
            next_adc_ms = now + 250;

            for (size_t i = 0; i < ARRAY_SIZE(adc_channels); i++) {
                int32_t mv;

                adc_sequence_init_dt(&adc_channels[i], &sequence);
                err = adc_read_dt(&adc_channels[i], &sequence);
                if (err) {
                    printk("adc_read failed (%d)\n", err);
                    continue;
                }

                mv = sample;

                printk("adc=%d ", sample);

                err = adc_raw_to_millivolts_dt(&adc_channels[i], &mv);
                if (err == 0) {
                    printk("mv=%d\n", mv);
                } else {
                    printk("(mV unavailable)\n");
                }
            }
        }

        k_msleep(150); /* chaser speed */
    }
}
