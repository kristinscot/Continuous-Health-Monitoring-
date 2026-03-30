/**
 * @file ad5940_spi.h
 * @brief Low-level SPI driver for the AD5940/AD5941 on Zephyr.
 *
 * Implements the two-transaction register protocol described in the
 * AD5940 datasheet § "SPI Interface":
 *   Transaction 1: SPICMD_SETADDR + 16-bit address
 *   Transaction 2: SPICMD_WRITEREG/READREG + data
 */
#ifndef AD5940_SPI_H
#define AD5940_SPI_H

#include <stdint.h>

/**
 * @brief Initialise the SPI bus, CS, RESET and GP0 (interrupt) GPIOs.
 * @return 0 on success, negative errno on failure.
 */
int ad5940_spi_init(void);

/**
 * @brief Hardware-reset the AD5940 via the RESET pin.
 *
 * Drives RESET low for ≥10 µs then high, waits ≥1 ms for the chip
 * to boot (see datasheet t_RESET / power-on timing).
 */
void ad5940_hw_reset(void);

/**
 * @brief Write a 32-bit value to an AD5940 register.
 * @param addr 16-bit register address.
 * @param data 32-bit value to write.
 * @return 0 on success, negative errno on failure.
 */
int ad5940_write_reg(uint16_t addr, uint32_t data);

/**
 * @brief Read a 32-bit value from an AD5940 register.
 * @param addr  16-bit register address.
 * @param data  Pointer to store the 32-bit result.
 * @return 0 on success, negative errno on failure.
 */
int ad5940_read_reg(uint16_t addr, uint32_t *data);

/**
 * @brief Run the mandatory initialisation sequence from Table 14.
 *
 * Must be called once after every hardware or software reset.
 * @return 0 on success.
 */
int ad5940_chip_init(void);

/**
 * @brief Verify the ADIID register == 0x4144.
 * @return 0 if valid, -1 on mismatch.
 */
int ad5940_verify_id(void);

/**
 * @brief Wait for the AD5940 GP0 interrupt pin to assert (active-low).
 * @param timeout_ms  Maximum wait in milliseconds.
 * @return 0 if interrupt seen, -ETIMEDOUT on timeout.
 */
int ad5940_wait_interrupt(uint32_t timeout_ms);

#endif /* AD5940_SPI_H */
