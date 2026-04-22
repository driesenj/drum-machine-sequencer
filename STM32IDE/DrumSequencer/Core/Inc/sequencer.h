#pragma once
#include <stdint.h>

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

#define SEQ_NUM_CHANNELS  15    // Total number of independent gate output channels
#define SEQ_NUM_BANKS      2    // Number of step pattern banks per channel (A and B)
#define SEQ_MAX_STEPS     64    // Maximum pattern length in steps
#define SEQ_VIEW_SIZE      8    // Number of steps visible in the button window at once

// Written to flash as a validity marker. If a slot reads this value the preset is valid.
#define SEQ_MAGIC 0xDEADBEEF

// ─────────────────────────────────────────────────────────────────────────────
// SeqChannelPending_t
//
// Mirrors the editable fields of SeqChannel_t for use in commit mode.
// While live_mode == 0, edits are staged here and only promoted to the live
// channel state when Seq_CommitPending() is called. This allows the user to
// audition changes before committing them.
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    uint16_t custom_bpm;   // 0 = follow global BPM, 1 = use pending per-channel BPM
    uint16_t bpm;          // Staged per-channel BPM, valid only when custom_bpm == 1
    uint8_t  length;       // Staged pattern length (1–SEQ_MAX_STEPS)
    uint8_t  steps[SEQ_NUM_BANKS][SEQ_MAX_STEPS]; // Staged step on/off state
} SeqChannelPending_t;

// ─────────────────────────────────────────────────────────────────────────────
// SeqChannel_t
//
// All state for a single sequencer channel. Channels advance independently
// and each drives one hardware gate output via the HC595 shift register.
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    // ── BPM ───────────────────────────────────────────────────────────────────
    uint16_t custom_bpm;   // 0 = follow global BPM, 1 = use per-channel bpm field
    uint16_t bpm;          // Per-channel BPM; only used when custom_bpm == 1

    // ── Tick timing ───────────────────────────────────────────────────────────
    uint32_t last_tick_ms; // HAL_GetTick() value at the last channel advance;
                           // used only when custom_bpm == 1

    // ── Gate timing ───────────────────────────────────────────────────────────
    uint32_t gate_on_ms;   // Earliest time the gate may open (now + swing delay).
                           // Gate is suppressed while HAL_GetTick() < gate_on_ms.
    uint32_t gate_off_ms;  // Time at which the gate closes. UINT32_MAX = no active gate.
    uint8_t  gate_forced;  // 1 = gate is held open by fill/record/roll regardless
                           //     of whether the current step is on.

    // ── Pattern data ──────────────────────────────────────────────────────────
    uint8_t steps[SEQ_NUM_BANKS][SEQ_MAX_STEPS]; // Step on/off state; 1 = on, 0 = off
    uint8_t length;        // Active pattern length in steps (1–SEQ_MAX_STEPS)
    uint8_t view_offset;   // First step shown in the 8-button window (0..length-SEQ_VIEW_SIZE)
    uint8_t active_bank;   // Currently playing bank: 0 = A, 1 = B
    uint8_t current_step;  // Step index that will fire on the next advance (0..length-1)

    // ── Mute ──────────────────────────────────────────────────────────────────
    uint8_t muted;         // 1 = channel gate outputs are suppressed

    // ── Randomisation ─────────────────────────────────────────────────────────
    uint8_t rand_amount;        // Probability of random gate mutation (0–100 %)
    uint8_t rand_gate_suppress; // Set by Seq_AdvanceChannel: suppress an active step this tick
    uint8_t rand_gate_inject;   // Set by Seq_AdvanceChannel: fire a gate on an inactive step this tick

    // ── Swing ─────────────────────────────────────────────────────────────────
    uint8_t swing_amount;  // Delay applied to every odd-numbered step (0–100 %).
                           // 100 % ≈ period/3 (triplet feel). Takes the higher of
                           // this value and seq->global_swing.

    // ── Ratchet ───────────────────────────────────────────────────────────────
    uint8_t  ratchet[SEQ_NUM_BANKS][SEQ_MAX_STEPS]; // Repeat count per step: 1 = normal,
                                                     // 2–8 = fire N times within the step period
    uint8_t  ratchet_remaining;    // Sub-fires still pending this step (counts down to 0)
    uint32_t ratchet_interval_ms;  // Time between sub-fires = period / ratchet count
    uint32_t next_ratchet_ms;      // HAL_GetTick() value at which the next sub-fire is due

    // ── Commit mode ───────────────────────────────────────────────────────────
    uint8_t             has_pending; // 1 = pending contains unsaved edits
    SeqChannelPending_t pending;     // Staged edits waiting for Seq_CommitPending()
} SeqChannel_t;

