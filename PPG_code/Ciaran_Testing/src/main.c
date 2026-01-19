// Copied from 'digital_out' application

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

//Required for high drive
#include <zephyr/dt-bindings/gpio/nordic-nrf-gpio.h>


// Initializing LED GPIO pin stuff
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
#define LED2_NODE DT_ALIAS(led2)

#if !DT_NODE_HAS_STATUS(LED0_NODE, okay) || \
    !DT_NODE_HAS_STATUS(LED1_NODE, okay) || \
    !DT_NODE_HAS_STATUS(LED2_NODE, okay)
#error "One or more LED aliases (led0/led1/led2) are not defined in the debietree"
#endif

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios); //Green
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios); //Red
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(LED2_NODE, gpios); //Infrared

static int init_led(const struct gpio_dt_spec *led)
{
    if (!device_is_ready(led->port)) {
        printf("MYERROR: GPIO Pin was not ready so not running code (specifically the port?)!!!\n");
        return -1;
    }
    return gpio_pin_configure_dt(led, GPIO_OUTPUT_INACTIVE | NRF_GPIO_DRIVE_H0H1);
}

static void turn_state_led_on(int curState)
{
    switch (curState) {
    case 0:
        // Turn on green LED
        gpio_pin_set_dt(&led0, 1);
        break;
    case 1:
        // Turn on Red LED
        gpio_pin_set_dt(&led1, 1);
        break;
    case 2:
        // Turn on IR LED
        gpio_pin_set_dt(&led2, 1);
        break;
    case 3:
        // Don't turn on any LED
        break;
    }
}

static void turn_prev_state_led_off(int curState)
{
    switch (curState) {
    case 0:
        // All LEDs were off before
        break;
    case 1:
        // Turn off Green LED
        gpio_pin_set_dt(&led0, 0);
        break;
    case 2:
        // Turn off Red LED
        gpio_pin_set_dt(&led1, 0);
        break;
    case 3:
        // Turn off IR LED
        gpio_pin_set_dt(&led2, 0);
        break;
    }
}

// Initializing Variables and Parameters that will be used for sampling
// NOTE: All 4 element arrays specify one value for each 'state'.
// Each sampling period goes over all 4 states of 0-GreenOn, 1-RedOn, 2-InfraredON, 3-AllOff

static const int stateDuration[4]  = {1000*1000, 1000*1000, 1000*1000, 1000*1000}; //us
static const int settlingDuration[4] = {1000*100, 1000*100, 1000*100, 1000*100}; //us




int main(void)
{
    k_msleep(3000); // wait a minute for stuff to start. Otherwise I miss this print
    printf("Compiled %s at %s %s \n", __FILE__, __DATE__, __TIME__); //This is when *compiled* (helpful for knowing if you successfully uploaded newly compiled code)

    //Initial Setup Stuff
    if (init_led(&led0) != 0 || init_led(&led1) != 0 || init_led(&led2) != 0) {
        printf("MYERROR: GPIO Pin either was not ready or failed to configure so not running code!!!\n");
		k_msleep(1000*10); //Don't quit for 10s, so I can read serial error message
        return -1;
    }

    while (1) {
        for (int state=0; state<4; state++) {
            // Goes over every state. 0-GreenOn, 1-RedOn, 2-InfraredON, 3-AllOff

            // Make sure LEDs are in correct state
            printf("State %d\n", state);
            turn_prev_state_led_off(state);
            turn_state_led_on(state);

            

            // Wait settling duration
            // k_usleep(settlingDuration[state]);


            // Take Measurements
            k_usleep(stateDuration[state]);



        }

        // One sampling period happened, log all the data
        
    }

    return 0;
}
