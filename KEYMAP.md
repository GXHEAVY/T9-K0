# Keymap reference

Bindings and indicators defined by `keymap/keymap.c`. The two layers are `BASE`
and `FN` (held). BASE behavior depends on the typing mode, cycled with `Num`.
The engine intercepts keys by keycode. See [README.md](README.md) for an
introduction and [installation](README.md#build-and-flash).

Holding `/` or `M1` activates a shortcut map above ordinary typing in all three modes.
Mode-specific shortcut entries override the T9 map; missing entries inherit T9's
shortcut, never its typing action. All modes share the T9 map below.

VIA stores its own copy of the keymap in EEPROM. Remapping a key in VIA also changes its
T9 role, since the engine matches keycodes, not positions.

## Physical layout and keycap legends

Phone digits differ from the numpad legends: the top digit row is phone `1 2 3`.

```
 [knob]  [Esc]  [Del]  [Tab]  [Bksp]
 [M1]    [Num]  [ / ]  [ * ]  [ - ]
 [M2]    [ 7 ]  [ 8 ]  [ 9 ]  [   ]
 [M3]    [ 4 ]  [ 5 ]  [ 6 ]  [ + ]
 [M4]    [ 1 ]  [ 2 ]  [ 3 ]  [   ]
 [Fn]    [   0    ]    [ . ]  [Ent]
```

`+` and `Enter` are 2u tall, `0` is 2u wide. The knob turns and presses.

## Visual maps

Each key shows its keycap legend in brackets, then what it does. `(knob)` is the
rotary encoder: the turn action first (`<...>` = counter-clockwise / clockwise),
`p:` = press. `h:` = hold 500 ms. `(base)` = transparent, the BASE key acts.

The grids are generated: edit the layer tables in `keymap_grid.py`, then run
`make keymap-doc`. Do not edit them here.

### BASE, T9 mode

Phone digits with predictive lookup. With completions on (`Fn`+`Enter`; off by
default) longer suggestions appear first, their untyped rest in brackets
(`cand[idate]`); `Enter` takes the whole word plus a space, `0` and `.` keep the typed
part. Exact words follow the longer suggestions unless their spelling is already
represented by a longer suggestion's typed part (`good` is covered by `good[bye]`,
but `fox` stays selectable for `369`). Reaching the end of a word prefers its best
extension with the same letters.

**Complete-word priority:** with completions off, complete words rank before
remaining prefixes, each group ordered by frequency. Cycling steps through
distinct typed parts (`cand`). New
digits prefer a complete match unless a candidate was explicitly selected; deleting
or toggling completions preserves the current spelling where possible.

**Staged `>` completion:** with completions off, when one word remains or every
match is a prefix of the longest word, the shortest extension of the typed part
is offered. `keybo>ard` becomes `keyboard>s` on `Enter`; `0` then accepts `keyboard `, while
another `Enter` finishes `keyboards `. Intermediate Enter presses add letters to
the typed part; only the final extension adds a space. `0` always accepts the
current typed part plus a space, and `.` accepts it without a space. Delete
removes one typed letter, including letters filled by Enter. Branching matches
and full lookup lists do not qualify. With no completion shown, `Enter`
accepts the typed part, then sends Enter. The dial cycles candidates while
composing and jumps left/right by word when idle; after `1` it cycles punctuation.
Press `.` to accept the typed text before navigating. `Del`, `Tab`
and the macros accept the typed part first; `Esc`, `Bksp` and `-` act on the host only
when nothing is composing.

A single `1` can be the apostrophe inside a common contraction: `416` -> `i'm`,
`4183` -> `i've`, `96618` -> `won't`. It initially shows a period; the next digit
resolves it. Another `1` or a knob turn chooses ordinary punctuation and disables
that interpretation, even after cycling back to the period. With an eligible base,
the first `1` keeps the whole composition editable; delete can remove the separator
and cancel erases the whole preview. Accepting without a suffix keeps the period.

In T9 and multitap, hold phone `1` and tap `2` to cycle `(` `[` `{` `<`, or tap
`3` to cycle `)` `]` `}` `>`. Hold `1` and tap `4` for `:` `;` `|` `~`, or tap
`5` for `+`, backtick, `@`, backslash. Each chord has four symbols. Phone `1`–`3`
are physical numpad `7`–`9`; `4` and `5` keep their printed positions.
Repeated taps replace one symbol; changing groups starts at the new group's
first symbol. Releasing `1` accepts it. The chord works before or
after the long hold types `1`, removing that digit if necessary. The text before
the held `1` is kept, without any suggested suffix. A plain tap/hold of `1`
retains its punctuation/literal-digit behavior: plain `1` cycles `.` `,` `?` `!`
`'` `-`. Numpad mode has no typing chords.