// ─────────────────────────────────────────────────────────────────────────────
// Sequencer_t
//
// Top-level sequencer state. Owns all channels and global clock/transport state.
// Pass a pointer to this struct to every Seq_* function.
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    // ── Clock mode ────────────────────────────────────────────────────────────
    uint8_t clock_mode;    // 0 = external clock (EXTI on CLK_IN pin),
                           // 1 = internal clock (generated in Seq_TickClock)

    // ── External clock ────────────────────────────────────────────────────────
    uint32_t last_clock_ms;    // HAL_GetTick() at the last received external clock edge
    uint32_t clock_period_ms;  // Measured period between the last two clock edges (ms).
                               // 0 until at least two edges have been received.
    uint32_t gate_off_ms;      // Time at which the clock output pin should return low
    uint16_t current_bpm;      // BPM derived from clock_period_ms; 0 when unknown

    // ── Internal clock ────────────────────────────────────────────────────────
    uint32_t last_clk_tick;           // HAL_GetTick() at the last generated internal tick
    uint16_t internal_bpm;            // Internal clock rate (20–300 BPM)
    uint16_t pending_internal_bpm;    // Staged BPM for commit mode
    uint8_t  has_pending_internal_bpm; // 1 = pending_internal_bpm differs from internal_bpm

    // ── Channels ──────────────────────────────────────────────────────────────
    SeqChannel_t channels[SEQ_NUM_CHANNELS];
    uint8_t  current_channel; // Index of the channel selected for UI operations (0–14)
    int8_t   solo_channel;    // Channel whose output is isolated: -1 = none, 0–14 = soloed.
                              // When set, all other channels are silenced regardless of mute.
    uint8_t  live_mode;       // 1 = edits apply immediately (live),
                              // 0 = edits stage into pending until committed

    // ── Transport ─────────────────────────────────────────────────────────────
    uint8_t  running;      // 1 = sequencer is running, 0 = stopped (reserved for future use)
    uint32_t global_step;  // Monotonically incrementing master step counter.
                           // Used to re-align channels after a reset.

    // ── Global modifiers ──────────────────────────────────────────────────────
    uint8_t global_mute;   // 1 = all channels muted
    uint8_t global_rand;   // Global randomisation floor (0–100 %).
                           // Each channel uses max(ch->rand_amount, global_rand).
    uint8_t global_swing;  // Global swing floor (0–100 %).
                           // Each channel uses max(ch->swing_amount, global_swing).

    // ── Roll ──────────────────────────────────────────────────────────────────
    uint8_t  roll_division; // 0 = off. Active values: 4/8/16/32 (subdivisions of a 1/4 note).
                            // When non-zero, Seq_UpdateRoll fires gates on the current channel
                            // at this subdivision rate, bypassing step data, swing, and ratchet.
    uint32_t last_roll_ms;  // HAL_GetTick() at the last roll gate trigger

    // ── Hardware callback ─────────────────────────────────────────────────────
    void (*clock_output)(uint8_t high); // Called to drive the CLK_IN pin high (1) or low (0).
                                        // Injected at init so sequencer.c has no HAL dependency.
                                        // May be NULL in unit tests.
} Sequencer_t;

