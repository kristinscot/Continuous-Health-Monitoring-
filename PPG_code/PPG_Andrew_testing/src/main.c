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

/*-------------------------All of the below are initializations for the IMU (until the next comment of this type).*/
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
                #include <zephyr/drivers/spi.h>
                #include <zephyr/drivers/gpio.h>
                #include <math.h>//Not actually sure if I need this one.

                //keep these global so I can write to them from within a function.
                typedef struct {
                int tempx;
                int tempy;
                int tempz;
                } three_ints;

                //Register values
                /* ----- READ registers ----- */
                    #define READ_REG1  0x00
                    #define READ_REG2  0x20
                    #define READ_REG3  0x01
                    #define READ_REG4  0x02
                    #define READ_REG5  0x05
                    #define READ_REG6  0x16
                    #define READ_REG7  0x15
                    #define READ_REG8  0x03
                    #define READ_REG9  0x04
                    #define READ_REG10 0x05

                    /* ----- WRITE registers ----- */
                    #define WRITE_REG1  0x36
                    #define WRITE_REG2  0x20
                    #define WRITE_REG3  0x37
                    #define WRITE_REG4  0x00
                    #define WRITE_REG5  0x00
                    #define WRITE_REG6  0x00
                    #define WRITE_REG7  0x00
                    #define WRITE_REG8  0x00
                    #define WRITE_REG9  0x00
                    #define WRITE_REG10 0x00

                    /* ----- 16‑bit write values ----- */
                    #define WRITE_VALUE16_1  0x0200
                    #define WRITE_VALUE16_2  0x4039
                    #define WRITE_VALUE16_3  0x0001
                    #define WRITE_VALUE16_4  0x0000
                    #define WRITE_VALUE16_5  0x0000
                    #define WRITE_VALUE16_6  0x0000
                    #define WRITE_VALUE16_7  0x0000
                    #define WRITE_VALUE16_8  0x0000
                    #define WRITE_VALUE16_9  0x0000
                    #define WRITE_VALUE16_10 0x0000
                //end of Register values

                // SPI configuration details
                    /*SPI configuration stuff*/
                    #define WRITE_MSB_FIRST  0

                    /* ---------------------- SPI controller and manual CS pin --------------------- */
                    #define SPI_CTRL_NODE  DT_NODELABEL(spi1)   /* &spi1 enabled via overlay */
                    #define CS_GPIO_NODE   DT_NODELABEL(gpio0)  /* &gpio0 */
                    #define CS_GPIO_PIN    31                   /* Manual CS at P0.29 (active-low) listed at pin 29 in this code*/

                    /* ---------------------- Helper: assert/deassert manual CS -------------------- */
                    static inline void cs_assert(const struct device *gpio0)
                    {
                        /* Active-low CS: drive low to select */
                        gpio_pin_set(gpio0, CS_GPIO_PIN, 0);
                    }
                    static inline void cs_deassert(const struct device *gpio0)
                    {
                        /* Drive high to release */
                        gpio_pin_set(gpio0, CS_GPIO_PIN, 1);
                    }
                //end of spi config details


                //Write 16 bits to register
                    static int bmi330_write_reg16(const struct device *spi_dev,
                                                const struct spi_config *cfg,
                                                const struct device *gpio0,
                                                uint8_t base_reg,
                                                uint16_t value)
                    {
                        uint8_t addr_wr = (uint8_t)(base_reg & 0x7F);  /* bit7 = 0 for WRITE */
                        uint8_t msb = (uint8_t)((value >> 8) & 0xFF);
                        uint8_t lsb = (uint8_t)( value       & 0xFF);
                        uint8_t b0 = WRITE_MSB_FIRST ? msb : lsb;      /* LSB first by default */
                        uint8_t b1 = WRITE_MSB_FIRST ? lsb : msb;

                        uint8_t tx[3] = { addr_wr, b0, b1 };
                        uint8_t rx[3] = { 0, 0, 0 };                   /* sink */

                        struct spi_buf txb = { .buf = tx, .len = sizeof(tx) };
                        struct spi_buf rxb = { .buf = rx, .len = sizeof(rx) };
                        struct spi_buf_set tx_set = { .buffers = &txb, .count = 1 };
                        struct spi_buf_set rx_set = { .buffers = &rxb, .count = 1 };

                        cs_assert(gpio0);
                        k_msleep(1);
                        int ret = spi_transceive(spi_dev, cfg, &tx_set, &rx_set);
                        cs_deassert(gpio0);
                        return ret;
                    }
                //end of write

                // Read 16 bits
                    static int bmi330_read_reg16(const struct device *spi_dev,
                                                const struct spi_config *cfg,
                                                const struct device *gpio0,
                                                uint8_t base_reg,
                                                uint16_t *out)
                    {
                        if (!out) return -EINVAL;

                        uint8_t tx[4] = { (uint8_t)(base_reg | 0x80), 0x00, 0x00, 0x00 }; /* bit7=1 for READ */
                        uint8_t rx[4] = { 0, 0, 0, 0 };

                        struct spi_buf txb = { .buf = tx, .len = sizeof(tx) };
                        struct spi_buf rxb = { .buf = rx, .len = sizeof(rx) };
                        struct spi_buf_set tx_set = { .buffers = &txb, .count = 1 };
                        struct spi_buf_set rx_set = { .buffers = &rxb, .count = 1 };

                        cs_assert(gpio0);
                        k_msleep(1);
                        int ret = spi_transceive(spi_dev, cfg, &tx_set, &rx_set);
                        cs_deassert(gpio0);


                        uint8_t zero = rx[0];
                        uint8_t one = rx[1];
                        uint8_t two = rx[2];
                        uint8_t three = rx[3];


                    // Combine as unsigned, then cast to signed
                    uint16_t u16 = ((uint16_t)three << 8) | (uint16_t)two; // e.g., 0x10 0x14 -> 0x1014
                    int16_t  s16 = (int16_t)u16;                           // two's complement signed
                    int      value = (int)s16;                              // sign-extended to int
                    int mg = (int)(( (float)s16 / 4096.0f ) * 1000.0f);  // example for ±8g

                    // Combine and interpret as signed
                    //int16_t s16 = combine_to_s16_from_big_endian(three, two);


                        // printk("                         READ16 reg 0x%02X ->%02X %02X %02X %02X     (As Hex)%02X      As dedimal  %02d      in mg %d \n",
                        //        base_reg, zero, one, two, three, value, value, mg);
                        //        //Take special not that %02X prints a 2 letter HEX VALUE, d prints a signed decimal.
                        // k_msleep(250);//temporary to slow down the printing


                        return value;
                    }
                //end reading 16 bits



                //This is the function that I made to read data from the fifo buffer.
                three_ints bmi330_read_fifo(const struct device *spi_dev,
                                                const struct spi_config *cfg,
                                                const struct device *gpio0,
                                                uint8_t base_reg)
                {
                    // if (!outx || !outy || !outz) return -EINVAL;
                    // The first 8 bytes (4 hex letters) are dummy bytes.
                    // After that the data is x,y,z in little edian format,  thus FF00 is 255 in decimal.

                    uint8_t tx[8] = {
                        (uint8_t)(base_reg | 0x80),  // READ command (bit7 = 1)
                        0x00,0x00,0x00,0x00,0x00,0x00,0x00
                    };
                    uint8_t rx[8] = {0};

                    struct spi_buf txb = { .buf = tx, .len = sizeof(tx) };
                    struct spi_buf rxb = { .buf = rx, .len = sizeof(rx) };
                    struct spi_buf_set tx_set = { .buffers = &txb, .count = 1 };
                    struct spi_buf_set rx_set = { .buffers = &rxb, .count = 1 };

                    cs_assert(gpio0);
                    k_usleep(20);
                    int ret = spi_transceive(spi_dev, cfg, &tx_set, &rx_set);
                    cs_deassert(gpio0);

                    // if (ret) {
                    //     printk("bmi330 burst(8) spi_transceive failed: %d\n", ret);
                    //     return ret;
                    // }

                    uint8_t zero = rx[0];
                    uint8_t one = rx[1];
                    uint8_t two = rx[2];
                    uint8_t three = rx[3];
                    uint8_t four = rx[4];
                    uint8_t five = rx[5];
                    uint8_t six = rx[6];
                    uint8_t seven = rx[7];


                    int xnow;
                    int ynow;
                    int znow;


                    uint16_t u16 = ((uint16_t)three << 8) | (uint16_t)two; // e.g., 0x10 0x14 -> 0x1014
                    int16_t  s16 = (int16_t)u16;                           // two's complement signed
                        xnow = (int)s16;                              // sign-extended to int

                    u16 = ((uint16_t)five << 8) | (uint16_t)four; // e.g., 0x10 0x14 -> 0x1014
                    s16 = (int16_t)u16;                           // two's complement signed
                        ynow = (int)s16;                              // sign-extended to int

                    u16 = ((uint16_t)seven << 8) | (uint16_t)six; // e.g., 0x10 0x14 -> 0x1014
                    s16 = (int16_t)u16;                           // two's complement signed
                        znow = (int)s16;                              // sign-extended to int

                        three_ints out = {xnow,ynow,znow};
                    // outx=little_endian_to_int(rx[2],rx[3]);
                    // outy=little_endian_to_int(rx[4],rx[5]);
                    // outz=little_endian_to_int(rx[6],rx[7]);
                        
                    //printk("%d,  ", outy);

                    /* Print all 8 bytes */
                    //printk("  %02X %02X %02X %02X  %02X %02X %02X %02X\n",
                        //    rx[0], rx[1], rx[2], rx[3],
                        //    rx[4], rx[5], rx[6], rx[7]);

                    return out;
                }

                //needed to initialize spi
                const struct device *spi_dev = DEVICE_DT_GET(SPI_CTRL_NODE);
                const struct device *gpio0   = DEVICE_DT_GET(CS_GPIO_NODE);
                    struct spi_config cfg = {
                        .frequency = 8000000U,
                        .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
                        .slave     = 0,
                        .cs        = NULL, /* manual CS via GPIO */
                    };
                //Temporary variables for reading and writing
                int ret;
                uint16_t v16;

                int downsampled_IMU();//Prototype to avoid implicit decleration later in code

                //magnitude function so that I can get the magnidue of x,y,x acceleration data.
                    int magnitude(int x, int y, int z) {
                        float fx = (float)x;//convert to float so I can do math.
                        float fy = (float)y;//convert to float so I can do math.
                        float fz = (float)z;//convert to float so I can do math.
                        float m = sqrtf(fx*fx + fy*fy + fz*fz);
                        return (int)m;   // truncate to integer and return.
                    }
                //end of magnitude

                //These must be global so that it can use the previous values in the downsampled values.
                float downsampled_x=0.0;
                float downsampled_y=0.0;
                float downsampled_z=0.0;

                // This is my filtered-X LMS algoritm funciton which will be fed the current ppg value and output an adjusted value.
                int downsampled_IMU(){

                    int fill_level;
                    int magnitude_of_downsampled;

                    /* ---- READ #7 ---- */ //This checks the FIFO level
                    fill_level = bmi330_read_reg16(spi_dev, &cfg, gpio0, READ_REG7, &v16);
                    fill_level=fill_level/3;
                    k_usleep(20);

                    if (fill_level<37){//checks if there is not way to much data in the buffer.
                        for(int i=0; i<fill_level; i++){
                    three_ints myoutput = bmi330_read_fifo(spi_dev, &cfg, gpio0, READ_REG6);
                    downsampled_x = downsampled_x + (0.6 * (myoutput.tempx - downsampled_x));
                    downsampled_y = downsampled_y + (0.6 * (myoutput.tempy - downsampled_y));
                    downsampled_z = downsampled_z + (0.6 * (myoutput.tempz - downsampled_z));
                    //----------------put the below back in to print stuff.
                    // printk("Fifo data: ");
                    // printk("%d,  ", myoutput.tempx);
                    // printk("%d,  ", myoutput.tempy);
                    // printk("%d,  ", myoutput.tempz);
                    // printk("\n");
                    k_usleep(10);
                        }
                    //----------------put the below back in to print stuff.
                    // printk("Downsampled cast as integers: ");    
                    // printk("%d,  ", (int)downsampled_x);
                    // printk("%d,  ", (int)downsampled_y);
                    // printk("%d,  ", (int)downsampled_z);
                    // printk("\n");

                    }
                    else{
                        /* ---- WRITE #3 ---- *///clear the fifo buffer of it's data, since there is too much currently stored in it.
                    printk("WRITE16 #3: reg 0x%02X <- 0x%04X\n", WRITE_REG3, WRITE_VALUE16_3);
                    ret = bmi330_write_reg16(spi_dev, &cfg, gpio0, WRITE_REG3, WRITE_VALUE16_3);
                    if (ret) { printk("Write #3 failed: %d\n", ret); return; }
                    k_usleep(10);
                    downsampled_x=0;
                    downsampled_y=0;
                    downsampled_z=2048;//default to 1g so that we don't mess up the data to much just becasue we haven't read recently.
                }//end of downsampled_IMU function.

                magnitude_of_downsampled = magnitude((int)downsampled_x,(int)downsampled_y,(int)downsampled_z);
                //----------------put the below back in to print stuff.
                //printk("Magnitude: ");
                //k_usleep(10);
                //printk("%d,  ", magnitude_of_downsampled);
                //k_usleep(10);
                //printk("\n");

                printk(",IMU->,%d,%d,%d,",
                    (int)downsampled_x, 
                    (int)downsampled_y, 
                    (int)downsampled_z);

                return magnitude_of_downsampled;
                }
