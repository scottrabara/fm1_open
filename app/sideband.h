/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_SIDEBAND_H
#define FM1_SIDEBAND_H
#include <stdint.h>

/* Version of the Sideband protocol extensions (sideband.c). */
enum { SIDEBAND_VERSION = 1, SIDEBAND_NOT_MINE = 1000 };

/* Commands 40..42; SIDEBAND_NOT_MINE for any other. */
int sideband_command(unsigned cmd, unsigned source, const uint8_t *data, unsigned n,
                     uint8_t *out, unsigned *count);
/* Appends 'S' and the version to command 22's reply. */
void sideband_capabilities(uint8_t *out, unsigned *count);
/* F0 43 10 gg pp vv F7 to the track selected on the panel. */
void sideband_parameter_change(const uint8_t *message, unsigned bytes);
/* After a single-voice dump was imported. */
void sideband_dump_applied(void);
void sideband_poll(uint32_t now);

#endif
