#include "mahler.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

// ============================================================
//  MUSICAL HOROSCOPE - What does your name sound like?
//  Uses mahler.c to derive your musical personality.
// ============================================================

static unsigned hash_name(const char *name) {
    unsigned h = 5381;
    for (int i = 0; name[i]; i++)
        h = h * 33 + (unsigned char)name[i];
    return h;
}

static const char *mood_from_quality(enum mah_quality q) {
    switch (q) {
        case MAH_DIMINISHED: return "deeply suspicious of everyone around you";
        case MAH_MINOR:      return "contemplating the meaning of existence";
        case MAH_MAJOR:      return "annoyingly optimistic for no reason";
        case MAH_AUGMENTED:  return "about to do something chaotic and irreversible";
        case MAH_PERFECT:    return "smugly superior (and correct about it)";
        default:             return "confused, as usual";
    }
}

static const char *vibe_from_chord(const struct mah_chord_base *type) {
    if (type == &MAH_MAJOR_TRIAD)       return "a golden retriever in human form";
    if (type == &MAH_MINOR_TRIAD)       return "a poet who only writes in the rain";
    if (type == &MAH_AUGMENTED_TRIAD)   return "that friend who always escalates the situation";
    if (type == &MAH_DIMINISHED_TRIAD)  return "a detective in a noir film who trusts nobody";
    if (type == &MAH_DIMINISHED_7)      return "a supervillain monologuing before their plan fails";
    if (type == &MAH_HALF_DIMINISHED_7) return "someone who almost committed but backed out";
    if (type == &MAH_MINOR_7)           return "a jazz cat at 2am who knows too much";
    if (type == &MAH_MAJOR_7)           return "a sunset that makes strangers cry on the bus";
    if (type == &MAH_DOMINANT_7)        return "the person who HAS to resolve every argument";
    return "an enigma wrapped in a riddle wrapped in a time signature";
}

static const char *destiny_from_scale(const struct mah_scale_base *type) {
    if (type == &MAH_MAJOR_SCALE)           return "You will find a $20 bill in an old jacket.";
    if (type == &MAH_NATURAL_MIN_SCALE)     return "You will dramatically stare out a window today.";
    if (type == &MAH_HARMONIC_MIN_SCALE)    return "A mysterious stranger will ask you for directions. They are not lost.";
    if (type == &MAH_MELODIC_MIN_SCALE)     return "You will ascend to greatness, then immediately descend into snacking.";
    if (type == &MAH_PENTATONIC_MAJ_SCALE)  return "You will hum a tune that gets stuck in 4 people's heads.";
    if (type == &MAH_PENTATONIC_MIN_SCALE)  return "You are destined to play a sick guitar solo. Somewhere. Someday.";
    if (type == &MAH_BLUES_SCALE)           return "Your soul is too funky for this mortal plane.";
    if (type == &MAH_WHOLE_TONE_SCALE)      return "You will float through the day like a Debussy fever dream.";
    if (type == &MAH_OCTATONIC_HALF_SCALE)  return "Chaos follows you, but in a cool way.";
    if (type == &MAH_OCTATONIC_WHOLE_SCALE) return "You are two half-steps away from enlightenment at all times.";
    return "The stars are confused about you. Check back later.";
}

static const char *key_sig_roast(int alter) {
    if (alter == 0)  return "Zero accidentals. You are the C major of people: basic, but functional.";
    if (alter == 1)  return "One sharp? How adventurous. You put salt on your food sometimes.";
    if (alter == -1) return "One flat. You're the 'I'll have what they're having' of music.";
    if (alter >= 5)  return "5+ sharps?! You don't read music, music reads YOU.";
    if (alter <= -5) return "5+ flats?! You live in a world of suffering and enharmonic nightmares.";
    if (alter > 0)   return "A few sharps. Edgy enough to be interesting, not enough to be a problem.";
    return "A few flats. You have a gentle melancholy, like a slightly deflated balloon.";
}

