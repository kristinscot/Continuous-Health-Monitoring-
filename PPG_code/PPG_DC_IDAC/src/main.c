//general
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

//hardware timers
#include <hal/nrf_gpio.h>
#include <hal/nrf_gpiote.h>
#include <hal/nrf_pwm.h>
#include <hal/nrf_ppi.h>
#include <hal/nrf_timer.h>

/* ===================== INITIALIZE ADC CHANNELS ===================== */
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
/* =================================================================== */

/* define parameters
    The frequency of the PWM signal is limited by the resolution of the DAC:
     8-bit - f_pwm_max = 62.5 kHz
     9-bit - f_pwm = 31.25 kHz
    10-bit - f_pwm_max = 15.625 kHz
    12-bit - f_pwm_max = 3.90625 kHz

    The nRF PWM uses a 16MHz timebase and integer counter. To get
    an exact frequency/resolution, use value alligned with
    16MHz/countertop
*/

//CHANGE THESE PARAMETERS TO UPDATE PWM COUNTERTOP
#define F_PWM_HZ 31250U
#define T_PWM_NS (1000000000UL / F_PWM_HZ)
//#define V_SUPPLY_mV 3300U
#define V_SUPPLY_mV 3000U
#define V_REF_mV 1650U

#define PWM_DEV NRF_PWM0

// 31.25 kHz from 16 MHz base:
// f = 16e6 / COUNTERTOP = 31.25k => COUNTERTOP = 512
#define PWM_COUNTERTOP 512

#define PWM_SEQ_POL_INV (1u << 15)


#define TS_USEC  100U

// nRF PWM sequence values are 16-bit.
// Bit15 is polarity, bits[14:0] is compare value.
// For "common" load mode with 1 channel, use value in [0..COUNTERTOP].
static volatile uint16_t idac_seq[4]; // {green, red, ir, off}

/* ================= INITIALIZE LED HARDWARE TIMING ================== */

#define T_ON_US   2000U
#define T_OFF_US  2000U

BUILD_ASSERT(T_ON_US  >= 1000U  && T_ON_US  <= 5000U,  "T_ON_US out of range");
BUILD_ASSERT(T_OFF_US >= 1000U  && T_OFF_US <= 5000U,  "T_OFF_US out of range");

/* ==================== Sampling-enable timing ======================= */
/* For concept testing (your request): For LPF of 5.9kHz -> 3*TAU */
#define T_SET_RISING_US   1450U
#define T_SET_FALLING_US  1200U

BUILD_ASSERT(T_SET_RISING_US  <= T_ON_US,  "T_SET_RISING_US must be <= T_ON_US");
BUILD_ASSERT(T_SET_FALLING_US <= T_OFF_US, "T_SET_FALLING_US must be <= T_OFF_US");

/* Sample-enable output pin you can probe on a scope */
#define S_EN_PIN   NRF_GPIO_PIN_MAP(0, 13)
/* =================================================================== */

/* LED timing output pins */
#define GRN_EN_PIN   NRF_GPIO_PIN_MAP(1, 0)
#define RED_EN_PIN   NRF_GPIO_PIN_MAP(0, 9)
#define IR_EN_PIN    NRF_GPIO_PIN_MAP(0, 10)

/* Hardware instances */
#define LED_TIMER   NRF_TIMER3   /* drives LED edges */
#define SEN_TIMER   NRF_TIMER2   /* drives delayed S_EN assert edges */
#define GPIOTE      NRF_GPIOTE
#define PPI         NRF_PPI

/* GPIOTE channels */
#define CH_GRN   0
#define CH_RED   1
#define CH_IR    2
#define CH_SEN   3

//===================== IDAC CIRCUIT PARAMETERS ======================
//define circuit parameters
#define R_F_KOHM 470
#define R_INJ_KOHM 470

//define maximum IDAC current/voltage
#define IDAC_CURRENT_MAX_uA (V_SUPPLY_mV/R_INJ_KOHM)
#define IDAC_VOLTAGE_MAX_mV (IDAC_CURRENT_MAX_uA*R_F_KOHM)
//====================================================================

