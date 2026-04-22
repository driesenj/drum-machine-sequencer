#include "MAX7219.h"
#include "string.h"

/* ── Frame buffer ────────────────────────────────────────────────────────── */
uint8_t frameBuffer[MAX7219_NUM_CHAINS][MAX7219_DEVICES_PER_CHAIN][8];

/* ── CS pin lookup tables ────────────────────────────────────────────────── */
static GPIO_TypeDef * const CS_PORT[MAX7219_NUM_CHAINS] = {
    MAX7219_CS0_PORT,
    MAX7219_CS1_PORT,
};
static const uint16_t CS_PIN[MAX7219_NUM_CHAINS] = {
    MAX7219_CS0_PIN,
    MAX7219_CS1_PIN,
};

/* ── Low-level CS helpers ────────────────────────────────────────────────── */
static inline void CS_Low(uint8_t chain)
{
    HAL_GPIO_WritePin(CS_PORT[chain], CS_PIN[chain], GPIO_PIN_RESET);
}

static inline void CS_High(uint8_t chain)
{
    HAL_GPIO_WritePin(CS_PORT[chain], CS_PIN[chain], GPIO_PIN_SET);
}

/* ── Raw register send ───────────────────────────────────────────────────── */

/*
 * Write one register value to every device in a single chain.
 * values[0] = device 0 (closest to MCU), values[N-1] = device farthest away.
 * The SPI frame is built in reverse because the MAX7219 shift register means
 * the last byte clocked in ends up at device 0.
 */
static void _sendChain(uint8_t chain, uint8_t reg,
                       const uint8_t values[MAX7219_DEVICES_PER_CHAIN])
{
    uint8_t buf[MAX7219_DEVICES_PER_CHAIN * 2];

    /* Pack MSB-first: last device's word goes in first */
    for (int i = MAX7219_DEVICES_PER_CHAIN - 1; i >= 0; i--) {
        int idx = (MAX7219_DEVICES_PER_CHAIN - 1 - i) * 2;
        buf[idx]     = reg;
        buf[idx + 1] = values[i];
    }

    CS_Low(chain);
    __NOP(); __NOP(); __NOP();   /* tCSS ≥ 100 ns */

    HAL_SPI_Transmit(MAX7219_SPI, buf, sizeof(buf), HAL_MAX_DELAY);

    /* Wait until the SPI shift register is truly idle before releasing CS */
    while (MAX7219_SPI->State != HAL_SPI_STATE_READY);
    __NOP(); __NOP(); __NOP();

    CS_High(chain);
}

/*
 * Convenience: send the same register+value to every device in one chain.
 */
