/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Sideband extensions to the project transfer protocol (transfer.c), for the
 * Sideband editor and any other that wants them. All additive: no existing
 * command changes, and a client that never sends these sees V14 exactly.
 * Kept small: the application has only a few hundred bytes of Flash to spare.
 *
 *   22  capabilities: two more bytes after V14's nine, 'S' and the
 *       extension version (SIDEBAND_VERSION).
 *   40  track map, no data: per track, engine, role and MIDI input channel
 *       (0..15), 24 bytes.
 *   41  set DX7 parameters: track, then (parameter, value) pairs, up to 65.
 *       0..144 voice parameters, 145..154 the name, 155 operator on/off
 *       (bit 5 OP1 .. bit 0 OP6). Applied live, without cutting notes.
 *   42  write steps (version 2): track, first step, then 4 bytes a step as
 *       struct groove_step (note, velocity, drums low, drums high), up to 15.
 *       The pattern changes in place: the transport keeps running, the other
 *       tracks keep sounding, undo works. The engine ignores a track that
 *       isn't a normal one and a range past step 64; the client validates
 *       the steps (groove_sequence_valid's rules) and reads them back.
 *
 * Changes made on the panel aren't announced (no room for it): an editor
 * reads the track back (commands 1 and 2) to see them.
 */
#include "instrument.h"
#include "sideband.h"

#define SMALL __attribute__((cold, minsize))

/* Each parameter to an absolute value through the panel's own edit paths,
 * so notes keep sounding, undo works and the project shows edited. */
static SMALL int set_parameters(unsigned track, const uint8_t *pairs, unsigned count)
{
    /* groove_fm_copy refuses a bad track; the edit calls skip a track that
     * isn't DX7 or is off. */
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
    return 0;
}

SMALL int sideband_command(unsigned cmd, const uint8_t *data, unsigned n, uint8_t *out, unsigned *count)
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
    if (cmd == 42 && n >= 6) {
        groove_edit_checkpoint(0x80000U + data[0]);
        groove_sequence_replace(data[0], data[1], (const struct groove_step *)(const void *)(data + 2), (n - 2) / 4);
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

