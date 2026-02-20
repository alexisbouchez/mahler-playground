#include "mahler.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
//  CURSED COMPOSER v2 - Generates a WAV file from your name
//  Uses mahler.c for theory. Now with reverb, arpeggios, bass,
//  smarter melody, stereo, and better timbres.
// ============================================================

#define SAMPLE_RATE   44100
#define CHANNELS      2
#define BITS_PER_SAMP 16
#define MAX_FRAMES    (SAMPLE_RATE * 45) // max 45 seconds

// Stereo sample buffer (interleaved L R L R ...)
static int32_t *g_left;
static int32_t *g_right;
static int g_num_frames = 0;

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

// --- ADSR envelope ---

static double envelope(double t, double dur, double attack, double decay,
                       double sustain_level, double release) {
    if (release > dur * 0.4) release = dur * 0.4;
    double sustain_end = dur - release;

    if (t < attack)
        return t / attack;
    if (t < attack + decay)
        return 1.0 - (1.0 - sustain_level) * ((t - attack) / decay);
    if (t < sustain_end)
        return sustain_level;
    return sustain_level * (dur - t) / release;
}

// --- Timbres ---

typedef enum {
    TIMBRE_PIANO,
    TIMBRE_PAD,
    TIMBRE_BASS
} timbre_t;

static double oscillator(double freq, double t, timbre_t timbre) {
    double phase = 2.0 * M_PI * freq * t;
    switch (timbre) {
    case TIMBRE_PIANO: {
        // Bright piano-ish: fundamental + decaying harmonics
        double s = sin(phase) * 0.50
                 + sin(phase * 2.0) * 0.20
                 + sin(phase * 3.0) * 0.12
                 + sin(phase * 4.0) * 0.06
                 + sin(phase * 5.0) * 0.03;
        // Slight detuning for width
        s += sin(phase * 1.002) * 0.05;
        return s;
    }
    case TIMBRE_PAD: {
        // Soft pad: mostly fundamental + slow beating
        double s = sin(phase) * 0.60
                 + sin(phase * 1.001) * 0.30  // slow beat
                 + sin(phase * 2.0) * 0.08;
        return s;
    }
    case TIMBRE_BASS: {
        // Warm bass: fundamental + sub + light grit
        double s = sin(phase) * 0.55
                 + sin(phase * 0.5) * 0.25    // sub octave
                 + sin(phase * 2.0) * 0.10
                 + sin(phase * 3.0) * 0.05;
        // Soft saturation
        s = tanh(s * 1.5) * 0.7;
        return s;
    }
    }
    return sin(phase);
}

// --- Synthesis: add a note to stereo buffer ---
// pan: 0.0 = full left, 0.5 = center, 1.0 = full right

static void synth_note_stereo(double freq, double start_sec, double duration,
                               double volume, double pan, timbre_t timbre,
                               double atk, double dec, double sus, double rel) {
    int start = (int)(start_sec * SAMPLE_RATE);
    int len   = (int)(duration * SAMPLE_RATE);
    double l_gain = cos(pan * M_PI * 0.5);
    double r_gain = sin(pan * M_PI * 0.5);

    for (int i = 0; i < len; i++) {
        int idx = start + i;
        if (idx < 0 || idx >= MAX_FRAMES) continue;

        double t = (double)i / SAMPLE_RATE;
        double env = envelope(t, duration, atk, dec, sus, rel);
        double sample = oscillator(freq, t, timbre) * env * volume * 10000.0;

        g_left[idx]  += (int32_t)(sample * l_gain);
        g_right[idx] += (int32_t)(sample * r_gain);

        if (idx >= g_num_frames) g_num_frames = idx + 1;
    }
}

// Convenience wrappers
static void synth_melody(double freq, double start, double dur, double vol, double pan) {
    synth_note_stereo(freq, start, dur, vol, pan, TIMBRE_PIANO, 0.01, 0.08, 0.6, 0.12);
}