/* ====================== ADC HELPER FUNCTIONS ======================= */
static int adc_init(void)
{
    int err;

    for (size_t i = 0; i < ARRAY_SIZE(adc_channels); i++) {
        if (!adc_is_ready_dt(&adc_channels[i])) {
            printk("ADC %s not ready\n", adc_channels[i].dev->name);
            return -1;
        }

        err = adc_channel_setup_dt(&adc_channels[i]);
        if (err) {
            printk("Channel setup failed (%d)\n", err);
            return err;
        }
    }
    return 0;
}

static int sample_channels(int32_t *p)
{
    int err;
    int16_t sample;
    int32_t mv;

    struct adc_sequence sequence = {
        .buffer = &sample,
        .buffer_size = sizeof(sample),
    };

    for (size_t i = 0; i < ARRAY_SIZE(adc_channels); i++) {
        adc_sequence_init_dt(&adc_channels[i], &sequence);
        err = adc_read_dt(&adc_channels[i], &sequence);
        if (err) {
            printk("adc_read failed (%d)\n", err);
            return err;
        }

        mv = sample;
        err = adc_raw_to_millivolts_dt(&adc_channels[i], &mv);
        if (err) {
            printk("(mV unavailable)\n");
            return err;
        }

        p[i] = mv;
    }
    return 0;
}

static inline bool sen_is_high(void) {
    return (NRF_P0->IN & (1u << 13)) != 0;   // P0.13
}

typedef enum {
    LED_GREEN = 0,
    LED_RED   = 1,
    LED_IR    = 2,
    LED_OFF   = 3,
    LED_BAD   = 4   // more than one high (should never happen)
} led_state_t;

static inline led_state_t get_led_state(void)
{
    /* Read both ports because GRN is on P1.0, RED/IR on P0.9/P0.10 */
    uint32_t p0 = NRF_P0->IN;
    uint32_t p1 = NRF_P1->IN;

    //bool grn = (p1 & (1u << 0))  != 0;   // P1.0
    bool grn = (p1 & (1u << 0))  == 0;   // P1.0 active low
    bool red = (p0 & (1u << 9))  == 0;   // P0.9 active low
    bool ir  = (p0 & (1u << 10)) == 0;   // P0.10 active low

    uint32_t m = (grn << 0) | (red << 1) | (ir << 2);

    switch (m) {
    case 0b001: return LED_GREEN;
    case 0b010: return LED_RED;
    case 0b100: return LED_IR;
    case 0b000: return LED_OFF;
    default:    return LED_BAD;  // overlap/glitch
    }
}
/* =================================================================== */

/* =================== GPIO/GPIOTE/PPI/TIMER HELPERS ================= */
static void gpiote_toggle_init(uint32_t ch, uint32_t pin, bool initial_high)
{
    nrf_gpio_cfg_output(pin);

    if (initial_high) {
        nrf_gpio_pin_set(pin);
    } else {
        nrf_gpio_pin_clear(pin);
    }
    
    nrf_gpiote_task_configure(GPIOTE, ch, pin,
                              NRF_GPIOTE_POLARITY_TOGGLE,
                              initial_high ? NRF_GPIOTE_INITIAL_VALUE_HIGH
                                           : NRF_GPIOTE_INITIAL_VALUE_LOW);
    nrf_gpiote_task_enable(GPIOTE, ch);
}

static void ppi_connect(uint8_t ppi_ch, uint32_t evt_addr, uint32_t task_addr)
{
    nrf_ppi_channel_endpoint_setup(PPI, (nrf_ppi_channel_t)ppi_ch, evt_addr, task_addr);
    nrf_ppi_channel_enable(PPI, (nrf_ppi_channel_t)ppi_ch);
}

