/**
  ******************************************************************************
  * @file    MCP23017.c
  * @brief   MCP23017 I2C GPIO Expander Driver — implementation
  ******************************************************************************
  *
  * Quick-start
  * -----------
  *
  *  MCP23017_Handle mcp;
  *
  *  // Port-level callback — see which pins changed via the bitmask
  *  void OnPortA(MCP23017_Port port, uint8_t old, uint8_t new, uint8_t changed)
  *  {
  *      if (changed & (1 << 0))           // PA0 changed
  *          PA0_Handler(new & (1 << 0));  // non-zero = high
  *  }
  *
  *  // Pin-level callback — called only when that exact pin changes
  *  void OnButton(MCP23017_Port port, MCP23017_Pin pin, uint8_t state)
  *  {
  *      if (state == 0) ButtonPressed();  // active-low
  *  }
  *
  *  // In main() / init:
  *  MCP23017_InitHandle(&mcp, &hi2c1, 0);
  *  MCP23017_SetPortCallback(&mcp, MCP23017_PORT_A, OnPortA);
  *  MCP23017_SetPinCallback (&mcp, MCP23017_PORT_A, 3, OnButton);
  *
  *  // In your super-loop (or a ~10 ms timer):
  *  MCP23017_Poll(&mcp);
  *
  ******************************************************************************
  */

#include "MCP23017.h"
#include <string.h>

/* ── Internal helpers ───────────────────────────────────────────────────────── */

/** Map a MCP23017_Port to its GPIO register address */
static inline uint8_t PortToGPIOReg(MCP23017_Port port)
{
    return (port == MCP23017_PORT_A) ? MCP23017_GPIOA : MCP23017_GPIOB;
}

/**
 * @brief  Compare @p new_val against the shadow, update the shadow, then fire
 *         the port callback and any per-pin callbacks for bits that changed.
 */
static void ProcessPortChange(MCP23017_Handle *hdev,
                               MCP23017_Port    port,
                               uint8_t          new_val)
{
    uint8_t old_val = hdev->port_shadow[port];
    uint8_t changed = old_val ^ new_val;

    if (changed == 0) return;   /* nothing to do */

    hdev->port_shadow[port] = new_val;

    /* Port-level callback */
    if (hdev->port_cb[port] != NULL)
    {
        hdev->port_cb[port](port, old_val, new_val, changed);
    }

    /* Pin-level callbacks — iterate only the bits that actually changed */
    uint8_t mask = changed;
    while (mask)
    {
        /* Find lowest set bit */
        MCP23017_Pin pin = 0;
        uint8_t      tmp = mask;
        while ((tmp & 1) == 0) { tmp >>= 1; pin++; }

        if (hdev->pin_cb[port][pin] != NULL)
        {
            uint8_t state = (new_val >> pin) & 0x01;
            hdev->pin_cb[port][pin](port, pin, state);
        }

        mask &= (uint8_t)(mask - 1);   /* clear lowest set bit */
    }
}

/* ── Low-level register helpers ─────────────────────────────────────────────── */

/* Legacy helpers — use the default address (A2:A0 = 0) */
HAL_StatusTypeDef MCP23017_WriteReg(I2C_HandleTypeDef *hi2c,
                                    uint8_t reg,
                                    uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    return HAL_I2C_Master_Transmit(hi2c, MCP23017_I2C_ADDR, buf, 2, HAL_MAX_DELAY);
}

HAL_StatusTypeDef MCP23017_ReadReg(I2C_HandleTypeDef *hi2c,
                                   uint8_t reg,
                                   uint8_t *value)
{
    HAL_StatusTypeDef status;
    status = HAL_I2C_Master_Transmit(hi2c, MCP23017_I2C_ADDR, &reg, 1, HAL_MAX_DELAY);
    if (status != HAL_OK) return status;
    return HAL_I2C_Master_Receive(hi2c, MCP23017_I2C_ADDR, value, 1, HAL_MAX_DELAY);
}

/* Internal helpers — use the address stored in the handle */
static HAL_StatusTypeDef WriteReg_H(MCP23017_Handle *hdev, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    return HAL_I2C_Master_Transmit(hdev->hi2c, hdev->addr, buf, 2, HAL_MAX_DELAY);
}

