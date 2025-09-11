#ifndef OV7675_H
#define OV7675_H

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"

// Pin definitions
#define PIN_XVCLK 28
#define VSYNC     27
#define HREF      26
#define PCLK      22

// I2C definitions
#define I2C_PORT    i2c1
#define I2C_SDA     14
#define I2C_SCL     15
#define OV7675_ADDR 0x21

// Image dimensions and buffer size
#define WIDTH      320
#define HEIGHT     240
#define FRAME_SIZE (WIDTH * HEIGHT * 2)

// TCP chunk size for network transmission (if used)
#define TCP_CHUNK_SIZE 1460

// External declarations for global variables
extern volatile bool frame_started;
extern volatile bool frame_done;
extern volatile uint32_t write_index;
extern uint8_t image[FRAME_SIZE];

/**
 * @brief Generates the clock signal for the OV7675 camera module.
 */
void Gen_clock();

/**
 * @brief Writes a value to a register of the OV7675.
 *
 * @param reg The register address to write to.
 * @param val The value to write.
 */
void ov7675_write(uint8_t reg, uint8_t val);

/**
 * @brief Reads a value from a register of the OV7675.
 *
 * @param reg The register address to read from.
 * @return The value read from the register.
 */
uint8_t ov7675_read(uint8_t reg);

/**
 * @brief Initializes the OV7675 camera module with default settings.
 */
void ov7675_init();

/**
 * @brief Sets up the GPIO pins for data input from the camera.
 */
void GPIO_set();

/**
 * @brief Initializes the GPIO pins used for capturing the picture.
 */
void cap_pic_init();

/**
 * @brief Captures a single frame from the OV7675 camera.
 */
void capture();

#endif // OV7675_H