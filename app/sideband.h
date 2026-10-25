/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_SIDEBAND_H
#define FM1_SIDEBAND_H
#include <stdint.h>

/* Version of the Sideband protocol extensions (sideband.c). */
enum { SIDEBAND_VERSION = 2, SIDEBAND_NOT_MINE = 1000 };

/* Commands 40 to 42; SIDEBAND_NOT_MINE for any other. */
int sideband_command(unsigned cmd, const uint8_t *data, unsigned n, uint8_t *out, unsigned *count);
/* Appends 'S' and the version to command 22's reply. */
void sideband_capabilities(uint8_t *out, unsigned *count);

#endif