// ─────────────────────────────────────────────────────────────────────────────
// SeqPreset_t
//
// Flat, flash-friendly snapshot of all sequencer state that should persist
// across power cycles. Loaded and saved via Seq_SaveToPreset / Seq_LoadFromPreset.
// The magic field is checked on load; a slot is considered empty if magic != SEQ_MAGIC.
// Size must remain a multiple of 8 bytes for the doubleword flash write loop.
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    uint32_t magic;         // SEQ_MAGIC when slot is valid, 0xFF…F when erased
    uint8_t  steps[SEQ_NUM_CHANNELS][SEQ_NUM_BANKS][SEQ_MAX_STEPS];
    uint8_t  ratchet[SEQ_NUM_CHANNELS][SEQ_NUM_BANKS][SEQ_MAX_STEPS];
    uint8_t  length[SEQ_NUM_CHANNELS];
    uint16_t bpm[SEQ_NUM_CHANNELS];      // 0 = follow global BPM
    uint8_t  rand_amount[SEQ_NUM_CHANNELS];
    uint8_t  swing_amount[SEQ_NUM_CHANNELS];
    uint8_t  muted[SEQ_NUM_CHANNELS];
    uint8_t  active_bank[SEQ_NUM_CHANNELS];
    uint8_t  global_rand;
    uint8_t  global_swing;
    uint16_t internal_bpm;
    uint8_t  _pad[2];       // Padding to keep sizeof(SeqPreset_t) a multiple of 8
} SeqPreset_t;

// ─────────────────────────────────────────────────────────────────────────────
// Initialisation
// ─────────────────────────────────────────────────────────────────────────────

// Zeroes all sequencer state and sets safe defaults. Must be called before any
// other Seq_* function. clock_output is the HAL GPIO callback used to drive the
// CLK_IN pin; pass NULL in unit tests.
void Seq_Init(Sequencer_t *seq, void (*clock_output)(uint8_t high));

// ─────────────────────────────────────────────────────────────────────────────
// Clock
// ─────────────────────────────────────────────────────────────────────────────

// Generates one internal clock tick if the period has elapsed. Drives the clock
// output pin high on the tick and low at the midpoint. Returns 1 if a tick fired
// this call, 0 otherwise. No-op when clock_mode != 1.
uint8_t  Seq_TickClock(Sequencer_t *seq, uint32_t now);

// Returns the HAL_GetTick() value at which the next clock tick is expected.
// Used by the main loop to avoid starting a slow display update too close to
// a tick boundary. Falls back to now+1000 if no external period has been measured.
uint32_t Seq_NextTickMs(Sequencer_t *seq, uint32_t now);

// Returns half the current clock period in ms, floored at 5 ms.
// Used as the default gate duration for fill, record, and triggered gates.
uint32_t Seq_HalfPeriodMs(Sequencer_t *seq);

// ─────────────────────────────────────────────────────────────────────────────
// Transport
// ─────────────────────────────────────────────────────────────────────────────

// Advances all channels for the current tick. Updates clock_period_ms and
// global_step when clock_fired == 1. Returns a bitmask of which channels
// ticked this call (bit N set = channel N advanced). The caller uses this
// to apply fill_held and set matrix_dirty without reaching into channel state.
uint16_t Seq_AdvanceAll(Sequencer_t *seq, uint32_t now, uint8_t clock_fired);

// Advances a single channel by one step, applies swing delay, sets up ratchet
// sub-fires, and evaluates randomisation. Called by Seq_AdvanceAll; only call
// directly when a channel has a custom BPM.
void Seq_AdvanceChannel(Sequencer_t *seq, uint8_t ch_index, uint32_t period_ms);

// Fires repeated gates on the current channel at roll_division subdivisions of
// a quarter note. Bypasses step data, swing, ratchet, and randomisation entirely.
// No-op when roll_division == 0 or clock_period_ms == 0.
void Seq_UpdateRoll(Sequencer_t *seq, uint32_t now);

