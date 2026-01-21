// Copied from 'digital_out' application
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

// Copied from 'analog_read_serial_out' application
#include <zephyr/drivers/adc.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

//Required for high drive
#include <zephyr/dt-bindings/gpio/nordic-nrf-gpio.h>



// Initializing ADC stuff for analog input pin
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



// Initializing Variables and Parameters that will be used for sampling
// NOTE: All 4 element arrays specify one value for each 'state'.
// Each sampling period goes over all 4 states of 0-GreenOn, 1-RedOn, 2-InfraredON, 3-AllOff

static const int stateDuration[4]  = {1000*1000, 1000*1000, 1000*1000, 1000*1000}; //us
static const int settlingDuration[4] = {1000*100, 1000*100, 1000*100, 1000*100}; //us
static const int adc_read_delay = 1000*100; //us //NOTE: Minimum resolution is ~30us, delay between reads may be more than this (on scale of ~100us), can test code execution time by setting this to 0
//static const int BUFFER_SIZE = 20;
#define BUFFER_SIZE 9

//SOME BASIC FUNCTIONS FOR SETTING THINGS UP

/** Initialize one LED. returns 0 if succeeded*/
static int init_led(const struct gpio_dt_spec *led)
{
    if (!device_is_ready(led->port)) {
        printf("MYERROR: GPIO Pin was not ready so not running code (specifically the port?)!!!\n");
        return -1;
    }
    return gpio_pin_configure_dt(led, GPIO_OUTPUT_INACTIVE | NRF_GPIO_DRIVE_H0H1);
}

