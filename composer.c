#include "mahler.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
//  CURSED COMPOSER - Generates a WAV file from your name
//  Uses mahler.c for theory, sine waves for synthesis.
// ============================================================

#define SAMPLE_RATE   44100
#define CHANNELS      1
#define BITS_PER_SAMP 16
#define MAX_SAMPLES   (SAMPLE_RATE * 30) // max 30 seconds

static int16_t *g_samples;
static int g_num_samples = 0;

// --- Note to frequency conversion ---

static const int SEMITONE_MAP[] = {
    /*C*/ 0, /*D*/ 2, /*E*/ 4, /*F*/ 5, /*G*/ 7, /*A*/ 9, /*B*/ 11
};

static double note_to_freq(struct mah_note n) {
    int midi = 12 * (n.pitch + 1) + SEMITONE_MAP[n.tone] + n.acci;
    if (midi < 0) midi = 0;
    if (midi > 127) midi = 127;
    return 440.0 * pow(2.0, (midi - 69) / 12.0);
}

// --- Simple ADSR envelope ---

static double envelope(double t, double duration) {
    double attack  = 0.02;
    double release = 0.08;
    if (release > duration * 0.3) release = duration * 0.3;

    if (t < attack)
        return t / attack;
    if (t > duration - release)
        return (duration - t) / release;
    return 1.0;
}

// --- Synthesis: add a note to the sample buffer ---

static void synth_note(double freq, double start_sec, double duration, double volume) {
    int start = (int)(start_sec * SAMPLE_RATE);
    int len   = (int)(duration * SAMPLE_RATE);

    for (int i = 0; i < len; i++) {
        int idx = start + i;
        if (idx < 0 || idx >= MAX_SAMPLES) continue;

        double t = (double)i / SAMPLE_RATE;
        double env = envelope(t, duration);

        // Mix of sine + a softer harmonic for warmth
        double sample = sin(2.0 * M_PI * freq * t) * 0.7
                      + sin(2.0 * M_PI * freq * 2.0 * t) * 0.15
                      + sin(2.0 * M_PI * freq * 3.0 * t) * 0.08;

        int32_t val = (int32_t)g_samples[idx] + (int32_t)(sample * env * volume * 8000.0);
        if (val > 32767)  val = 32767;
        if (val < -32768) val = -32768;
        g_samples[idx] = (int16_t)val;

        if (idx >= g_num_samples) g_num_samples = idx + 1;
    }
}

// --- Synthesize a chord (all notes at once) ---

static void synth_chord(struct mah_chord *chord, double start, double duration, double vol) {
    for (int i = 0; i < chord->size; i++) {
        synth_note(note_to_freq(chord->notes[i]), start, duration, vol);
    }
}

// --- WAV file writer ---

static void write_u16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }
static void write_u32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }

static int write_wav(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror("fopen"); return 1; }

    uint32_t data_size = g_num_samples * sizeof(int16_t);
    uint32_t file_size = 36 + data_size;

    // RIFF header
    fwrite("RIFF", 1, 4, f);
    write_u32(f, file_size);
    fwrite("WAVE", 1, 4, f);

    // fmt chunk
    fwrite("fmt ", 1, 4, f);
    write_u32(f, 16);                                    // chunk size
    write_u16(f, 1);                                     // PCM
    write_u16(f, CHANNELS);
    write_u32(f, SAMPLE_RATE);
    write_u32(f, SAMPLE_RATE * CHANNELS * BITS_PER_SAMP / 8);
    write_u16(f, CHANNELS * BITS_PER_SAMP / 8);
    write_u16(f, BITS_PER_SAMP);

    // data chunk
    fwrite("data", 1, 4, f);
    write_u32(f, data_size);
    fwrite(g_samples, sizeof(int16_t), g_num_samples, f);

    fclose(f);
    return 0;
}

// --- Name hashing ---