/*-------------------------All of the above are initializations for the IMU (until the next comment of this type).*/
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////







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
// #define V_REF_mV 1650U
#define V_REF_mV 1500U

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
#define R_F_KOHM 1500
#define R_INJ_KOHM 470 //THIS IS NOT BEING USED SINCE THE IDAC IS DISCONNECTED

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
/*Below is some initiation code for the IMU to talk to SPI*/
            k_msleep(2000);

            /* Resolve devices and make sure drivers are ready */

            if (!device_is_ready(spi_dev)) { printk("SPI not ready\n"); return; }
            if (!device_is_ready(gpio0))   { printk("GPIO0 not ready\n"); return; }

            /* Manual CS pin setup: idle HIGH (inactive) */
            gpio_pin_configure(gpio0, CS_GPIO_PIN, GPIO_OUTPUT);
            gpio_pin_set(gpio0, CS_GPIO_PIN, 1);

            k_msleep(1);

            /* ---- READ #1 ---- */
            ret = bmi330_read_reg16(spi_dev, &cfg, gpio0, READ_REG1, &v16);

            /* ---- WRITE #1 ---- */
            printk("WRITE16 #1: reg 0x%02X <- 0x%04X\n", WRITE_REG1, WRITE_VALUE16_1);
            ret = bmi330_write_reg16(spi_dev, &cfg, gpio0, WRITE_REG1, WRITE_VALUE16_1);
            k_msleep(1);

            /* ---- WRITE #2 ---- */
            printk("WRITE16 #2: reg 0x%02X <- 0x%04X\n", WRITE_REG2, WRITE_VALUE16_2);
            ret = bmi330_write_reg16(spi_dev, &cfg, gpio0, WRITE_REG2, WRITE_VALUE16_2);
            if (ret) { printk("Write #2 failed: %d\n", ret); return; }
            k_msleep(1);

            ret = downsampled_IMU();//to help clear out initial junk values from buffer.
            k_msleep(500);
            ret = downsampled_IMU();//to help clear out initial junk values from buffer.
            k_msleep(500);
/*Above is some initiation code for the IMU to talk to SPI*/






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
                
                printk("%u,%ld,%ld,%ld,%ld",//I took out the newline in this print, since I will include it in my print -Andrew
                    (unsigned)k_cyc_to_us_floor32(cycle_dt),
                    (long)ac_reading[0],
                    (long)ac_reading[1],
                    (long)ac_reading[2],
                    (long)ac_reading[3]
                );
/*Below is the call to get and print the IMU data for this specific time*/
                    int IMU_mag = downsampled_IMU();
                    printk("%d,\n", IMU_mag);
                    //Using the print here and in the funciton itself this prints: x,y,z, magnitude (all downsampled)
/*Above is the call to get and print the IMU data for this specific time*/


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