static void ppi_connect_with_fork(uint8_t ppi_ch, uint32_t evt_addr, uint32_t task0_addr, uint32_t task1_addr)
{
    PPI->CH[ppi_ch].EEP = evt_addr;
    PPI->CH[ppi_ch].TEP = task0_addr;
    PPI->FORK[ppi_ch].TEP = task1_addr;
    nrf_ppi_channel_enable(PPI, (nrf_ppi_channel_t)ppi_ch);
}

static void timer_init_1mhz(NRF_TIMER_Type *t)
{
    nrf_timer_task_trigger(t, NRF_TIMER_TASK_STOP);
    nrf_timer_task_trigger(t, NRF_TIMER_TASK_CLEAR);

    nrf_timer_mode_set(t, NRF_TIMER_MODE_TIMER);
    nrf_timer_bit_width_set(t, NRF_TIMER_BIT_WIDTH_32);

    /* 16 MHz / 2^4 = 1 MHz -> 1 tick = 1 us */
    nrf_timer_prescaler_set(t, 4);

    /* No interrupts; use EVENTS via PPI */
    nrf_timer_int_disable(t, 0xFFFFFFFF);
}

/* LED sequence boundary times (in us, relative to timer clear) */
static void program_led_timing(uint32_t t_on_us, uint32_t t_off_us)
{
    const uint32_t t0 = 1U;                 /* Green ON */
    const uint32_t t1 = t0 + t_on_us;       /* Green OFF + Red ON */
    const uint32_t t2 = t1 + t_on_us;       /* Red OFF   + IR  ON */
    const uint32_t t3 = t2 + t_on_us;       /* IR OFF    (enter OFF) */
    const uint32_t t4 = t3 + t_off_us;      /* end of frame */

    nrf_timer_cc_set(LED_TIMER, NRF_TIMER_CC_CHANNEL0, t0);
    nrf_timer_cc_set(LED_TIMER, NRF_TIMER_CC_CHANNEL1, t1);
    nrf_timer_cc_set(LED_TIMER, NRF_TIMER_CC_CHANNEL2, t2);
    nrf_timer_cc_set(LED_TIMER, NRF_TIMER_CC_CHANNEL3, t3);
    nrf_timer_cc_set(LED_TIMER, NRF_TIMER_CC_CHANNEL4, t4);

    /* Auto-clear LED timer on CC4 to repeat */
    nrf_timer_shorts_disable(LED_TIMER, 0xFFFFFFFF);
    nrf_timer_shorts_enable(LED_TIMER, NRF_TIMER_SHORT_COMPARE4_CLEAR_MASK);

    /* Program SEN_TIMER compare points for delayed sampling-enable asserts */
    const uint32_t s0 = t0 + T_SET_RISING_US;   /* enable during Green */
    const uint32_t s1 = t1 + T_SET_RISING_US;   /* enable during Red   */
    const uint32_t s2 = t2 + T_SET_RISING_US;   /* enable during IR    */
    const uint32_t s3 = t3 + T_SET_FALLING_US;  /* enable during OFF   */

    nrf_timer_cc_set(SEN_TIMER, NRF_TIMER_CC_CHANNEL0, s0);
    nrf_timer_cc_set(SEN_TIMER, NRF_TIMER_CC_CHANNEL1, s1);
    nrf_timer_cc_set(SEN_TIMER, NRF_TIMER_CC_CHANNEL2, s2);
    nrf_timer_cc_set(SEN_TIMER, NRF_TIMER_CC_CHANNEL3, s3);
}

/* Route:
 * LED edges from LED_TIMER -> LED GPIOTE
 * S_EN OFF edges from LED_TIMER boundaries -> S_EN GPIOTE
 * S_EN ON edges from SEN_TIMER delayed compares -> S_EN GPIOTE
 * Keep SEN_TIMER phase-locked: LED_TIMER CC4 event clears SEN_TIMER each frame
 */
