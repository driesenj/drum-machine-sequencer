/**
  ******************************************************************************
  * @file    MCP23017.h
  * @brief   MCP23017 I2C GPIO Expander Driver — header
  ******************************************************************************
  */

#ifndef MCP23017_H
#define MCP23017_H

#include "stm32l4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* ── Device address (A2=A1=A0=0) ───────────────────────────────────────────── */
#define MCP23017_I2C_ADDR   (0x20 << 1)   /* 8-bit HAL address */

/* ── Register map (BANK=0, default) ────────────────────────────────────────── */
#define MCP23017_IODIRA     0x00
#define MCP23017_IODIRB     0x01
#define MCP23017_IPOLA      0x02
#define MCP23017_IPOLB      0x03
#define MCP23017_GPINTENA   0x04
#define MCP23017_GPINTENB   0x05
#define MCP23017_DEFVALA    0x06
#define MCP23017_DEFVALB    0x07
#define MCP23017_INTCONA    0x08
#define MCP23017_INTCONB    0x09
#define MCP23017_IOCON      0x0A
#define MCP23017_GPPUA      0x0C
#define MCP23017_GPPUB      0x0D
#define MCP23017_INTFA      0x0E
#define MCP23017_INTFB      0x0F
#define MCP23017_INTCAPA    0x10
#define MCP23017_INTCAPB    0x11
#define MCP23017_GPIOA      0x12
#define MCP23017_GPIOB      0x13
#define MCP23017_OLATA      0x14
#define MCP23017_OLATB      0x15

/* ── Port identifier ────────────────────────────────────────────────────────── */
typedef enum {
    MCP23017_PORT_A = 0,
    MCP23017_PORT_B = 1,
} MCP23017_Port;

/* ── Pin number (0–7) ───────────────────────────────────────────────────────── */
typedef uint8_t MCP23017_Pin;   /* 0 = GP_0 … 7 = GP_7 */

/* ── Callback types ─────────────────────────────────────────────────────────── */

/**
 * @brief  Fired when any pin on a port changes.
 *
 * @param  port     Which port (A or B).
 * @param  old_val  Full 8-bit port value before the change.
 * @param  new_val  Full 8-bit port value after the change.
 * @param  changed  Bitmask of pins that changed  (old_val ^ new_val).
 */
typedef void (*MCP23017_PortCallback)(MCP23017_Port port,
                                      uint8_t old_val,
                                      uint8_t new_val,
                                      uint8_t changed);

/**
 * @brief  Fired when a specific pin changes state.
 *
 * @param  port   Which port (A or B).
 * @param  pin    Which pin (0–7).
 * @param  state  New logical state: 0 or 1.
 */
typedef void (*MCP23017_PinCallback)(MCP23017_Port port,
                                     MCP23017_Pin  pin,
                                     uint8_t       state);

/* ── Device handle ──────────────────────────────────────────────────────────── */
typedef struct {
    I2C_HandleTypeDef    *hi2c;
    uint8_t               addr;         /* 8-bit HAL address                  */

    /* Shadow copies of the two GPIO ports */
    uint8_t               port_shadow[2];

    /* Per-port callbacks (NULL = disabled) */
    MCP23017_PortCallback port_cb[2];

    /* Per-pin callbacks [port 0..1][pin 0..7] (NULL = disabled) */
    MCP23017_PinCallback  pin_cb[2][8];

} MCP23017_Handle;

/* ── Initialisation ─────────────────────────────────────────────────────────── */

/**
 * @brief  Initialise a handle and configure the MCP23017.
 *         Both ports are set to inputs with pull-ups enabled.
 *
 * @param  hdev  Handle to initialise.
 * @param  hi2c  HAL I2C peripheral.
 * @param  addr  Hardware address pins A2:A0 (0–7). Pass 0 when all tied low.
 */
HAL_StatusTypeDef MCP23017_InitHandle(MCP23017_Handle   *hdev,
                                      I2C_HandleTypeDef *hi2c,
                                      uint8_t            addr);

/* ── Port read ──────────────────────────────────────────────────────────────── */

/**
 * @brief  Read an entire port (8 pins) as a bitmask.
 *         Updates the internal shadow; fires port and pin callbacks on change.
 *
 * @param  hdev   Device handle.
 * @param  port   MCP23017_PORT_A or MCP23017_PORT_B.
 * @param  value  Output: 8-bit pin states (bit 0 = pin 0, …, bit 7 = pin 7).
 */
HAL_StatusTypeDef MCP23017_ReadPort(MCP23017_Handle *hdev,
                                    MCP23017_Port    port,
                                    uint8_t         *value);

/** Convenience wrappers */
HAL_StatusTypeDef MCP23017_ReadPortA(MCP23017_Handle *hdev, uint8_t *value);
HAL_StatusTypeDef MCP23017_ReadPortB(MCP23017_Handle *hdev, uint8_t *value);

/* ── Pin read ───────────────────────────────────────────────────────────────── */

/**
 * @brief  Read a single pin state (0 or 1).
 *         Reads the full port so all port/pin callbacks fire correctly.
 *
 * @param  hdev   Device handle.
 * @param  port   MCP23017_PORT_A or MCP23017_PORT_B.
 * @param  pin    Pin number 0–7.
 * @param  state  Output: 0 or 1.
 */