<!-- grid:t9 -->
```
+---------+---------+---------+---------+---------+
| (knob)  | [Esc]   | [Del]   | [Tab]   | [Bksp]  |
| cand/wd | cancel  | Delete  | Tab     | delete  |
| p: mute | word    |         |         | word    |
+---------+---------+---------+---------+---------+
| [M1]    | [Num]   | [/]     | [*]     | [-]     |
| tap:MC1 | mode    | tap:caps| next    | delete  |
| h:chords| h: NumLk| h:chords| cand    | last    |
+---------+---------+---------+---------+---------+
| [M2]    | [7]     | [8]     | [9]     | [+]     |
| macro   | 1       | 2 abc   | 3 def   | h: spell|
|         | .,?!'-  |         |         | word    |
+---------+---------+---------+---------+         |
| [M3]    | [4]     | [5]     | [6]     |         |
| macro   | 4 ghi   | 5 jkl   | 6 mno   |         |
|         |         |         |         |         |
+---------+---------+---------+---------+---------+
| [M4]    | [1]     | [2]     | [3]     | [Ent]   |
| macro   | 7 pqrs  | 8 tuv   | 9 wxyz  | complete|
|         |         |         |         | word    |
+---------+---------+---------+---------+         |
| [Fn]    | [0]               | [.]     | else    |
| hold: FN| accept + space    | accept  | Enter   |
|         | hold: 0           | no space|         |
+---------+-------------------+---------+---------+
```
<!-- /grid -->

### BASE, multitap mode

Repeated taps on a digit key cycle its letters; another key or 800 ms finishes the
letter (never the word). The dial accepts any active letters, then moves the cursor
left/right by one character. After `1`, the dial and `*` cycle punctuation instead. `+` finishes the pending
letter so the same key starts a new one. Everything else matches T9 mode.

<!-- grid:multitap -->
```
+---------+---------+---------+---------+---------+
| (knob)  | [Esc]   | [Del]   | [Tab]   | [Bksp]  |
| <L/R>   | cancel  | Delete  | Tab     | delete  |
| p: mute | word    |         |         | word    |
+---------+---------+---------+---------+---------+
| [M1]    | [Num]   | [/]     | [*]     | [-]     |
| tap:MC1 | mode    | tap:caps| next    | delete  |
| h:chords| h: NumLk| h:chords| punct   | last    |
+---------+---------+---------+---------+---------+
| [M2]    | [7]     | [8]     | [9]     | [+]     |
| macro   | 1       | 2 abc   | 3 def   | next    |
|         | .,?!'-  |         |         | letter  |
+---------+---------+---------+---------+         |
| [M3]    | [4]     | [5]     | [6]     |         |
| macro   | 4 ghi   | 5 jkl   | 6 mno   |         |
|         |         |         |         |         |
+---------+---------+---------+---------+---------+
| [M4]    | [1]     | [2]     | [3]     | [Ent]   |
| macro   | 7 pqrs  | 8 tuv   | 9 wxyz  | Enter   |
|         |         |         |         |         |
+---------+---------+---------+---------+         |
| [Fn]    | [0]               | [.]     |         |
| hold: FN| accept + space    | accept  |         |
|         | hold: 0           | no space|         |
+---------+-------------------+---------+---------+
```
<!-- /grid -->

### BASE, numpad mode

Stock numpad: every key sends its keycap legend (physical layout above), except `Num`,
which still cycles the mode (hold: Num Lock), `/`, which sends division on a
short solo tap and selects shortcuts while held, and M1, whose solo macro runs
on release and whose hold selects its shortcut map. Knob: volume, press: mute.

### Slash shortcuts (hold `/`, all modes)

Press `/` first, then the other key or turn the dial. Shortcuts are available
immediately; no hold delay is required. Releasing `/` returns to ordinary keys.
Tap `/` alone in under 200 ms to cycle caps once / lock / off in T9 or multitap,
or send keypad division in Default/numpad. Holding it alone for 200 ms or more
does nothing on release (`T9_SHORTCUT_TAP_MS` in `keymap/config.h`).

