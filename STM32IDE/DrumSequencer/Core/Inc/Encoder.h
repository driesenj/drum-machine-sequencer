#pragma once

#include "stm32l4xx_hal.h"
#include <stdint.h>

// -------------------------------------------------------
// Encoder instance — one per physical rotary encoder.
// Initialise with Encoder_Add(), then call
// Encoder_Start() to begin counting.
// -------------------------------------------------------
typedef struct {
    TIM_HandleTypeDef  *htim;       // e.g. &htim3, &htim4
    volatile int16_t    count;      // signed running count (wraps at ±32767)
    volatile int16_t    last_cnt;   // previous raw CNT, used to compute delta
    volatile uint8_t    changed;    // set to 1 by ISR, cleared by user
} Encoder_t;

// -------------------------------------------------------
// Register an encoder with its timer handle.
// Call once per encoder before Encoder_Start().
// Returns pointer to the encoder instance, or NULL if
// the registry is full (increase ENCODER_MAX_INSTANCES).
// -------------------------------------------------------
Encoder_t *Encoder_Add(TIM_HandleTypeDef *htim);

// -------------------------------------------------------
// Start the encoder timer interrupt.
// Call after HAL peripheral init (MX_TIMx_Init).
// -------------------------------------------------------
HAL_StatusTypeDef Encoder_Start(Encoder_t *enc);

// -------------------------------------------------------
// Returns 1 if the encoder has moved since last check.
// -------------------------------------------------------
uint8_t Encoder_HasChanged(Encoder_t *enc);

// -------------------------------------------------------
// Returns the signed running count and clears changed flag.
// -------------------------------------------------------
int16_t Encoder_GetCount(Encoder_t *enc);

// -------------------------------------------------------
// Reset the running count to zero.
// -------------------------------------------------------
void Encoder_Reset(Encoder_t *enc);
