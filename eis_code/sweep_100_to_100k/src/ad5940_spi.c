/**
 * @file ad5940_spi.c
 * @brief Zephyr SPI driver for AD5940/AD5941.
 *
 * Uses manual CS control and byte-level SPI protocol matching
 * the Analog Devices AD5940 library approach.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "ad5940_spi.h"
#include "ad5940_regs.h"

LOG_MODULE_REGISTER(ad5940_spi, CONFIG_LOG_DEFAULT_LEVEL);

/* ---------- Hardware Definitions ------------------------------------ */

static const struct device *spi_bus;
static struct spi_config spi_cfg;

#define GPIO_NODE   DT_NODELABEL(gpio0)
#define CS_PIN      20
#define RESET_PIN   22
#define GP0_PIN     24

static const struct device *gpio_dev;

static struct gpio_callback gp0_cb_data;
static struct k_sem         gp0_sem;

/* ---------- Manual CS control --------------------------------------- */

static inline void cs_low(void)
{
    gpio_pin_set(gpio_dev, CS_PIN, 0);
}

static inline void cs_high(void)
{
    gpio_pin_set(gpio_dev, CS_PIN, 1);
}

/* ---------- GP0 interrupt callback ---------------------------------- */

static void gp0_isr(const struct device *dev, struct gpio_callback *cb,
                    uint32_t pins)
{
    k_sem_give(&gp0_sem);
}

/* ---------- Low-level SPI (no automatic CS) ------------------------- */

static void spi_xfer(uint8_t *tx, uint8_t *rx, size_t len)
{
    struct spi_buf     tx_buf = { .buf = tx, .len = len };
    struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

    if (rx) {
        struct spi_buf     rx_buf = { .buf = rx, .len = len };
        struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };
        spi_transceive(spi_bus, &spi_cfg, &tx_set, &rx_set);
    } else {
        spi_write(spi_bus, &spi_cfg, &tx_set);
    }
}

static uint8_t spi_rw_8(uint8_t data)
{
    uint8_t tx = data, rx = 0;
    spi_xfer(&tx, &rx, 1);
    return rx;
}

static uint16_t spi_rw_16(uint16_t data)
{
    uint8_t tx[2], rx[2];
    tx[0] = (data >> 8) & 0xFF;
    tx[1] = data & 0xFF;
    spi_xfer(tx, rx, 2);
    return ((uint16_t)rx[0] << 8) | rx[1];
}

static uint32_t spi_rw_32(uint32_t data)
{
    uint8_t tx[4], rx[4];
    tx[0] = (data >> 24) & 0xFF;
    tx[1] = (data >> 16) & 0xFF;
    tx[2] = (data >> 8) & 0xFF;
    tx[3] = data & 0xFF;
    spi_xfer(tx, rx, 4);
    return ((uint32_t)rx[0] << 24) | ((uint32_t)rx[1] << 16) |
           ((uint32_t)rx[2] << 8) | rx[3];
}

/**
 * @brief Check if register is 32-bit wide.
 *
 * From the AD5940 library: registers at 0x1000–0x3014 are 32-bit,
 * everything else is 16-bit.
 */
static inline bool is_32bit_reg(uint16_t addr)
{
    return (addr >= 0x1000) && (addr <= 0x3014);
}

/* ---------- Public API ---------------------------------------------- */

