/* T9-K0: T9 / multitap composition engine. Pure C, no QMK dependency, no
 * heap, bounded buffers. The engine turns semantic key events plus an explicit
 * clock into an ordered stream of host edit operations (see t9_output.h).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Text model
 * ----------
 * The host text is "committed text" followed by an optional active composition
 * ("preview") that sits immediately before the cursor. The engine only ever
 * edits the preview: it emits backspaces for the changed suffix and retypes the
 * new suffix (longest unchanged prefix kept). Committed text is never touched,
 * except that T9_KEY_DELETE / T9_KEY_CANCEL / T9_KEY_OTHER are passed through to
 * the host when no composition is active. `shown` mirrors what the host will
 * display for the preview once the output queue drains; every diff is computed
 * against it, so later input is always ordered after earlier output.
 *
 * Modes (T9_KEY_MODE tap cycles T9 -> MULTITAP -> NUMPAD -> T9):
 *   T9       digits 2-9 extend a digit sequence, the top candidate is shown.
 *            A candidate is a whole word shown split at the typed length,
 *            `base[suffix]` (T9_MARKERS), or just `base` when the word is
 *            exactly that long. With completions on, longer words come first;
 *            exact words follow unless a listed longer word starts with the
 *            same letters. Accepting keeps the base; T9_KEY_COMPLETE
 *            takes the whole word. With completions off (t9_set_completions)
 *            exact words come first, then remaining prefixes, each group in
 *            rank order. Only the first entry per base is kept and the base
 *            alone is shown, so cycling steps through distinct bases.
 *            Experimental: if the unfiltered lookup is one word or one chain
 *            of word prefixes, show `base>suffix` for the shortest word beyond
 *            the base. A full lookup table cannot establish a complete family.
 *            COMPLETE fills that word and offers the next extension, or accepts
 *            with a space when no further extension remains.
 *   MULTITAP repeated taps on a key cycle its letters; another key or the
 *            multitap timeout finalizes the letter (never the word).
 *   NUMPAD   nothing is intercepted except T9_KEY_MODE (stock numpad behaviour).
 * The completions setting is not a mode: it survives the mode cycle and
 * t9_reset. Toggling it while composing in T9 re-renders the preview in place
 * (`cand[idate]` <-> `cand`, or `cand>idate` for a prefix family) with the
 * selection rule below, never accepting.
 * T9_KEY_MODE first finishes the active composition without a space. Holding
 * T9_KEY_MODE for long_press_ms reverts the cycle and emits the passthrough
 * code given with the press (intended: host Num Lock).
 *
 * Keys while composing (T9 and MULTITAP):
 *   2-9       extend; in T9 a digit with no word starting with the sequence
 *             is rejected (sequence and preview unchanged, T9_EVENT_MISS).
 *   NEXT/PREV cycle candidates (T9), punctuation (active punctuation) or do
 *             nothing (MULTITAP). Cycling wraps within the current list.
 *   0         accept the base and type a space; with no composition type a
 *             space. Holding 0 erases that space and types '0'.
 *   DOT       accept the base without a space; with no composition do nothing.
 *   COMPLETE  with []: replace the preview with the whole word plus a space
 *             and accept. With >: fill the offered word into the typed base;
 *             if another family member remains, show its suffix and keep the
 *             composition active. Otherwise accept the filled word plus a space.
 *             Filled letters become digits that DELETE can remove individually.
 *             Without a completion: exactly OTHER with its passthrough code.
 *   DELETE    remove the last digit / letter / punctuation and recompute the
 *             preview (T9 keeps the same whole word when the shorter sequence
 *             lists it, else one with the same base); deleting the last unit
 *             ends the composition but not a temporary spelling. With no
 *             composition the key is not handled (host backspace), unless
 *             output is still queued: then a backspace is queued behind it.
 *   CANCEL    erase the preview (markers included), end the composition and
 *             any temporary spelling. With no composition and no spelling the
 *             key is not handled (host escape), unless output is still queued:
 *             then the passthrough key is queued behind it.
 *   SPELL     held long_press_ms in T9 mode: erase the preview and restart the
 *             word in temporary multitap; accepting returns to T9. A shorter
 *             press, or another key before the hold fires, does nothing. During a
 *             multitap word (MULTITAP mode or spelling) SPELL only finishes
 *             the pending letter, so the same key starts a new letter without
 *             waiting for the timeout. An active punctuation is accepted
 *             instead of erased. Every accepting key (0, DOT, 1, OTHER, MODE,
 *             long press) ends a temporary spelling.
 *   CAPS      cycle caps OFF -> ONCE -> LOCK -> OFF. ONCE capitalizes the first
 *             letter of the active or next word and is consumed when a word with
 *             letters is accepted; its case stays stable while cycling. LOCK
 *             uppercases the suffix too.
 *   1         punctuation, cycling . , ? ! ' - . In T9, the first 1 after a
 *             potential contraction base keeps the word and provisional '.'
 *             together (JOIN). Following digits try common contraction endings:
 *             416 -> i'm, 4183 -> i've, 96618 -> won't. An explicitly cycled
 *             base is pinned; otherwise the ending can resolve its ambiguity.
 *             A nonmatching first suffix digit accepts the word and period,
 *             then starts an ordinary word. Another 1 or NEXT/PREV selects
 *             ordinary punctuation, accepting the base and disabling joining
 *             even if the cycle wraps. Other accepting keys keep the period.
 *             Once matched, a contraction is one T9 composition; DELETE removes
 *             suffix digits, restores the provisional period, then removes 1.
 *             CANCEL erases the entire pending/matched contraction. Caps once
 *             is consumed only when it is accepted or resolved as punctuation.
 *             Elsewhere, 1 accepts the preceding word without a space and starts
 *             one-character punctuation as before (manual don ' t still works).
 *   OTHER     accept the base without a space, then pass the key through after
 *             queued output (T9_OUT_KEY with the passthrough code). With no
 *             composition and an empty queue the key is not handled (host
 *             processes it).
 * Long press of 1-9 (held long_press_ms): the tap effect is reverted, the
 * preceding composition is accepted without a space, and the digit is typed.
 * While 1 is held in either typing mode, tapping 2 cycles ( [ { < and tapping
 * 3 cycles ) ] } >; tapping 4 cycles : ; | ~ and 5 cycles + ` @ \.
 * The first chord replaces 1's
 * tap effect or its already emitted literal 1, preserving the pre-press base.
 * Repeated taps replace one symbol; switching groups starts at the new group's
 * first character. The chord suppresses further long-press output; releasing 1
 * accepts the symbol. Another key cancels the held prefix and acts on the symbol
 * as punctuation. Numpad mode retains stock rollover, with no typing chords.
 *
 * Accepting with a suffix shown first replaces the preview with the base, an
 * edit that is refused like any other when the queue is full (T9_EVENT_BUSY);
 * the composition, the mode and the host text then stay as they were.
 *
 * Candidate selection: with completions off, extending the sequence prefers
 * the first exact word unless a candidate was explicitly cycled. Otherwise,
 * extending or shortening the sequence, or toggling completions, keeps the same
 * whole word when the new list holds it. With completions on, reaching
 * its end prefers the best longer word with those same letters. Otherwise
 * the top entry is shown,
 * except that an explicit choice (a cycle on the previous sequence), a
 * deletion and a toggle prefer the first entry whose first letters equal the
 * previous base. Without that exception `36` (fo[r]) `6` would show `foo` instead of
 * `don`.
 *
 * Determinism: t9_key_press/release and t9_tick take the current time in ms.
 * Key handlers run the tick logic first, so timing does not depend on how often
 * the host calls t9_tick. Multitap finalizes when now >= press_time + timeout;
 * long press fires when now >= press_time + long_press_ms while still held.
 *
 * Output limits: an operation that could need more than the free queue space is
 * refused, the key is ignored and T9_EVENT_BUSY is raised; host text is never
 * corrupted by partial output. Erasing a character that is still queued takes
 * the queued item back instead of adding a backspace (t9_output_retract).
 *
 * Reset: t9_reset() drops the composition, timers and queued output without
 * emitting anything; mode, caps and the completions setting are kept. Call
 * it on host switch, transport change, disconnect and suspend.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "t9_dictionary.h"
#include "t9_output.h"

typedef enum {
    T9_KEY_0 = 0,
    T9_KEY_1,
    T9_KEY_2,
    T9_KEY_3,
    T9_KEY_4,
    T9_KEY_5,
    T9_KEY_6,
    T9_KEY_7,
    T9_KEY_8,
    T9_KEY_9,
    T9_KEY_NEXT,     /* '*' or knob clockwise */
    T9_KEY_PREV,     /* knob counter-clockwise */
    T9_KEY_DOT,      /* '.' accept without space */
    T9_KEY_DELETE,   /* '-' or Backspace */
    T9_KEY_CANCEL,   /* Escape */
    T9_KEY_SPELL,    /* '+' */
    T9_KEY_CAPS,     /* '/' */
    T9_KEY_MODE,     /* Num */
    T9_KEY_COMPLETE, /* Enter */
    T9_KEY_OTHER,    /* any other key: Tab, Delete, macros, navigation */
    T9_KEY_COUNT
} t9_key_t;

