#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/usb/usbd.h>

/* ── USB device setup (new stack) ──────────────────────────────── */
USBD_DEVICE_DEFINE(usbd_dev,
                   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
                   0x2FE3, 0x0001);

USBD_DESC_LANG_DEFINE(usbd_lang);
USBD_DESC_MANUFACTURER_DEFINE(usbd_mfr, "Custom");
USBD_DESC_PRODUCT_DEFINE(usbd_product, "ADC Reader");
USBD_DESC_SERIAL_NUMBER_DEFINE(usbd_sn);
USBD_CONFIGURATION_DEFINE(fs_config, USB_SCD_SELF_POWERED, 200);

static int usb_init(void)
{
    int err;

    err = usbd_add_descriptor(&usbd_dev, &usbd_lang);
    if (err) { return err; }

    err = usbd_add_descriptor(&usbd_dev, &usbd_mfr);
    if (err) { return err; }

    err = usbd_add_descriptor(&usbd_dev, &usbd_product);
    if (err) { return err; }

    err = usbd_add_descriptor(&usbd_dev, &usbd_sn);
    if (err) { return err; }

    err = usbd_add_configuration(&usbd_dev, USBD_SPEED_FS, &fs_config);
    if (err) { return err; }

    err = usbd_register_all_classes(&usbd_dev, USBD_SPEED_FS, &fs_config);
    if (err) { return err; }

    err = usbd_init(&usbd_dev);
    if (err) { return err; }

    return usbd_enable(&usbd_dev);
}

/* ── ADC channels from devicetree ──────────────────────────────── */
static const struct adc_dt_spec adc_channels[] = {
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0),
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1),
};

#define NUM_CHANNELS ARRAY_SIZE(adc_channels)

/* ── GPIO outputs from devicetree overlay ──────────────────────── */
#define GPIO_OUT_NODE(n) DT_ALIAS(gpioout##n)

static const struct gpio_dt_spec gpio_out[] = {
    GPIO_DT_SPEC_GET(GPIO_OUT_NODE(0), gpios),
    GPIO_DT_SPEC_GET(GPIO_OUT_NODE(1), gpios),
};

#define NUM_OUTPUTS ARRAY_SIZE(gpio_out)

/* ── Thread config ─────────────────────────────────────────────── */
#define STACK_SIZE       1024
#define GPIO_PRIORITY    5
#define ADC_PRIORITY     5

/* ── Context structs ───────────────────────────────────────────── */
struct output_ctx {
    const struct gpio_dt_spec *spec;
    uint32_t interval_ms;
};

struct sample_ctx {
    const struct adc_dt_spec *spec;
    uint32_t sample_interval_ms;
};

/* ── Stacks and thread objects ─────────────────────────────────── */
K_THREAD_STACK_ARRAY_DEFINE(gpio_stacks, 2, STACK_SIZE);
K_THREAD_STACK_ARRAY_DEFINE(adc_stacks, 2, STACK_SIZE);
static struct k_thread gpio_threads[2];
static struct k_thread adc_threads[2];

static struct output_ctx gpio_ctx[] = {
    { .spec = &gpio_out[0], .interval_ms = 500  },
    { .spec = &gpio_out[1], .interval_ms = 1000 },
};

static struct sample_ctx adc_ctx[] = {
    { .spec = &adc_channels[0], .sample_interval_ms = 500  },
    { .spec = &adc_channels[1], .sample_interval_ms = 500  },
};

/* ── Thread functions ──────────────────────────────────────────── */
static void output_toggle_thread(void *p1, void *p2, void *p3)
{
    struct output_ctx *c = (struct output_ctx *)p1;

    while (1) {
        gpio_pin_toggle_dt(c->spec);
        k_sleep(K_MSEC(c->interval_ms));
    }
}

static void adc_sample_thread(void *p1, void *p2, void *p3)
{
    struct sample_ctx *c = (struct sample_ctx *)p1;
    int16_t buf;
    struct adc_sequence sequence = {
        .buffer = &buf,
        .buffer_size = sizeof(buf),
    };

    adc_sequence_init_dt(c->spec, &sequence);

    while (1) {
        int ret = adc_read_dt(c->spec, &sequence);
        if (ret == 0) {
            int32_t mv = (int32_t)buf;
            adc_raw_to_millivolts_dt(c->spec, &mv);
            printk("Ch %d: %d mV\n", c->spec->channel_id, mv);
        }
        k_sleep(K_MSEC(c->sample_interval_ms));
    }
}

/* ── Main ──────────────────────────────────────────────────────── */
int main(void)
{
    /* Initialize USB console */
    int err = usb_init();
    if (err) {
        printk("USB init failed: %d\n", err);
        return err;
    }

    /* Give host time to enumerate USB device */
    k_sleep(K_SECONDS(2));

    printk("ADC + GPIO app started\n");

    /* Initialize GPIO outputs */
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        if (!gpio_is_ready_dt(&gpio_out[i])) {
            printk("GPIO %d not ready\n", i);
            return -ENODEV;
        }
        if (gpio_pin_configure_dt(&gpio_out[i], GPIO_OUTPUT_INACTIVE) < 0) {
            printk("Failed to configure GPIO %d\n", i);
            return -EIO;
        }
    }

    /* Initialize ADC channels */
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (!adc_is_ready_dt(&adc_channels[i])) {
            printk("ADC ch %d not ready\n", i);
            return -ENODEV;
        }
        if (adc_channel_setup_dt(&adc_channels[i]) < 0) {
            printk("Failed to setup ADC ch %d\n", i);
            return -EIO;
        }
    }

    /* Spawn GPIO threads */
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        k_thread_create(&gpio_threads[i], gpio_stacks[i], STACK_SIZE,
                        output_toggle_thread,
                        &gpio_ctx[i], NULL, NULL,
                        GPIO_PRIORITY, 0, K_NO_WAIT);
    }

    /* Spawn ADC threads */
    for (int i = 0; i < NUM_CHANNELS; i++) {
        k_thread_create(&adc_threads[i], adc_stacks[i], STACK_SIZE,
                        adc_sample_thread,
                        &adc_ctx[i], NULL, NULL,
                        ADC_PRIORITY, 0, K_NO_WAIT);
    }

    return 0;
}
