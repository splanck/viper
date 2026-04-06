// gen_sounds.c — Procedural WAV sound effect generator for sidescroller demo
// Compile: cc -o gen_sounds gen_sounds.c -lm
// Run:     ./gen_sounds
// Creates .wav files in a sounds/ subdirectory next to this file.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SAMPLE_RATE 22050
#define PI 3.14159265358979323846

// Write a mono 16-bit PCM WAV file
static void write_wav(const char *path, const int16_t *samples, int count)
{
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "Cannot open %s\n", path); return; }

    int data_size = count * 2;
    int file_size = 36 + data_size;

    // RIFF header
    fwrite("RIFF", 1, 4, f);
    uint32_t v = (uint32_t)file_size; fwrite(&v, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    // fmt chunk
    fwrite("fmt ", 1, 4, f);
    v = 16; fwrite(&v, 4, 1, f);           // chunk size
    uint16_t s = 1; fwrite(&s, 2, 1, f);   // PCM format
    s = 1; fwrite(&s, 2, 1, f);            // mono
    v = SAMPLE_RATE; fwrite(&v, 4, 1, f);  // sample rate
    v = SAMPLE_RATE * 2; fwrite(&v, 4, 1, f); // byte rate
    s = 2; fwrite(&s, 2, 1, f);            // block align
    s = 16; fwrite(&s, 2, 1, f);           // bits per sample

    // data chunk
    fwrite("data", 1, 4, f);
    v = (uint32_t)data_size; fwrite(&v, 4, 1, f);
    fwrite(samples, 2, (size_t)count, f);

    fclose(f);
    printf("  wrote %s (%d samples, %.2fs)\n", path, count, (double)count / SAMPLE_RATE);
}

static double clamp(double x) { return x < -1.0 ? -1.0 : x > 1.0 ? 1.0 : x; }

// Envelope: attack-decay-sustain-release (all in seconds)
static double envelope(double t, double dur, double atk, double dec, double sus, double rel)
{
    double rel_start = dur - rel;
    if (t < atk) return t / atk;
    if (t < atk + dec) return 1.0 - (1.0 - sus) * (t - atk) / dec;
    if (t < rel_start) return sus;
    if (t < dur) return sus * (1.0 - (t - rel_start) / rel);
    return 0.0;
}

// Simple sine
static double osc_sin(double phase) { return sin(phase * 2.0 * PI); }

// Square wave
static double osc_square(double phase)
{
    double p = fmod(phase, 1.0);
    return p < 0.5 ? 1.0 : -1.0;
}

// Noise
static double osc_noise(void) { return (double)rand() / RAND_MAX * 2.0 - 1.0; }

// Triangle wave
static double osc_tri(double phase)
{
    double p = fmod(phase, 1.0);
    if (p < 0.25) return p * 4.0;
    if (p < 0.75) return 2.0 - p * 4.0;
    return p * 4.0 - 4.0;
}

// ============================================================================

static void gen_jump(const char *path)
{
    // Rising square wave chirp
    double dur = 0.15;
    int n = (int)(dur * SAMPLE_RATE);
    int16_t *buf = calloc((size_t)n, sizeof(int16_t));
    for (int i = 0; i < n; i++) {
        double t = (double)i / SAMPLE_RATE;
        double freq = 300.0 + 600.0 * (t / dur); // 300 -> 900 Hz
        double phase = t * freq;
        double env = envelope(t, dur, 0.005, 0.02, 0.6, 0.05);
        buf[i] = (int16_t)(clamp(osc_square(phase) * env * 0.4) * 32000);
    }
    write_wav(path, buf, n);
    free(buf);
}

static void gen_shoot(const char *path)
{
    // Short descending noise + sine zap
    double dur = 0.12;
    int n = (int)(dur * SAMPLE_RATE);
    int16_t *buf = calloc((size_t)n, sizeof(int16_t));
    for (int i = 0; i < n; i++) {
        double t = (double)i / SAMPLE_RATE;
        double freq = 1200.0 - 800.0 * (t / dur);
        double phase = t * freq;
        double env = envelope(t, dur, 0.002, 0.03, 0.3, 0.04);
        double sig = osc_sin(phase) * 0.5 + osc_noise() * 0.2;
        buf[i] = (int16_t)(clamp(sig * env) * 32000);
    }
    write_wav(path, buf, n);
    free(buf);
}

