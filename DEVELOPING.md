# Developing T9-K0

Architecture, checks, coding conventions and simulator reference.
The engine API is defined in [t9_engine.h](keymap/t9_engine.h);
build instructions are in [README.md](README.md#build-and-flash).
Commands run from the repository root.

[Repository map](#repository-map) · [Checks](#checks) · [Style](#style) ·
[Simulator](#simulator) · [Script format](#script-format)

## Repository map

| Path | Responsibility |
| --- | --- |
| `keymap/t9_engine.*` | Portable C composition, candidate selection, multitap and completion |
| `keymap/t9_output.*` | Ordered queue of text edits and key actions |
| `keymap/t9_shortcuts.*`, `t9_contractions.*` | Shortcut routing and contraction matching |
| `keymap/keymap.c`, `config.h`, `rules.mk` | QMK integration, bindings, host profiles, indicators and firmware build |
| `keymap/t9_dict_packed.*`, `t9_dictionary.h` | Production lookup and shared ranking interface |
| `keymap/t9_dict_simple.*`, `t9_experiment_words.h` | Reference lookup and small diagnostic dictionary |
| `dictionary/` | Pinned sources, generated word list, packing tool and format documentation |
| `host/` | Tests, QMK stubs, replay scenarios and simulator |
| `keymap_grid.py` | Diagram tables used to generate the grids in `KEYMAP.md` |
| `scripts/`, `tests/` | Setup, contributor checks and tooling regression tests |
| `licenses/` | Full GPL licence texts; distribution details are in `LICENSES.md` |

The engine has no QMK dependency or heap allocation. It plans edits before
committing them to its bounded output queue; QMK handles pacing and host reports.

## Checks

Host checks require Python 3.12+, Make and a C11 compiler. Formatting
uses pinned tools in `.venv-dev`; its first setup needs network access.

```sh
make test test-tools check-generated
make check-format
```

CI runs host/tooling tests and generated-file checks on Linux and macOS, and
formatting checks on Linux. Firmware builds and physical radio behavior are
separate checks; passing host tests does not establish hardware compatibility.

For firmware changes, also run:

```sh
make build EXTRAFLAGS=-fstack-usage
make sizes
```

The ChibiOS process stack is 4 KiB. Keep large scratch tables in engine or static
storage and inspect `.su` frames along changed call chains. The packed decoder's
static scratch storage makes it non-reentrant. Preserve ordered output, whole-edit
queue refusal, and resets on host/transport changes and suspend.

## Style

Run `make format` to apply the repository's clang-format and Ruff configurations.
Use four-space indentation and a 120-column target for hand-written C and Python.
Keep public C names under `t9_`, make internal helpers `static`, and explain
invariants and hardware constraints in comments. Expand nested conditional logic
when it becomes hard to follow. Python helpers should use descriptive names and
type annotations where they clarify interfaces.

Generated dictionary headers and fixture tables are excluded from formatting.
Replay files (`host/*.t9`) contain meaningful trailing spaces after `expect`;
do not strip them. `.editorconfig` and `.gitattributes` preserve that convention.

Project code uses GPL-2.0-or-later; preserve copyright statements and add the SPDX
identifier to new code. Dictionary data has separate terms in [LICENSES.md](LICENSES.md).

## Tests and generated files

| Change | Update or check |
| --- | --- |
| Typing, selection, completion or queue handling | `host/t9_engine_test.c` and production cases where relevant |
| Physical bindings, shortcuts, host profiles or indicators | `host/t9_keymap_test.c`, `KEYMAP.md` and `keymap_grid.py` |
| Dictionary packing or decoding | Fixture/reference comparisons, production tests and [packed dictionary format](dictionary/README.md#packed-dictionary-format) |
| Setup and developer tooling | `tests/` |
| Word sources or filtering | Pins, attribution, `make fetch-sources`, `make dict`, then host tests |

`make keymap-doc` regenerates key diagrams from the hand-maintained tables in
`keymap_grid.py`; it does not parse the firmware bindings. Review both together.
`make check-generated` checks those diagrams and regenerates the packed dictionary
in a temporary directory, leaving the working tree untouched.

Keep `dictionary/words_en.tsv` and `keymap/t9_dictionary_data.h` in source control.
They allow firmware builds without downloading the source corpora or installing
wordfreq. Generated reports, downloaded corpora and binaries remain ignored.

## Host test coverage

`make test` checks engine behavior, the real keymap against Apple and PC QMK
stubs, packed dictionary lookup against a reference implementation, production
dictionary integrity, and scenario replays with instant and delayed output.
The production tests also check complete-word priority with suggestions off
(`dog`, `fox`, `yes`, `media`, `begin`) and previews with suggestions enabled.
Engine tests cover frequency ordering within groups, explicit selection,
deletion, toggles and queued output when a complete match replaces a prefix.
Staged `>` completion tests cover shortest-first word families, Enter advancing
the typed boundary, `0` acceptance at each step, branching/truncated lookup
rejection and queue refusal. Production replays cover `keyboard`/`keyboards`
with instant and delayed output; keymap tests check Enter, `0` and the `*` LED.
Typing chord tests cover held `1` with repeated `2`–`5` taps, replacement of an
already emitted `1`, release order, queue refusal, and T9/multitap/numpad behavior.

`host/t9_fixture_words.txt` supplies deterministic words and frequencies; its
C headers are generated by Make. `host/t9_test.h` provides the fake host and event rig.
The keymap tests exercise USB, Bluetooth host profiles and the dongle profile,
including word-delete press/release across host changes. Wireless stubs cover
disconnected macros and passthrough, stalled output, connection changes during a
flush, and ordering when the report backlog drains. A test deadline catches an
unbounded flush. These stubs do not emulate radio delivery or host OS behavior.

## Simulator

`make sim` builds `host/build/t9_simulator`, which runs the same engine and edit model
against a simulated host text buffer.

```sh
make sim
host/build/t9_simulator                       # interactive, production dictionary
host/build/t9_simulator --dict fixture        # the test fixture
host/build/t9_simulator --pace 50             # 50 ms per output item, shows pending output
host/build/t9_simulator --dict fixture --script host/scenario_paragraph.t9   # replay, exit 1 on mismatch
host/build/t9_simulator --script host/scenario_wrappers.t9 --pace 50        # overlapping bracket chords
```

| Flag                               | Meaning                                                                                                                           |
| ---------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| `--dict packed\|fixture`           | `packed` (default): production data via `t9_dict_packed_lookup`. `fixture`: `host/t9_fixture_table.h` via `t9_dict_simple_lookup` |
| `--pace MS`                        | ms between host output items, like `keymap.c` `output_task` (default 0 = drain at once)                                           |
| `--multitap MS`, `--long-press MS` | engine timing, defaults from `keymap/config.h`                                                                                    |
| `--script FILE`                    | replay a script; exit 0 on success, 1 on any failed expectation, 2 on parse error                                                 |
| `--verbose`                        | print host text, preview and queue depth after each script line                                                                   |

### Interactive keys

Phone order, as in `keymap.c`: digits `0`-`9` (main row or numpad) are the T9 digits.

| Key              | Action                                                                                                                          |
| ---------------- | ------------------------------------------------------------------------------------------------------------------------------- |
| `*`              | next candidate                                                                                                                  |
| `#`              | previous candidate |
| `[` / `]`        | dial counter-clockwise / clockwise: candidates or punctuation while active; otherwise T9 word jumps, multitap arrows, numpad volume |
| `.`              | accept without space                                                                                                            |
| Backspace or `-` | delete                                                                                                                          |
| Esc              | cancel                                                                                                                          |
| `h+` / `+` / `/` | spell (hold) / finish the letter / caps                                                                                         |
| `n` or `m`       | mode cycle (T9, multitap, numpad)                                                                                       |
| Enter            | accept a bracketed completion + space, advance a staged `>` completion, or accept the typed part and send Enter (`T9_KEY_COMPLETE`, code 1, `<ENTER>`) |
| Tab              | passthrough (`T9_KEY_OTHER`, code 2; shown as `<TAB>`)                                                                          |
| `c`              | completions on / off (`Fn`+`Enter`; starts off); off offers qualifying word families as `base>suffix`; script key `FN_ENTER` |
| `h` then a key   | hold that key for `long_press_ms` in real time, then release                                                                    |
| `r`              | host/transport change (`t9_reset`)                                                                                              |
| `?`              | toggle help; `q` or Ctrl-C quits and prints the final host text                                                                 |

A terminal delivers no key releases, so every key is pressed and released at once; use
`h<key>` for a long press. Real hold timing needs a script or the hardware. Esc: a lone
Esc byte (nothing following within 25 ms) is cancel; `ESC [ ...` sequences (arrows, function
keys) are ignored and `ESC O x` application-keypad codes map to the keypad key. Alt+key
arrives as Esc followed by the key. The poll loop runs every 5 ms, so interactive pacing
below 5 ms behaves like 5 ms.

The status shows the host text (cursor as an inverse block, last 72 characters), the
preview with its brackets, composition kind and digits, mode, spelling, caps, queue
depth, the candidate list (whole words, `<selected>`, exact-word count), events since the
last key (MISS, BUSY, mode, caps, reset) and dictionary lookups since the last key with
the last lookup's decoded entries and skipped blocks (packed dictionary only).

In numpad mode keys reach the host with the pad's numpad legends, as in `keymap.c`:
phone `1 2 3` type `7 8 9`, phone `7 8 9` type `1 2 3`, `-` and Backspace send Backspace.

### Script format

One item per line; blank lines and `#` comments are ignored. Times are absolute ms and
must not decrease.

```
<time_ms> press <KEY>          KEY: 0-9 NEXT PREV DIAL_LEFT DIAL_RIGHT DOT DELETE CANCEL SPELL CAPS MODE ENTER TAB FN_ENTER
<time_ms> release <KEY>
<time_ms> expect <text>        drain the queue, then the whole host text must equal <text>
<time_ms> expect-cursor <offset>  drain output, check cursor byte offset
<time_ms> expect-preview <text>   engine preview (t9_preview) must equal <text>
<time_ms> reset                host/transport change (t9_reset)
<time_ms> drain                apply all queued output now
```

`<text>` is the rest of the line after one space, verbatim (trailing spaces count; an
empty rest means empty text). Before each line the clock advances from the previous time
in 1 ms steps; each step calls `t9_tick` then the paced output step, so timeouts and long
presses fire at their exact boundary ms (`time - 1` and `time` are both ticked). Output is
paced in virtual time; no sleeping. A key press the engine does not handle goes to the
host model, and its release is not sent to the engine (as in `keymap.c`).

The simulator models text edits and output timing. Physical controls, radio
delivery and host application behavior require hardware testing.

## Scope and limitations

The dictionary is read-only; personal word learning and USB vocabulary editing
are not implemented. Typing mode resets at power-up. Bug reports should include
the input sequence, mode/completion setting, host profile and transport, expected
behavior and actual text. Include a simulator replay for reproducible engine issues.

Output assumes a US QWERTY host layout. The firmware cannot observe external
cursor or focus changes. Accept the preview before those actions. Radio timing
needs physical-host evidence beyond the simulator.

## Preparing a release

Run the checks above and build the firmware from the intended source revision.
Record the compiler version and artifact hashes. The first planned release is
`v0.1.0`; its notice draft is in [LICENSES.md](LICENSES.md#notice-ship-with-every-firmware-binary).
Preserve all component notices, including the custom sync-timer terms.
Include a matching source archive with pinned QMK and
submodules; verify that archive builds. The notice alone is not a release bundle.
