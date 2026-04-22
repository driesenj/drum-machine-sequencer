#ifndef MAX7219_H
#define MAX7219_H

#include "main.h"
#include "stdbool.h"

/* Forward-declare the SPI handle defined in main.c */
extern SPI_HandleTypeDef hspi1;

/* ── Hardware configuration ──────────────────────────────────────────────────
   Edit these four lines to match your board.                                  */

#define MAX7219_SPI              (&hspi1)      /* SPI handle used by both chains */
#define MAX7219_DEVICES_PER_CHAIN  4           /* 8×8 matrices per row            */
#define MAX7219_NUM_CHAINS         2           /* number of independent CS lines  */

/* Chain 0 – top row    → PB14 */
#define MAX7219_CS0_PORT         GPIOB
#define MAX7219_CS0_PIN          GPIO_PIN_14

/* Chain 1 – bottom row → PB5  */
#define MAX7219_CS1_PORT         GPIOB
#define MAX7219_CS1_PIN          GPIO_PIN_5

/* ── Register addresses ──────────────────────────────────────────────────── */
#define MAX7219_REG_NOOP          0x00
#define MAX7219_REG_DIGIT0        0x01        /* digit registers: 0x01 – 0x08 */
#define MAX7219_REG_DECODE_MODE   0x09
#define MAX7219_REG_INTENSITY     0x0A
#define MAX7219_REG_SCAN_LIMIT    0x0B
#define MAX7219_REG_SHUTDOWN      0x0C
#define MAX7219_REG_DISPLAY_TEST  0x0F

/* ── Frame buffer ────────────────────────────────────────────────────────────
   [chain][device][row]
   chain  : 0 = top row, 1 = bottom row
   device : 0 = first device in chain (closest to MCU)
   row    : 0 = top row of pixels                                              */
extern uint8_t frameBuffer[MAX7219_NUM_CHAINS][MAX7219_DEVICES_PER_CHAIN][8];

/* ── Public API ──────────────────────────────────────────────────────────── */

/** Initialise both chains. Call after HAL_Init and SPI init. */
void MAX7219_init(void);

/**
 * Set a single pixel in the frame buffer.
 * Does NOT push to hardware — call MAX7219_flushChain() or MAX7219_flushAll().
 * @param chain   0 = top row, 1 = bottom row
 * @param device  0-based index within the chain
 * @param col     0 = left column of that device
 * @param row     0 = top row of pixels
 * @param on      true = pixel on
 */
void MAX7219_setPixel(uint8_t chain, uint8_t device,
                      uint8_t col,   uint8_t row, bool on);

/** Push one pixel-row of a single chain to hardware. */
void MAX7219_flushRow(uint8_t chain, uint8_t row);

/** Push the entire frame buffer of one chain to hardware. */
void MAX7219_flushChain(uint8_t chain);

/** Push both chains to hardware. */
void MAX7219_flushAll(void);

/** Zero the frame buffer for one chain (does not flush). */
void MAX7219_clearChain(uint8_t chain);

/** Zero the entire frame buffer (does not flush). */
void MAX7219_clearBuffer(void);

/* ── Demo helpers ────────────────────────────────────────────────────────── */
void MAX7219_demo_allOn(void);
void MAX7219_demo_checkerboard(void);
void MAX7219_demo_scroll(void);

#endif /* MAX7219_H */
