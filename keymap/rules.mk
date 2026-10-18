# SPDX-License-Identifier: GPL-2.0-or-later
VIA_ENABLE = yes

# ChibiOS main-thread stack (QMK default 0x800). The engine and decoder keep
# their big tables in static memory, so the key-press chain needs about 500 B
# on top of QMK's frames; 4 KB leaves room, the heap remainder is over 40 KB.
USE_PROCESS_STACKSIZE = 0x1000

# T9 engine (pure C, also compiled by the host tests)
SRC += t9_engine.c t9_output.c t9_contractions.c t9_shortcuts.c

# T9_DICT=experiment builds the hardware diagnostic with a handful of
# hardcoded words through the reference lookup instead of the packed dictionary.
ifeq ($(strip $(T9_DICT)), experiment)
    SRC += t9_dict_simple.c
    OPT_DEFS += -DT9_DICT_EXPERIMENT
else
    SRC += t9_dict_packed.c
endif
