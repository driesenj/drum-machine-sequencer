#include "flash.h"
#include <string.h>
#include "utils.h"
#include <assert.h>

static_assert(
    FLASH_PRESET_BASE + NUM_SAVE_SLOTS * PRESET_PAGES * FLASH_PAGE_SIZE
        <= 0x08100000UL,
    "Flash preset area exceeds end of 1024KB flash"
);

// Returns 1 if slot has valid data
uint8_t Flash_SlotValid(uint8_t slot)
{
	const SeqPreset_t *p = (const SeqPreset_t *)FLASH_PRESET_ADDR(slot);
	return p->magic == SEQ_MAGIC;
}

HAL_StatusTypeDef Flash_WritePreset(uint8_t slot, const SeqPreset_t *preset)
{
	if (slot >= NUM_SAVE_SLOTS) return HAL_ERROR;

	HAL_StatusTypeDef status;
	FLASH_EraseInitTypeDef erase = {
			.TypeErase = FLASH_TYPEERASE_PAGES,
			.Banks     = FLASH_BANK_1,
			.Page      = (FLASH_PRESET_ADDR(slot) - FLASH_BASE) / FLASH_PAGE_SIZE,
			.NbPages   = PRESET_PAGES
	};

	status = HAL_FLASH_Unlock();
	if (status != HAL_OK) return status;

	uint32_t page_error;
	status = HAL_FLASHEx_Erase(&erase, &page_error);
	if (status != HAL_OK) { HAL_FLASH_Lock(); return status; }

	const uint64_t *src  = (const uint64_t *)preset;
	uint32_t        addr = FLASH_PRESET_ADDR(slot);
	uint32_t        n    = (sizeof(SeqPreset_t) + 7) / 8;

	for (uint32_t i = 0; i < n; i++) {
		status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, src[i]);
		if (status != HAL_OK) { HAL_FLASH_Lock(); return status; }
		addr += 8;
	}

	HAL_FLASH_Lock();
	return HAL_OK;
}

void Flash_ReadPreset(uint8_t slot, SeqPreset_t *preset)
{
	memcpy(preset, (const void *)FLASH_PRESET_ADDR(slot), sizeof(SeqPreset_t));
}