| While holding `/` | Action | Windows/Linux | Mac |
| --- | --- | --- | --- |
| `Esc` | Undo | Ctrl+Z | Command+Z |
| `Del` | Cut | Ctrl+X | Command+X |
| `Tab` | Copy | Ctrl+C | Command+C |
| `Bksp` | Paste | Ctrl+V | Command+V |
| `-` | Tab | Tab | Tab |
| `*` | Backnavigate; previous app after `+` starts switching | Shift+Tab; Alt+Shift+Tab in chooser | Shift+Tab; Command+Shift+Tab in chooser |
| `+` | Switch application | Alt+Tab | Command+Tab |
| `Enter` | VS Code show terminal (user binding) | Ctrl+backtick | Ctrl+backtick |
| `.` | Close tab | Ctrl+W | Command+W |
| Knob press | Quit / close active window | Alt+F4 | Command+Q |
| `Num` | Previous mode | T9 → Default/numpad → multitap → T9 | Same |
| Dial clockwise | Down | Down arrow | Down arrow |
| Dial counter-clockwise | Up | Up arrow | Up arrow |
| Any unassigned key, including digits and M1–M5/Fn | No action | None | None |

Holding a prefix alone or pressing an unassigned combination leaves the composition
unchanged. A mapped shortcut accepts the typed portion without a space, removes
any untyped suffix and its markers, and then sends the shortcut after queued text edits.
Reverse mode cycling also accepts without a space. You can press multiple
shortcuts or turn the dial repeatedly while holding `/`. Neither a mapped nor an
unmapped combination also toggles caps.

For **app switching**, the first `+` tap holds Command (Mac) or Alt (Windows/Linux)
and taps Tab to open the app chooser. Keep `/` held: tap `+` to cycle forward or
`*` to cycle backward (Command+Shift+Tab / Alt+Shift+Tab). Release `/` to confirm.
Before `+` starts switching, `/`+`*` remains ordinary Shift+Tab. Holding `/` alone
sends no modifier. Another mapped shortcut ends app switching before its action;
an unassigned chord leaves the
chooser open. Other shortcut modifiers are released after each action.
Pending text edits finish before the app-switch modifier is pressed, and queued
Tab taps finish before it is released. Suspend and host changes cancel pending
app-switch taps and release the modifier.

The Mac editing modifier is **Command (⌘)**, not Option. The connection profile
shared with word navigation selects the variant: USB, Bluetooth 2 and Bluetooth 3
are Apple; Bluetooth 1 and the 2.4 GHz dongle are Windows/Linux. These are configured
pairings, not automatic OS detection; change `T9_WORD_NAV_APPLE_*` in
`keymap/config.h` if devices move. The two terminal shortcuts deliberately retain
Ctrl on Apple connections. Alt+F4 closes the active window; Command+Q quits the app.

Pressing Fn first selects the FN layer: Fn then `/` changes brightness, and
Fn then M1 enters the bootloader. Slash-first `/`+Fn is unassigned.
Shortcut maps are defined in firmware and require no VIA/EEPROM reset. The
shortcut router selects each map by typing mode and held prefix.

In the grid, `C` means Command on Apple connections, Ctrl otherwise.

