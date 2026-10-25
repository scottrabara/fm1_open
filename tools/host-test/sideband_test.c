/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Host tests for the Sideband extensions (app/sideband.c), against the
 * browser simulator's platform (web/engine). Build and run: tools/host-test/run.sh */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "instrument.h"

extern void sim_boot(unsigned); extern uint8_t *sim_file(void); extern int sim_midi_from(unsigned, unsigned);
extern void sim_midi_capture(unsigned); extern unsigned sim_midi_output(void); extern const float *sim_render(void);
extern void groove_screen_snapshot(uint8_t state[8]);

static int failures;
#define CHECK(c, ...) do { if (!(c)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); puts(""); } } while (0)

static unsigned frame(uint8_t *m, unsigned cmd, const uint8_t *d, unsigned n)
{
    unsigned at = 0;
    m[at++] = 0xF0; m[at++] = 0x7D; m[at++] = 'F'; m[at++] = 'M'; m[at++] = 2; m[at++] = (uint8_t)cmd;
    static uint8_t token = 1; m[at++] = token++ & 127; m[at++] = 0x22;
    unsigned v = 0, b = 0;
    for (unsigned i = 0; i < n; i++) { v |= (unsigned)d[i] << b; b += 8; while (b >= 7) { m[at++] = v & 127; v >>= 7; b -= 7; } }
    if (b) m[at++] = v & 127;
    unsigned s = 0; for (unsigned i = 1; i < at; i++) s += m[i];
    m[at++] = (0U - s) & 127; m[at++] = 0xF7;
    return at;
}
static void run(unsigned blocks) { for (unsigned i = 0; i < blocks; i++) sim_render(); }
static void send_raw(unsigned port, const uint8_t *m, unsigned n) { memcpy(sim_file(), m, n); CHECK(sim_midi_from(port, n) == 0, "sim_midi_from refused"); run(4); }

/* The next message out: its port, and its payload unpacked (status first). */
static int next_reply(unsigned *port, unsigned *cmd, uint8_t *payload, unsigned *count)
{
    unsigned o = sim_midi_output();
    if (!o) return 0;
    unsigned n = o & 255; const uint8_t *m = sim_file();
    *port = o >> 8; *cmd = m[5];
    unsigned v = 0, b = 0, k = 0;
    for (unsigned i = 8; i + 2 < n; i++) { v |= (unsigned)m[i] << b; b += 7; if (b >= 8) { payload[k++] = v & 255; v >>= 8; b -= 8; } }
    *count = k;
    return 1;
}
static unsigned ask(unsigned port, unsigned cmd, const uint8_t *d, unsigned n, uint8_t *payload)
{
    uint8_t m[200]; unsigned k = frame(m, cmd, d, n);
    sim_midi_capture(1);
    send_raw(port, m, k);
    unsigned p, c, count = 0;
    int got = next_reply(&p, &c, payload, &count);
    CHECK(got && p == port && c == (cmd | 64), "no reply to command %u on port %u", cmd, port);
    return count;
}
static void patch_of(unsigned track, uint8_t patch[156]) { CHECK(!groove_fm_copy(track, patch, NULL), "copy track %u", track); }