HAL_StatusTypeDef MCP23017_ReadPin(MCP23017_Handle *hdev,
                                   MCP23017_Port    port,
                                   MCP23017_Pin     pin,
                                   uint8_t         *state);

/* ── Poll ───────────────────────────────────────────────────────────────────── */

/**
 * @brief  Read both ports and fire any change callbacks.
 *         Call this from your main loop or a periodic timer ISR.
 */
HAL_StatusTypeDef MCP23017_Poll(MCP23017_Handle *hdev);

/* ── Pin mode ───────────────────────────────────────────────────────────────── */

/**
 * @brief  Pin mode options for MCP23017_SetPinMode().
 *
 *  INPUT            — input, pull-up disabled  (use with active-high signals
 *                     that drive the line, e.g. the touch sensor circuit)
 *  INPUT_PULLUP     — input, pull-up enabled   (default after InitHandle;
 *                     use with active-low / open-drain signals)
 *  INPUT_INVERTED   — input, pull-up enabled, polarity inverted via IPOL
 *                     (pin reads 1 when low, 0 when high)
 *  OUTPUT           — output, pull-up disabled
 */
typedef enum {
    MCP23017_MODE_INPUT          = 0,
    MCP23017_MODE_INPUT_PULLUP   = 1,
    MCP23017_MODE_INPUT_INVERTED = 2,
    MCP23017_MODE_OUTPUT         = 3,
} MCP23017_PinMode;


/**
 * @brief  Configure all 8 pins on a port to the same mode in one go.
 *         Writes IODIR, GPPU, and IPOL in three I2C transactions.
 *
 * @param  hdev  Device handle.
 * @param  port  MCP23017_PORT_A or MCP23017_PORT_B.
 * @param  mode  One of the MCP23017_PinMode values.
 *
 * @code
 *   // All touch sensors on Port A — active-high, no pull-ups needed
 *   MCP23017_SetPortMode(&mcp, MCP23017_PORT_A, MCP23017_MODE_INPUT);
 * @endcode
 */
HAL_StatusTypeDef MCP23017_SetPortMode(MCP23017_Handle *hdev,
                                        MCP23017_Port    port,
                                        MCP23017_PinMode mode);

/**
 * @brief  Configure the mode of a single pin.
 *         Updates IODIR, GPPU, and IPOL registers as needed.
 *         Safe to call after InitHandle and before or after setting callbacks.
 *
 * @param  hdev  Device handle.
 * @param  port  MCP23017_PORT_A or MCP23017_PORT_B.
 * @param  pin   Pin number 0–7.
 * @param  mode  One of the MCP23017_PinMode values.
 *
 * @code
 *   // Touch sensor — active-high, drives the line, no pull-up needed
 *   MCP23017_SetPinMode(&mcp, MCP23017_PORT_A, 0, MCP23017_MODE_INPUT);
 *
 *   // Mechanical button to GND — needs pull-up, reads 0 when pressed
 *   MCP23017_SetPinMode(&mcp, MCP23017_PORT_A, 1, MCP23017_MODE_INPUT_PULLUP);
 * @endcode
 */
HAL_StatusTypeDef MCP23017_SetPinMode(MCP23017_Handle *hdev,
                                       MCP23017_Port    port,
                                       MCP23017_Pin     pin,
                                       MCP23017_PinMode mode);

/* ── Callback registration ──────────────────────────────────────────────────── */

/**
 * @brief  Register a callback fired whenever any pin on @p port changes.
 *         Pass NULL to remove an existing callback.
 *
 * @code
 *   void OnPortA(MCP23017_Port port, uint8_t old, uint8_t new, uint8_t changed)
 *   {
 *       if (changed & (1 << 3))              // pin 3 changed
 *           HandlePin3(new & (1 << 3));       // non-zero = high
 *   }
 *   MCP23017_SetPortCallback(&mcp, MCP23017_PORT_A, OnPortA);
 * @endcode
 */
void MCP23017_SetPortCallback(MCP23017_Handle      *hdev,
                               MCP23017_Port         port,
                               MCP23017_PortCallback cb);

/**
 * @brief  Register a callback fired when a specific pin changes state.
 *         Pass NULL to remove an existing callback.
 *
 * @code
 *   void OnButton(MCP23017_Port port, MCP23017_Pin pin, uint8_t state)
 *   {
 *       if (state == 0) ButtonPressed();   // active-low button
 *   }
 *   MCP23017_SetPinCallback(&mcp, MCP23017_PORT_A, 3, OnButton);
 * @endcode
 */
void MCP23017_SetPinCallback(MCP23017_Handle     *hdev,
                              MCP23017_Port        port,
                              MCP23017_Pin         pin,
                              MCP23017_PinCallback cb);

/* ── Low-level register helpers (kept for advanced use) ─────────────────────── */
HAL_StatusTypeDef MCP23017_WriteReg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t value);
HAL_StatusTypeDef MCP23017_ReadReg (I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t *value);

#endif /* MCP23017_H */