// ─────────────────────────────────────────────────────────────────────────────
// Gate outputs
// ─────────────────────────────────────────────────────────────────────────────

// Writes the current gate state for all channels to the HC595 shift register.
// Also services ratchet sub-fires. Must be called every main loop iteration.
void Seq_UpdateGateOutputs(Sequencer_t *seq, uint32_t now);

// Immediately silences all gate outputs. Does not affect channel gate timing state.
void Seq_AllGatesOff(Sequencer_t *seq);

// Forces a gate open on the given channel for duration_ms milliseconds.
// Sets gate_forced so the gate fires even if the current step is off.
// Used by fill, record, and roll.
void Seq_TriggerGate(Sequencer_t *seq, uint8_t channel, uint32_t now, uint32_t duration_ms);

// ─────────────────────────────────────────────────────────────────────────────
// Channel selection and navigation
// ─────────────────────────────────────────────────────────────────────────────

// Moves current_channel by delta steps, wrapping at both ends.
void Seq_SelectChannel(Sequencer_t *seq, int delta);

// Scrolls the 8-step view window of the current channel by delta * SEQ_VIEW_SIZE steps.
// Clamps at 0 and length - SEQ_VIEW_SIZE.
void Seq_SetViewOffset(Sequencer_t *seq, int delta);

// Returns 1 if the given channel should produce gate output, taking solo and
// mute state into account. Solo always wins over mute.
uint8_t Seq_IsChannelActive(const Sequencer_t *seq, uint8_t channel);

// ─────────────────────────────────────────────────────────────────────────────
// Pattern editing
// ─────────────────────────────────────────────────────────────────────────────

// Adjusts the current channel's pattern length by delta steps (clamped 1–SEQ_MAX_STEPS).
// In commit mode, writes to pending.length rather than length.
void    Seq_SetLength(Sequencer_t *seq, int delta);

// Returns the effective length of the current channel: pending.length in commit
// mode (when has_pending), otherwise length.
uint8_t Seq_GetLength(Sequencer_t *seq);

// Sets step on/off state directly. Always writes to the live step array.
// Use Seq_ToggleStep for commit-mode-aware editing.
void    Seq_SetStep(Sequencer_t *seq, uint8_t channel, uint8_t bank, uint8_t step, uint8_t state);

// Toggles a step. In live mode writes directly; in commit mode writes to pending.steps.
void    Seq_ToggleStep(Sequencer_t *seq, uint8_t channel, uint8_t bank, uint8_t step);

// Returns the effective step state: pending.steps in commit mode (when has_pending),
// otherwise steps.
uint8_t Seq_GetStep(const Sequencer_t *seq, uint8_t channel, uint8_t bank, uint8_t step);

// Returns a pointer into the live step array at the current view offset.
// Provided for bulk reads; prefer Seq_GetStep for individual step access.
uint8_t *Seq_GetVisibleSteps(Sequencer_t *seq, uint8_t bank);

// Returns the number of steps currently visible in the window (≤ SEQ_VIEW_SIZE).
// May be less than SEQ_VIEW_SIZE if the pattern is shorter than the window.
uint8_t  Seq_GetVisibleLength(const Sequencer_t *seq);

// Clears all steps in the active bank of the given channel and discards any
// pending edits. Does not affect ratchet data or channel settings.
void Seq_ClearChannel(Sequencer_t *seq, uint8_t ch_index);
void Seq_ClearAllChannels(Sequencer_t *seq);

// ─────────────────────────────────────────────────────────────────────────────
// Ratchet
// ─────────────────────────────────────────────────────────────────────────────

// Returns the ratchet count for the given step (1 = no ratchet, 2–8 = sub-fires).
uint8_t Seq_GetRatchet(const Sequencer_t *seq, uint8_t ch, uint8_t bank, uint8_t step);

