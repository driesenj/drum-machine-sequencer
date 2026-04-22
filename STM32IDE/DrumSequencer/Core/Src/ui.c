// ui_queue.c
#include "ui.h"
#include <string.h>
#include "stdio.h"
#include "utils.h"
#include "sequencer.h"
#include "flash.h"
#include "MAX7219.h"
#include "SSD1306.h"
#include "74HC595.h"
#include "stm32l4xx_hal.h"

static UIEvent_t _queue[UI_QUEUE_SIZE];
static uint8_t   _head = 0;
static uint8_t   _tail = 0;

void UI_Init(UIContext_t *ctx,
             UIFlashResult_t (*flash_write)(uint8_t, const SeqPreset_t *),
             void            (*flash_read) (uint8_t, SeqPreset_t *),
             uint8_t         (*flash_valid)(uint8_t),
             void            (*apply_clock_mode)(uint8_t))
{
    memset(ctx, 0, sizeof(UIContext_t));

    ctx->flash_write       = flash_write;
    ctx->flash_read        = flash_read;
    ctx->flash_valid       = flash_valid;
    ctx->apply_clock_mode  = apply_clock_mode;
    ctx->saveload_state    = SAVELOAD_IDLE;
    ctx->display_dirty     = 1;
    ctx->matrix_dirty      = 1;
    ctx->stepleds_dirty    = 1;
}

void UIQueue_Push(UIEvent_t evt) {
	uint8_t next = (_tail + 1) % UI_QUEUE_SIZE;
	if (next == _head) return;   // full, drop oldest or newest — your choice
	_queue[_tail] = evt;
	_tail = next;
}

uint8_t UIQueue_Pop(UIEvent_t *out) {
	if (_head == _tail) return 0;
	*out = _queue[_head];
	_head = (_head + 1) % UI_QUEUE_SIZE;
	return 1;
}

static void dispatch_enc_a_turn(UIContext_t *ctx, Sequencer_t *seq, int16_t delta, uint32_t now) {
	if (ctx->alt_mode) {
		if (ctx->all_held) {
			seq->global_rand = (uint8_t)CLAMP((int)seq->global_rand + delta, 0, 100);
			printf("Global random: %d%%\r\n", seq->global_rand);
		} else {
			SeqChannel_t *ch = &seq->channels[seq->current_channel];
			ch->rand_amount = (uint8_t)CLAMP((int)ch->rand_amount + delta, 0, 100);
			printf("CH:%d random: %d%%\r\n", seq->current_channel + 1, ch->rand_amount);
		}
	} else {
		Seq_SelectChannel(seq, delta);
		ctx->channel_flash_until_ms = now + 300;
		printf("Channel: %d\r\n", seq->current_channel + 1);
		ctx->stepleds_dirty = 1;
		ctx->matrix_dirty   = 1;
	}

	ctx->display_dirty = 1;
}

static void dispatch_enc_b_turn(UIContext_t *ctx, Sequencer_t *seq, int16_t delta, uint32_t now) {
	if (ctx->ratchet_mode) {
		SeqChannel_t *ch = &seq->channels[seq->current_channel];
		uint8_t any = 0;
		for (uint8_t i = 0; i < 8; i++) {
			if (!ctx->step_held[i]) continue;
			uint8_t step = ch->view_offset + i;
			if (step >= ch->length) continue;
			uint8_t cur = Seq_GetRatchet(seq, seq->current_channel, ch->active_bank, step);
			Seq_SetRatchet(seq, seq->current_channel, ch->active_bank, step,
					(uint8_t)CLAMP((int)cur + delta, 1, 8));
			// Ensure step is on if ratchet > 1
			if (Seq_GetRatchet(seq, seq->current_channel, ch->active_bank, step) > 1)
				Seq_SetStep(seq, seq->current_channel, ch->active_bank, step, 1);
			printf("Step %d ratchet: %d\r\n", step,
					Seq_GetRatchet(seq, seq->current_channel, ch->active_bank, step));
			any = 1;
		}
		if (any) {
			ctx->stepleds_dirty = 1;
			ctx->display_dirty = 1;
		}

	} else if (ctx->alt_mode) {
		if (ctx->all_held) {
			seq->global_swing = (uint8_t)CLAMP((int)seq->global_swing + delta, 0, 100);
			printf("Global swing: %d%%\r\n", seq->global_swing);
		} else {
			SeqChannel_t *ch = &seq->channels[seq->current_channel];
			ch->swing_amount = (uint8_t)CLAMP((int)ch->swing_amount + delta, 0, 100);
			printf("CH:%d swing: %d%%\r\n", seq->current_channel + 1, ch->swing_amount);
		}
	} else {
		Seq_SetLength(seq, delta);
		ctx->length_flash_until_ms = now + 300;
		printf("Length: %d\r\n", Seq_GetLength(seq));
		ctx->matrix_dirty = 1;
	}

	ctx->display_dirty = 1;
}

