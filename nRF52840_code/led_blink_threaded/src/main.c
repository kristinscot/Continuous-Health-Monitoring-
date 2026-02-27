#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

/* GPIO outpts from dvicetree overlay */
#define GPIO_OUT_NODE(n) DT_ALIAS(gpioout##n)

static const struct gpio_dt_spec gpio_out[] = {
    GPIO_DT_SPEC_GET(GPIO_OUT_NODE(0), gpios),
    GPIO_DT_SPEC_GET(GPIO_OUT_NODE(1), gpios),
};

#define NUM_OUTPUTS ARRAY_SIZE(gpio_out)

/* Thread config */
#define STACK_SIZE   512
#define PRIORITY     5

/* Per-output thread resources */
struct output_ctx {
    const struct gpio_dt_spec *spec;
    uint32_t interval_ms;
};

K_THREAD_STACK_ARRAY_DEFINE(stacks, 2, STACK_SIZE);
static struct k_thread threads[2];

static struct output_ctx ctx[] = {
    { .spec = &gpio_out[0], .interval_ms = 500  },
    { .spec = &gpio_out[1], .interval_ms = 1000 },
};

static void output_toggle_thread(void *p1, void *p2, void *p3)
{
    struct output_ctx *c = (struct output_ctx *)p1;

    while (1) {
        gpio_pin_toggle_dt(c->spec);
        k_sleep(K_MSEC(c->interval_ms));
    }
}

int main(void)
{
    /* Initialize all outputs */
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

    /* Spawn a thread per output */
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        k_thread_create(&threads[i], stacks[i], STACK_SIZE,
                        output_toggle_thread,
                        &ctx[i], NULL, NULL,
                        PRIORITY, 0, K_NO_WAIT);
    }

    return 0;
}