static void gen_coin(const char *path)
{
    // Two-tone chime (classic coin sound)
    double dur = 0.2;
    int n = (int)(dur * SAMPLE_RATE);
    int16_t *buf = calloc((size_t)n, sizeof(int16_t));
    for (int i = 0; i < n; i++) {
        double t = (double)i / SAMPLE_RATE;
        double sig = 0.0;
        if (t < 0.1) {
            double env = envelope(t, 0.1, 0.002, 0.02, 0.5, 0.03);
            sig = osc_sin(t * 988.0) * env; // B5
        } else {
            double t2 = t - 0.1;
            double env = envelope(t2, 0.1, 0.002, 0.02, 0.4, 0.05);
            sig = osc_sin(t * 1319.0) * env; // E6
        }
        buf[i] = (int16_t)(clamp(sig * 0.5) * 32000);
    }
    write_wav(path, buf, n);
    free(buf);
}

static void gen_hurt(const char *path)
{
    // Low thud with noise
    double dur = 0.25;
    int n = (int)(dur * SAMPLE_RATE);
    int16_t *buf = calloc((size_t)n, sizeof(int16_t));
    for (int i = 0; i < n; i++) {
        double t = (double)i / SAMPLE_RATE;
        double freq = 120.0 - 60.0 * (t / dur);
        double phase = t * freq;
        double env = envelope(t, dur, 0.005, 0.05, 0.3, 0.1);
        double sig = osc_sin(phase) * 0.5 + osc_noise() * 0.3;
        buf[i] = (int16_t)(clamp(sig * env) * 32000);
    }
    write_wav(path, buf, n);
    free(buf);
}

static void gen_enemy_death(const char *path)
{
    // Quick pop/splat
    double dur = 0.18;
    int n = (int)(dur * SAMPLE_RATE);
    int16_t *buf = calloc((size_t)n, sizeof(int16_t));
    for (int i = 0; i < n; i++) {
        double t = (double)i / SAMPLE_RATE;
        double freq = 400.0 - 300.0 * (t / dur);
        double phase = t * freq;
        double env = envelope(t, dur, 0.003, 0.03, 0.2, 0.08);
        double sig = osc_square(phase) * 0.3 + osc_noise() * 0.4;
        buf[i] = (int16_t)(clamp(sig * env) * 32000);
    }
    write_wav(path, buf, n);
    free(buf);
}

static void gen_powerup(const char *path)
{
    // Ascending arpeggio — three quick notes
    double dur = 0.35;
    int n = (int)(dur * SAMPLE_RATE);
    int16_t *buf = calloc((size_t)n, sizeof(int16_t));
    double notes[] = {523.0, 659.0, 784.0}; // C5, E5, G5
    for (int i = 0; i < n; i++) {
        double t = (double)i / SAMPLE_RATE;
        int note_idx = (int)(t / (dur / 3.0));
        if (note_idx > 2) note_idx = 2;
        double note_t = t - note_idx * (dur / 3.0);
        double note_dur = dur / 3.0;
        double env = envelope(note_t, note_dur, 0.005, 0.02, 0.5, 0.03);
        double sig = osc_sin(t * notes[note_idx]) * 0.4 + osc_tri(t * notes[note_idx] * 2.0) * 0.15;
        buf[i] = (int16_t)(clamp(sig * env) * 32000);
    }
    write_wav(path, buf, n);
    free(buf);
}

static void gen_checkpoint(const char *path)
{
    // Cheerful two-note ding
    double dur = 0.3;
    int n = (int)(dur * SAMPLE_RATE);
    int16_t *buf = calloc((size_t)n, sizeof(int16_t));
    for (int i = 0; i < n; i++) {
        double t = (double)i / SAMPLE_RATE;
        double sig = 0.0;
        if (t < 0.12) {
            double env = envelope(t, 0.12, 0.003, 0.02, 0.5, 0.03);
            sig = osc_sin(t * 880.0) * env; // A5
        } else {
            double t2 = t - 0.12;
            double env = envelope(t2, 0.18, 0.003, 0.03, 0.4, 0.08);
            sig = osc_sin(t * 1175.0) * env; // D6
        }
        buf[i] = (int16_t)(clamp(sig * 0.5) * 32000);
    }
    write_wav(path, buf, n);
    free(buf);
}

