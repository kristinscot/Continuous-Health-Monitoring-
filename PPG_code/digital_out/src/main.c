#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
#define LED2_NODE DT_ALIAS(led2)

#if !DT_NODE_HAS_STATUS(LED0_NODE, okay) || \
    !DT_NODE_HAS_STATUS(LED1_NODE, okay) || \
    !DT_NODE_HAS_STATUS(LED2_NODE, okay)
#error "One or more LED aliases (led0/led1/led2) are not defined in the debietree"
#endif

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(LED2_NODE, gpios);

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
    if (init_led(&led0) != 0 || init_led(&led1) != 0 || init_led(&led2) != 0) {
        return -1;
    }

    const struct gpio_dt_spec *leds[3] = { &led0, &led1, &led2 };
    int idx = 0;

    while (1) {
        all_off();
        gpio_pin_set_dt(leds[idx], 1);

        idx = (idx + 1) % 3;
        k_msleep(150);  /* speed */
        printk("idx=%d\n", idx);


    }

    return 0;
}
