#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

//==================== INITIALIZE ANALOG READ PIN ====================
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
//====================================================================

//======================= ASSIGN PWM OUTPUT PIN ======================
#define PWM_OUT_NODE DT_ALIAS(pwmout0)
static const struct pwm_dt_spec pwm = PWM_DT_SPEC_GET(PWM_OUT_NODE);
//====================================================================

/* define parameters
    The frequency of the PWM signal is limited by the resolution of the DAC:
     8-bit - f_pwm_max = 62.5 kHz
    10-bit - f_pwm_max = 15.625 kHz
    12-bit - f_pwm_max = 3.90625 kHz

    The nRF PWM uses a 16MHz timebase and integer counter. To get
    an exact frequency/resolution, use value alligned with
    16MHz/countertop
*/
#define F_PWM_HZ 15625U //15.625 kHz
//#define V_SUPPLY_mV 3300U
#define V_SUPPLY_mV 3000U //when driving output using Laptop

#define V_REF_mV 1650U

// set indicies for parsing ADC channels
#define ADC_CH_CONTROL 0
#define ADC_CH_MONITOR 1

int main(void)
{
    //===================== INITIALIZE PWM SIGNAL ====================
    // ensure hardware driver is ready
    if (!device_is_ready(pwm.dev)) {
        return -1;
    }
    
    // calculate period of PWM signal in ns
    uint32_t T_pwm_ns = 1000000000UL / F_PWM_HZ;

    // initialize PWM signal for 0V
    (void)pwm_set_dt(&pwm, T_pwm_ns, 0);
    //================================================================

    //======================== INITIALIZE ADC ========================
    int err;
    int16_t sample; // signed buffer required by Zephyr ADC helpers
    int32_t mv;
    int32_t mv_control = 0;
    int32_t mv_sample = 0;

    struct adc_sequence sequence = {
        .buffer = &sample,
        .buffer_size = sizeof(sample),
    };

    // ensure hardware driver is ready
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
    //================================================================
    
    while (1) {
        // use for loop to read from multiple ADC channels
        for (size_t i = 0; i < ARRAY_SIZE(adc_channels); i++) {
            adc_sequence_init_dt(&adc_channels[i], &sequence); // configure sequence for ADC channel
            err = adc_read_dt(&adc_channels[i], &sequence); // take reading and store the value in sample
            // if an error occurs, the error message is printed and the rest of the code for the ADC channel is skipped
            if (err) {
                printk("adc_read failed (%d)\n", err);
                continue;
            }

            mv = sample; // copies value of sample into mv to prevent sample from being overwritten later on
            //printk("adc=%d ", sample); // prints the measured sample value

            err = adc_raw_to_millivolts_dt(&adc_channels[i], &mv); //converts mv to the sample measured in mV
            // check for errors converting sampled voltage into mV
            if (err) {
                printk("(mV unavailable)\n"); // if an error occurs, print mV unavailable
                continue;
            }

            if (i == ADC_CH_CONTROL) {
                mv_control = mv;
            } else if (i == ADC_CH_MONITOR) {
                mv_sample = mv;
            }
        }

        //========== CALCULATE INJECTED CURRENT AND PWM VOLTAGE ==========
        // set circuit parameters
        uint32_t Rf_kOhm = 14900;
        uint32_t Rinj_kOhm = 22400;
        
        int64_t Vdiff_nV = ((int64_t)mv_sample - (int64_t)V_REF_mV)*1000000; //calculate difference (nV)

        int64_t Iinj_pA = Vdiff_nV/(int64_t)Rf_kOhm; // calculate injected current (pA)

        int64_t Vdrop_nV = Iinj_pA*(int64_t)Rinj_kOhm; // calculate the voltage drop across Rinj (nV)

        int32_t Vcontrol_mV = Vdrop_nV/500000;

        //================================================================

        //======================== SET PWM SIGNAL ========================
        // calculate duty from target from sampled voltage
        if (mv_control > 3300U){mv_control = 3300U; }

        uint32_t pulse_ns = (uint32_t)(
            ((uint64_t)T_pwm_ns * mv_control + V_SUPPLY_mV/2U) / V_SUPPLY_mV
        );

        // set PWM signal
        int ret = pwm_set_dt(&pwm, T_pwm_ns, pulse_ns);
        // check for any errors setting PWM signal
        if (ret) {
            printk("pwm_set_dt failed (%d)\n", ret);
            continue;
        }
        //================================================================

        /* Print both so it’s obvious what’s happening */
        printk("CTRL=%ld mV -> PWM=%u ns | MON=%ld mV | DIFF=%ld mV\n",
           (long)mv_control, pulse_ns, (long)mv_sample, (long)(Vdiff_nV/1000000));
        printk("I_inj=%ld nA -> V_CONTROL=%ld mV\n",
           (long)(Iinj_pA/1000), (long)Vcontrol_mV);

        k_sleep(K_MSEC(250)); // create delay between next sample

    }
}