static void synth_pad(double freq, double start, double dur, double vol, double pan) {
    synth_note_stereo(freq, start, dur, vol, pan, TIMBRE_PAD, 0.15, 0.2, 0.7, 0.3);
}

static void synth_bass(double freq, double start, double dur, double vol) {
    synth_note_stereo(freq, start, dur, vol, 0.5, TIMBRE_BASS, 0.01, 0.1, 0.8, 0.08);
}

// --- Simple delay-based reverb ---

static void apply_reverb(void) {
    // Multiple delay taps for a diffuse sound
    const int delays[]  = { 4410, 7350, 11025, 15876, 21609 }; // ~100ms to ~490ms
    const double gains[] = { 0.25, 0.18, 0.13, 0.09, 0.05 };
    const int num_taps = 5;

    for (int tap = 0; tap < num_taps; tap++) {
        int d = delays[tap];
        double g = gains[tap];
        for (int i = d; i < g_num_frames; i++) {
            g_left[i]  += (int32_t)(g_left[i - d]  * g);
            g_right[i] += (int32_t)(g_right[i - d] * g);
        }
    }
}

// --- WAV file writer (stereo) ---

static void write_u16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }
static void write_u32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }

static int write_wav(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror("fopen"); return 1; }

    // Clamp and interleave
    int16_t *interleaved = malloc(g_num_frames * CHANNELS * sizeof(int16_t));
    if (!interleaved) { fclose(f); return 1; }

    for (int i = 0; i < g_num_frames; i++) {
        int32_t l = g_left[i];
        int32_t r = g_right[i];
        if (l > 32767)  l = 32767;  if (l < -32768) l = -32768;
        if (r > 32767)  r = 32767;  if (r < -32768) r = -32768;
        interleaved[i * 2]     = (int16_t)l;
        interleaved[i * 2 + 1] = (int16_t)r;
    }

    uint32_t data_size = g_num_frames * CHANNELS * sizeof(int16_t);
    uint32_t file_size = 36 + data_size;

    fwrite("RIFF", 1, 4, f);
    write_u32(f, file_size);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    write_u32(f, 16);
    write_u16(f, 1);                                         // PCM
    write_u16(f, CHANNELS);
    write_u32(f, SAMPLE_RATE);
    write_u32(f, SAMPLE_RATE * CHANNELS * BITS_PER_SAMP / 8);
    write_u16(f, CHANNELS * BITS_PER_SAMP / 8);
    write_u16(f, BITS_PER_SAMP);

    fwrite("data", 1, 4, f);
    write_u32(f, data_size);
    fwrite(interleaved, sizeof(int16_t), g_num_frames * CHANNELS, f);

    free(interleaved);
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

// --- Simple LCG PRNG ---

static unsigned g_rng;
static unsigned rng_next(void) { g_rng = g_rng * 1103515245 + 12345; return g_rng; }
static int rng_range(int lo, int hi) { return lo + (int)(rng_next() % (unsigned)(hi - lo + 1)); }

// --- Chord progression patterns ---
// Each entry: scale_degree (0-indexed), chord_quality (0=major, 1=minor, 2=dom7)
static const int PROGRESSIONS[][8] = {
    { 0,0,  3,0,  4,0,  0,0 },   // I  - IV  - V   - I
    { 0,0,  5,1,  3,0,  4,0 },   // I  - vi  - IV  - V
    { 0,0,  4,0,  5,1,  3,0 },   // I  - V   - vi  - IV   (pop)
    { 0,1,  3,0,  4,2,  0,1 },   // i  - IV  - V7  - i    (minor)
    { 0,1,  5,0,  2,0,  4,2 },   // i  - VI  - III - V7   (minor)
    { 0,0,  3,0,  1,1,  4,0 },   // I  - IV  - ii  - V    (classic)
};
#define NUM_PROGRESSIONS 6
#define PROG_LEN 4