typedef enum { T9_MODE_T9 = 0, T9_MODE_MULTITAP, T9_MODE_NUMPAD, T9_MODE_COUNT } t9_mode_t;

typedef enum { T9_CAPS_OFF = 0, T9_CAPS_ONCE, T9_CAPS_LOCK } t9_caps_t;

typedef enum { T9_PUNCT_NORMAL = 0, T9_PUNCT_OPEN, T9_PUNCT_CLOSE, T9_PUNCT_SYMBOL, T9_PUNCT_EXTRA } t9_punct_group_t;

typedef enum {
    T9_COMPOSE_NONE = 0,
    T9_COMPOSE_T9,       /* digit sequence with candidates */
    T9_COMPOSE_MULTITAP, /* letters typed one by one */
    T9_COMPOSE_PUNCT,    /* one replaceable punctuation character */
    T9_COMPOSE_JOIN,     /* T9 base + first 1: provisional period or contraction */
} t9_compose_t;

enum {
    T9_EVENT_MISS         = 1 << 0, /* rejected digit: no word or stem */
    T9_EVENT_BUSY         = 1 << 1, /* output queue too full for the operation */
    T9_EVENT_MODE_CHANGED = 1 << 2,
    T9_EVENT_CAPS_CHANGED = 1 << 3,
};