int ad5940_spi_init(void)
{
    int ret;

    /* Get SPI bus */
    spi_bus = DEVICE_DT_GET(DT_NODELABEL(spi1));
    if (!device_is_ready(spi_bus)) {
        LOG_ERR("SPI bus (spi1) not ready");
        return -ENODEV;
    }

    /* Get GPIO port */
    gpio_dev = DEVICE_DT_GET(GPIO_NODE);
    if (!device_is_ready(gpio_dev)) {
        LOG_ERR("GPIO0 port not ready");
        return -ENODEV;
    }

    /* Configure SPI — CRITICAL: disable Zephyr CS management */
    spi_cfg.frequency = 8000000u;
    spi_cfg.operation = SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB |
                        SPI_WORD_SET(8) | SPI_LINES_SINGLE;
    spi_cfg.slave = 0;
    spi_cfg.cs.gpio.port = NULL;   /* Manual CS via GPIO */
    spi_cfg.cs.delay = 0;

    /* Configure CS as manual GPIO output, idle high */
    ret = gpio_pin_configure(gpio_dev, CS_PIN, GPIO_OUTPUT_HIGH);
    if (ret) {
        LOG_ERR("Failed to configure CS pin: %d", ret);
        return ret;
    }
    LOG_INF("CS on P0.%d (manual GPIO)", CS_PIN);

    /* Configure RESET pin, idle high */
    ret = gpio_pin_configure(gpio_dev, RESET_PIN, GPIO_OUTPUT_HIGH);
    if (ret) {
        LOG_ERR("Failed to configure RESET pin: %d", ret);
        return ret;
    }

    /* Configure GP0 as input, interrupt on falling edge */
    ret = gpio_pin_configure(gpio_dev, GP0_PIN,
                             GPIO_INPUT | GPIO_PULL_UP);
    if (ret) {
        LOG_ERR("Failed to configure GP0 pin: %d", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure(gpio_dev, GP0_PIN,
                                       GPIO_INT_EDGE_FALLING);
    if (ret) {
        LOG_ERR("Failed to configure GP0 interrupt: %d", ret);
        return ret;
    }

    gpio_init_callback(&gp0_cb_data, gp0_isr, BIT(GP0_PIN));
    gpio_add_callback(gpio_dev, &gp0_cb_data);

    k_sem_init(&gp0_sem, 0, 1);

    LOG_INF("AD5940 SPI interface initialised");
    return 0;
}

void ad5940_hw_reset(void)
{
    gpio_pin_set(gpio_dev, RESET_PIN, 0);
    k_msleep(1);
    gpio_pin_set(gpio_dev, RESET_PIN, 1);
    k_msleep(10);
    LOG_INF("AD5940 hardware reset complete");
}

/* ---------- Register Access ----------------------------------------- */

int ad5940_write_reg(uint16_t addr, uint32_t data)
{
    /* Transaction 1: set address */
    cs_low();
    spi_rw_8(SPICMD_SETADDR);
    spi_rw_16(addr);
    cs_high();

    /* Transaction 2: write data */
    cs_low();
    spi_rw_8(SPICMD_WRITEREG);
    if (is_32bit_reg(addr))
        spi_rw_32(data);
    else
        spi_rw_16((uint16_t)data);
    cs_high();

    return 0;
}

int ad5940_read_reg(uint16_t addr, uint32_t *data)
{
    /* Transaction 1: set address */
    cs_low();
    spi_rw_8(SPICMD_SETADDR);
    spi_rw_16(addr);
    cs_high();

    /* Transaction 2: read data */
    cs_low();
    spi_rw_8(SPICMD_READREG);
    spi_rw_8(0);  /* Dummy byte */

    if (is_32bit_reg(addr))
        *data = spi_rw_32(0);
    else
        *data = spi_rw_16(0);

    cs_high();

    return 0;
}

/* ---------- Chip initialisation (Table 14 + AD5940 library) --------- */

int ad5940_chip_init(void)
{
    int ret = 0;

    /* Full init sequence from AD5940_Initialize() in the ADI library */
    static const struct {
        uint16_t addr;
        uint32_t data;
    } init_seq[] = {
        {0x0908, 0x02C9},
        {0x0C08, 0x206C},
        {0x21F0, 0x0010},
        {0x0410, 0x02C9},
        {0x0A28, 0x0009},
        {0x238C, 0x0104},
        {0x0A04, 0x4859},
        {0x0A04, 0xF27B},
        {0x0A00, 0x8009},
        {0x22F0, 0x0000},
        /* Additional calibration entries from ADI library */
        {0x2230, 0xDE87A5AF},
        {0x2250, 0x103F},
        {0x22B0, 0x203C},
        {0x2230, 0xDE87A5A0},
    };

    /* Ensure CS starts high */
    cs_high();

    for (size_t i = 0; i < ARRAY_SIZE(init_seq); i++) {
        ret = ad5940_write_reg(init_seq[i].addr, init_seq[i].data);
        if (ret) {
            LOG_ERR("Init sequence step %u failed: %d", (unsigned)i, ret);
            return ret;
        }
    }

    LOG_INF("AD5940 init sequence complete");
    return 0;
}

int ad5940_verify_id(void)
{
    uint32_t id = 0;
    int ret = ad5940_read_reg(REG_ADIID, &id);
    if (ret) {
        return ret;
    }

    if (id != 0x4144) {
        LOG_ERR("ADIID mismatch: expected 0x4144, got 0x%04X", id);
        return -1;
    }

    LOG_INF("ADIID verified: 0x%04X", id);

    ret = ad5940_read_reg(REG_CHIPID, &id);
    if (ret == 0) {
        LOG_INF("CHIPID: 0x%04X  (Part=%03X  Rev=%X)",
                id, (id >> 4) & 0xFFF, id & 0xF);
    }

    return 0;
}

int ad5940_wait_interrupt(uint32_t timeout_ms)
{
    k_sem_reset(&gp0_sem);

    if (gpio_pin_get(gpio_dev, GP0_PIN) == 0) {
        return 0;
    }

    int ret = k_sem_take(&gp0_sem, K_MSEC(timeout_ms));
    if (ret == -EAGAIN) {
        LOG_WRN("GP0 interrupt timeout (%u ms)", timeout_ms);
        return -ETIMEDOUT;
    }
    return 0;
}