<!-- grid:slash -->
```
+---------+---------+---------+---------+---------+
| (knob)  | [Esc]   | [Del]   | [Tab]   | [Bksp]  |
| <Up/Dn> | undo    | cut     | copy    | paste   |
| p: quit | C+Z     | C+X     | C+C     | C+V     |
+---------+---------+---------+---------+---------+
| [M1]    | [Num]   | [/]     | [*]     | [-]     |
| (none)  | previous| held    | back tab| Tab     |
|         | mode    |         | ShiftTab|         |
+---------+---------+---------+---------+---------+
| [M2]    | [7]     | [8]     | [9]     | [+]     |
| (none)  | (none)  | (none)  | (none)  | switch  |
|         |         |         |         | app     |
+---------+---------+---------+---------+         |
| [M3]    | [4]     | [5]     | [6]     | Alt/Cmd |
| (none)  | (none)  | (none)  | (none)  | +Tab    |
|         |         |         |         |         |
+---------+---------+---------+---------+---------+
| [M4]    | [1]     | [2]     | [3]     | [Ent]   |
| (none)  | (none)  | (none)  | (none)  | VS Code |
|         |         |         |         | terminal|
+---------+---------+---------+---------+         |
| [Fn]    | [0]               | [.]     | Ctrl+`  |
| (none)  | (none)            | close   |         |
|         |                   | C+W     |         |
+---------+-------------------+---------+---------+
```
<!-- /grid -->

### M1 shortcuts (hold `M1`, all modes)

| While holding `M1` | Action | Host keys, including Mac |
| --- | --- | --- |
| `Tab` | Interrupt the current terminal process | **Ctrl+C** |
| Any other key or dial turn | No action | None |

M1+Tab sends Ctrl+C on all host profiles; `/`+Tab uses Command+C for Mac clipboard
copy. Press M1 first, then Tab; there is no hold delay. The typed portion is accepted
without a space before Ctrl+C is sent. Release order does not matter.

A solo M1 tap under 200 ms accepts the typed portion and runs VIA macro `MC_1`
on release. Holding M1 longer, or using any combination, suppresses the solo
macro. M2–M4 are ordinary VIA macro keys.
All modes inherit this T9 shortcut map;
an unassigned M1 chord does not inherit the slash map or ordinary typing.

### FN layer (hold `Fn`)

<!-- grid:fn -->
```
+---------+---------+---------+---------+---------+
| (knob)  | [Esc]   | [Del]   | [Tab]   | [Bksp]  |
| <bright>| BT 1    | BT 2    | BT 3    | 2.4 GHz |
| p: RGB  |         |         |         |         |
+---------+---------+---------+---------+---------+
| [M1]    | [Num]   | [/]     | [*]     | [-]     |
| BOOT    | effect  | bright +| hue +   | (base)  |
| (DFU)   | next    |         |         |         |
+---------+---------+---------+---------+---------+
| [M2]    | [7]     | [8]     | [9]     | [+]     |
| (base)  | effect  | bright -| hue -   | (base)  |
|         | prev    |         |         |         |
+---------+---------+---------+---------+         |
| [M3]    | [4]     | [5]     | [6]     |         |
| (base)  | sat +   | speed + | prev    |         |
|         |         |         | track   |         |
+---------+---------+---------+---------+---------+
| [M4]    | [1]     | [2]     | [3]     | [Ent]   |
| (base)  | sat -   | speed - | play/   | word    |
|         |         |         | pause   | complete|
+---------+---------+---------+---------+         |
| [Fn]    | [0]               | [.]     | on/off  |
| (held)  | RGB on/off        | next    |         |
|         |                   | track   |         |
+---------+-------------------+---------+---------+
```
<!-- /grid -->

## BASE layer, per key

| Key (legend)  | Keycode   | T9 / multitap                                           | Numpad mode     |
| ------------- | --------- | ------------------------------------------------------- | --------------- |
| knob turn     | Vol -/+   | T9: candidates while composing, word jumps when idle; multitap: accept + arrows; punctuation cycles      | volume          |
| knob press    | Mute      | mute (does not accept the word)                         | mute            |
| `Esc`         | Esc       | erase the preview; idle: Escape                         | Escape          |
| `Del`         | Delete    | accept, then Delete                                     | Delete          |
| `Tab`         | Tab       | accept, then Tab                                        | Tab             |
| `Bksp`        | Backspace | erase the preview; idle: delete the previous word       | Backspace       |
| `M1`          | VIA macro | solo tap: accept, run macro on release; hold: shortcuts | solo tap: macro; hold: shortcuts |
| `M2`-`M4`     | VIA macro | accept, then run the macro (empty by default)           | macro           |
| `Num`         | Num Lock  | accept, next mode; hold 500 ms: host Num Lock           | next mode; hold: Num Lock |
| `/`           | KP /      | tap alone: caps once / lock / off; hold: shortcuts       | tap alone: KP /; hold: shortcuts |
| `*`           | KP \*     | next candidate (multitap: cycles punctuation only)      | KP \*           |
| `-`           | KP -      | delete last input; idle: Backspace                      | KP -            |
| `+`           | KP +      | hold 500 ms: spell this word / tap: finish the letter   | KP +            |
| `7` `8` `9`   | KP 7 8 9  | phone `1` (punctuation `. , ? ! ' -`), `2 abc`, `3 def` | KP 7 8 9        |
| `4` `5` `6`   | KP 4 5 6  | phone `4 ghi`, `5 jkl`, `6 mno`                         | KP 4 5 6        |
| `1` `2` `3`   | KP 1 2 3  | phone `7 pqrs`, `8 tuv`, `9 wxyz`                       | KP 1 2 3        |
| `0`           | KP 0      | accept + space; idle: space; hold: `0`                  | KP 0            |
| `.`           | KP .      | accept, no space                                        | KP .            |
| `Enter`       | KP Enter  | `[]`: word + space; `>`: fill word, offer next extension or finish + space; else accept, Enter | KP Enter |
| `Fn`          | MO(FN)    | hold for the FN layer                                   | same            |

