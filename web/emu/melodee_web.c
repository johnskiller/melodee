/* SPDX-License-Identifier: GPL-3.0-only
 * Melodee in the browser: the firmware's sources, as the host tests build them (tests/hostsim.c: libc,
 * engines, voices, sequencer; then the UI, main.c, storage and projects, in src/melodee.c's order),
 * compiled to WebAssembly and run in an AudioWorklet against a simulated FM-1. The design (the device
 * clock driven by the audio the worklet asks for, the exports below, the saved flash handed back before
 * boot) follows Felucca's web/emu/felucca_web.c (Hugelton Instruments, GPL-3.0), itself after X0X.
 * Not here: USB (MIDI, audio, the editor, updates), TRS MIDI, the serial console, the battery (full),
 * the crash / UBOOT paths, the power-on LED sweep (Melodee has none). Nothing of the firmware changes.
 *   web/emu/build.sh  ->  build/emu/{melodee.wasm, worklet.js, index.html} */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* the HAL bits the UI needs, as hal/fm1_input.h has them (hostsim.c's fm1_in keeps its two fields) */
#define FM1_NCOL 11u
/* the HAL bits the UI needs, as hal/fm1_input.h has them (hostsim.c's fm1_in keeps its two fields) */
#define FM1_NCOL 11u
#define FM1_IRQ_TARGET 0
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {      /* key id at (physical column, packed row bit), -1 = none */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
static uint8_t fm1_led[FM1_NCOL];
static uint8_t fm1_led_dim[3][FM1_NCOL];
static uint8_t fm1_led_dim_mask[3];       /* 0 = full, 1 = 1/2, 3 = 1/4, 7 = 1/8 duty; 0xff = dark */
static uint32_t fm1_icfg(void) { return 0; }
static void fm1_icfg_set(uint32_t f) { (void)f; }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void fm1_core1_stop(void) {}
/* hal/fm1_clock.h + the codec pins of hal/fm1_audio.h: the web clock never changes */
enum { FCLK_MULT_320 = 240u, FCLK_MULT_360 = 270u, FCLK_MULT_396 = 297u };
static int fm1_clock_set(uint32_t mult) { (void)mult; return 0; }
static uint32_t fm1_clock_mult(void) { return FCLK_MULT_320; }
static uint32_t fm1_clock_mhz_of(uint32_t mult) { return mult * 4u / 3u; }
static void fm1__pc_out(uint32_t bit, int v) { (void)bit; (void)v; }

#define MELODEE_FLASH 1
#define main hostsim_main
#include "../../tests/hostsim.c"
#undef main

/* ------------------------------------------------------------ the clock --- */
#define FM1_TICKS_PER_US 24u             /* TIMER4: 24 MHz */
static uint32_t web_ticks;
static uint32_t fm1_ticks(void) { return web_ticks; }

/* ------------------------------------------------------ audio (ALNK0) --- */
static uint32_t web_half;                /* the half the next render fills */
#define FM1_AUDIO_HALF 0x80u
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return web_half; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *buf, uint32_t words, void (*isr)(void), uint32_t prio)
{ (void)buf; (void)words; (void)isr; (void)prio; }
static void fm1_audio_stop(void) {}
void isr_alnk0(void) {}
#include "../../firmware/src/audio.c"

/* ------------------------------------------------------------ the panel --- */
/* the input: buttons bit b = label b (panel.c B_*, 0..13), keys bit k = key k (0 = F3 .. 26 = G5),
 * encoders by role (EN_SELECT .. EN_K4), MASTER 0..1023. Edges are collected here; the debounced
 * state and the holds are the page's job (worklet.js HOLD_MS) */
static uint32_t web_pressed, web_released, web_notes_pressed;
enum { FM1_NENC = 7 };
static int32_t web_enc_steps[FM1_NENC];
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t p = web_pressed;
    if (released)
        *released = web_released;
    web_pressed = web_released = 0;
    return p;
}
static uint32_t fm1_input_note_edges(void) { uint32_t n = web_notes_pressed; web_notes_pressed = 0; return n; }
static int32_t fm1_enc_take(uint32_t e) { int32_t s = web_enc_steps[e % FM1_NENC]; web_enc_steps[e % FM1_NENC] = 0; return s; }
/* the LED planes, as hal/fm1_input.h has them */
static void fm1_input_init(void) {}
static void fm1_input_tick(void) {}
static void fm1_wdt_feed(void) {}
/* ----------------------------------------------------------- the screen --- */
static uint16_t web_fb[240 * 240];       /* RGB565 as the panel gets it (high byte first) */
static uint32_t web_draws;
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t sw = (uint16_t)((c >> 8) | (c << 8));
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            web_fb[(y + j) * 240u + x + i] = sw;
    web_draws++;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            web_fb[(y + j) * 240u + x + i] = p[j * w + i];
    web_draws++;
}
static void lcd_sync(void) {}
static void lcd_power(uint32_t s)              /* MENU > SCREEN OFF (screen.c): off, the page dark */
{
    if (!s) {
        memset(web_fb, 0, sizeof web_fb);
        web_draws++;
    }
}
static void lcd_wake_now(void) {}
static void lcd_init(void) {}