static const struct mah_chord_base *ALL_CHORDS[] = {
    &MAH_MAJOR_TRIAD, &MAH_MINOR_TRIAD, &MAH_AUGMENTED_TRIAD,
    &MAH_DIMINISHED_TRIAD, &MAH_DIMINISHED_7, &MAH_HALF_DIMINISHED_7,
    &MAH_MINOR_7, &MAH_MAJOR_7, &MAH_DOMINANT_7
};
#define NUM_CHORDS 9

static const struct mah_scale_base *ALL_SCALES[] = {
    &MAH_MAJOR_SCALE, &MAH_NATURAL_MIN_SCALE, &MAH_HARMONIC_MIN_SCALE,
    &MAH_MELODIC_MIN_SCALE, &MAH_PENTATONIC_MAJ_SCALE, &MAH_PENTATONIC_MIN_SCALE,
    &MAH_BLUES_SCALE, &MAH_WHOLE_TONE_SCALE, &MAH_OCTATONIC_HALF_SCALE,
    &MAH_OCTATONIC_WHOLE_SCALE
};
#define NUM_SCALES 10

static const enum mah_quality ALL_QUALITIES[] = {
    MAH_DIMINISHED, MAH_MINOR, MAH_MAJOR, MAH_AUGMENTED, MAH_PERFECT
};
#define NUM_QUALITIES 5

