/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Sideband extensions to the project transfer protocol (transfer.c), for the
 * Sideband editor and any other that wants them. All additive: no existing
 * command changes, and a client that never sends these sees V14 exactly.
 *
 *   22  capabilities: two more bytes after V14's nine, 'S' and the
 *       extension version (SIDEBAND_VERSION).
 *   40  track map, no data: per track, engine, role and MIDI input channel
 *       (0..15), 24 bytes.
 *   41  set DX7 parameters: track, then (parameter, value) pairs, up to 65.
 *       0..144 voice parameters, 145..154 the name, 155 operator on/off
 *       (bit 5 OP1 .. bit 0 OP6). Applied live, without cutting notes.
 *   42  watch, one byte: 1 sends this port an event whenever a DX7 track's
 *       voice changes by any other means (the panel, a program change, a
 *       dump, an undo); 0 stops. Events are cmd 0x6A with token 0:
 *       status 0, then the track.
 *
 * A DX7 parameter change, F0 43 10 gg pp vv F7 (device 1, as Yamaha's
 * editors send it), also reaches the track selected on the panel.
 */
#include "instrument.h"
#include "sideband.h"
#include <string.h>

extern void groove_screen_snapshot(uint8_t state[8]);

static unsigned watch_port = 3; /* 3: nobody watching */
static uint32_t last_check, sums[GROOVE_TRACKS];
static uint8_t pending_events; /* tracks whose event is still to go out */

static uint32_t patch_sum(unsigned track)
{
    uint8_t patch[156];
    if (groove_source_type(track) != GROOVE_DX7 || groove_fm_copy(track, patch, NULL)) return 0;
    uint32_t h = 2166136261U; /* FNV-1a */
    for (unsigned i = 0; i < 156; ++i) h = (h ^ patch[i]) * 16777619U;
    return h | 1U; /* never 0, which means "not DX7" */
}

static void rebaseline(void)
{
    for (unsigned t = 0; t < GROOVE_TRACKS; ++t) sums[t] = patch_sum(t);
}

/* One parameter to an absolute value through the panel's own edit paths, so
 * notes keep sounding, undo works and the project shows edited. */
static int set_parameter(unsigned track, const uint8_t patch[156], unsigned parameter, unsigned value)
{
    if (parameter < 145) {
        if (value > 127) return GROOVE_PROJECT_INVALID;
        int delta = (int)value - (int)patch[parameter];
        if (delta) groove_fm_edit(track, parameter, delta);
    } else if (parameter < 155) {
        if (value < 32 || value > 126) return GROOVE_PROJECT_INVALID;
        groove_fm_name_edit(track, parameter - 145, (char)value);
    } else if (parameter == 155) {
        if (value > 63) return GROOVE_PROJECT_INVALID;
        /* Bit 5 is OP1. groove_operator_toggle's op 0 is OP1. */
        for (unsigned op = 0; op < 6; ++op) {
            unsigned bit = 1U << (5U - op);
            if ((patch[155] & bit) != (value & bit)) groove_operator_toggle(track, op);
        }
    } else
        return GROOVE_PROJECT_INVALID;
    return 0;
}

static int set_parameters(unsigned track, const uint8_t *pairs, unsigned count)
{
    if (track >= GROOVE_TRACKS || groove_source_type(track) != GROOVE_DX7 ||
        groove_track_role(track) == GROOVE_TRACK_OFF)
        return GROOVE_PROJECT_INVALID;
    for (unsigned i = 0; i < count; ++i)
        if (pairs[2 * i] > 155) return GROOVE_PROJECT_INVALID;
    for (unsigned i = 0; i < count; ++i) {
        uint8_t patch[156];
        if (groove_fm_copy(track, patch, NULL)) return GROOVE_PROJECT_INVALID;
        int r = set_parameter(track, patch, pairs[2 * i], pairs[2 * i + 1]);
        if (r) return r;
    }
    sums[track] = patch_sum(track); /* the watcher asked for this: no event */
    return 0;
}

int sideband_command(unsigned cmd, unsigned source, const uint8_t *data, unsigned n,
                     uint8_t *out, unsigned *count)
{
    if (cmd == 40 && !n) {
        for (unsigned t = 0; t < GROOVE_TRACKS; ++t) {
            out[t * 3] = (uint8_t)groove_source_type(t);
            out[t * 3 + 1] = (uint8_t)groove_track_role(t);
            out[t * 3 + 2] = (uint8_t)groove_midi_input_channel(t);
        }
        *count = GROOVE_TRACKS * 3;
        return 0;
    }
    if (cmd == 41 && n >= 3 && (n & 1U))
        return set_parameters(data[0], data + 1, (n - 1) / 2);
    if (cmd == 42 && n == 1 && data[0] <= 1) {
        watch_port = data[0] ? source : 3;
        pending_events = 0;
        rebaseline();
        return 0;
    }
    return SIDEBAND_NOT_MINE;
}

void sideband_capabilities(uint8_t *out, unsigned *count)
{
    out[*count] = 'S';
    out[*count + 1] = SIDEBAND_VERSION;
    *count += 2;
}

void sideband_parameter_change(const uint8_t *m, unsigned n)
{
    /* F0 43 10 gg pp vv F7 */
    if (n != 7 || m[2] != 0x10 || m[3] > 1) return;
    unsigned parameter = (unsigned)m[3] * 128U + m[4];
    uint8_t view[8], pair[2] = {(uint8_t)parameter, m[5]};
    if (parameter > 155) return;
    groove_screen_snapshot(view);
    (void)set_parameters(view[0], pair, 1);
}

void sideband_dump_applied(void)
{
    /* A dump is the watcher's own doing too, when it watches. */
    if (watch_port < 3) rebaseline();
}

static int send_event(unsigned track)
{
    /* F0 7D 'F' 'M' 02 6A 00 00, packed {0, track}, checksum, F7. */
    uint8_t m[16] = {0xf0, 0x7d, 'F', 'M', 2, 0x6a, 0, 0};
    unsigned v = (unsigned)track << 8, at = 8, bits = 16;
    while (bits >= 7) { m[at++] = v & 127U; v >>= 7; bits -= 7; }
    if (bits) m[at++] = v & 127U;
    unsigned sum = 0;
    for (unsigned i = 1; i < at; ++i) sum += m[i];
    m[at++] = (uint8_t)((0U - sum) & 127U);
    m[at++] = 0xf7;
    return fm1_midi_send_port(watch_port, m, at);
}

void sideband_poll(uint32_t now)
{
    if (watch_port >= 3) return;
    if (now - last_check >= 100U) {
        last_check = now;
        for (unsigned t = 0; t < GROOVE_TRACKS; ++t) {
            uint32_t s = patch_sum(t);
            if (s != sums[t]) {
                sums[t] = s;
                if (s) pending_events |= (uint8_t)(1U << t);
            }
        }
    }
    for (unsigned t = 0; t < GROOVE_TRACKS && pending_events; ++t)
        if (pending_events & (1U << t)) {
            int r = send_event(t);
            if (r == -2) break; /* port busy: try again next poll */
            pending_events &= (uint8_t)~(1U << t);
        }
}
