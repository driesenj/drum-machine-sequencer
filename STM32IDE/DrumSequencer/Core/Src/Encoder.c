#include "encoder.h"
#include <stdio.h>

// -------------------------------------------------------
// Increase this if you need more than 4 encoders.
// -------------------------------------------------------
#define ENCODER_MAX_INSTANCES 4

// -------------------------------------------------------
// Internal registry — all instances live here.
// -------------------------------------------------------
static Encoder_t  _encoders[ENCODER_MAX_INSTANCES];
static uint8_t    _encoder_count = 0;

// -------------------------------------------------------
// Register an encoder with its timer handle.
// -------------------------------------------------------
Encoder_t *Encoder_Add(TIM_HandleTypeDef *htim) {
    if (_encoder_count >= ENCODER_MAX_INSTANCES) return NULL;

    Encoder_t *enc = &_encoders[_encoder_count++];
    enc->htim     = htim;
    enc->count    = 0;
    enc->last_cnt = 0;
    enc->changed  = 0;

    // Seed last_cnt from current CNT so first delta is zero
    enc->last_cnt = (int16_t)htim->Instance->CNT;

    return enc;
}

// -------------------------------------------------------
// Start interrupt-driven encoder counting.
// -------------------------------------------------------
HAL_StatusTypeDef Encoder_Start(Encoder_t *enc) {
    return HAL_TIM_Encoder_Start_IT(enc->htim, TIM_CHANNEL_ALL);
}

// -------------------------------------------------------
// Public API
// -------------------------------------------------------
uint8_t Encoder_HasChanged(Encoder_t *enc) {
    return enc->changed;
}

int16_t Encoder_GetCount(Encoder_t *enc) {
    enc->changed = 0;
    return enc->count / 2;
}

void Encoder_Reset(Encoder_t *enc) {
    enc->count    = 0;
    enc->last_cnt = (int16_t)enc->htim->Instance->CNT;
    enc->changed  = 0;
}

// -------------------------------------------------------
// Single HAL callback — dispatches to all registered
// encoder instances automatically.
//
// The encoder timer fires on every edge. We compute a
// signed delta from the 16-bit CNT so the running count
// is direction-aware and survives counter wrap-around.
// -------------------------------------------------------
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim) {
    for (int i = 0; i < _encoder_count; i++) {
        Encoder_t *enc = &_encoders[i];

        if (htim->Instance == enc->htim->Instance) {
            int16_t current = (int16_t)htim->Instance->CNT;
            int16_t delta   = enc->last_cnt - current;
            enc->last_cnt   = current;
            enc->count     += delta;
            if (delta != 0 && enc->count % 2 == 0) {
            	enc->changed    = 1;
            }
            break;
        }
    }
}