int main(int argc, char *argv[]) {
    const char *name = (argc > 1) ? argv[1] : "Mahler";
    unsigned h = hash_name(name);

    // Derive musical properties from the name hash
    enum mah_tone tone      = (enum mah_tone)(h % 7);
    int acci                = (int)((h >> 3) % 5) - 2;   // -2 to +2
    int octave              = (int)((h >> 6) % 8);        // 0 to 7
    int chord_idx           = (int)((h >> 9) % NUM_CHORDS);
    int scale_idx           = (int)((h >> 12) % NUM_SCALES);
    int quality_idx         = (int)((h >> 15) % NUM_QUALITIES);
    int interval_steps      = (int)((h >> 18) % 7) + 1;  // 1 to 7

    struct mah_note root = { tone, acci, octave };
    char buf[MAH_DISP_LEN];
    char buf2[MAH_DISP_LEN];

    printf("\n");
    printf("  ♪♫♪ MUSICAL HOROSCOPE ♪♫♪\n");
    printf("  ══════════════════════════\n\n");
    printf("  Subject: %s\n\n", name);

    // === YOUR NOTE ===
    mah_write_note(root, buf, MAH_DISP_LEN, NULL);
    printf("  ★ Your Soul Note: %s\n", buf);
    if (acci >= 2)       printf("    You are double-sharp. Overachiever.\n");
    else if (acci == 1)  printf("    You are sharp. Literally and figuratively.\n");
    else if (acci == 0)  printf("    You are natural. Boringly pure.\n");
    else if (acci == -1) printf("    You are flat. Like your sense of humor.\n");
    else                 printf("    You are double-flat. You've flatlined.\n");
    printf("\n");

    // === YOUR CHORD ===
    const struct mah_chord_base *chord_type = ALL_CHORDS[chord_idx];
    struct mah_note base_notes[8], chord_notes[8];
    struct mah_chord chord = mah_get_chord(root, chord_type, base_notes, chord_notes, NULL);

    printf("  ★ Your Spirit Chord: %s %s\n", buf, chord_type->name);
    printf("    Notes: ");
    for (int i = 0; i < chord.size; i++) {
        mah_write_note(chord.notes[i], buf2, MAH_DISP_LEN, NULL);
        printf("%s ", buf2);
    }
    printf("\n");
    printf("    Personality: You are %s.\n\n", vibe_from_chord(chord_type));

    // === YOUR SCALE ===
    const struct mah_scale_base *scale_type = ALL_SCALES[scale_idx];
    struct mah_note scale_notes[20];
    struct mah_scale scale = mah_get_scale(root, scale_type, scale_notes, MAH_ASCEND, NULL);

    printf("  ★ Your Life Scale: %s %s\n", buf, scale_type->name);
    printf("    Notes: ");
    for (int i = 0; i < scale.size; i++) {
        mah_write_note(scale.notes[i], buf2, MAH_DISP_LEN, NULL);
        printf("%s ", buf2);
    }
    printf("\n");
    printf("    Destiny: %s\n\n", destiny_from_scale(scale_type));

    // === YOUR KEY SIGNATURE ===
    struct mah_note key_note = { tone, acci, 0 };
    struct mah_key_sig key = mah_get_key_sig(key_note, MAH_MAJOR_KEY);
    printf("  ★ Your Key Signature: %d %s\n",
        key.size, key.alter >= 0 ? "sharp(s)" : "flat(s)");
    printf("    Verdict: %s\n\n", key_sig_roast(key.alter));

    // === YOUR INTERVAL OF DESTINY ===
    enum mah_quality qual = ALL_QUALITIES[quality_idx];
    enum mah_error err = MAH_ERROR_NONE;
    struct mah_note dest = mah_get_inter(root, (struct mah_interval){ interval_steps, qual }, &err);

    printf("  ★ Your Interval of Destiny: ");
    if (err == MAH_ERROR_NONE) {
        mah_write_note(dest, buf2, MAH_DISP_LEN, NULL);
        printf("%s → %s (a %d%s)\n", buf, buf2,
            interval_steps,
            qual == MAH_PERFECT ? "P" :
            qual == MAH_MAJOR ? "M" :
            qual == MAH_MINOR ? "m" :
            qual == MAH_AUGMENTED ? "A" :
            qual == MAH_DIMINISHED ? "d" : "?");
    } else {
        printf("FORBIDDEN INTERVAL (%s)\n", mah_get_error(err));
    }
    printf("    Today you are %s.\n\n", mood_from_quality(qual));

    // === SOULMATE ===
    struct mah_key_sig relative = mah_get_key_relative(&key);
    char soulmate_buf[MAH_DISP_LEN];
    mah_write_note(relative.key, soulmate_buf, MAH_DISP_LEN, NULL);
    printf("  ★ Your Musical Soulmate: %s %s\n",
        soulmate_buf,
        relative.type == MAH_MINOR_KEY ? "minor" : "major");
    printf("    (They complete your harmonic series.)\n\n");

    // === ENHARMONIC TWIN ===
    struct mah_note twin = { (tone + 1) % 7, acci - (tone == MAH_E || tone == MAH_B ? 1 : 2), octave };
    if (mah_is_enharmonic(root, twin)) {
        char twin_buf[MAH_DISP_LEN];
        mah_write_note(twin, twin_buf, MAH_DISP_LEN, NULL);
        printf("  ★ Your Enharmonic Twin: %s\n", twin_buf);
        printf("    Same person, different font.\n\n");
    } else {
        printf("  ★ Enharmonic Twin: You are unique. Nobody sounds like you.\n");
        printf("    (This is not necessarily a compliment.)\n\n");
    }

    // === FINAL WISDOM ===
    int wisdom_idx = h % 8;
    const char *wisdoms[] = {
        "Remember: every dissonance resolves... eventually.",
        "You are the tritone in someone's perfect cadence.",
        "Life is a fermata. Hold on as long as you need.",
        "Be the accidental someone didn't expect but secretly needed.",
        "Your rest notes matter more than your played notes.",
        "Modulate to a new key when life gets boring.",
        "Every cadence is just a fancy way of saying goodbye.",
        "The circle of fifths always brings you back home.",
    };
    printf("  ♪ Final Wisdom: %s\n\n", wisdoms[wisdom_idx]);

    return 0;
}