static void dispatch_enc_c_turn(UIContext_t *ctx, Sequencer_t *seq, int16_t delta, uint32_t now) {
	if (ctx->saveload_state == SAVELOAD_ACTIVE) {
		ctx->saveload_cursor = (int8_t)CLAMP((int)ctx->saveload_cursor + delta,
				0, SAVELOAD_MENU_COUNT - 1);
	} else if (ctx->alt_mode) {
		if (ctx->all_held) {
			if (seq->clock_mode == 1) {
				// Alt + turn + ALL held: edit internal clock BPM
				Seq_SetGlobalBPM(seq, delta);
				printf("Internal BPM: %d\r\n", Seq_GetGlobalBPM(seq));
			}
		} else {
			// Alt + turn: edit per-channel BPM
			Seq_SetChannelBPM(seq, delta);
			printf("CH:%d BPM: %d\r\n", seq->current_channel + 1,
					Seq_GetChannelBPM(seq));
		}
	} else {
		// Normal turn: scroll view window by 8
		Seq_SetViewOffset(seq, delta * 8);
		printf("View offset: %d\r\n",
				seq->channels[seq->current_channel].view_offset);
		ctx->matrix_dirty = 1;
		ctx->stepleds_dirty = 1;
	}
	ctx->display_dirty = 1;
}

static void dispatch_enc_a_push(UIContext_t *ctx, Sequencer_t *seq, uint32_t now) {
	if (ctx->alt_mode) {
		if (ctx->all_held) {
			Seq_MuteAllChannels(seq);
			printf("Mute all channels=%d\r\n", seq->global_mute);
		} else {
			// Alt + enc A push: mute/unmute current channel
			SeqChannel_t *ch = &seq->channels[seq->current_channel];
			Seq_MuteChannel(seq, seq->current_channel, !ch->muted);
			printf("Mute: ch=%d muted=%d\r\n",
					seq->current_channel, ch->muted);
		}
	} else {
		// Enc A push: solo/unsolo current channel
		Seq_ToggleSolo(seq);
		printf("Solo: ch=%d solo_ch=%d\r\n",
				seq->current_channel, seq->solo_channel);
	}
	ctx->display_dirty = 1;
}

static void dispatch_enc_b_push(UIContext_t *ctx, Sequencer_t *seq, uint32_t now) {
	if (ctx->alt_mode) {
		if (ctx->all_held) {
			Seq_ResetAllChannels(seq);
			printf("Reset all channels to step %lu\r\n", seq->global_step);
		} else {
			Seq_ResetChannel(seq, seq->current_channel);
			printf("Reset CH:%d to step %d\r\n",
					seq->current_channel + 1,
					seq->channels[seq->current_channel].current_step);
		}
		ctx->matrix_dirty   = 1;
		ctx->stepleds_dirty = 1;
	} else {
		ctx->ratchet_mode ^= 1;
		if (!ctx->ratchet_mode) {
			// clear any held state on exit so stale holds don't linger
			memset(ctx->step_held, 0, sizeof(ctx->step_held));
		}
		printf("Ratchet mode: %d\r\n", ctx->ratchet_mode);
		ctx->display_dirty  = 1;
		ctx->stepleds_dirty = 1;
	}
}