typedef struct {
    uint16_t multitap_timeout_ms; /* default 800 */
    uint16_t long_press_ms;       /* default 500 */
} t9_config_t;

#define T9_PUNCTUATION ".,?!'-"
#define T9_WRAPPERS_OPEN "([{<"
#define T9_WRAPPERS_CLOSE ")]}>"
#define T9_SYMBOLS ":;|~"
#define T9_SYMBOLS_EXTRA "+`@\\"
/* opening and closing marker around the completion suffix in the preview */
#define T9_MARKERS "[]"
/* longest preview: a whole word plus the two markers */
#define T9_SHOWN_MAX (T9_MAX_WORD_LEN + 2)

typedef struct {
    t9_config_t cfg;
    t9_lookup_fn lookup;

    t9_mode_t mode;
    bool spelling; /* temporary multitap entered with T9_KEY_SPELL from T9 */
    t9_caps_t caps;
    bool completions; /* on: base[suffix]; off: distinct bases, family base>suffix */

    t9_compose_t compose;
    uint8_t digits[T9_MAX_WORD_LEN]; /* T9 sequence, 2..9 and an optional apostrophe (1) */
    uint8_t digit_len;
    char letters[T9_MAX_WORD_LEN]; /* multitap word, lowercase */
    uint8_t letter_len;
    uint8_t punct_index;
    t9_punct_group_t punct_group;        /* ordinary punctuation or a typing chord */
    uint8_t join_pos;                    /* apostrophe position, 0 when no contraction is active */
    char join_base[T9_MAX_WORD_LEN + 1]; /* lowercase displayed base before 1 */

    t9_candidates_t cands; /* list for `digits` */
    uint8_t cand_index;    /* selected entry */
    bool user_selected;    /* cycled on the current sequence */
    t9_candidates_t next;  /* lookup result until an edit applies it; about 650 B, too big for the MCU stack */

    char shown[T9_SHOWN_MAX + 1]; /* preview as the host will display it */
    uint8_t shown_len;

    /* multitap letter in progress */
    bool mt_pending;
    uint8_t mt_key;
    uint8_t mt_cycle;
    uint32_t mt_deadline;

    /* long press tracking; typing chords keep 1 held across 2-5 taps */
    bool held;
    bool held_fired;
    t9_key_t held_key;
    uint16_t held_code;
    uint32_t held_since;
    t9_mode_t mode_before_hold;
    /* base of the preview before the held digit's tap effect, restored by a long press */
    char held_prev[T9_MAX_WORD_LEN + 1];
    t9_compose_t held_prev_compose;
    uint8_t held_committed_len; /* leading bytes of held_prev accepted by the tap */

    uint8_t events; /* T9_EVENT_* flags, cleared by t9_take_events() */

    t9_output_t out;
} t9_engine_t;