static unsigned hash_name(const char *name) {
    unsigned h = 5381;
    for (int i = 0; name[i]; i++)
        h = h * 33 + (unsigned char)name[i];
    return h;
}

// --- Composition logic ---

// Chord progression patterns (scale degrees, 0-indexed)
// Each pattern is: degree, is_minor flag pairs
static const int PROGRESSIONS[][8] = {
    { 0,0,  3,0,  4,0,  0,0 },  // I  - IV - V  - I
    { 0,0,  5,1,  3,0,  4,0 },  // I  - vi - IV - V
    { 0,0,  4,0,  5,1,  3,0 },  // I  - V  - vi - IV
    { 0,1,  3,0,  4,0,  0,1 },  // i  - IV - V  - i   (minor)
    { 0,1,  5,0,  2,0,  4,0 },  // i  - VI - III - V  (minor)
};
#define NUM_PROGRESSIONS 5
#define PROG_LEN 4

// Melody rhythm patterns (in 8th notes, how long each note lasts)
static const int RHYTHMS[][8] = {
    { 2, 2, 1, 1, 2, 2, 2, 4 },
    { 1, 1, 2, 2, 1, 1, 2, 2 },
    { 4, 2, 2, 1, 1, 1, 1, 4 },
    { 2, 1, 1, 4, 2, 2, 2, 2 },
};
#define NUM_RHYTHMS 4
#define RHYTHM_LEN 8