static void dispatch_enc_c_push(UIContext_t *ctx, Sequencer_t *seq, uint32_t now) {
	if (ctx->saveload_state == SAVELOAD_IDLE) {
		ctx->saveload_state  = SAVELOAD_ACTIVE;
		ctx->saveload_cursor = 0;
		printf("Save/load mode\r\n");
	} else if (ctx->alt_mode) {
	    if (ctx->all_held) {
	        Seq_ResetAllChannelSettings(seq);
	        printf("Reset settings all channels\r\n");
	    } else {
	        Seq_ResetChannelSettings(seq, seq->current_channel);
	        printf("Reset settings CH:%d\r\n", seq->current_channel + 1);
	    }
	    ctx->display_dirty = 1;
	    return;
	} else {
		// Confirm selection
		if (ctx->saveload_cursor == 0) {
			printf("Save/load cancelled\r\n");
		} else if (ctx->saveload_cursor <= NUM_SAVE_SLOTS) {
			uint8_t slot = ctx->saveload_cursor - 1;
			SeqPreset_t preset;
			Seq_SaveToPreset(seq, &preset, seq->internal_bpm);
			if (Flash_WritePreset(slot, &preset) == HAL_OK)
				printf("Saved slot %d\r\n", slot + 1);
			else
				printf("Save FAILED slot %d\r\n", slot + 1);
		} else {
			uint8_t slot = ctx->saveload_cursor - NUM_SAVE_SLOTS - 1;
			if (Flash_SlotValid(slot)) {
				SeqPreset_t preset;
				Flash_ReadPreset(slot, &preset);
				Seq_LoadFromPreset(seq, &preset, &seq->internal_bpm);
				seq->pending_internal_bpm     = seq->internal_bpm;
				seq->has_pending_internal_bpm = 0;
				printf("Loaded slot %d\r\n", slot + 1);
				ctx->matrix_dirty   = 1;
				ctx->stepleds_dirty = 1;
			} else {
				printf("Slot %d empty\r\n", slot + 1);
			}
		}
		ctx->saveload_state = SAVELOAD_IDLE;
	}
	ctx->display_dirty = 1;
}

static void dispatch_step_press(UIContext_t *ctx, Sequencer_t *seq, uint8_t pin, uint32_t now) {
	ctx->step_held[pin] = 1;

	if (ctx->alt_mode) {
		if (pin == 0) { ctx->all_held = 1; return; }

		if (pin >= 4 && pin <= 7) {
			uint8_t divs[] = {4, 8, 16, 32};
			seq->roll_division = divs[pin - 4];
			seq->last_roll_ms  = 0;
			return;
		}

		if (pin == 1) {
			if (ctx->all_held) Seq_ClearAllChannels(seq);
			else               Seq_ClearChannel(seq, seq->current_channel);
			ctx->matrix_dirty = ctx->stepleds_dirty = ctx->display_dirty = 1;
			return;
		}

		if (pin == 2) {
			seq->live_mode ^= 1;
			if (seq->live_mode) {
				seq->has_pending_internal_bpm = 0;
				for (uint8_t i = 0; i < SEQ_NUM_CHANNELS; i++)
					seq->channels[i].has_pending = 0;
			}
			ctx->stepleds_dirty = ctx->display_dirty = 1;
			return;
		}

		if (pin == 3) {
			seq->clock_mode ^= 1;

			if (ctx->apply_clock_mode) ctx->apply_clock_mode(seq->clock_mode);

			ctx->display_dirty = ctx->stepleds_dirty = 1;
			return;
		}

	} else {
		if (ctx->ratchet_mode) {
			ctx->stepleds_dirty = 1;
			return;
		}

		SeqChannel_t *ch = &seq->channels[seq->current_channel];
		uint8_t step = ch->view_offset + pin;
		if (step < ch->length) {
			Seq_ToggleStep(seq, seq->current_channel, ch->active_bank, step);
		    ctx->matrix_dirty = ctx->stepleds_dirty = ctx->display_dirty = 1;
		}
	}
}