#include "../../firmware/src/gfx.c"
#include "../../firmware/src/panel.c"
#include "../../firmware/src/ui.c"
#include "../../firmware/src/icons.c"
#include "../../firmware/src/ui_graph.c"
#include "../../firmware/src/ui_draw.c"
#include "../../firmware/src/ui_menu.c"
#include "../../firmware/src/ui_input.c"
#include "../../firmware/src/ui_layer.c"
/* ------------------------------------------------------------ the flash --- */
static uint32_t web_flash_writes;
static uint8_t nor[0x100000], flash_ok = 1;  /* the SPI NOR (erased: 0xFF) */
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off >= sizeof nor || n > sizeof nor - off)
        return -1;
    memcpy(dst, nor + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if (off >= sizeof nor || 4096u > sizeof nor - off)
        return -8;
    memset(nor + (off & ~4095u), 0xFF, 4096);
    web_flash_writes++;
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)       /* NOR: a write only clears bits */
{
    const uint8_t *s = src;
    uint32_t i;
    if (off >= sizeof nor || n > sizeof nor - off)
        return -8;
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    web_flash_writes++;
    return 0;
}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0x856014u; }             /* the FM-1's part (project.c) */
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
#define MELODEE_SLICE 0
#include "../../firmware/src/storage.c"
#include "../../firmware/src/upreset.c"
#include "../../firmware/src/project.c"
#include "../../firmware/src/cz_store.c"
#include "../../firmware/src/fm6_store.c"

/* ------------------------------------------------- main.c and its HAL --- */
typedef struct { uint32_t magic, count, vec, pc, emu, dbg, rets; } fm1_crash_t;
static struct { uint8_t p3_rst, wdt_con; uint32_t rst_src; } fm1_boot;
uint32_t _data_start[1], _data_end[1], _data_load[1], _bss_start[1], _bss_end[1];
/* the resource arena: real sizes, as the hardware linker script provides */
uint8_t _resource_data_start[512], _resource_data_end[1];
uint8_t _resource_retained_start[1024], _resource_retained_end[1];
uint8_t _resource_low_start[5120], _resource_low_end[1];
uint8_t _resource_start[40960], _resource_end[1];
uint8_t _resource_cache_start[1], _resource_cache_end[1];
uint32_t _pool_start[1], _pool_end[1], _rt_start[1], _rt_end[1], _rt_load[1];
static uint32_t web_master_adc = 700;   /* MASTER, 0..1023 as the ADC reads it */
enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
static int32_t fm1_adc_read(uint32_t ch) { return ch == FM1_ADC_MASTER ? (int32_t)web_master_adc : 620; }
static void fm1_adc_init(void) {}
static void fm1_timer5_ack(void) {}
static void fm1_timer5_start(void (*isr)(void), uint32_t prio) { (void)isr; (void)prio; }
void isr_timer5(void) {}
static void fm1_reboot(void) {}
static void fm1_enter_uboot(void) {}
static void fm1_guard_lock_top(void) {}
static void fm1_irq_enable_all(void) {}
static void fm1_irq_init(void) {}
static void fm1_time_init(void) {}
static void fm1_reset_reason(void) {}
static void fm1_wdt_arm(uint32_t v) { (void)v; }
static void fm1_mailbox_clear(void) {}
static void fm1_guard_enable(uint32_t m) { (void)m; }
enum { FM1_GUARD_STACK = 1, FM1_GUARD_WRITE = 2, FM1_GUARD_BUS = 4, FM1_GUARD_PC = 8 };
#define usb_poll() ((void)0)
#define usb_start() ((void)0)
#define usb_retry(ms) ((void)(ms))
#define usb_detach() ((void)0)
#include "../../firmware/src/main.c"
#undef usb_poll
#undef usb_start
#undef usb_retry
#undef usb_detach