static HAL_StatusTypeDef ReadReg_H(MCP23017_Handle *hdev, uint8_t reg, uint8_t *value)
{
    HAL_StatusTypeDef status;
    status = HAL_I2C_Master_Transmit(hdev->hi2c, hdev->addr, &reg, 1, HAL_MAX_DELAY);
    if (status != HAL_OK) return status;
    return HAL_I2C_Master_Receive(hdev->hi2c, hdev->addr, value, 1, HAL_MAX_DELAY);
}

/* ── Initialisation ─────────────────────────────────────────────────────────── */

HAL_StatusTypeDef MCP23017_InitHandle(MCP23017_Handle   *hdev,
                                      I2C_HandleTypeDef *hi2c,
                                      uint8_t            addr)
{
    HAL_StatusTypeDef status;

    hdev->hi2c = hi2c;
    hdev->addr = (uint8_t)((0x20 | (addr & 0x07)) << 1);

    memset(hdev->port_shadow, 0xFF, sizeof(hdev->port_shadow));
    memset(hdev->port_cb,     0x00, sizeof(hdev->port_cb));
    memset(hdev->pin_cb,      0x00, sizeof(hdev->pin_cb));

    /* Port A: all inputs + pull-ups */
    status = WriteReg_H(hdev, MCP23017_IODIRA, 0xFF);
    if (status != HAL_OK) return status;

    status = WriteReg_H(hdev, MCP23017_GPPUA, 0xFF);
    if (status != HAL_OK) return status;

    /* Port B: all inputs + pull-ups */
    status = WriteReg_H(hdev, MCP23017_IODIRB, 0xFF);
    if (status != HAL_OK) return status;

    status = WriteReg_H(hdev, MCP23017_GPPUB, 0xFF);
    if (status != HAL_OK) return status;

    return HAL_OK;
}

/* ── Port read ──────────────────────────────────────────────────────────────── */

HAL_StatusTypeDef MCP23017_ReadPort(MCP23017_Handle *hdev,
                                    MCP23017_Port    port,
                                    uint8_t         *value)
{
    HAL_StatusTypeDef status;
    uint8_t val = 0;

    status = ReadReg_H(hdev, PortToGPIOReg(port), &val);
    if (status != HAL_OK) return status;

    ProcessPortChange(hdev, port, val);

    if (value != NULL) *value = val;
    return HAL_OK;
}

HAL_StatusTypeDef MCP23017_ReadPortA(MCP23017_Handle *hdev, uint8_t *value)
{
    return MCP23017_ReadPort(hdev, MCP23017_PORT_A, value);
}

HAL_StatusTypeDef MCP23017_ReadPortB(MCP23017_Handle *hdev, uint8_t *value)
{
    return MCP23017_ReadPort(hdev, MCP23017_PORT_B, value);
}

/* ── Pin read ───────────────────────────────────────────────────────────────── */

HAL_StatusTypeDef MCP23017_ReadPin(MCP23017_Handle *hdev,
                                   MCP23017_Port    port,
                                   MCP23017_Pin     pin,
                                   uint8_t         *state)
{
    if (pin > 7) return HAL_ERROR;

    uint8_t port_val = 0;
    HAL_StatusTypeDef status = MCP23017_ReadPort(hdev, port, &port_val);
    if (status != HAL_OK) return status;

    if (state != NULL) *state = (port_val >> pin) & 0x01;
    return HAL_OK;
}

/* ── Poll ───────────────────────────────────────────────────────────────────── */

HAL_StatusTypeDef MCP23017_Poll(MCP23017_Handle *hdev)
{
    HAL_StatusTypeDef status;

    status = MCP23017_ReadPort(hdev, MCP23017_PORT_A, NULL);
    if (status != HAL_OK) return status;

    return MCP23017_ReadPort(hdev, MCP23017_PORT_B, NULL);
}

/* ── Pin mode ───────────────────────────────────────────────────────────────── */