static void dispatch_step_release(UIContext_t *ctx, Sequencer_t *seq, uint8_t pin, uint32_t now) {
	ctx->step_held[pin] = 0;
	if (ctx->alt_mode && pin == 0) ctx->all_held = 0;
	if (pin >= 4 && pin <= 7 && seq->roll_division == (uint8_t[]){4,8,16,32}[pin-4])
		seq->roll_division = 0;
}

static void dispatch_fill_press(UIContext_t *ctx, Sequencer_t *seq, uint32_t now) {
	if (ctx->alt_mode) {
		if (ctx->all_held) {
			// Alt + ALL: switch bank on all channels
			for (uint8_t i = 0; i < SEQ_NUM_CHANNELS; i++)
				seq->channels[i].active_bank ^= 1;
			printf("Bank all: %d\r\n", seq->channels[0].active_bank);
		} else {
			// Alt: switch bank on current channel
			SeqChannel_t *ch = &seq->channels[seq->current_channel];
			ch->active_bank ^= 1;
			printf("Bank: %d\r\n", ch->active_bank);
		}
		ctx->matrix_dirty   = 1;
		ctx->stepleds_dirty = 1;
		ctx->display_dirty  = 1;
	} else {
		// Normal: track hold state — gate fires on each tick while held
		ctx->fill_held = 1;

		// Trigger immediately on press, don't wait for next tick
		Seq_TriggerGate(seq, seq->current_channel, now, Seq_HalfPeriodMs(seq));
		printf("Fill press: ch=%d\r\n", seq->current_channel);
	}
	ctx->display_dirty = 1;
}

static void dispatch_fill_release(UIContext_t *ctx, Sequencer_t *seq, uint32_t now) {
	ctx->fill_held = 0;
}

static void dispatch_record_press(UIContext_t *ctx, Sequencer_t *seq, uint32_t now) {
	if (ctx->alt_mode) {
		if (!seq->live_mode) {
			// Commit mode: commit pending + BPM
			Seq_CommitPendingAll(seq);
			if (seq->has_pending_internal_bpm) {
				seq->internal_bpm = seq->pending_internal_bpm;
				seq->has_pending_internal_bpm = 0;
			}
			printf("Committed all + global BPM: %d\r\n", seq->internal_bpm);

			ctx->matrix_dirty   = 1;
			ctx->display_dirty = 1;
			ctx->stepleds_dirty = 1;
		} else {
			// Live mode: tap tempo
			static uint32_t last_tap_ms  = 0;
			static uint32_t tap_period   = 0;
			static uint8_t  tap_count    = 0;

			uint32_t gap     = now - last_tap_ms;

			if (gap > 2000) {
				// Gap too long — reset tap sequence
				tap_count = 0;
			}

			if (tap_count > 0 && gap > 0) {
				// Average in the new gap
				tap_period = (tap_period * (tap_count - 1) + gap) / tap_count;
				// period is one 1/16th step, BPM = 60000 / (period * 4)
				if (tap_period > 0) {
					uint16_t tapped_bpm = (uint16_t)CLAMP(60000 / (tap_period * 4), 20, 300);
					seq->internal_bpm         = tapped_bpm;
					seq->pending_internal_bpm = tapped_bpm;
					printf("Tap BPM: %d\r\n", seq->internal_bpm);
					ctx->display_dirty = 1;
				}
			}

			last_tap_ms = now;
			tap_count   = (tap_count < 8) ? tap_count + 1 : 8; // cap averaging window
		}
	} else {
		// Record current step and trigger gate
		SeqChannel_t *ch = &seq->channels[seq->current_channel];
		uint8_t step = ch->current_step;
		Seq_SetStep(seq, seq->current_channel, ch->active_bank, step, 1);
		Seq_TriggerGate(seq, seq->current_channel, now, Seq_HalfPeriodMs(seq));
		printf("Record: channel=%d step=%d\r\n", seq->current_channel, step);
		ctx->matrix_dirty = 1;
		ctx->display_dirty = 1;
		ctx->stepleds_dirty = 1;
	}
}