// --- Arpeggio patterns (chord tone indices) ---
static const int ARP_PATTERNS[][8] = {
    { 0, 1, 2, 1, 0, 1, 2, 1 },  // up-down
    { 0, 2, 1, 0, 2, 1, 0, 2 },  // skip
    { 0, 0, 1, 1, 2, 2, 1, 0 },  // pairs
    { 2, 1, 0, 1, 2, 0, 1, 2 },  // down-up
};
#define NUM_ARP_PATTERNS 4
#define ARP_LEN 8

int main(int argc, char *argv[]) {
    const char *name = (argc > 1) ? argv[1] : "Mahler";
    const char *outfile = (argc > 2) ? argv[2] : "output.wav";
    unsigned h = hash_name(name);
    g_rng = h;

    g_left  = calloc(MAX_FRAMES, sizeof(int32_t));
    g_right = calloc(MAX_FRAMES, sizeof(int32_t));
    if (!g_left || !g_right) { fprintf(stderr, "Out of memory\n"); return 1; }

    // Derive musical properties from name
    enum mah_tone root_tone = (enum mah_tone)(h % 7);
    int root_acci = (int)((h >> 3) % 3) - 1;  // -1, 0, or 1
    int prog_idx  = (int)((h >> 5) % NUM_PROGRESSIONS);
    int arp_idx   = (int)((h >> 8) % NUM_ARP_PATTERNS);
    int tempo_bpm = 100 + (int)((h >> 11) % 60);  // 100-159 BPM
    int is_minor  = (prog_idx >= 3) ? 1 : 0;
    int swing     = (h >> 14) & 1;  // 50% chance of swing

    struct mah_note root = { root_tone, root_acci, 3 };
    char buf[MAH_DISP_LEN];
    mah_write_note(root, buf, MAH_DISP_LEN, NULL);

    // Get the scale
    const struct mah_scale_base *scale_type = is_minor
        ? &MAH_NATURAL_MIN_SCALE
        : &MAH_MAJOR_SCALE;
    struct mah_note scale_notes[16];
    struct mah_scale scale = mah_get_scale(root, scale_type, scale_notes, MAH_ASCEND, NULL);
    int sd = scale.size - 1; // usable degrees (exclude octave duplicate)

    // Print info
    printf("\n");
    printf("  CURSED COMPOSER v2\n");
    printf("  ══════════════════\n\n");
    printf("  Composing for: %s\n", name);
    printf("  Key: %s %s\n", buf, is_minor ? "minor" : "major");
    printf("  Tempo: %d BPM%s\n", tempo_bpm, swing ? " (swing)" : "");
    printf("  Progression: ");

    double beat_sec = 60.0 / tempo_bpm;
    double eighth = beat_sec / 2.0;
    double swing_offset = swing ? eighth * 0.16 : 0.0; // swing delays offbeats
    double cursor = 0.0;

    const int *prog = PROGRESSIONS[prog_idx];
    const int *arp_pat = ARP_PATTERNS[arp_idx];

    // ===== INTRO: 2 bars, gentle pad chords fading in =====
    for (int c = 0; c < 2; c++) {
        int degree = prog[c * 2];
        struct mah_note chord_root = scale_notes[degree % sd];
        chord_root.pitch = 3;

        int use_minor = prog[c * 2 + 1];
        const struct mah_chord_base *ctype = (use_minor == 1) ? &MAH_MINOR_TRIAD
                                           : (use_minor == 2) ? &MAH_DOMINANT_7
                                           : &MAH_MAJOR_TRIAD;
        struct mah_note bn[5], cn[5];
        struct mah_chord chord = mah_get_chord(chord_root, ctype, bn, cn, NULL);

        double vol = 0.3 + 0.15 * c; // fade in
        for (int i = 0; i < chord.size; i++) {
            synth_pad(note_to_freq(chord.notes[i]), cursor, beat_sec * 4.0 * 0.95, vol, 0.35 + 0.1 * i);
        }
        cursor += beat_sec * 4.0;
    }

    // ===== MAIN SECTION: 3 repetitions of the full progression =====

    // Melody state for stepwise motion
    int mel_pos = sd / 2; // start in the middle of the scale

    for (int rep = 0; rep < 3; rep++) {
        for (int c = 0; c < PROG_LEN; c++) {
            int degree = prog[c * 2];
            int use_minor = prog[c * 2 + 1];

            struct mah_note chord_root = scale_notes[degree % sd];
            chord_root.pitch = 3;

            const struct mah_chord_base *ctype = (use_minor == 1) ? &MAH_MINOR_TRIAD
                                               : (use_minor == 2) ? &MAH_DOMINANT_7
                                               : &MAH_MAJOR_TRIAD;
            struct mah_note bn[5], cn[5];
            struct mah_chord chord = mah_get_chord(chord_root, ctype, bn, cn, NULL);

            if (rep == 0) {
                char cb[MAH_DISP_LEN];
                mah_write_note(chord_root, cb, MAH_DISP_LEN, NULL);
                printf("%s%s ", cb, use_minor == 1 ? "m" : use_minor == 2 ? "7" : "");
            }

            double bar_dur = beat_sec * 4.0;

            // --- PAD CHORDS (background, wide stereo) ---
            double pad_vol = (rep == 2 && c >= 2) ? 0.35 : 0.25; // louder on final bars
            for (int i = 0; i < chord.size; i++) {
                double pan = 0.25 + 0.5 * ((double)i / (chord.size - 1)); // spread L-R
                synth_pad(note_to_freq(chord.notes[i]), cursor, bar_dur * 0.92, pad_vol, pan);
            }

            // --- BASS LINE (root note, center) ---
            {
                struct mah_note bass_note = chord_root;
                bass_note.pitch = 2;
                double bass_freq = note_to_freq(bass_note);

                // Walking bass: root, root, 5th, root (or variations)
                double bass_times[] = { 0.0, beat_sec, beat_sec * 2.0, beat_sec * 3.0 };
                double bass_durs[]  = { beat_sec * 0.9, beat_sec * 0.9, beat_sec * 0.9, beat_sec * 0.9 };

                // Get the 5th for the walking pattern
                struct mah_note fifth = mah_get_inter(bass_note,
                    (struct mah_interval){ 5, MAH_PERFECT }, NULL);
                double fifth_freq = note_to_freq(fifth);

                double bass_freqs[] = { bass_freq, bass_freq, fifth_freq, bass_freq };

                // On rep 2+, vary bass pattern
                if (rep >= 1) {
                    struct mah_note third = chord.notes[1];
                    third.pitch = 2;
                    bass_freqs[1] = note_to_freq(third);
                }

                for (int b = 0; b < 4; b++) {
                    synth_bass(bass_freqs[b], cursor + bass_times[b], bass_durs[b], 0.45);
                }
            }

            // --- ARPEGGIATED CHORD (mid-range, panned slightly right) ---
            {
                double arp_cursor = cursor;
                for (int a = 0; a < ARP_LEN; a++) {
                    int ci = arp_pat[a] % chord.size;
                    struct mah_note arp_note = chord.notes[ci];
                    arp_note.pitch = 4;

                    double arp_dur = eighth;
                    // Apply swing to offbeats
                    double t_offset = (a % 2 == 1) ? swing_offset : 0.0;

                    synth_note_stereo(note_to_freq(arp_note), arp_cursor + t_offset,
                        arp_dur * 0.7, 0.3, 0.62, TIMBRE_PIANO,
                        0.005, 0.05, 0.4, 0.1);
                    arp_cursor += arp_dur;
                }
            }

            // --- MELODY (stepwise motion with occasional leaps, panned left) ---
            {
                double mel_cursor = cursor;
                int notes_in_bar = 8; // 8 eighth notes per bar

                for (int n = 0; n < notes_in_bar; n++) {
                    int r = rng_range(0, 99);

                    // Movement rules for musical melody:
                    // 55% stepwise (move ±1), 20% stay, 15% leap (±2-3), 10% rest
                    if (r < 10) {
                        // Rest - silence
                    } else {
                        int step;
                        if (r < 65)      step = (rng_next() & 1) ? 1 : -1;  // step
                        else if (r < 85) step = 0;                            // repeat
                        else             step = rng_range(-3, 3);             // leap

                        mel_pos += step;

                        // Constrain to scale range, with wrap
                        while (mel_pos < 0)  mel_pos += sd;
                        while (mel_pos >= sd) mel_pos -= sd;

                        struct mah_note mel_note = scale_notes[mel_pos];
                        mel_note.pitch = 5;

                        double dur = eighth;
                        double t_offset = (n % 2 == 1) ? swing_offset : 0.0;

                        // Longer notes occasionally (on beats 1 and 3)
                        if ((n == 0 || n == 4) && rng_range(0, 2) == 0) {
                            dur = beat_sec * 0.9;
                        }

                        // Velocity variation
                        double vel = 0.45 + 0.2 * ((n == 0 || n == 4) ? 1.0 : 0.5);

                        // Accent first note of each bar more
                        if (n == 0) vel += 0.1;

                        synth_melody(note_to_freq(mel_note),
                            mel_cursor + t_offset, dur * 0.85, vel, 0.3);
                    }
                    mel_cursor += eighth;
                }
            }

            cursor += bar_dur;
        }
    }

    printf("\n");

    // ===== OUTRO: ritardando final chord =====
    {
        struct mah_note final_root = scale_notes[0];
        final_root.pitch = 3;

        // Use a 7th chord for a richer ending
        const struct mah_chord_base *final_type = is_minor ? &MAH_MINOR_7 : &MAH_MAJOR_7;
        struct mah_note fb[5], fn[5];
        struct mah_chord final_chord = mah_get_chord(final_root, final_type, fb, fn, NULL);

        // Ritardando: play chord tones one by one, slowing down
        double rit_cursor = cursor;
        for (int i = 0; i < final_chord.size; i++) {
            double delay = 0.15 + 0.08 * i; // each note slightly later
            struct mah_note n = final_chord.notes[i];
            double pan = 0.2 + 0.6 * ((double)i / (final_chord.size - 1));
            synth_pad(note_to_freq(n), rit_cursor, beat_sec * 8.0, 0.5, pan);
            rit_cursor += delay;
        }

        // High melody note landing on tonic
        struct mah_note high_root = final_root;
        high_root.pitch = 5;
        synth_melody(note_to_freq(high_root), cursor + 0.3, beat_sec * 6.0, 0.55, 0.45);

        // Bass
        struct mah_note bass_root = final_root;
        bass_root.pitch = 2;
        synth_bass(note_to_freq(bass_root), cursor, beat_sec * 8.0, 0.5);

        cursor += beat_sec * 10.0;
    }

    // ===== Apply reverb =====
    printf("  Applying reverb...\n");
    apply_reverb();

    double total_sec = (double)g_num_frames / SAMPLE_RATE;
    printf("  Duration: %.1f seconds\n", total_sec);
    printf("  Scale: %s\n", scale_type->name);
    printf("  Notes in scale: ");
    for (int i = 0; i < scale.size; i++) {
        char nb[MAH_DISP_LEN];
        mah_write_note(scale_notes[i], nb, MAH_DISP_LEN, NULL);
        printf("%s ", nb);
    }
    printf("\n\n");

    if (write_wav(outfile) == 0) {
        printf("  Wrote: %s\n", outfile);
        printf("  Play it:  aplay %s\n", outfile);
        printf("            or: ffplay -nodisp %s\n\n", outfile);
    } else {
        printf("  Failed to write %s\n", outfile);
    }

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

    free(g_left);
    free(g_right);
    return 0;
}