HAL_StatusTypeDef MCP23017_SetPinMode(MCP23017_Handle *hdev,
                                       MCP23017_Port    port,
                                       MCP23017_Pin     pin,
                                       MCP23017_PinMode mode)
{
    if (pin > 7 || port > MCP23017_PORT_B) return HAL_ERROR;

    uint8_t iodir_reg = (port == MCP23017_PORT_A) ? MCP23017_IODIRA : MCP23017_IODIRB;
    uint8_t gppu_reg  = (port == MCP23017_PORT_A) ? MCP23017_GPPUA  : MCP23017_GPPUB;
    uint8_t ipol_reg  = (port == MCP23017_PORT_A) ? MCP23017_IPOLA  : MCP23017_IPOLB;

    uint8_t iodir, gppu, ipol;
    HAL_StatusTypeDef status;

    /* Read current register values so we only touch the one bit we care about */
    status = ReadReg_H(hdev, iodir_reg, &iodir); if (status != HAL_OK) return status;
    status = ReadReg_H(hdev, gppu_reg,  &gppu);  if (status != HAL_OK) return status;
    status = ReadReg_H(hdev, ipol_reg,  &ipol);  if (status != HAL_OK) return status;

    uint8_t mask = (uint8_t)(1 << pin);

    switch (mode)
    {
        case MCP23017_MODE_INPUT:
            iodir |=  mask;   /* input          */
            gppu  &= ~mask;   /* pull-up off     */
            ipol  &= ~mask;   /* no inversion    */
            break;

        case MCP23017_MODE_INPUT_PULLUP:
            iodir |=  mask;   /* input          */
            gppu  |=  mask;   /* pull-up on      */
            ipol  &= ~mask;   /* no inversion    */
            break;

        case MCP23017_MODE_INPUT_INVERTED:
            iodir |=  mask;   /* input          */
            gppu  |=  mask;   /* pull-up on      */
            ipol  |=  mask;   /* invert          */
            break;

        case MCP23017_MODE_OUTPUT:
            iodir &= ~mask;   /* output         */
            gppu  &= ~mask;   /* pull-up off     */
            ipol  &= ~mask;   /* no inversion    */
            break;

        default:
            return HAL_ERROR;
    }

    status = WriteReg_H(hdev, iodir_reg, iodir); if (status != HAL_OK) return status;
    status = WriteReg_H(hdev, gppu_reg,  gppu);  if (status != HAL_OK) return status;
    status = WriteReg_H(hdev, ipol_reg,  ipol);  if (status != HAL_OK) return status;

    return HAL_OK;
}

HAL_StatusTypeDef MCP23017_SetPortMode(MCP23017_Handle *hdev,
                                        MCP23017_Port    port,
                                        MCP23017_PinMode mode)
{
    if (port > MCP23017_PORT_B) return HAL_ERROR;

    uint8_t iodir_reg = (port == MCP23017_PORT_A) ? MCP23017_IODIRA : MCP23017_IODIRB;
    uint8_t gppu_reg  = (port == MCP23017_PORT_A) ? MCP23017_GPPUA  : MCP23017_GPPUB;
    uint8_t ipol_reg  = (port == MCP23017_PORT_A) ? MCP23017_IPOLA  : MCP23017_IPOLB;

    uint8_t iodir, gppu, ipol;
    HAL_StatusTypeDef status;

    switch (mode)
    {
        case MCP23017_MODE_INPUT:
            iodir = 0xFF; gppu = 0x00; ipol = 0x00; break;

        case MCP23017_MODE_INPUT_PULLUP:
            iodir = 0xFF; gppu = 0xFF; ipol = 0x00; break;

        case MCP23017_MODE_INPUT_INVERTED:
            iodir = 0xFF; gppu = 0xFF; ipol = 0xFF; break;

        case MCP23017_MODE_OUTPUT:
            iodir = 0x00; gppu = 0x00; ipol = 0x00; break;

        default:
            return HAL_ERROR;
    }

    status = WriteReg_H(hdev, iodir_reg, iodir); if (status != HAL_OK) return status;
    status = WriteReg_H(hdev, gppu_reg,  gppu);  if (status != HAL_OK) return status;
    status = WriteReg_H(hdev, ipol_reg,  ipol);  if (status != HAL_OK) return status;

    return HAL_OK;
}

/* ── Callback registration ──────────────────────────────────────────────────── */

void MCP23017_SetPortCallback(MCP23017_Handle      *hdev,
                               MCP23017_Port         port,
                               MCP23017_PortCallback cb)
{
    if (port > MCP23017_PORT_B) return;
    hdev->port_cb[port] = cb;
}

void MCP23017_SetPinCallback(MCP23017_Handle     *hdev,
                              MCP23017_Port        port,
                              MCP23017_Pin         pin,
                              MCP23017_PinCallback cb)
{
    if (port > MCP23017_PORT_B || pin > 7) return;
    hdev->pin_cb[port][pin] = cb;
}