Word jumps use Option+arrow on USB and Bluetooth slots 2 and 3,
and Ctrl+arrow on Bluetooth slot 1 and the 2.4 GHz dongle. Configure these in
`T9_WORD_NAV_APPLE_*` (`keymap/config.h`) if the paired devices change. Dial navigation
is queued after preview edits; turning the dial never leaves delayed edits to run
after the cursor has moved. Fn + dial adjusts brightness; Fn + knob press toggles lighting.

Idle word delete follows the same host profile: Option+Backspace on Apple hosts,
Ctrl+Backspace on Windows and Linux. Customize `T9_WORD_DELETE_APPLE_KEY` and
`T9_WORD_DELETE_PC_KEY` in `keymap/config.h` if needed.

In T9 a tap on `+` does nothing; spelling starts only after a 500 ms hold, and `+`
lights magenta as soon as it does. While spelling or in multitap a tap finishes the
pending letter.

Mode order: T9 -> multitap -> numpad -> T9. Power-up mode is T9
(`T9_DEFAULT_MODE` in `keymap/config.h`, not persisted). In T9 and multitap, holding a
phone digit key for 500 ms types that digit instead.

## FN layer, per key

| Key (legend) | Keycode   | Action                                           |
| ------------ | --------- | ------------------------------------------------ |
| knob turn    | UG_VALD/U | backlight brightness down / up                   |
| knob press   | UG_TOGG   | backlight on / off                               |
| `Esc`        | BT_HST1   | Bluetooth host 1 (hold to pair)                  |
| `Del`        | BT_HST2   | Bluetooth host 2 (hold to pair)                  |
| `Tab`        | BT_HST3   | Bluetooth host 3 (hold to pair)                  |
| `Bksp`       | P2P4G     | 2.4 GHz dongle                                   |
| `M1`         | QK_BOOT   | reboot into the DFU bootloader (Num lights red)  |
| `Num`        | UG_NEXT   | next backlight effect                            |
| `7`          | UG_PREV   | previous backlight effect                        |
| `/`          | UG_VALU   | brightness up                                    |
| `8`          | UG_VALD   | brightness down                                  |
| `*`          | UG_HUEU   | hue up                                           |
| `9`          | UG_HUED   | hue down                                         |
| `4`          | UG_SATU   | saturation up                                    |
| `1`          | UG_SATD   | saturation down                                  |
| `5`          | UG_SPDU   | effect speed up                                  |
| `2`          | UG_SPDD   | effect speed down                                |
| `6`          | KC_MPRV   | previous track                                   |
| `3`          | KC_MPLY   | play / pause                                     |
| `.`          | KC_MNXT   | next track                                       |
| `0`          | UG_TOGG   | backlight on / off                               |
| `Enter`      | T9_COMPLETIONS | T9 completions on / off (kept in EEPROM)    |
| `M2`-`M4`, `-`, `+` | transparent | same as BASE (including T9 roles)        |

FN media and lighting keys do not accept the active composition; a media key pressed mid-word leaves the
preview in place, and `Fn`+`Enter` mid-word redraws it in place (`cand[idate]` <->
`cand`, or `cand>idate` for a qualifying word family). `T9_COMPLETIONS` is `SAFE_RANGE` (QMK's user range), so VIA may show it as a
raw or user keycode rather than a named key.

Lighting is status-only in every mode. Decorative RGB effects are suppressed,
including effects selected with the stock FN controls; `Fn`+`0` toggles lighting.
The `*` LED indicates completion availability because Enter's LED is hidden by
its tall keycap. Keychron connection and battery indications remain active.

## Status indicators

Num is green in T9, cyan in multitap, magenta during temporary spelling, and
follows host Num Lock in numpad mode. Slash indicates capitalization; plus marks
temporary spelling; dot is dim while composing.

`*` is bright green when a completion is available, including a staged `>` offer.
Otherwise it is dim green with completions enabled and dark with them disabled.
The digits flash red for a lookup miss and orange when an edit cannot fit in the
output queue. Num lights red on entering DFU through Fn+M1. Decorative effects
are suppressed, while Keychron's connection and battery indicators remain active.
