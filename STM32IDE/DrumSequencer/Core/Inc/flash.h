#pragma once
#include <stdint.h>
#include "stm32l4xx_hal.h"
#include "sequencer.h"

#define NUM_SAVE_SLOTS      8
#define FLASH_PRESET_BASE   0x080F8000UL  // last 16KB of 256KB flash — adjust as needed
#define PRESET_PAGES        ((sizeof(SeqPreset_t) + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE)
#define FLASH_PRESET_ADDR(slot) (FLASH_PRESET_BASE + (slot) * PRESET_PAGES * FLASH_PAGE_SIZE)

uint8_t Flash_SlotValid(uint8_t slot);
HAL_StatusTypeDef Flash_WritePreset(uint8_t slot, const SeqPreset_t *preset);
void Flash_ReadPreset(uint8_t slot, SeqPreset_t *preset);
