#ifndef HC595_H
#define HC595_H

#include "stm32l4xx_hal.h"
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Number of daisy-chained 74HC595 devices
// ---------------------------------------------------------------------------
#define HC595_NUM_DEVICES       3

// ---------------------------------------------------------------------------
// Latch (RCLK) pin — adjust if your wiring differs
// ---------------------------------------------------------------------------
#define HC595_LATCH_PORT        GPIOB
#define HC595_LATCH_PIN         GPIO_PIN_4

// ---------------------------------------------------------------------------
// Pass in the CubeMX-generated SPI handle once at startup.
// Call this after MX_SPI2_Init() and MX_GPIO_Init() in main.c.
// ---------------------------------------------------------------------------
void HC595_init(SPI_HandleTypeDef *hspi);

// ---------------------------------------------------------------------------
// Write HC595_NUM_DEVICES bytes to the shift-register chain.
// data[0] = device 0 (first chip, closest to MCU)
// data[N-1] = last chip in the chain
// ---------------------------------------------------------------------------
void HC595_write(uint8_t data[HC595_NUM_DEVICES]);

// ---------------------------------------------------------------------------
// Convenience: write the same byte to every device in the chain.
// ---------------------------------------------------------------------------
void HC595_writeAll(uint8_t value);

// ---------------------------------------------------------------------------
// Set / clear a single output bit, preserving all other outputs.
// device : 0 = first chip (closest to MCU)
// bit    : 0 (QA) … 7 (QH)
// ---------------------------------------------------------------------------
void HC595_setBit(uint8_t device, uint8_t bit, uint8_t state);

// ---------------------------------------------------------------------------
// Clear all outputs (all zeros, latch pulsed).
// ---------------------------------------------------------------------------
void HC595_clear(void);

void HC595_getShadow(uint8_t out[HC595_NUM_DEVICES]);

#endif /* HC595_H */