static void gen_level_complete(const char *path)
{
    // Victory fanfare: C-E-G-C ascending with harmonics
    double dur = 0.8;
    int n = (int)(dur * SAMPLE_RATE);
    int16_t *buf = calloc((size_t)n, sizeof(int16_t));
    double notes[] = {523.0, 659.0, 784.0, 1047.0}; // C5-E5-G5-C6
    for (int i = 0; i < n; i++) {
        double t = (double)i / SAMPLE_RATE;
        int note_idx = (int)(t / 0.2);
        if (note_idx > 3) note_idx = 3;
        double note_t = t - note_idx * 0.2;
        double note_dur = (note_idx == 3) ? 0.2 : 0.2;
        double env = envelope(note_t, note_dur, 0.005, 0.03, 0.6, 0.05);
        double freq = notes[note_idx];
        double sig = osc_sin(t * freq) * 0.35 + osc_sin(t * freq * 2.0) * 0.1 +
                     osc_tri(t * freq * 3.0) * 0.05;
        buf[i] = (int16_t)(clamp(sig * env) * 32000);
    }
    write_wav(path, buf, n);
    free(buf);
}

static void gen_menu_select(const char *path)
{
    // Quick blip
    double dur = 0.06;
    int n = (int)(dur * SAMPLE_RATE);
    int16_t *buf = calloc((size_t)n, sizeof(int16_t));
    for (int i = 0; i < n; i++) {
        double t = (double)i / SAMPLE_RATE;
        double env = envelope(t, dur, 0.002, 0.01, 0.5, 0.02);
        double sig = osc_square(t * 660.0) * 0.3;
        buf[i] = (int16_t)(clamp(sig * env) * 32000);
    }
    write_wav(path, buf, n);
    free(buf);
}

static void gen_death(const char *path)
{
    // Descending tone with noise
    double dur = 0.6;
    int n = (int)(dur * SAMPLE_RATE);
    int16_t *buf = calloc((size_t)n, sizeof(int16_t));
    for (int i = 0; i < n; i++) {
        double t = (double)i / SAMPLE_RATE;
        double freq = 500.0 - 400.0 * (t / dur);
        double phase = t * freq;
        double env = envelope(t, dur, 0.01, 0.05, 0.5, 0.2);
        double sig = osc_square(phase) * 0.3 + osc_sin(phase * 0.5) * 0.2 +
                     osc_noise() * 0.1;
        buf[i] = (int16_t)(clamp(sig * env) * 32000);
    }
    write_wav(path, buf, n);
    free(buf);
}

static void gen_stomp(const char *path)
{
    // Bouncy pop for stomping enemies
    double dur = 0.1;
    int n = (int)(dur * SAMPLE_RATE);
    int16_t *buf = calloc((size_t)n, sizeof(int16_t));
    for (int i = 0; i < n; i++) {
        double t = (double)i / SAMPLE_RATE;
        double freq = 600.0 + 200.0 * (t / dur);
        double phase = t * freq;
        double env = envelope(t, dur, 0.002, 0.02, 0.4, 0.03);
        double sig = osc_sin(phase) * 0.5 + osc_square(phase * 0.5) * 0.15;
        buf[i] = (int16_t)(clamp(sig * env) * 32000);
    }
    write_wav(path, buf, n);
    free(buf);
}

static void gen_menu_move(const char *path)
{
    // Subtle tick for menu navigation
    double dur = 0.04;
    int n = (int)(dur * SAMPLE_RATE);
    int16_t *buf = calloc((size_t)n, sizeof(int16_t));
    for (int i = 0; i < n; i++) {
        double t = (double)i / SAMPLE_RATE;
        double env = envelope(t, dur, 0.001, 0.01, 0.3, 0.01);
        double sig = osc_sin(t * 440.0) * 0.3;
        buf[i] = (int16_t)(clamp(sig * env) * 32000);
    }
    write_wav(path, buf, n);
    free(buf);
}

int main(void)
{
    srand(42); // deterministic noise

    const char *dir = "sounds";
#ifdef _WIN32
    _mkdir(dir);
#else
    mkdir(dir, 0755);
#endif

    printf("Generating sidescroller sound effects...\n");
    gen_jump("sounds/jump.wav");
    gen_shoot("sounds/shoot.wav");
    gen_coin("sounds/coin.wav");
    gen_hurt("sounds/hurt.wav");
    gen_enemy_death("sounds/enemy_death.wav");
    gen_powerup("sounds/powerup.wav");
    gen_checkpoint("sounds/checkpoint.wav");
    gen_level_complete("sounds/level_complete.wav");
    gen_menu_select("sounds/menu_select.wav");
    gen_menu_move("sounds/menu_move.wav");
    gen_death("sounds/death.wav");
    gen_stomp("sounds/stomp.wav");
    printf("Done! 12 sound effects generated.\n");

    return 0;
}