static void SaveLoad_CursorLabel(int8_t cursor, char *buf, uint8_t len)
{
	if (cursor == 0)
		snprintf(buf, len, "     CANCEL     ");
	else if (cursor <= NUM_SAVE_SLOTS)
		snprintf(buf, len, "  SAVE SLOT %d  %s",
				cursor,
				Flash_SlotValid(cursor - 1) ? "*" : " ");
	else
		snprintf(buf, len, "  LOAD SLOT %d  %s",
				cursor - NUM_SAVE_SLOTS,
				Flash_SlotValid(cursor - NUM_SAVE_SLOTS - 1) ? "*" : " ");
}


void UI_Dispatch(UIContext_t *ctx, Sequencer_t *seq, UIEvent_t *evt) {
	switch (evt->type) {
	case EVT_STEP_PRESS:    dispatch_step_press  (ctx, seq, evt->value, evt->tick_ms); break;
	case EVT_STEP_RELEASE:  dispatch_step_release(ctx, seq, evt->value, evt->tick_ms); break;
	case EVT_ENC_A_TURN:    dispatch_enc_a_turn  (ctx, seq, evt->value, evt->tick_ms); break;
	case EVT_ENC_B_TURN:    dispatch_enc_b_turn  (ctx, seq, evt->value, evt->tick_ms); break;
	case EVT_ENC_C_TURN:    dispatch_enc_c_turn  (ctx, seq, evt->value, evt->tick_ms); break;
	case EVT_ENC_A_PUSH:    dispatch_enc_a_push  (ctx, seq, evt->tick_ms);             break;
	case EVT_ENC_B_PUSH:    dispatch_enc_b_push  (ctx, seq, evt->tick_ms);             break;
	case EVT_ENC_C_PUSH:    dispatch_enc_c_push  (ctx, seq, evt->tick_ms);             break;
	case EVT_ALT_PRESS:     ctx->alt_mode = 1; ctx->stepleds_dirty = 1; break;
	case EVT_ALT_RELEASE:   ctx->alt_mode = 0; ctx->stepleds_dirty = 1; break;
	case EVT_FILL_PRESS:    dispatch_fill_press  (ctx, seq, evt->tick_ms);             break;
	case EVT_FILL_RELEASE:  dispatch_fill_release(ctx, seq, evt->tick_ms);     	   break;
	case EVT_RECORD_PRESS:  dispatch_record_press(ctx, seq, evt->tick_ms);             break;
	default: break;
	}
}