int main(void)
{
    sim_boot(1);
    run(200);
    uint8_t r[80], patch[156];
    enum { USB = 1, BLE = 2 };

    /* 22: V14's nine bytes, then 'S' and the version. */
    unsigned n = ask(BLE, 22, 0, 0, r);
    CHECK(n == 12 && r[0] == 0 && r[10] == 'S' && r[11] == 2, "capabilities: %u bytes, S=%c v%u", n, r[10], r[11]);

    /* 40: the track map. */
    n = ask(USB, 40, 0, 0, r);
    CHECK(n == 25 && r[0] == 0, "track map: %u bytes", n);
    int dx7 = -1;
    for (unsigned t = 0; t < 8; t++) {
        printf("track %u: engine %u role %u channel %u\n", t + 1, r[1 + t * 3], r[2 + t * 3], r[3 + t * 3] + 1);
        CHECK(r[1 + t * 3] == groove_source_type(t) && r[3 + t * 3] == groove_midi_input_channel(t), "track %u map", t);
        if (dx7 < 0 && r[1 + t * 3] == GROOVE_DX7 && r[2 + t * 3] != GROOVE_TRACK_OFF) dx7 = (int)t;
    }
    CHECK(dx7 >= 0, "a DX7 track in the default project");
    if (dx7 < 0) { printf("%d failures\n", failures); return 1; }

    /* 41: algorithm, a name character and the operator mask, live. */
    patch_of((unsigned)dx7, patch);
    uint8_t algorithm = patch[134] == 7 ? 8 : 7;
    uint8_t set[] = {(uint8_t)dx7, 134, algorithm, 145, 'Z', 155, 0x1F /* OP1 off */};
    n = ask(BLE, 41, set, sizeof set, r);
    CHECK(n == 1 && r[0] == 0, "set parameters: status %u", r[0]);
    patch_of((unsigned)dx7, patch);
    CHECK(patch[134] == algorithm, "algorithm %u, wanted %u", patch[134], algorithm);
    CHECK(patch[145] == 'Z', "name %c", patch[145]);
    CHECK(patch[155] == 0x1F, "operators %02X", patch[155]);
    uint8_t back[] = {(uint8_t)dx7, 155, 0x3F};
    ask(BLE, 41, back, sizeof back, r);
    patch_of((unsigned)dx7, patch);
    CHECK(patch[155] == 0x3F, "operators back on: %02X", patch[155]);

    /* Refused: a parameter past 155, a value past its range, a track that isn't DX7. */
    uint8_t bad[] = {(uint8_t)dx7, 156, 1};
    ask(USB, 41, bad, sizeof bad, r);
    CHECK(r[0] != 0, "parameter 156 accepted");
    uint8_t badname[] = {(uint8_t)dx7, 146, 10};
    ask(USB, 41, badname, sizeof badname, r);
    CHECK(r[0] != 0, "name character 10 accepted");
    for (unsigned t = 0; t < 8; t++) if (groove_source_type(t) != GROOVE_DX7) {
        uint8_t other[] = {(uint8_t)t, 134, 3};
        ask(USB, 41, other, sizeof other, r);
        CHECK(r[0] != 0, "track %u isn't DX7 but took a parameter", t);
        break;
    }

    /* 42: write steps in place, while the sequencer plays. */
    unsigned t42 = (unsigned)dx7;
    struct groove_view view;
    uint8_t startm[] = {0xFA};
    send_raw(USB, startm, 1); run(400);
    groove_sequence_view(t42, 0, &view);
    unsigned was_playing = view.playing;
    printf("playing before: %u\n", was_playing);
    uint8_t steps[2 + 8] = {(uint8_t)t42, 5, 64, 100, 0x40 /* gate 2 */, 0, 67, 90, 0x00, 0x10 /* tie */};
    n = ask(BLE, 42, steps, sizeof steps, r);
    CHECK(n == 1 && r[0] == 0, "write steps: status %u", r[0]);
    struct groove_step cells[2];
    groove_sequence_read(t42, 5, cells, 2);
    CHECK(cells[0].note == 64 && cells[0].velocity == 100 && cells[0].drums == 0x40, "step 6: %u %u %04X", cells[0].note, cells[0].velocity, cells[0].drums);
    CHECK(cells[1].note == 67 && cells[1].velocity == 90 && cells[1].drums == 0x1000, "step 7: %u %u %04X", cells[1].note, cells[1].velocity, cells[1].drums);
    groove_sequence_view(t42, 0, &view);
    CHECK(view.playing == was_playing, "the transport changed: %u -> %u", was_playing, view.playing);
    /* Undo puts them back. */
    CHECK(!groove_edit_undo(), "undo");
    groove_sequence_read(t42, 5, cells, 1);
    CHECK(!(cells[0].note == 64 && cells[0].velocity == 100 && cells[0].drums == 0x40), "undo left the step");
    /* Past step 64: nothing changes. */
    struct groove_step before[1]; groove_sequence_read(t42, 63, before, 1);
    uint8_t past[2 + 8] = {(uint8_t)t42, 63, 1, 1, 0, 0, 2, 2, 0, 0};
    ask(BLE, 42, past, sizeof past, r);
    groove_sequence_read(t42, 63, cells, 1);
    CHECK(cells[0].note == before[0].note && cells[0].velocity == before[0].velocity, "a range past 64 was written");
    /* Too short: refused like an unknown command. */
    uint8_t on = (uint8_t)(dx7 + 1);
    ask(BLE, 42, &on, 1, r);
    CHECK(r[0] != 0, "a one-byte 42 was answered");

    /* V14's own commands still answer as before. */
    n = ask(USB, 0, 0, 0, r);
    CHECK(n == 15 && r[0] == 0 && r[1] == 4, "info: %u bytes", n);

    printf(failures ? "%d failures\n" : "all passed\n", failures);
    return failures != 0;
}