/* --------------------------------------------------------- the device --- */
static uint32_t web_booted, web_boot_ms, web_last_frame;

/* main.c fm1_main's boot, up to the main loop (no USB, no UART, no panel setup hold) */
static void web_power_on(void)
{
    uint32_t *d;
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    persist_boot();
    settings_init();
    lcd_init();
    lcd_fill(0, 0, 240, 240, T_BG);
    draw_text_box(0, 94, 240, &AF_L, "MELODEE", T_THEME, 1);
    draw_text_box(0, 134, 240, &AF_S, "MULTI-ENGINE SYNTH", T_MID, 1);
    if (melodee_dbg.magic != DBG_MAGIC) {
        memset(&melodee_dbg, 0, sizeof melodee_dbg);
        melodee_dbg.magic = DBG_MAGIC;
    }
    melodee_dbg.boots++;
    melodee_dbg.in_audio = 0;
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    melodee_init();
    audio_init();
    timer5_start();
    fm1_guard_lock_top();
    glo_restore();
    project_boot();
    web_boot_ms = fm1_ms + 430u;         /* main.c: 30 + 400 ms before the first frame */
    web_booted = 1;
}

/* one pass of main.c fm1_main's loop (what the browser has: no USB, editor, UBOOT) */
static void web_frame(void)
{
    int32_t b = fm1_adc_read(FM1_ADC_BATT);
    if (b > 0)
        song.batt_raw = song.batt_raw ? song.batt_raw + (b - song.batt_raw) / 32 : b;
    {
        int32_t a = fm1_adc_read(FM1_ADC_MASTER);       /* main.c: MASTER, an IIR over the ADC */
        static int32_t knob = 512 * 16;
        if (a >= 0) {
            uint32_t k10;
            knob += (a * 16 - knob) / 8;
            k10 = (uint32_t)(knob / 16);
            song.master_q12 = (k10 * k10) >> 8;
        }
    }
    melodee_dbg.ui_frames++;
    melodee_dbg.page = ui.page;
    melodee_dbg.home = ui.home;
    melodee_dbg.stage = 1;
    ui_input();
    glo_poll();
    settings_poll();
    melodee_dbg.stage = 2;
    ui_leds();
    ui_draw();
    melodee_dbg.stage = 9;
}

/* output ring, stereo float, filled by web_ms, drained by web_render */
#define RING 4096u
static float ring[RING * 2];
static uint32_t ring_w, ring_r, web_samples_due;

/* one millisecond of the device: the clock, the audio halves due by now, the main loop */
static void web_ms(void)
{
    fm1_ms++;
    web_ticks += 1000u * FM1_TICKS_PER_US;
    web_samples_due += FS;
    while (web_samples_due >= 1000u * HALF_FRAMES) {      /* an I2S half: the audio ISR renders it */
        uint32_t k;
        const int32_t *o = &abuf[web_half * HALF_WORDS];
        web_samples_due -= 1000u * HALF_FRAMES;
        fm1_alnk0_irq();
        for (k = 0; k < HALF_FRAMES; k++) {
            uint32_t i = (ring_w++ % RING) * 2u;
            ring[i] = (float)o[2u * k] / 8388608.0f;
            ring[i + 1] = (float)o[2u * k + 1u] / 8388608.0f;
        }
        web_half ^= 1u;
    }
    if (!web_booted || (int32_t)(fm1_ms - web_boot_ms) < 0)
        return;
    if (web_boot_ms) {                                    /* main.c: the splash goes, the loop starts */
        web_boot_ms = 0;
        lcd_fill(0, 0, 240, 240, T_BG);
        web_last_frame = fm1_ms - 15u;
    }
    if (fm1_ms - web_last_frame >= 15u) {                /* main.c: a frame, then ui_input until 15 ms are over */
        web_last_frame = fm1_ms;
        web_frame();
    } else {
        ui_input();                                       /* main.c's wait loop */
    }
}

#define EXPORT __attribute__((used, visibility("default")))
#undef __attribute__

static float out_l[1024], out_r[1024];
EXPORT float *web_out_l(void) { return out_l; }
EXPORT float *web_out_r(void) { return out_r; }
/* n frames (<= 1024) into out_l / out_r: the device runs until they exist */
EXPORT void web_render(uint32_t n)
{
    uint32_t k;
    if (n > 1024u)
        n = 1024u;
    while (ring_w - ring_r < n)
        web_ms();
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_r++ % RING) * 2u;
        out_l[k] = ring[i];
        out_r[k] = ring[i + 1];
    }
}

