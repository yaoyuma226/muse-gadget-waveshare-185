/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "muse_weather_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "muse_busdata.h"
#include "muse_weather_photo.h"

/* Big digits (clock, temperature): Montserrat 48 where the board enables
 * it (devices/sdkconfig.muse does), 28 elsewhere. */
#if LV_FONT_MONTSERRAT_48
#define FONT_BIG (&lv_font_montserrat_48)
#else
#define FONT_BIG (&lv_font_montserrat_28)
#endif

/* unscii 16 with Unifont's CJK glyphs behind it, same as muse_ui.c: a
 * Chinese condition label is only guaranteed at 16 px. English labels
 * use Montserrat 16 instead (muse_bus_zh() picks at build time). */
extern const lv_font_t muse_font_cjk_16;
static lv_font_t s_text_16;
#define FONT_TEXT_16 (&s_text_16)

static lv_obj_t *s_photo;
static lv_obj_t *s_clock_lbl;
static lv_obj_t *s_temp_lbl;
static lv_obj_t *s_deg_lbl;
static lv_obj_t *s_info_lbl;

static char s_last_clock[8] = "";
static char s_last_temp[16] = "";
static char s_last_info[64] = "";
static int s_last_group = -1;
static bool s_built;

/* Temperature over a light icon (mockup: deep navy on the white
 * cloud); white over the dark groups — muse_weather_group_dark(). */
#define TEMP_NAVY 0x0b3b68

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