// Sets the ratchet count for the given step, clamped to 1–8.
void    Seq_SetRatchet(Sequencer_t *seq, uint8_t ch, uint8_t bank, uint8_t step, uint8_t count);

// ─────────────────────────────────────────────────────────────────────────────
// BPM
// ─────────────────────────────────────────────────────────────────────────────

// Adjusts the global (internal) BPM by delta. In live mode applies immediately;
// in commit mode writes to pending_internal_bpm.
void     Seq_SetGlobalBPM(Sequencer_t *seq, int delta);

// Returns the effective global BPM: pending_internal_bpm in commit mode
// (when has_pending_internal_bpm), otherwise internal_bpm.
uint16_t Seq_GetGlobalBPM(Sequencer_t *seq);

// Adjusts the current channel's BPM by delta, enabling custom_bpm automatically.
// If the result equals the global BPM, custom_bpm is cleared (reverts to follow).
// In commit mode writes to pending.bpm and pending.custom_bpm.
void     Seq_SetChannelBPM(Sequencer_t *seq, int delta);

// Returns the effective channel BPM: pending.bpm in commit mode (when has_pending),
// otherwise bpm.
uint16_t Seq_GetChannelBPM(Sequencer_t *seq);

// ─────────────────────────────────────────────────────────────────────────────
// Mute and solo
// ─────────────────────────────────────────────────────────────────────────────

// Sets the mute state of the given channel. Muted channels still advance but
// produce no gate output unless they are the solo channel.
void Seq_MuteChannel(Sequencer_t *seq, uint8_t ch_index, uint8_t mute);

// Toggles global_mute and applies the new state to all channels.
void Seq_MuteAllChannels(Sequencer_t *seq);

// Toggles solo on the current channel. If the channel is already soloed,
// clears solo_channel (-1). Only one channel can be soloed at a time.
void Seq_ToggleSolo(Sequencer_t *seq);

// ─────────────────────────────────────────────────────────────────────────────
// Reset
// ─────────────────────────────────────────────────────────────────────────────

// Rewinds the given channel's current_step to align with global_step, so it
// re-enters phase with the master transport on the next advance.
void Seq_ResetChannel(Sequencer_t *seq, uint8_t ch_index);
void Seq_ResetAllChannels(Sequencer_t *seq);

// Resets per-channel settings (BPM, swing, rand) to defaults without touching
// step data, mute state, or pattern length.
void Seq_ResetChannelSettings(Sequencer_t *seq, uint8_t ch_index);
void Seq_ResetAllChannelSettings(Sequencer_t *seq);

// ─────────────────────────────────────────────────────────────────────────────
// Commit mode
// ─────────────────────────────────────────────────────────────────────────────

// Seeds the pending struct from the current live state and sets has_pending.
// Called automatically on the first edit in commit mode; safe to call again.
void Seq_InitPending(Sequencer_t *seq, uint8_t ch);

// Promotes pending edits to the live channel state and clears has_pending.
// No-op if has_pending == 0.
void Seq_CommitPending(Sequencer_t *seq, uint8_t ch);
void Seq_CommitPendingAll(Sequencer_t *seq);

// ─────────────────────────────────────────────────────────────────────────────
// Preset save / load
// ─────────────────────────────────────────────────────────────────────────────

// Serialises the current live sequencer state into a flat preset struct
// suitable for writing to flash. global_bpm is passed explicitly because
// internal_bpm may differ from the effective BPM during commit mode.
void Seq_SaveToPreset(const Sequencer_t *seq, SeqPreset_t *preset, uint16_t global_bpm);

// Restores sequencer state from a preset. Returns immediately if magic != SEQ_MAGIC.
// Resets all runtime gate and ratchet state. Writes the stored BPM to
// *global_bpm_out if non-NULL.
void Seq_LoadFromPreset(Sequencer_t *seq, const SeqPreset_t *preset, uint16_t *global_bpm_out);