static void _sendChainSame(uint8_t chain, uint8_t reg, uint8_t value)
{
    uint8_t vals[MAX7219_DEVICES_PER_CHAIN];
    for (int i = 0; i < MAX7219_DEVICES_PER_CHAIN; i++) vals[i] = value;
    _sendChain(chain, reg, vals);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void MAX7219_init(void)
{
    /* Make sure both CS lines start high (deselected) */
    for (uint8_t c = 0; c < MAX7219_NUM_CHAINS; c++) {
        CS_High(c);
    }
    HAL_Delay(1);

    /* Display-test flash so you can confirm both chains are alive */
    for (uint8_t c = 0; c < MAX7219_NUM_CHAINS; c++) {
        _sendChainSame(c, MAX7219_REG_DISPLAY_TEST, 0x01);
    }
    HAL_Delay(500);
    for (uint8_t c = 0; c < MAX7219_NUM_CHAINS; c++) {
        _sendChainSame(c, MAX7219_REG_DISPLAY_TEST, 0x00);
    }

    /* Configure both chains identically */
    for (uint8_t c = 0; c < MAX7219_NUM_CHAINS; c++) {
        _sendChainSame(c, MAX7219_REG_SHUTDOWN,     0x00); /* enter shutdown    */
        HAL_Delay(1);
        _sendChainSame(c, MAX7219_REG_DISPLAY_TEST, 0x00); /* test mode off     */
        _sendChainSame(c, MAX7219_REG_DECODE_MODE,  0x00); /* raw bit control   */
        _sendChainSame(c, MAX7219_REG_SCAN_LIMIT,   0x07); /* scan all 8 rows   */
        _sendChainSame(c, MAX7219_REG_INTENSITY,    0x03); /* brightness 0–0x0F */

        /* Clear all digit registers */
        for (uint8_t row = 0; row < 8; row++) {
            _sendChainSame(c, MAX7219_REG_DIGIT0 + row, 0x00);
        }

        _sendChainSame(c, MAX7219_REG_SHUTDOWN, 0x01); /* normal operation  */
        HAL_Delay(1);
    }

    MAX7219_clearBuffer();
}

void MAX7219_setPixel(uint8_t chain, uint8_t device,
                      uint8_t col,   uint8_t row, bool on)
{
    if (chain  >= MAX7219_NUM_CHAINS)       return;
    if (device >= MAX7219_DEVICES_PER_CHAIN) return;
    if (col >= 8 || row >= 8)              return;

    if (on)
		frameBuffer[chain][device][7 - row] |=  (1u << col);  // was 7u - col
	else
		frameBuffer[chain][device][7 - row] &= ~(1u << col);
}

void MAX7219_flushRow(uint8_t chain, uint8_t row)
{
    if (chain >= MAX7219_NUM_CHAINS || row >= 8) return;

    uint8_t vals[MAX7219_DEVICES_PER_CHAIN];
    for (int d = 0; d < MAX7219_DEVICES_PER_CHAIN; d++) {
        vals[d] = frameBuffer[chain][d][row];
    }
    _sendChain(chain, MAX7219_REG_DIGIT0 + row, vals);
}

void MAX7219_flushChain(uint8_t chain)
{
    if (chain >= MAX7219_NUM_CHAINS) return;
    for (uint8_t row = 0; row < 8; row++) {
        MAX7219_flushRow(chain, row);
    }
}

void MAX7219_flushAll(void)
{
    for (uint8_t c = 0; c < MAX7219_NUM_CHAINS; c++) {
        MAX7219_flushChain(c);
    }
}

void MAX7219_clearChain(uint8_t chain)
{
    if (chain >= MAX7219_NUM_CHAINS) return;
    memset(frameBuffer[chain], 0, sizeof(frameBuffer[chain]));
}

void MAX7219_clearBuffer(void)
{
    memset(frameBuffer, 0, sizeof(frameBuffer));
}

/* ── Demo patterns ───────────────────────────────────────────────────────── */

void MAX7219_demo_allOn(void)
{
    for (uint8_t c = 0; c < MAX7219_NUM_CHAINS; c++)
        for (uint8_t d = 0; d < MAX7219_DEVICES_PER_CHAIN; d++)
            for (uint8_t r = 0; r < 8; r++)
                frameBuffer[c][d][r] = 0xFF;
    MAX7219_flushAll();
}

void MAX7219_demo_checkerboard(void)
{
    for (uint8_t c = 0; c < MAX7219_NUM_CHAINS; c++)
        for (uint8_t d = 0; d < MAX7219_DEVICES_PER_CHAIN; d++)
            for (uint8_t r = 0; r < 8; r++)
                frameBuffer[c][d][r] = (r % 2 == 0) ? 0xAA : 0x55;
    MAX7219_flushAll();
}

void MAX7219_demo_scroll(void)
{
    /* Sweep a fully-lit row across both chains top-to-bottom */
    for (uint8_t c = 0; c < MAX7219_NUM_CHAINS; c++) {
        for (uint8_t row = 0; row < 8; row++) {
            MAX7219_clearBuffer();
            for (uint8_t d = 0; d < MAX7219_DEVICES_PER_CHAIN; d++)
                frameBuffer[c][d][row] = 0xFF;
            MAX7219_flushAll();
            HAL_Delay(80);
        }
    }
    MAX7219_clearBuffer();
    MAX7219_flushAll();
}