/* A soft dark band: gradient from `opa` at one edge to transparent. */
static lv_obj_t *make_scrim(lv_obj_t *parent, int h, bool top)
{
    static lv_grad_dsc_t grad_top, grad_bottom;
    lv_grad_dsc_t *g = top ? &grad_top : &grad_bottom;
    memset(g, 0, sizeof(*g));
    g->dir = LV_GRAD_DIR_VER;
    g->stops_count = 2;
    g->stops[0].color = lv_color_black();
    g->stops[0].opa = top ? LV_OPA_60 : LV_OPA_0;
    g->stops[0].frac = 0;
    g->stops[1].color = lv_color_black();
    g->stops[1].opa = top ? LV_OPA_0 : LV_OPA_60;
    g->stops[1].frac = 255;

    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_PCT(100), h);
    lv_obj_align(o, top ? LV_ALIGN_TOP_MID : LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad(o, g, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

/* Condition name from the WMO code, in the gadget's system language. */
static const char *label_for_code(int code, bool zh)
{
    switch (code) {
        case 0: return zh ? "晴" : "Clear";
        case 1: return zh ? "晴" : "Mostly clear";
        case 2: return zh ? "多云" : "Partly cloudy";
        case 3: return zh ? "阴" : "Overcast";
        case 45: case 48: return zh ? "雾" : "Fog";
        case 51: case 53: case 55: return zh ? "小雨" : "Drizzle";
        case 56: case 57: case 66: case 67: return zh ? "冻雨" : "Freezing rain";
        case 61: return zh ? "小雨" : "Light rain";
        case 63: return zh ? "中雨" : "Rain";
        case 65: return zh ? "大雨" : "Heavy rain";
        case 71: return zh ? "小雪" : "Light snow";
        case 73: return zh ? "中雪" : "Snow";
        case 75: return zh ? "大雪" : "Heavy snow";
        case 77: return zh ? "雪" : "Snow grains";
        case 80: case 81: return zh ? "阵雨" : "Showers";
        case 82: return zh ? "暴雨" : "Violent showers";
        case 85: case 86: return zh ? "阵雪" : "Snow showers";
        case 95: case 96: case 99: return zh ? "雷雨" : "Thunderstorm";
        default: return zh ? "未知" : "—";
    }
}

void muse_weather_ui_build(lv_obj_t *tile)
{
    if (!tile || s_built) {
        return;
    }
    s_built = true;
    s_text_16 = lv_font_unscii_16;
    s_text_16.fallback = &muse_font_cjk_16;
    bool zh = muse_bus_zh();
    const lv_font_t *small = zh ? FONT_TEXT_16 : &lv_font_montserrat_16;

    lv_obj_set_style_bg_color(tile, lv_color_hex(0x1c2f4a), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    /* Background art fills most of the tile: it is drawn slightly
     * smaller than the tile and shifted up (in the tick), so the
     * elements hanging below the icon (flakes, drops, bolt) stay
     * clear of the condition line. Layout per the final mockup:
     * clock at the top (the one label that still gets a scrim), the
     * big temperature digits centred on the screen centre over the
     * icon, and one combined condition + high/low line below it. */
    s_photo = lv_image_create(tile);
    lv_obj_set_pos(s_photo, 0, 0);
    lv_obj_remove_flag(s_photo, LV_OBJ_FLAG_CLICKABLE);
    make_scrim(tile, 130, true);

    s_clock_lbl = make_label(tile, FONT_BIG);
    lv_label_set_text(s_clock_lbl, "--:--");
    lv_obj_align(s_clock_lbl, LV_ALIGN_TOP_MID, 0, 34);

    /* Digits only, anchored on the screen centre (both axes). The
     * degree sign is a separate smaller label riding at the digits'
     * top right (mockup-wx3) — it is NOT part of the centred string,
     * so the digits themselves sit dead centre. */
    s_temp_lbl = make_label(tile, FONT_BIG);
    lv_label_set_text(s_temp_lbl, "--");
    lv_obj_set_style_text_color(s_temp_lbl, lv_color_hex(TEMP_NAVY), 0);
    lv_obj_align(s_temp_lbl, LV_ALIGN_CENTER, 0, 0);

    s_deg_lbl = make_label(tile, &lv_font_montserrat_28);
    lv_label_set_text(s_deg_lbl, "°");
    lv_obj_set_style_text_color(s_deg_lbl, lv_color_hex(TEMP_NAVY), 0);

    s_info_lbl = make_label(tile, small);
    lv_obj_align(s_info_lbl, LV_ALIGN_CENTER, 0, 104);

    s_last_clock[0] = s_last_temp[0] = s_last_info[0] = '\0';
    s_last_group = -1;
}

void muse_weather_ui_tick(void)
{
    if (!s_built) {
        return;
    }
    time_t t = time(NULL);
    if (t > 1700000000) {   /* clock synced (SNTP on device, host in sim) */
        struct tm lt;
        localtime_r(&t, &lt);
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", lt.tm_hour, lt.tm_min);
        if (strcmp(buf, s_last_clock) != 0) {
            strcpy(s_last_clock, buf);
            lv_label_set_text(s_clock_lbl, buf);
        }
    }
    muse_bus_snapshot_t snap;
    muse_bus_snapshot(&snap);
    if (!snap.weather_valid) {
        return;
    }
    bool zh = muse_bus_zh();

    int group = muse_weather_group(snap.weather_code, snap.is_day);
    if (group != s_last_group) {
        const lv_image_dsc_t *photo = muse_weather_photo(group);
        if (photo) {
            s_last_group = group;
            lv_image_set_src(s_photo, photo);
            int w = lv_obj_get_width(lv_obj_get_parent(s_photo));
            if (w > 0 && photo->header.w > 0) {
                /* Placement tuned against the measured artwork
                 * geometry: the cloud body sits at 28-56% of the
                 * frame and the hanging elements (flakes, drops,
                 * bolt) reach ~73%. At 90% of the tile width with
                 * the top at +7.2%, the cloud spans roughly 32-58%
                 * of the screen, so the screen-centred temp digits
                 * sit inside the cloud's lower body (the cloud wraps
                 * around them, per the approved mockup) while the
                 * hanging elements end just clear of the condition
                 * line under the temperature. */
                int32_t scale = (int32_t)w * 256 / photo->header.w * 90 / 100;
                lv_image_set_scale(s_photo, scale);
                int disp = (int)((int64_t)photo->header.w * scale / 256);
                lv_obj_set_pos(s_photo, (w - disp) / 2, w * 72 / 1000);
            }
            lv_obj_move_background(s_photo);
            /* Temperature colour follows the artwork: navy over the
             * light icons, white over the dark ones. */
            lv_color_t tc = muse_weather_group_dark(group)
                ? lv_color_white() : lv_color_hex(TEMP_NAVY);
            lv_obj_set_style_text_color(s_temp_lbl, tc, 0);
            lv_obj_set_style_text_color(s_deg_lbl, tc, 0);
        }
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "%.0f", (double)snap.temp_c);
    if (strcmp(buf, s_last_temp) != 0) {
        strcpy(s_last_temp, buf);
        lv_label_set_text(s_temp_lbl, buf);
    }
    /* Degree sign as a superscript at the digits' top right; the
     * offset drops it about a third of the digit height down from
     * the top edge (mockup-wx3). Re-applied every tick so it tracks
     * digit-width changes ("7" -> "12" -> "-2"). */
    lv_obj_align_to(s_deg_lbl, s_temp_lbl, LV_ALIGN_OUT_TOP_RIGHT, 2, 14);
    const char *cond = label_for_code(snap.weather_code, zh);
    if (zh) {
        snprintf(buf, sizeof(buf), "%s · 高 %.0f° 低 %.0f°", cond,
                 (double)snap.high_c, (double)snap.low_c);
    } else {
        snprintf(buf, sizeof(buf), "%s · H %.0f° L %.0f°", cond,
                 (double)snap.high_c, (double)snap.low_c);
    }
    if (strcmp(buf, s_last_info) != 0) {
        strcpy(s_last_info, buf);
        lv_label_set_text(s_info_lbl, buf);
    }
}
