#ifndef SUNSENSOR_H
#define SUNSENSOR_H
#include "pico/stdlib.h"
#include "hardware/spi.h"

#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

void sunSensor_init();
uint16_t sunSensor_read(int ch);

uint16_t read_mcp3008(int ch);

bool isSunValid();

float calculate_sun_angle(uint16_t ch0, uint16_t ch1, uint16_t ch2, uint16_t ch3);

#endif
