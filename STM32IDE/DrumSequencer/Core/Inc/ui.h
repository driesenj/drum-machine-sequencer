#pragma once
#include <stdint.h>
#include "sequencer.h"
#include "flash.h"

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

// Capacity of the input event ring buffer. Events dropped silently when full.
// Size should exceed the maximum number of inputs that can arrive between two
// consecutive calls to UI_Dispatch in the main loop.
#define UI_QUEUE_SIZE 16

// Total items in the save/load menu: one cancel entry + N save slots + N load slots.
#define SAVELOAD_MENU_COUNT  (1 + NUM_SAVE_SLOTS + NUM_SAVE_SLOTS)

// ─────────────────────────────────────────────────────────────────────────────
// UIEventType_t
//
// All physical input events that can be pushed onto the queue. Callbacks in
// main.c translate raw MCP23017 pin states and encoder deltas into these events;
// UI_Dispatch translates them into sequencer and UI state changes.
// ─────────────────────────────────────────────────────────────────────────────
typedef enum {
    EVT_STEP_PRESS,     // One of the 8 step buttons was pressed   (value = pin index 0–7)
    EVT_STEP_RELEASE,   // One of the 8 step buttons was released  (value = pin index 0–7)

    EVT_ENC_A_TURN,     // Encoder A rotated  (value = signed delta, typically ±1)
    EVT_ENC_B_TURN,     // Encoder B rotated  (value = signed delta)
    EVT_ENC_C_TURN,     // Encoder C rotated  (value = signed delta)

    EVT_ENC_A_PUSH,     // Encoder A shaft button pressed
    EVT_ENC_B_PUSH,     // Encoder B shaft button pressed
    EVT_ENC_C_PUSH,     // Encoder C shaft button pressed

    EVT_ALT_PRESS,      // ALT modifier button pressed  — enables alt_mode
    EVT_ALT_RELEASE,    // ALT modifier button released — clears alt_mode
    EVT_FILL_PRESS,     // FILL button pressed  — holds gate open every step
    EVT_FILL_RELEASE,   // FILL button released
    EVT_RECORD_PRESS,   // RECORD button pressed  — records and triggers current step
    EVT_RECORD_RELEASE, // RECORD button released (currently unused but queued for symmetry)
} UIEventType_t;

// ─────────────────────────────────────────────────────────────────────────────
// UIEvent_t
//
// A single input event placed on the queue by a hardware callback or the
// encoder polling loop. tick_ms is the HAL_GetTick() value at the moment
// the event was detected, preserved for any time-sensitive dispatch logic
// (e.g. tap tempo).
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    uint32_t      tick_ms; // HAL_GetTick() at the moment the event was generated
    UIEventType_t type;
    int16_t       value;   // Signed delta for encoder turns; pin index for step events
} UIEvent_t;

// ─────────────────────────────────────────────────────────────────────────────
// SaveLoadState_t
// ─────────────────────────────────────────────────────────────────────────────
typedef enum {
    SAVELOAD_IDLE = 0, // Normal operation; ENC C push opens the menu
    SAVELOAD_ACTIVE,   // Save/load menu is open; ENC C turn scrolls, push confirms
} SaveLoadState_t;

// ─────────────────────────────────────────────────────────────────────────────
// UIFlashResult_t
//
// Return type for injected flash callbacks. Mirrors HAL_StatusTypeDef success /
// failure but without introducing a HAL dependency into ui.h.
// ─────────────────────────────────────────────────────────────────────────────
typedef enum {
    UI_FLASH_OK = 0,
    UI_FLASH_ERR,
} UIFlashResult_t;