/** Initialize the ADC. returns 0 if succeeded*/
static int init_adc() {
    for (size_t i = 0; i < ARRAY_SIZE(adc_channels); i++) {
        if (!adc_is_ready_dt(&adc_channels[i])) {
            printf("ADC %s not ready\n", adc_channels[i].dev->name);
            return -1;
        }

        int err = adc_channel_setup_dt(&adc_channels[i]);
        if (err) {
            printf("Channel setup failed (%d)\n", err);
            return -1;
        }
    }
    return 0;
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

static void turn_prev_state_led_off(int curState) //TODO - would make sense to combine turn_state_led_on and turn_prev_state_led_off
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

/**Reads one channel of the adc and returns the measurement in mV. Returns -1 if failed to take measurement. If only one adc then chan==0*/
static int32_t read_adc_chan(int chan) {
    int16_t sample;   // SIGNED buffer for reading adc values
    struct adc_sequence sequence = {
        .buffer = &sample,
        .buffer_size = sizeof(sample),
    };
    int32_t mv;

    adc_sequence_init_dt(&adc_channels[chan], &sequence);
    int err = adc_read_dt(&adc_channels[chan], &sequence);
    if (err) {
        printf("adc_read failed (%d)\n", err);
        return -1;
    }

    /* sample is already signed */
    mv = sample;

    if (adc_raw_to_millivolts_dt(&adc_channels[chan], &mv) == 0) {
        return mv;
    } else {
        printf("(mV unavailable)\n");
        return -1;
    }
}



int main(void)
{
    // TODO: using 32 bit for storing time. It will overflow after ~4000s. So we need to be aware of this and deal with overflow
    uint32_t stateStartTime[4]; //us
    uint32_t stateEndTime[4]; //us
    int readsTaken[4] = {0};
    long readsRunningTotal[4] = {0}; //Used to compute the average read without fancy filtering
    int32_t readsBuffer[4][BUFFER_SIZE]; //mV //NOTE: Only need this for initial tests while we want to see all measurements - TODO Could make it smaller
    uint32_t readsBufferTimestamps[4][BUFFER_SIZE]; //us 
    //int readsAverage[4] = {0}; //TODO - not using this right now. Right now sending running total and getting python to average so I don't need to deal with floats here

    k_msleep(3000); // wait a 3s for stuff to start. Otherwise I miss this print
    printf("Compiled %s at %s %s \n", __FILE__, __DATE__, __TIME__); //This is when *compiled* (helpful for knowing if you successfully uploaded newly compiled code)

    //Initial Setup Stuff
    if (init_led(&led0) != 0 || init_led(&led1) != 0 || init_led(&led2) != 0) {
        printf("MYERROR: GPIO Pin either was not ready or failed to configure so not running code!!!\n");
		k_msleep(1000*10); //Don't quit for 10s, so I can read serial error message
        return -1;
    }
    if (init_adc() != 0) {
        printf("MYERROR: Failed to initialize ADC. Quitting main loop\n");
        k_msleep(1000*10); //Don't quit for 10s, so I can read serial error message
        return -1;
    }

    while (1) {

        for (int state=0; state<4; state++) {
            // Goes over every state. 0-GreenOn, 1-RedOn, 2-InfraredON, 3-AllOff

            // debug prints
            // printf("\nState %d\n", state);
            // printf("Time: %u\n", k_cyc_to_us_near32(k_cycle_get_32()));
            // printf("Cycles: %u\n", k_cycle_get_32());
            // printf("Conversion Rate: %u\n", sys_clock_hw_cycles_per_sec());

            // Make sure LEDs are in correct state
            turn_prev_state_led_off(state);
            turn_state_led_on(state);
            stateStartTime[state] = k_cyc_to_us_near32(k_cycle_get_32());

            // Reset variables
            readsTaken[state] = 0;
            readsRunningTotal[state] = 0;

            

            // Wait settling duration
            k_usleep(settlingDuration[state]);


            // Take Measurements
            // TODO - add and condition to avoid overflow
            while (readsTaken[state]<BUFFER_SIZE && (k_cyc_to_us_near32(k_cycle_get_32()) - stateStartTime[state]) < stateDuration[state]) {
                // Keep taking reads until time for the state is up

                // int readIndex = readsTaken[state];
                
                readsBuffer[state][readsTaken[state]] = read_adc_chan(0);
                if (readsBuffer[state][readsTaken[state]] == -1) {
                    printf("failed to read adc\n");
                }
                readsBufferTimestamps[state][readsTaken[state]] = k_cyc_to_us_near32(k_cycle_get_32()); //TODO optimization, can reduce getting time from 2 times per loop to 1 per loop
                readsRunningTotal[state] += readsBuffer[state][readsTaken[state]];
                // printf("mv=%d\n", readsBuffer[state][readsTaken[state]]);

                readsTaken[state] += 1;
                k_usleep(adc_read_delay);
            }

            stateEndTime[state] = k_cyc_to_us_near32(k_cycle_get_32());
        }

        // One sampling period happened, log all the data (print the buffer and the timestamps for things)
        
        // Nice debug prints
        // printf("\nNEW SAMPLE\n");
        // for (int state=0; state<4; state++) {
        //     printf("State:%d\n", state);
        //     printf("stateStartTime:%d\n", stateStartTime[state]);
        //     printf("stateEndTime:%d\n", stateEndTime[state]);
        //     printf("readsTaken:%d\n", readsTaken[state]);
        //     printf("readsRunningTotal:%ld\n", readsRunningTotal[state]);
        //     // for (int read=0; read<readsTaken[state]; read++)
        // }

        // Data prints used to turn into csv
        for (int state=0; state<4; state++) {
            int num_samples = readsTaken[state];
            // FORMAT - for now doing one sample per line. First X values are for state 0, next X values are for state 1, etc. so total 4X values
            printf("\n%d,", state); //Can probably remove this one, doing for sanity check
            printf("%u,", stateStartTime[state]);
            printf("%u,", stateEndTime[state]);
            printf("%d,", readsTaken[state]);
            printf("%ld,", readsRunningTotal[state]);
            printf("[");
            for (int read=0; read<readsTaken[state]; read++) {
                printf("%d,", readsBuffer[state][read]); //THIS LINE BREAKS IT FOE SOME REASON????
            }
            printf("],");
            printf("[");
            for (int read=0; read<readsTaken[state]; read++) {
                printf("%u,", readsBufferTimestamps[state][read]);
            }
            printf("]");
        }
    }

    return 0;
}

