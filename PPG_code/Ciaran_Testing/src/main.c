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

// Imported stuff that Tadhg recommended for time stuff (not using)
// #include <stdio.h>
#include <zephyr/sys/clock.h> // sys_clock_hw_cycles_per_sec, sys_clock_gettime, SYS_CLOCK_MONOTONIC
#include <zephyr/posix/time.h> // struct timespec




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


// Thing Tadhg made for time
void print_clock_cycles_according_to_timespec(){
    struct timespec timestamp;
    int err =   sys_clock_gettime(SYS_CLOCK_MONOTONIC, &timestamp);
    if(err != 0){
        printf("COULD NOT GET MONOTONIC CLOCK %d\n", err);
        return;
    }
    double sec_frac = (double)timestamp.tv_sec + ((double)timestamp.tv_nsec)/1.0e9;
    double cycles = sec_frac * sys_clock_hw_cycles_per_sec();
    printf("Timstamp in cycles via sys_clock_gettime: %ld\n",(long int)(1.0e9*cycles));
}

int main(void)
{
    

    uint64_t stateStartTime[4]; //us
    uint64_t stateEndTime[4]; //us
    int32_t sampleBuffer[4][10]; //mV //NOTE: Only need this for initial tests while we want to see all measurements
    uint64_t sampleBufferTimestamps[4][10]; //us
    int samplesTaken[4];



    k_msleep(3000); // wait a minute for stuff to start. Otherwise I miss this print
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
        // for (int i=0 ; i<100000 ; i++) {
        //     k_usleep(100);
        // }
        printf("Cycles: %u\n", k_cycle_get_32());
        printf("Time: %u\n", k_cyc_to_us_near32(k_cycle_get_32()));
        printf("Conversion Rate: %u\n", sys_clock_hw_cycles_per_sec());
        k_msleep(1000);
        



        // for (int state=0; state<4; state++) {
        //     // Goes over every state. 0-GreenOn, 1-RedOn, 2-InfraredON, 3-AllOff
        //     // TODO - Get state start time

        //     // Make sure LEDs are in correct state
        //     printf("\nState %d\n", state);
        //     printf("Time: %u\n", k_cyc_to_us_near32(k_cycle_get_32()));
        //     printf("Cycles: %u\n", k_cycle_get_32());
        //     printf("Conversion Rate: %u\n", sys_clock_hw_cycles_per_sec());
        //     turn_prev_state_led_off(state);
        //     turn_state_led_on(state);
        //     stateStartTime[state] = k_cyc_to_us_near64(k_cycle_get_64());

            

            

        //     // Wait settling duration
        //     // k_usleep(settlingDuration[state]);


        //     // Take Measurements
        //     // (TODO - add timing and store timestamp. And add taking multiple samples and add buffer to store multiple samples)
            
        //     for (int i=0; i<10; i++) {
        //         // Take 10 samples   - TODO: will make more sophisticated soon
                
        //         sampleBuffer[state][i] = read_adc_chan(0);
        //         if (sampleBuffer[state][i] == -1) {
        //             printf("failed to read adc\n");
        //         }
        //         sampleBufferTimestamps[state][i] = k_cyc_to_us_near64(k_cycle_get_64());
                
        //         printf("mv=%d\n", sampleBuffer[state][i]);
        //         k_usleep(150); //Temporary for testing
        //     }
        //     k_usleep(stateDuration[state]);

            


        //     stateEndTime[state] = k_cyc_to_us_near64(k_cycle_get_64());
        // }

        // One sampling period happened, log all the data (print the buffer and the timestamps for things)
        // printf("\nNEW SAMPLE\n");
        // for (int state=0; state<4; state++) {
        //     printf("State:%d\n", state);
        //     printf("stateStartTime:%lld\n", stateStartTime[state]);
        //     printf("stateEndTime:%lld\n", stateEndTime[state]);
        //     // for (int sample=0; sample<samplesTaken[state]; sample++)
        // }
        


        // uint64_t stateStartTime[4]; //us
        // uint64_t stateEndTime[4]; //us
        // int32_t sampleBuffer[4][10]; //mV //NOTE: Only need this for initial tests while we want to see all measurements
        // uint64_t sampleBufferTimestamps[4][10]; //us

    }

    return 0;
}
