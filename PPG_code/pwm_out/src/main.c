/*
 * main.c — nRF52840 (Zephyr)
 * Button (DT_ALIAS(sw0)) cycles PWM duty on P0.29 via DT_ALIAS(pwm_led0)
 *
 * Works with the overlay I sent that defines:
 *   aliases { sw0 = &sw1; pwm-led0 = &pwm_out0; }
 *   sw1 on gpio1 pin 6 (pull-up, active-low)
 *   pwm_out0 = pwm0 channel 0 routed to P0.29
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/printk.h>

#define BTN_NODE DT_ALIAS(sw0)
#define PWM_NODE DT_ALIAS(pwm_led0)

#if !DT_NODE_HAS_STATUS(BTN_NODE, okay)
#error "DT_ALIAS(sw0) not found or not okay (check overlay aliases)"
#endif

#if !DT_NODE_HAS_STATUS(PWM_NODE, okay)
#error "DT_ALIAS(pwm_led0) not found or not okay (check overlay aliases)"
#endif

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BTN_NODE, gpios);
static const struct pwm_dt_spec pwm = PWM_DT_SPEC_GET(PWM_NODE);

/* Debounce + event flag */
static struct gpio_callback button_cb;
static volatile bool button_event;

/* Duty cycle steps */
static const uint8_t duty_percent[] = { 0, 10, 25, 50, 75, 90, 100 };
static int duty_idx;

/* Choose a PWM frequency (20 kHz avoids LED flicker; fine for general PWM) */
static const uint32_t period_ns = 50000; /* 50,000 ns = 20 kHz */

static int pwm_set_duty(uint8_t pct)
{
    if (pct > 100) pct = 100;

    uint32_t pulse_ns = (uint64_t)period_ns * pct / 100;

    int ret = pwm_set_dt(&pwm, period_ns, pulse_ns);
    if (ret) {
        printk("pwm_set_dt failed (%d)\n", ret);
        return ret;
    }

    printk("Duty -> %u%%\n", pct);
    return 0;
}

static void button_isr(const struct device *dev,
                       struct gpio_callback *cb,
                       uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    button_event = true;
}

int main(void)
{
    int ret;

    if (!device_is_ready(button.port)) {
        printk("Button GPIO not ready\n");
        return 0;
    }
    if (!device_is_ready(pwm.dev)) {
        printk("PWM device not ready\n");
        return 0;
    }

    /* Configure button pin as input */
    ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (ret) {
        printk("gpio_pin_configure_dt failed (%d)\n", ret);
        return 0;
    }

    /*
     * Overlay sets GPIO_ACTIVE_LOW, and we want an interrupt when it becomes "active".
     * So EDGE_TO_ACTIVE triggers on the press.
     */
    ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret) {
        printk("gpio_pin_interrupt_configure_dt failed (%d)\n", ret);
        return 0;
    }

    gpio_init_callback(&button_cb, button_isr, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb);

    /* Start PWM at 0% */
    duty_idx = 0;
    (void)pwm_set_duty(duty_percent[duty_idx]);

    printk("Ready: press button to cycle duty on P0.29\n");

    while (1) {
        if (button_event) {
            button_event = false;

            /* Simple debounce: wait a bit, then advance one step */
            k_msleep(150);

            duty_idx = (duty_idx + 1) % (int)ARRAY_SIZE(duty_percent);
            (void)pwm_set_duty(duty_percent[duty_idx]);
        }

        k_msleep(10);
    }
}