// ─────────────────────────────────────────────────────────────────────────────
// UIContext_t
//
// All mutable UI state owned by the dispatcher. The main loop holds one instance
// of this struct and passes a pointer to UI_Dispatch and the render functions.
// No UI state lives as bare globals — everything is here so the context can be
// constructed deterministically in tests via UI_Init with fake callbacks.
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    // ── Modifier keys ─────────────────────────────────────────────────────────
    uint8_t alt_mode;      // 1 while the ALT button is held
    uint8_t all_held;      // 1 while step button 0 is held in alt_mode;
                           // makes per-channel operations apply to all channels
    uint8_t fill_held;     // 1 while the FILL button is held;
                           // forces a gate on the current channel every step
    uint8_t ratchet_mode;  // 1 while ratchet edit mode is active;
                           // ENC B turn changes the ratchet of any held step button
    uint8_t step_held[8];  // 1 for each step button currently held;
                           // used in ratchet mode to identify which steps to edit

    // ── LED matrix frame buffer ───────────────────────────────────────────────
    // 16 rows × 32 columns driven by two chains of four MAX7219 devices.
    // Each uint32_t is a bitmask for one row; bit 31 = leftmost column.
    // Built by Matrix_Build() and written to hardware by UI_UpdateMatrix().
    uint32_t matrix_frame[16];

    // ── Save/load menu ────────────────────────────────────────────────────────
    SaveLoadState_t saveload_state;  // Whether the save/load menu is open
    int8_t          saveload_cursor; // Currently highlighted menu item (0 = cancel,
                                     // 1–8 = save slots, 9–16 = load slots)

    // ── Render dirty flags ────────────────────────────────────────────────────
    // Set by the dispatcher when state changes require a hardware update.
    // Cleared by the main loop after the corresponding update function runs.
    uint8_t display_dirty;  // SSD1306 OLED needs a redraw
    uint8_t matrix_dirty;   // MAX7219 LED matrix needs a redraw
    uint8_t stepleds_dirty; // HC595 step button LEDs need a redraw

    // ── Timed flash indicators ────────────────────────────────────────────────
    // Set to HAL_GetTick() + duration when a flash effect starts.
    // The main loop sets matrix_dirty when the timestamp expires.
    uint32_t channel_flash_until_ms; // Flash the current channel row on channel select
    uint32_t length_flash_until_ms;  // Flash the length indicator on length change

    // ── Injected hardware callbacks ───────────────────────────────────────────
    // Populated by UI_Init. Using function pointers keeps ui.c free of direct
    // HAL and flash calls, allowing the dispatcher to be unit tested on a host
    // machine by substituting fakes. All pointers may be NULL in tests.

    // Reconfigures CLK_IN GPIO and NVIC for the given clock_mode (0 = external
    // EXTI input, 1 = output push-pull). Called when the user toggles clock mode.
    void (*apply_clock_mode)(uint8_t clock_mode);

    // Serialises and writes a preset to the given flash slot (0-based).
    UIFlashResult_t (*flash_write)(uint8_t slot, const SeqPreset_t *preset);

    // Reads a preset from the given flash slot into *preset via memcpy.
    void (*flash_read)(uint8_t slot, SeqPreset_t *preset);

    // Returns 1 if the given flash slot contains a valid preset (magic == SEQ_MAGIC).
    uint8_t (*flash_valid)(uint8_t slot);
} UIContext_t;

// ─────────────────────────────────────────────────────────────────────────────
// Initialisation
// ─────────────────────────────────────────────────────────────────────────────

// Zeroes the context, stores the injected callbacks, and sets the initial dirty
// flags so the first main loop iteration performs a full hardware redraw.
// Any callback pointer may be NULL; the dispatcher guards each call site.
void UI_Init(UIContext_t *ctx,
             UIFlashResult_t (*flash_write)(uint8_t, const SeqPreset_t *),
             void            (*flash_read) (uint8_t, SeqPreset_t *),
             uint8_t         (*flash_valid)(uint8_t),
             void            (*apply_clock_mode)(uint8_t));

// ─────────────────────────────────────────────────────────────────────────────
// Event queue
// ─────────────────────────────────────────────────────────────────────────────

// Enqueues an event. Silently drops the event if the queue is full.
// Called from MCP23017 pin callbacks and the encoder polling loop in main.c.
void UIQueue_Push(UIEvent_t evt);

// Dequeues the oldest event into *out. Returns 1 if an event was available,
// 0 if the queue was empty. Call in a loop until it returns 0.
uint8_t UIQueue_Pop(UIEvent_t *out);

// ─────────────────────────────────────────────────────────────────────────────
// Dispatcher
// ─────────────────────────────────────────────────────────────────────────────

// Processes a single event, updating ctx and seq as appropriate and setting
// dirty flags for any hardware that needs a redraw. All modifier state
// (alt_mode, all_held, ratchet_mode, step_held) is read from ctx.
void UI_Dispatch(UIContext_t *ctx, Sequencer_t *seq, UIEvent_t *evt);

// ─────────────────────────────────────────────────────────────────────────────
// Render functions
//
// Called by the main loop when the corresponding dirty flag is set. Each
// function reads from ctx and seq, writes to hardware, and does not clear
// the dirty flag — that is the main loop's responsibility.
// ─────────────────────────────────────────────────────────────────────────────

// Renders all sequencer state to the SSD1306 OLED display.
void UI_UpdateDisplay(UIContext_t *ctx, Sequencer_t *seq);

// Builds the LED matrix frame from sequencer state and flushes it to the
// MAX7219 chain via SPI.
void UI_UpdateMatrix(UIContext_t *ctx, Sequencer_t *seq);

// Updates the 8 step button LEDs via the HC595 to reflect the current view
// window, alt mode indicators, or ratchet state.
void UI_UpdateStepLEDs(UIContext_t *ctx, Sequencer_t *seq);