void t9_init(t9_engine_t *e, t9_lookup_fn lookup, const t9_config_t *cfg);
void t9_reset(t9_engine_t *e);

/* Returns true when the engine consumed the key. False means the host should
 * process the key itself (only possible for DELETE, CANCEL, OTHER and NUMPAD
 * mode); the host must then also pass the matching release through. */
bool t9_key_press(t9_engine_t *e, t9_key_t key, uint16_t passthrough_code, uint32_t now_ms);
void t9_key_release(t9_engine_t *e, t9_key_t key, uint32_t now_ms);
void t9_tick(t9_engine_t *e, uint32_t now_ms);

/* Shortcut actions: accept the typed portion without a space and queue a
 * complete host key tap, or reverse the mode cycle. Refused atomically when
 * output does not fit. Neither action starts a Num/digit long press. */
bool t9_send_shortcut(t9_engine_t *e, uint16_t keycode);
bool t9_previous_mode(t9_engine_t *e);

/* Dial: cycle active T9 candidates or punctuation (including provisional 1).
 * Otherwise accept any spelled/multitap text, then queue navigation: word_code
 * in T9 mode, character_code in multitap. The caller supplies direction-specific
 * host keycodes (including the OS word modifier). Returns false only in numpad
 * mode, where the host handles volume. Queue refusal consumes the turn and raises
 * BUSY without accepting text or moving the cursor. */
bool t9_dial_turn(t9_engine_t *e, bool clockwise, uint16_t character_code, uint16_t word_code, uint32_t now_ms);

/* Finish the active composition without a space (used by hosts before actions
 * that are not keys, e.g. a macro). True when nothing was composing or the
 * accept was applied; false when the edit was refused (queue full). */
bool t9_accept(t9_engine_t *e);

/* Turns T9 completions on or off (t9_init: off). While composing in T9 the
 * preview is re-rendered in place, never accepted; false when that edit was
 * refused (queue full, T9_EVENT_BUSY), the setting and preview then unchanged. */
bool t9_set_completions(t9_engine_t *e, bool on);

uint8_t t9_take_events(t9_engine_t *e);
bool t9_is_composing(const t9_engine_t *e);
bool t9_has_completion(const t9_engine_t *e); /* a suffix is shown */
const char *t9_preview(const t9_engine_t *e); /* current `shown`, NUL-terminated */