void UI_UpdateDisplay(UIContext_t *ctx, Sequencer_t *seq)
{
	char line[32];

	SSD1306_Fill(0);

	if (ctx->saveload_state == SAVELOAD_ACTIVE) {
		SSD1306_DrawString(0, 0, "  *** SAVE/LOAD ***  ", 1);
		SSD1306_DrawHLine(0, 11, 128, 1);

		// Show 5 menu items centered on cursor
		for (int8_t i = -2; i <= 2; i++) {
			int8_t item = ctx->saveload_cursor + i;
			if (item < 0 || item >= SAVELOAD_MENU_COUNT) continue;

			char item_label[17];
			SaveLoad_CursorLabel(item, item_label, sizeof(item_label));
			snprintf(line, sizeof(line), "%s%s",
					i == 0 ? ">" : " ",
							item_label);
			SSD1306_DrawString(0, 14 + (i + 2) * 10, line, 1);
		}

		SSD1306_Update();
		return;
	}

	SeqChannel_t *ch = &seq->channels[seq->current_channel];

	SSD1306_Fill(0);

	// ── Line 0: clock mode + BPM + live/commit ────────────────────────
	// [INT:180]      [LIVE]  (21 chars)
	// [EXT:180]      [CMIT]
	snprintf(line, sizeof(line), "[%s:%3d%s]  %s",
			seq->clock_mode ? "INT" : "EXT",
					seq->clock_mode ? (seq->live_mode ? seq->internal_bpm : seq->pending_internal_bpm)
							: seq->current_bpm,
							  (!seq->live_mode && seq->pending_internal_bpm != seq->internal_bpm) ? "*" : " ",
									  seq->live_mode ? (ctx->ratchet_mode ? " [RTCH]" : "  [LIVE]") : "[COMMIT]");

	SSD1306_DrawString(0, 0, line, 1);

	// ── Line 1: global random + swing ────────────────────────────────
	// RAND:099%  SWING:099%  (21 chars)
	uint8_t global_rand = seq->global_rand;
	uint8_t global_swing = seq->global_swing;
	snprintf(line, sizeof(line), "RAND:%3d%%  SWING:%3d%%",
			global_rand, global_swing);
	SSD1306_DrawString(0, 9, line, 1);

	// ── Separator ─────────────────────────────────────────────────────
	SSD1306_DrawHLine(0, 22, 128, 1);

	// ── Line 2: current channel + bank + length ───────────────────────
	// CHAN:01-A       LEN:64  (21 chars)
	snprintf(line, sizeof(line), "CHAN:%02d-%c      LEN:%02d",
			seq->current_channel + 1,
			ch->active_bank ? 'B' : 'A',
					ch->length);
	SSD1306_DrawString(0, 28, line, 1);

	// ── Line 3: per-channel random + swing ───────────────────────────
	// RAND:099%  SWING:099%  (21 chars)
	uint8_t ch_rand = ch->rand_amount;
	uint8_t ch_swing = ch->swing_amount;
	snprintf(line, sizeof(line), "RAND:%3d%%  SWING:%3d%%",
			ch_rand, ch_swing);
	SSD1306_DrawString(0, 37, line, 1);

	// ── Line 4: channel BPM + mute/solo/normal status ─────────────────
	// BPM:180        [MUTE]  (21 chars)
	// BPM:180        [SOLO]
	// BPM:180        [NORM]
	uint8_t ch_muted = ch->muted;
	const char *status;
	if (seq->solo_channel == (int8_t)seq->current_channel)
		status = " [SOLO]";
	else if (ch_muted)
		status = " [MUTE]";
	else if (seq->solo_channel >= 0)
		status = "[!SOLO]";
	else
		status = "";
	uint16_t disp_bpm = seq->live_mode
			? (ch->custom_bpm         ? ch->bpm         : (seq->clock_mode ? seq->internal_bpm : seq->current_bpm))
					: ((ch->has_pending && ch->pending.custom_bpm) ? ch->pending.bpm  : (seq->clock_mode ? seq->pending_internal_bpm : seq->current_bpm));
	uint8_t bpm_dirty = !seq->live_mode &&
			((ch->has_pending && ch->pending.bpm != ch->bpm) ||
					(ch->has_pending && ch->pending.custom_bpm != ch->custom_bpm));

	snprintf(line, sizeof(line), "BPM:%3d%s      %s",
			disp_bpm,
			bpm_dirty ? "*" : " ",
					status);
	SSD1306_DrawString(0, 46, line, 1);

	// ── Lines 5–6 (y=47, y=56): reserved for parameter overlay ───────

	SSD1306_Update();
}