static void ppi_route_all(void)
{
    /* LED_TIMER events */
    const uint32_t led_evt0 = (uint32_t)&LED_TIMER->EVENTS_COMPARE[0];
    const uint32_t led_evt1 = (uint32_t)&LED_TIMER->EVENTS_COMPARE[1];
    const uint32_t led_evt2 = (uint32_t)&LED_TIMER->EVENTS_COMPARE[2];
    const uint32_t led_evt3 = (uint32_t)&LED_TIMER->EVENTS_COMPARE[3];
    const uint32_t led_evt4 = (uint32_t)&LED_TIMER->EVENTS_COMPARE[4];

    /* SEN_TIMER events (delayed asserts) */
    const uint32_t sen_evt0 = (uint32_t)&SEN_TIMER->EVENTS_COMPARE[0];
    const uint32_t sen_evt1 = (uint32_t)&SEN_TIMER->EVENTS_COMPARE[1];
    const uint32_t sen_evt2 = (uint32_t)&SEN_TIMER->EVENTS_COMPARE[2];
    const uint32_t sen_evt3 = (uint32_t)&SEN_TIMER->EVENTS_COMPARE[3];

    /* GPIOTE tasks */
    const uint32_t task_grn = (uint32_t)&GPIOTE->TASKS_OUT[CH_GRN];
    const uint32_t task_red = (uint32_t)&GPIOTE->TASKS_OUT[CH_RED];
    const uint32_t task_ir  = (uint32_t)&GPIOTE->TASKS_OUT[CH_IR];
    const uint32_t task_sen = (uint32_t)&GPIOTE->TASKS_OUT[CH_SEN];

    /* ----------------------- LEDs ----------------------- */
    // CC0: Green ON
    ppi_connect(0, led_evt0, task_grn);

    // CC1: Green OFF + Red ON
    ppi_connect_with_fork(1, led_evt1, task_grn, task_red);

    //. CC2: Red OFF + IR ON
    ppi_connect_with_fork(2, led_evt2, task_red, task_ir);

    // CC3: IR OFF
    ppi_connect(3, led_evt3, task_ir);

    /* -------- S_EN: ON edges come from SEN_TIMER delayed compares -------- */
    ppi_connect(4, sen_evt0, task_sen); /* ON during Green after T_SET_RISING */
    ppi_connect(5, sen_evt1, task_sen); /* ON during Red   after T_SET_RISING */
    ppi_connect(6, sen_evt2, task_sen); /* ON during IR    after T_SET_RISING */
    ppi_connect(7, sen_evt3, task_sen); /* ON during OFF   after T_SET_FALLING */

    /* -------- S_EN: OFF edges reuse LED boundary events (shared idea) ---- */
    /* End Green window at t1 */
    ppi_connect(8,  led_evt1, task_sen);
    /* End Red window at t2 */
    ppi_connect(9,  led_evt2, task_sen);
    /* End IR window at t3 */
    ppi_connect(10, led_evt3, task_sen);
    /* End OFF window at t4 (frame end) */
    ppi_connect(11, led_evt4, task_sen);

    /* -------- Keep SEN_TIMER aligned each frame -------- */
    /* When LED frame ends (CC4), clear SEN_TIMER so its delayed events repeat aligned */
    const uint32_t sen_clear_task = (uint32_t)&SEN_TIMER->TASKS_CLEAR;
    ppi_connect(12, led_evt4, sen_clear_task);
}

static void sequencer_start(void)
{
    /* Force all outputs low before starting */
    nrf_gpio_pin_set(GRN_EN_PIN); //start inverted
    nrf_gpio_pin_set(RED_EN_PIN); //start inverted
    nrf_gpio_pin_set(IR_EN_PIN); //start inverted
    nrf_gpio_pin_clear(S_EN_PIN);

    /* Stop/clear timers */
    nrf_timer_task_trigger(LED_TIMER, NRF_TIMER_TASK_STOP);
    nrf_timer_task_trigger(LED_TIMER, NRF_TIMER_TASK_CLEAR);
    nrf_timer_task_trigger(SEN_TIMER, NRF_TIMER_TASK_STOP);
    nrf_timer_task_trigger(SEN_TIMER, NRF_TIMER_TASK_CLEAR);

    /* Clear event registers we use */
    for (int i = 0; i < 5; i++) {
        LED_TIMER->EVENTS_COMPARE[i] = 0;
    }
    for (int i = 0; i < 4; i++) {
        SEN_TIMER->EVENTS_COMPARE[i] = 0;
    }

    /* Program compare times */
    program_led_timing(T_ON_US, T_OFF_US);

    /* Start both timers.
     * Frame-to-frame alignment is maintained by PPI clearing SEN_TIMER on LED_TIMER CC4.
     */
    nrf_timer_task_trigger(SEN_TIMER, NRF_TIMER_TASK_START);
    nrf_timer_task_trigger(LED_TIMER, NRF_TIMER_TASK_START);
}
/* =================================================================== */