int main(int argc, char *argv[]) {
    const char *name = (argc > 1) ? argv[1] : "Mahler";
    const char *outfile = (argc > 2) ? argv[2] : "output.wav";
    unsigned h = hash_name(name);

    g_samples = calloc(MAX_SAMPLES, sizeof(int16_t));
    if (!g_samples) { fprintf(stderr, "Out of memory\n"); return 1; }

    // Derive musical properties from name
    enum mah_tone root_tone = (enum mah_tone)(h % 7);
    int root_acci = (int)((h >> 3) % 3) - 1;  // -1, 0, or 1 (keep it playable)
    int prog_idx  = (int)((h >> 5) % NUM_PROGRESSIONS);
    int rhy_idx   = (int)((h >> 8) % NUM_RHYTHMS);
    int tempo_bpm = 90 + (int)((h >> 11) % 80);  // 90-169 BPM
    int is_minor  = (prog_idx >= 3) ? 1 : 0;

    struct mah_note root = { root_tone, root_acci, 3 };  // octave 3 for chords
    char buf[MAH_DISP_LEN];
    mah_write_note(root, buf, MAH_DISP_LEN, NULL);

    // Get the scale for melody
    const struct mah_scale_base *scale_type = is_minor
        ? &MAH_NATURAL_MIN_SCALE
        : &MAH_MAJOR_SCALE;
    struct mah_note scale_notes[16];
    struct mah_scale scale = mah_get_scale(root, scale_type, scale_notes, MAH_ASCEND, NULL);

    // Print composition info
    printf("\n");
    printf("  CURSED COMPOSER\n");
    printf("  ═══════════════\n\n");
    printf("  Composing for: %s\n", name);
    printf("  Key: %s %s\n", buf, is_minor ? "minor" : "major");
    printf("  Tempo: %d BPM\n", tempo_bpm);
    printf("  Progression: ");

    double beat_sec = 60.0 / tempo_bpm;
    double eighth = beat_sec / 2.0;
    double cursor = 0.0;  // current time position in seconds

    const int *prog = PROGRESSIONS[prog_idx];
    const int *rhythm = RHYTHMS[rhy_idx];

    // --- Build and play chord progression (2 repetitions) ---
    for (int rep = 0; rep < 2; rep++) {
        for (int c = 0; c < PROG_LEN; c++) {
            int degree = prog[c * 2];
            int use_minor = prog[c * 2 + 1];

            // Get the root of this chord by going up scale degrees
            struct mah_note chord_root = scale_notes[degree % scale.size];
            chord_root.pitch = 3;  // keep in octave 3

            const struct mah_chord_base *chord_type = use_minor
                ? &MAH_MINOR_TRIAD
                : &MAH_MAJOR_TRIAD;

            struct mah_note base_n[4], chord_n[4];
            struct mah_chord chord = mah_get_chord(chord_root, chord_type, base_n, chord_n, NULL);

            if (rep == 0) {
                char cb[MAH_DISP_LEN];
                mah_write_note(chord_root, cb, MAH_DISP_LEN, NULL);
                printf("%s%s ", cb, use_minor ? "m" : "");
            }

            // Play chord for 4 beats
            double chord_dur = beat_sec * 4.0;
            synth_chord(&chord, cursor, chord_dur * 0.95, 0.5);

            // --- Melody over this chord ---
            double mel_cursor = cursor;
            unsigned mel_hash = h ^ (unsigned)(rep * 7 + c * 13);
            for (int n = 0; n < RHYTHM_LEN; n++) {
                int note_dur_eighths = rhythm[n];
                double note_dur = eighth * note_dur_eighths;

                // Pick a melody note from the scale
                int scale_idx = (int)((mel_hash >> (n * 3)) % (scale.size - 1));
                struct mah_note mel_note = scale_notes[scale_idx];
                mel_note.pitch = 5;  // octave 5 for melody

                // Occasional rest (skip ~15% of notes)
                if (((mel_hash >> (n * 2 + 1)) % 7) != 0) {
                    synth_note(note_to_freq(mel_note), mel_cursor, note_dur * 0.85, 0.7);
                }

                mel_cursor += note_dur;
                mel_hash = mel_hash * 1103515245 + 12345;  // LCG for variety
            }

            cursor += chord_dur;
        }
    }

    printf("\n");

    // --- Final chord (hold it longer) ---
    {
        struct mah_note final_root = scale_notes[0];
        final_root.pitch = 3;
        const struct mah_chord_base *final_type = is_minor ? &MAH_MINOR_TRIAD : &MAH_MAJOR_TRIAD;
        struct mah_note fb[4], fn[4];
        struct mah_chord final_chord = mah_get_chord(final_root, final_type, fb, fn, NULL);

        // Also add the octave above root for fullness
        struct mah_note high_root = final_root;
        high_root.pitch = 5;

        synth_chord(&final_chord, cursor, beat_sec * 6.0, 0.6);
        synth_note(note_to_freq(high_root), cursor, beat_sec * 6.0, 0.5);
        cursor += beat_sec * 6.0;
    }

    double total_sec = (double)g_num_samples / SAMPLE_RATE;
    printf("  Duration: %.1f seconds\n", total_sec);
    printf("  Scale: %s\n", scale_type->name);
    printf("  Notes in scale: ");
    for (int i = 0; i < scale.size; i++) {
        char nb[MAH_DISP_LEN];
        mah_write_note(scale_notes[i], nb, MAH_DISP_LEN, NULL);
        printf("%s ", nb);
    }
    printf("\n\n");

    // Write WAV
    if (write_wav(outfile) == 0) {
        printf("  Wrote: %s\n", outfile);
        printf("  Play it:  aplay %s\n", outfile);
        printf("            or: ffplay -nodisp %s\n\n", outfile);
    } else {
        printf("  Failed to write %s\n", outfile);
    }

    // Funny commentary
    const char *comments[] = {
        "This is either a masterpiece or a war crime. Possibly both.",
        "Debussy would weep. Not from beauty, but from confusion.",
        "If elevator music had an evil twin, this would be it.",
        "Certified banger. In the sense that it bangs pots and pans.",
        "This composition has been reported to the Geneva Convention.",
        "Your neighbors will love this. Play it at 3am for best results.",
        "Mozart rolled over in his grave. Then rolled back. Then left.",
        "This is what happens when math tries to be art.",
    };
    printf("  Review: %s\n\n", comments[h % 8]);

    free(g_samples);
    return 0;
}
