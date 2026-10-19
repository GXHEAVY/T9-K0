# T9-K0

T9 predictive typing on a **Keychron K0 Max** wireless numpad. The typing engine
and English dictionary run on the pad, which sends ordinary keyboard input over
USB, Bluetooth or 2.4 GHz. No host application or input method is required.
The firmware provides predictive input, dial-based candidate selection, multitap
spelling, punctuation, editing shortcuts and a standard numpad mode.

[Controls and keymap](KEYMAP.md) · [Build and flash](#build-and-flash) ·
[Development and simulator](DEVELOPING.md) ·
[Dictionary](dictionary/README.md) · [Licences](LICENSES.md)

Download the [v0.1.0 firmware](https://github.com/GXHEAVY/T9-K0/releases/download/v0.1.0/T9-K0-v0.1.0-firmware.zip)
or find the matching source and checksums on the [release page](https://github.com/GXHEAVY/T9-K0/releases/tag/v0.1.0).
Read the [flash layout](#flash-layout) before installing; the firmware archive
includes installation instructions and licence notices.

## How typing works

In T9 mode, digits use **phone order**, not the printed numpad order:

```text
Phone digits      Printed keycaps
  1  2  3            7  8  9
  4  5  6            4  5  6
  7  8  9            1  2  3
     0                  0
```

For example, enter phone digits `43556` to type `hello`, then `0` to accept it and
add a space. Turn the dial or press `*` to choose another candidate. Hold `+` to
restart a missing word in multitap; accepting it returns to prediction.

Suggestions appear as real, editable text before the cursor. The firmware uses
backspaces and replacement characters to update that preview. Accept a word
before moving the cursor with another device or changing application focus: the
pad cannot observe those actions.

| Control | Action in T9 mode |
| --- | --- |
| `2`–`9` | Add a phone digit and update the prediction |
| Dial | Cycle candidates while composing |
| `*` | Next candidate |
| `0` / `.` | Accept the typed part with / without a space |
| `-` | Delete the last input |
| Esc / Backspace | Cancel the current word; idle Backspace deletes a word |
| Hold `+` | Spell the current word using multitap |
| Tap `/` / hold `/` | Change capitalization / use editing shortcuts |
| Num | Cycle T9 → multitap → numpad |
| Fn+Enter | Toggle word completions; remembered across power cycles |

Completions are off by default. With them on, `good[bye]` shows the typed part and
suggested suffix: `0` accepts `good `, while Enter accepts `goodbye `.

With completions off, a `>` preview can still offer staged completion within a
word family. [KEYMAP.md](KEYMAP.md) documents completion rules, punctuation,
contractions, shortcuts and every physical binding.

## Compatibility and limitations

- **Hardware:** Keychron K0 Max, built against the pinned Keychron QMK revision.
  USB and Bluetooth typing have been checked on the author's pad. The 2.4 GHz
  integration is covered by host stubs; physical operation has not been verified
  for this build.
- **Flash capacity:** the firmware uses 256 KiB although the tested pad reports
  128 KB. Read the [flash layout and recovery notes](#flash-layout) before
  installing; the upper flash region has been verified on one unit only.
- **Host layout:** output assumes US QWERTY. The intended desktop hosts are
  Windows and macOS. Android support has not been validated.
- **Host profiles:** USB and Bluetooth slots 2/3 default to Apple shortcuts;
  Bluetooth slot 1 and the dongle default to PC shortcuts. Adjust
  `T9_WORD_NAV_APPLE_*` in [config.h](keymap/config.h) for your machines.
- **Vocabulary:** a fixed English dictionary with 31,904 words, filtered mainly
  for US spelling. Use multitap for missing words. Spelling does not save words;
  a personal dictionary and editing vocabulary over USB are not implemented.
- **State:** the completion setting persists; the typing mode resets at power-up.
  The pad cannot track external cursor movement, focus changes or host text edits.

## Try it without hardware

Requires Python 3.12+, Make and a C11 compiler. Clone the repository, then run
the host tests and simulator. The included dictionary is sufficient; QMK and
dictionary source downloads are not needed.

```sh
git clone https://github.com/GXHEAVY/T9-K0.git
cd T9-K0
make test
make sim
host/build/t9_simulator
```

The [simulator guide](DEVELOPING.md#simulator) covers terminal controls and scripted input.
To install on a pad, follow [Build and flash](#build-and-flash), including the
separate instructions for first installation and later firmware updates.

## Build and flash

Clone the repository as shown above and run commands from its root. Firmware
builds use Keychron QMK commit
`c5d998442710b38650903937249144a0cd728ebe` from its `2025q3` branch, with the
submodules referenced by that commit and QMK CLI 1.2.0.

### Flash layout

The full firmware uses about 232,006 bytes of flash and 19,228 bytes of static RAM
with the tested Arm GNU Toolchain 13.3.Rel1. Use `make sizes` for your build.

The tested K0 Max reports 128 KB of flash, but its upper half passed a write/read-back
test and the build uses 256 KiB. The production dictionary requires that upper
region. These measurements apply to one pad, not every K0 Max revision.

EEPROM backing occupies the final 4 KiB, `0x0803F000`–`0x0803FFFF`. Keep **both**
overrides in `keymap/config.h`: `WEAR_LEVELING_EFL_FLASH_SIZE 262144` and
`WEAR_LEVELING_EFL_FIRST_SECTOR 126`. With the size override alone, the pinned
upstream wear-leveling sector search erased page zero on the tested pad.

Keep the factory firmware and know the hardware reset procedure before installing.
`make build-stock` builds the stock keymap from the pinned QMK source as an
additional recovery option; it does not extract the factory image or settings.

### Dependencies and build

Requires Git, Python 3.12+, Make, a C11 compiler, `arm-none-eabi-gcc`, `dfu-util`
and `dfu-suffix`. The tested firmware build environment is macOS; the host tests
and simulator also target Linux. On macOS, install Xcode Command Line Tools and:

```sh
brew install python make
brew install --cask gcc-arm-embedded
brew install dfu-util
make setup
make test
make build
```

Setup creates `.venv`, validates the QMK CLI installation, fetches the pinned QMK
commit into `qmk_firmware/`, initializes required submodules and links `keymap/`
into the checkout. First setup needs network access. Completed setup can be reused
offline. The word list and packed dictionary are included, so normal firmware
builds do not need `make fetch-sources` or the dictionary Python dependencies.

The output is `qmk_firmware/keychron_k0_max_t9.bin`.

### First installation from stock firmware

1. Connect in Cable mode over USB. Disconnect, then hold the physical reset button
   under the `0` keycap while reconnecting USB to enter the ROM DFU bootloader.
2. Check that `dfu-util -l` lists the intended STM32 DFU device (`0483:df11`).
3. Run `make flash` and wait for transfer and restart to finish.
4. Check typing in a plain text editor. The initial mode is T9 and completions
   default to off after EEPROM initialization.

Fn+M1 is a shortcut supplied by T9-K0 and is not the first-installation procedure
for stock firmware.

### Updating T9-K0

Build and test first. In Cable mode over USB, press **Fn+M1** to enter DFU. Num
lights red while waiting. Then:

```sh
dfu-util -l
make flash
```

The pad restarts after flashing. `make flash` invokes QMK's build-and-flash target;
keep the checked source and build configuration in place until it completes.

VIA stores a separate keymap in EEPROM. A firmware update can leave older bindings
active. Reset the stored keymap in VIA if needed, or use Esc bootmagic to clear
EEPROM settings. Remapping a key in VIA also changes its T9 role because the
integration matches keycodes.

### Recovery

The reset button under `0`, held while connecting USB, enters DFU without working
application firmware. Use it to install a saved factory image or the stock build.
Holding Esc while connecting uses QMK bootmagic, requires working firmware, and
clears EEPROM settings, including macros and the completion preference.

```sh
make build-stock
# Enter DFU with the hardware reset procedure, then:
make flash-stock
```

### Setup troubleshooting

`make setup` checks installed package versions and dependencies on each run, so an
interrupted package installation can be retried. It resumes an empty or initialized
QMK directory whose first checkout has not completed, and repairs a stale keymap
symlink after moving the project.

Setup stops if the QMK checkout has a different origin, a different commit, tracked
changes, or a real directory at the keymap link. Inspect and preserve that work
before correcting it or moving the checkout aside and rerunning setup. It does
not reset an existing checkout to discard changes.

`make build-experiment` builds a small diagnostic dictionary through the reference
lookup. It is a hardware-debugging option, not the full production vocabulary.
`make help` lists all targets.

## Licences

Project source code is GPL-2.0-or-later. Generated dictionary data is
CC-BY-SA-4.0 with SCOWL notice terms. The combined firmware is intended for
distribution under GPL-3.0-only, subject to the component qualifications in
[LICENSES.md](LICENSES.md).

GPLv3 permits use, modification and commercial redistribution. Distributing the
firmware requires corresponding source under GPLv3 and preservation of the
required notices; private modifications can remain private. `LICENSES.md`
contains the attribution, component terms and distribution requirements, with
full licence texts and third-party notices in `licenses/`.