/* ====================== IDAC HELPER FUNCTIONS ====================== */
int32_t update_IDAC_ctrl(uint32_t dark_avg_mv){
    //calculate injected current and control voltage
    int64_t Vdiff_nV = ((int64_t)dark_avg_mv - (int64_t)V_REF_mV)*1000000; //calculate difference (nV) (this is the total amount of adc measurement voltage that the IDAC is compensating for)
    int64_t Iinj_pA = Vdiff_nV/(int64_t)R_F_KOHM; // calculate injected current (pA)
    int64_t Vdrop_nV = Iinj_pA*(int64_t)R_INJ_KOHM; // calculate the voltage drop across Rinj (nV)
    int32_t Vcontrol_mV = Vdrop_nV*2/1000000;

    //clamp at limits
    if (Vcontrol_mV > (int32_t)V_SUPPLY_mV){
        Vcontrol_mV = V_SUPPLY_mV;
    } else if (Vcontrol_mV < 0){
        Vcontrol_mV = 0U;
    }

    //printk("Delta Control=%ld mV \n",(long)delta_Vcontrol_mV);

    return Vcontrol_mV;
}

// Convert from mV control to countertop?
static inline uint16_t mv_to_pwm_cmp(int32_t Vcontrol_mV)
{
    if (Vcontrol_mV < 0) Vcontrol_mV = 0;
    if (Vcontrol_mV > V_SUPPLY_mV) Vcontrol_mV = V_SUPPLY_mV;

    uint32_t top = PWM_COUNTERTOP;        // COUNTERTOP register value
    uint32_t span = top + 1;              // number of ticks in the period

    uint32_t cmp = ((uint64_t)Vcontrol_mV * span + (V_SUPPLY_mV/2)) / V_SUPPLY_mV;

    if (cmp > top) cmp = top;             // or (top - 1) if you want never-100%
    return (uint16_t)cmp;
}

static void idac_pwm_init(void)
{
    // Stop
    nrf_pwm_task_trigger(PWM_DEV, NRF_PWM_TASK_STOP);

    // 1) Basic PWM config: 1 channel used (CH0)
    PWM_DEV->PRESCALER = PWM_PRESCALER_PRESCALER_DIV_1; // 16 MHz
    PWM_DEV->COUNTERTOP = PWM_COUNTERTOP;
    PWM_DEV->MODE = PWM_MODE_UPDOWN_Up;
    PWM_DEV->DECODER = (PWM_DECODER_LOAD_Common << PWM_DECODER_LOAD_Pos) |
                       (PWM_DECODER_MODE_NextStep << PWM_DECODER_MODE_Pos);

    // Connect PWM0 CH0 to P0.15
    PWM_DEV->PSEL.OUT[0] =
        (15 << PWM_PSEL_OUT_PIN_Pos) |
        (0  << PWM_PSEL_OUT_PORT_Pos) |
        (PWM_PSEL_OUT_CONNECT_Connected << PWM_PSEL_OUT_CONNECT_Pos);

    // Disconnect others
    PWM_DEV->PSEL.OUT[1] = (PWM_PSEL_OUT_CONNECT_Disconnected << PWM_PSEL_OUT_CONNECT_Pos);
    PWM_DEV->PSEL.OUT[2] = (PWM_PSEL_OUT_CONNECT_Disconnected << PWM_PSEL_OUT_CONNECT_Pos);
    PWM_DEV->PSEL.OUT[3] = (PWM_PSEL_OUT_CONNECT_Disconnected << PWM_PSEL_OUT_CONNECT_Pos);

    // 2) Output pin set in devicetree/pinctrl already; ensure CH0 enabled
    PWM_DEV->ENABLE = 1;

    // 3) Sequence 0 points at our 4-step buffer
    PWM_DEV->SEQ[0].PTR = (uint32_t)idac_seq;
    PWM_DEV->SEQ[0].CNT = 4; // 4 values
    PWM_DEV->SEQ[0].REFRESH = 0; // no extra hold
    PWM_DEV->SEQ[0].ENDDELAY = 0;

    // Optional: Loop behavior. We will re-SEQSTART[0] every frame via PPI.
    PWM_DEV->LOOP = 0;
}