/* the flash: erased, then the page's saved sectors (web_nor), then boot */
EXPORT uint8_t *web_nor(void) { return nor; }
EXPORT uint32_t web_nor_size(void) { return sizeof nor; }
EXPORT void web_nor_erase(void) { memset(nor, 0xFF, sizeof nor); }
EXPORT uint32_t web_flash_writes_count(void) { return web_flash_writes; }
EXPORT void web_boot(void)
{
    if (!web_booted)
        web_power_on();
}

/* input, by the printed labels (panel.c's tables map them to the matrix) */
static uint32_t web_btn_labels, web_key_mask;
EXPORT void web_buttons(uint32_t labels)
{
    uint32_t m = 0, b, old = fm1_in.buttons;
    web_btn_labels = labels;
    for (b = 0; b < NB; b++)
        if ((labels >> b) & 1u)
            m |= 1u << panel.btn[b];
    web_pressed |= m & ~old;
    web_released |= old & ~m;
    fm1_in.buttons = m;
}
EXPORT void web_keys(uint32_t mask)
{
    uint32_t old = fm1_in.notes;
    mask &= (1u << 27) - 1u;
    web_key_mask = mask;
    web_notes_pressed |= mask & ~old;
    fm1_in.notes = mask;
}
EXPORT void web_enc(uint32_t role, int32_t n)
{
    if (role < FM1_NENC)
        web_enc_steps[panel.enc[role] % FM1_NENC] += n * panel.dir[role];
}
EXPORT void web_master(uint32_t v) { web_master_adc = v > 1023u ? 1023u : v; }

/* the LEDs, by id: buttons bit b = label b (0..13); keys bit k = key k (0..26). Three planes:
 * bright (fm1_led), dim (fm1_led_dim[0], the keys' LIGHTS duty | [1] the idle buttons), and
 * breath (fm1_led_dim[2], swept full .. 1/8 .. dark over a second; the page animates it) */
static uint32_t led_on(const uint8_t *l, uint32_t id)
{
    uint32_t c, r;
    for (c = 0; c < FM1_NCOL; c++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][c] == (int8_t)id)
                return (l[c] >> r) & 1u;
    return 0;
}
static uint32_t led_buttons(const uint8_t *l)
{
    uint32_t b, m = 0;
    for (b = 0; b < NB; b++)
        m |= led_on(l, panel.btn[b]) << b;
    return m;
}
static uint32_t led_keys(const uint8_t *l)
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++)
        m |= led_on(l, 14u + k) << k;
    return m;
}
EXPORT uint32_t web_lit_buttons(void) { return led_buttons(fm1_led); }
EXPORT uint32_t web_lit_keys(void) { return led_keys(fm1_led); }
EXPORT uint32_t web_dim_buttons(void) { return led_buttons(fm1_led_dim[1]); }
EXPORT uint32_t web_dim_keys(void) { return led_keys(fm1_led_dim[0]); }
EXPORT uint32_t web_breath_buttons(void) { return led_buttons(fm1_led_dim[2]); }
EXPORT uint32_t web_breath_keys(void) { return led_keys(fm1_led_dim[2]); }
EXPORT uint32_t web_dim_level(void) { return fm1_led_dim_mask[2]; }   /* the breath's current step */

EXPORT uint16_t *web_screen(void) { return web_fb; }
EXPORT uint32_t web_screen_draws(void) { return web_draws; }
EXPORT uint32_t web_dbg_halves(void) { return audio_halves; }
EXPORT int32_t *web_abuf(void) { return abuf; }
EXPORT uint32_t web_dbg_track(uint32_t i) { return ((uint32_t)trk[i].engine << 24) | ((uint32_t)trk[i].eng_req << 16) | (uint32_t)(trk[i].xf_on ? 0x8000u : 0) | (uint32_t)(trk[i].p[P_MUTE] ? 0x4000u : 0) | (uint32_t)(trk[i].p[P_VOICE] & 0xFF); }
EXPORT uint32_t web_now_ms(void) { return fm1_ms; }
EXPORT uint32_t web_playing(void) { return song.playing; }
EXPORT uint32_t web_sample_rate(void) { return FS; }
