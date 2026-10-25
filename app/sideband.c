/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Sideband extensions to the project transfer protocol (transfer.c), for the
 * Sideband editor and any other that wants them. All additive: no existing
 * command changes, and a client that never sends these sees V14 exactly.
 * Kept small: the application has barely 1 KB of Flash to spare.
 *
 *   22  capabilities: two more bytes after V14's nine, 'S' and the
 *       extension version (SIDEBAND_VERSION).
 *   40  track map, no data: per track, engine, role and MIDI input channel
 *       (0..15), 24 bytes.
 *   41  set DX7 parameters: track, then (parameter, value) pairs, up to 65.
 *       0..144 voice parameters, 145..154 the name, 155 operator on/off
 *       (bit 5 OP1 .. bit 0 OP6). Applied live, without cutting notes.
 *   42  watch, one byte: track + 1 sends this port an event whenever that
 *       DX7 track's voice changes by any other means (the panel, a program
 *       change, an undo); 0 stops. Events are cmd 0x6A with token 0:
 *       status 0, then the track.
 */
#include "instrument.h"
#include "sideband.h"

#define SMALL __attribute__((cold, minsize))

static uint8_t watch_port = 3, watch_track, pending; /* 3: nobody watching */
static uint32_t last_check, sum;

static SMALL uint32_t patch_sum(void)
{
    uint8_t patch[156];
    if (groove_fm_copy(watch_track, patch, NULL)) return 0;
    uint32_t h = 2166136261U; /* FNV-1a */
    for (unsigned i = 0; i < 156; ++i) h = (h ^ patch[i]) * 16777619U;
    return h;
}

/* Each parameter to an absolute value through the panel's own edit paths,
 * so notes keep sounding, undo works and the project shows edited. */
static SMALL int set_parameters(unsigned track, const uint8_t *pairs, unsigned count)
{
    if (track >= GROOVE_TRACKS || groove_source_type(track) != GROOVE_DX7 ||
        groove_track_role(track) == GROOVE_TRACK_OFF)
        return GROOVE_PROJECT_INVALID;
    for (unsigned i = 0; i < count; ++i) {
        unsigned parameter = pairs[2 * i], value = pairs[2 * i + 1];
        uint8_t patch[156];
        if (groove_fm_copy(track, patch, NULL)) return GROOVE_PROJECT_INVALID;
        if (parameter < 145)
            groove_fm_edit(track, parameter, (int)value - (int)patch[parameter]);
        else if (parameter < 155 && value >= 32)
            groove_fm_name_edit(track, parameter - 145, (char)value);
        else if (parameter == 155)
            /* Bit 5 is OP1; groove_operator_toggle's op 0 is OP1. */
            for (unsigned op = 0; op < 6; ++op) {
                if ((patch[155] ^ value) & (32U >> op)) groove_operator_toggle(track, op);
            }
        else
            return GROOVE_PROJECT_INVALID;
    }
    if (track == watch_track) sum = patch_sum(); /* the watcher's own edit: no event */
    return 0;
}

SMALL int sideband_command(unsigned cmd, unsigned source, const uint8_t *data, unsigned n,
                           uint8_t *out, unsigned *count)
{
    if (cmd == 40 && !n) {
        for (unsigned t = 0; t < GROOVE_TRACKS; ++t) {
            *out++ = (uint8_t)groove_source_type(t);
            *out++ = (uint8_t)groove_track_role(t);
            *out++ = (uint8_t)groove_midi_input_channel(t);
        }
        *count = GROOVE_TRACKS * 3;
        return 0;
    }
    if (cmd == 41 && n >= 3 && (n & 1U))
        return set_parameters(data[0], data + 1, (n - 1) / 2);
    if (cmd == 42 && n == 1 && data[0] <= GROOVE_TRACKS) {
        watch_port = data[0] ? (uint8_t)source : 3;
        watch_track = data[0] ? (uint8_t)(data[0] - 1) : 0;
        pending = 0;
        sum = patch_sum();
        return 0;
    }
    return SIDEBAND_NOT_MINE;
}

SMALL void sideband_capabilities(uint8_t *out, unsigned *count)
{
    out[*count] = 'S';
    out[*count + 1] = SIDEBAND_VERSION;
    *count += 2;
}

SMALL void sideband_dump_applied(void)
{
    if (watch_port < 3) sum = patch_sum(); /* a dump is the watcher's doing too */
}

SMALL void sideband_poll(uint32_t now)
{
    if (watch_port >= 3) return;
    if (now - last_check >= 100U) {
        last_check = now;
        uint32_t s = patch_sum();
        if (s != sum) { sum = s; pending = 1; }
    }
    if (pending) {
        /* F0 7D 'F' 'M' 02 6A 00 00, packed {0, track}, checksum, F7. */
        uint8_t m[13] = {0xf0, 0x7d, 'F', 'M', 2, 0x6a, 0, 0, 0,
                         (uint8_t)((watch_track << 1) & 127U), (uint8_t)(watch_track >> 6), 0, 0xf7};
        unsigned total = 0;
        for (unsigned i = 1; i < 11; ++i) total += m[i];
        m[11] = (uint8_t)((0U - total) & 127U);
        if (fm1_midi_send_port(watch_port, m, sizeof m) != -2) pending = 0; /* busy: next poll */
    }
}