// PPI wiring:
// - CC0 starts the sequence at index 0 each frame
// - CC1/2/3 advance step to index 1/2/3
static void idac_pwm_ppi_route(void)
{
    uint32_t led_evt0 = (uint32_t)&LED_TIMER->EVENTS_COMPARE[0];
    uint32_t led_evt1 = (uint32_t)&LED_TIMER->EVENTS_COMPARE[1];
    uint32_t led_evt2 = (uint32_t)&LED_TIMER->EVENTS_COMPARE[2];
    uint32_t led_evt3 = (uint32_t)&LED_TIMER->EVENTS_COMPARE[3];

    uint32_t task_seqstart0 = (uint32_t)&PWM_DEV->TASKS_SEQSTART[0];
    uint32_t task_nextstep  = (uint32_t)&PWM_DEV->TASKS_NEXTSTEP;

    // pick unused PPI channels (example 13-16)
    PPI->CH[13].EEP = led_evt0;  PPI->CH[13].TEP = task_seqstart0;
    PPI->CH[14].EEP = led_evt1;  PPI->CH[14].TEP = task_nextstep;
    PPI->CH[15].EEP = led_evt2;  PPI->CH[15].TEP = task_nextstep;
    PPI->CH[16].EEP = led_evt3;  PPI->CH[16].TEP = task_nextstep;

    nrf_ppi_channel_enable(NRF_PPI, (nrf_ppi_channel_t)13);
    nrf_ppi_channel_enable(NRF_PPI, (nrf_ppi_channel_t)14);
    nrf_ppi_channel_enable(NRF_PPI, (nrf_ppi_channel_t)15);
    nrf_ppi_channel_enable(NRF_PPI, (nrf_ppi_channel_t)16);
}

/* =================================================================== */