static void Matrix_Build(UIContext_t *ctx, Sequencer_t *seq)
{
	memset(ctx->matrix_frame, 0, sizeof(ctx->matrix_frame));

	SeqChannel_t *active = &seq->channels[seq->current_channel];

	// ── Row 0: playhead of the active channel ─────────────────────────────
	uint8_t step         = active->current_step;
	uint8_t win_start    = active->view_offset;
	uint8_t win_end      = win_start + SEQ_VIEW_SIZE;  // exclusive

	// Light up the entire view window
	uint32_t row0 = 0;
	for (uint8_t i = win_start; i < win_end && i < 32; i++) {
		row0 |= (1UL << (31 - i));
	}

	// Playhead: XOR at current step — turns off a window LED while it's active,
	// or lights a single dot outside the window if step is in the second 32
	if (step < 32) {
		row0 ^= (1UL << (31 - step));
	}

	ctx->matrix_frame[0] = row0;

	// ── Rows 1–15: one row per channel ────────────────────────────────────
	uint8_t  flashing  = (HAL_GetTick() < ctx->channel_flash_until_ms);
	uint8_t  len_flashing = (HAL_GetTick() < ctx->length_flash_until_ms);
	uint8_t  flash_ch  = seq->current_channel;

	for (uint8_t ch = 0; ch < SEQ_NUM_CHANNELS; ch++) {
		if (flashing && ch == flash_ch) {
			// All 32 LEDs on for the selected channel row
			ctx->matrix_frame[ch + 1] = 0xFFFFFFFFUL;
			continue;
		}

		if (len_flashing && ch == flash_ch) {
			// Light up bits 0..length-1 (left-aligned, bit 31 = step 0)
			uint8_t len = seq->channels[ch].length;
			uint32_t bits = 0;
			uint8_t show = (len > 32) ? 32 : len;
			for (uint8_t s = 0; s < show; s++) {
				bits |= (1UL << (31 - s));
			}
			ctx->matrix_frame[ch + 1] = bits;
			continue;
		}

		SeqChannel_t *c = &seq->channels[ch];

		// Which 32-step block is currently playing?
		uint8_t block_offset = (c->current_step / 32) * 32;

		uint32_t bits = 0;
		uint8_t  show = c->length - block_offset;
		if (show > 32) show = 32;

		for (uint8_t s = 0; s < show; s++) {
			if (c->steps[c->active_bank][block_offset + s]) {
				bits |= (1UL << (31 - s));
			}
		}

		// Playhead: invert the bit at the current step so it's always visible
		uint8_t rel = c->current_step - block_offset;
		if (rel < 32) {
			bits ^= (1UL << (31 - rel));
		}

		ctx->matrix_frame[ch + 1] = bits;
	}
}

void UI_UpdateMatrix(UIContext_t *ctx, Sequencer_t *seq)
{
	Matrix_Build(ctx, seq);

	MAX7219_clearBuffer();

	for (uint8_t led_row = 0; led_row < 16; led_row++) {
		uint8_t  chain     = led_row / 8;   // chain 0 = rows 0–7, chain 1 = rows 8–15
		uint8_t  pixel_row = led_row % 8;
		uint32_t bits      = ctx->matrix_frame[led_row];

		for (uint8_t col = 0; col < 32; col++) {
			uint8_t device    = col / 8;    // device 0–3 within the chain
			uint8_t pixel_col = col % 8;
			bool    on        = (bits >> (31 - col)) & 1;
			MAX7219_setPixel(chain, device, pixel_col, pixel_row, on);
		}
	}

	MAX7219_flushAll();
}

void UI_UpdateStepLEDs(UIContext_t *ctx, Sequencer_t *seq)
{
	uint8_t mask = 0;

	if (ctx->ratchet_mode) {
		SeqChannel_t *ch = &seq->channels[seq->current_channel];
		for (uint8_t i = 0; i < 8; i++) {
			uint8_t step = ch->view_offset + i;
			if (step < ch->length &&
					Seq_GetStep(seq, seq->current_channel, ch->active_bank, step) &&
					Seq_GetRatchet(seq, seq->current_channel, ch->active_bank, step) > 1) {
				mask |= (1 << i);  // lit = has ratchet
			}
		}
	} else if (ctx->alt_mode) {
		// LED 3 (bit 2): lit = live mode
		// LED 4 (bit 3): lit = internal clock
		if (seq->live_mode) mask |= (1 << 2);
		if (seq->clock_mode)    mask |= (1 << 3);
	} else {
		SeqChannel_t *ch = &seq->channels[seq->current_channel];
		for (uint8_t i = 0; i < 8; i++) {
			uint8_t step = ch->view_offset + i;
			if (step < ch->length &&
					Seq_GetStep(seq, seq->current_channel, ch->active_bank, step)) {
				mask |= (1 << i);
			}
		}
	}

	uint8_t data[HC595_NUM_DEVICES];
	HC595_getShadow(data);
	data[0] = mask;
	HC595_write(data);
}