int main(void)
{
    /* ADC */
    int32_t samples[ARRAY_SIZE(adc_channels)];
    int32_t *p = samples;

    if (adc_init() != 0) {
        printk("ADC Initialization Failed\n");
        return -1;
    }

    /* GPIOTE outputs (LEDs + S_EN) */
    gpiote_toggle_init(CH_GRN, GRN_EN_PIN, true); // invert green
    gpiote_toggle_init(CH_RED, RED_EN_PIN, true); //invert red
    gpiote_toggle_init(CH_IR,  IR_EN_PIN, true); //invert IR
    gpiote_toggle_init(CH_SEN, S_EN_PIN, false);

    /* Timers */
    timer_init_1mhz(LED_TIMER);
    timer_init_1mhz(SEN_TIMER);

    /* PPI routing */
    ppi_route_all();

    /* Start sequence */
    sequencer_start();


    //for time multiplexing
    uint32_t last = k_cycle_get_32();
    led_state_t last_st = get_led_state();
    uint32_t st_reading[4];
    uint32_t cycle_dt = 0;
    uint32_t sample_sum = 0;
    uint32_t num_samples = 0;

    uint32_t ac_reading[4] = {0,0,0,0};

    //Initialize IDAC 0 feedback
    uint32_t state_DC_levels_mv[4] = {V_REF_mV, V_REF_mV, V_REF_mV, V_REF_mV};

    idac_seq[0] = PWM_SEQ_POL_INV | mv_to_pwm_cmp(0);
    idac_seq[1] = PWM_SEQ_POL_INV | mv_to_pwm_cmp(0);
    idac_seq[2] = PWM_SEQ_POL_INV | mv_to_pwm_cmp(0);
    idac_seq[3] = PWM_SEQ_POL_INV | mv_to_pwm_cmp(0);

    //for idac
    uint32_t f_IDAC = 1; //Hz
    uint32_t T_IDAC_us = 1000000/f_IDAC;
    uint32_t IDAC_time_acc_us = 0;
    uint32_t IDAC_sample_count = 0;
    uint32_t ac_sum = 0;
    uint32_t IDAC_sample_acc_mv[4];

    idac_pwm_init();
    idac_pwm_ppi_route();

    while (1) {

        // sample all signals
        if (sample_channels(p) != 0) {
            printk("Sampling failed\n");
            continue;
        }

        //get current LED state
        led_state_t st = get_led_state();

        //check if state changed and store avg sample
        if (st != last_st){

            if (num_samples != 0){
                st_reading[last_st] = sample_sum/num_samples;
                ac_reading[last_st] = ac_sum/num_samples;
                sample_sum = 0;
                ac_sum = 0;
                num_samples = 0;
            }
            //check if cycle completed and output cycle stats
            if (st == 0) {
                
                printk("%u,%ld,%ld,%ld,%ld\n",
                    (unsigned)k_cyc_to_us_floor32(cycle_dt),
                    (long)ac_reading[0],
                    (long)ac_reading[1],
                    (long)ac_reading[2],
                    (long)ac_reading[3]
                );

                for (size_t i=0; i<ARRAY_SIZE(state_DC_levels_mv); i++) {
                    // IDAC_sample_acc_mv is the sum of the adc readings since last IDAC calculation. When divided by IDAC_sample_count, this gives the average adc reading, the amount that should be additionally offset by the IDAC next time
                    IDAC_sample_acc_mv[i] += st_reading[i];
                }
                IDAC_time_acc_us += k_cyc_to_us_floor32(cycle_dt);
                IDAC_sample_count++;
                if (IDAC_time_acc_us >= T_IDAC_us){

                    //printk("DC LEVELS: ");

                    //calculate new IDAC DC Levels
                    for (size_t i = 0; i < ARRAY_SIZE(state_DC_levels_mv); i++) {
                        state_DC_levels_mv[i] = state_DC_levels_mv[i] + (IDAC_sample_acc_mv[i]/IDAC_sample_count) - V_REF_mV;

                        if (state_DC_levels_mv[i] < V_REF_mV){
                            state_DC_levels_mv[i] = V_REF_mV;
                        } else if (state_DC_levels_mv[i] > IDAC_VOLTAGE_MAX_mV) {
                            state_DC_levels_mv[i] = IDAC_VOLTAGE_MAX_mV;
                        }

                        uint32_t Vcontrol_mv = update_IDAC_ctrl(state_DC_levels_mv[i]);

                        idac_seq[i] = PWM_SEQ_POL_INV | mv_to_pwm_cmp(Vcontrol_mv);

                        IDAC_sample_acc_mv[i] = 0;

                       // printk("%ld ", (long)(state_DC_levels_mv[i]));

                    }

                   // printk("\n");

                    //IDAC cycle complete reset all
                    IDAC_time_acc_us = 0;
                    IDAC_sample_count = 0;
                }                
                cycle_dt = 0;
            }
            last_st = st;
        }

        //check if PPG sampling is enabled
        if (sen_is_high()) {
            // determine position in time
            uint32_t now = k_cycle_get_32();
            uint32_t dt = now - last;
            last = now;
            sample_sum += p[0];
            ac_sum += p[1];
            num_samples++;
            cycle_dt += dt;
        }
        
        //sleep to control sampling rate
        k_sleep(K_USEC(TS_USEC));
    }
}