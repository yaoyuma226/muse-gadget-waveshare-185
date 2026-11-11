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

#include "muse_bus_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "muse_bg_assets.h"
#include "muse_busdata.h"

#if LV_FONT_MONTSERRAT_48
#define FONT_BIG (&lv_font_montserrat_48)
#else
#define FONT_BIG (&lv_font_montserrat_28)
#endif

extern const lv_font_t muse_font_cjk_16;
static lv_font_t s_text_16;
#define FONT_TEXT_16 (&s_text_16)

/* White text on the tile's blue gradient (mockup-bus-v3: the bus art
 * shrank to a small round badge, the info column is the page now).
 * The route sits in a dark pill so it stays readable over the light
 * gradient at the top. */
#define COLOR_LIT 0xffffff
#define COLOR_DIM 0xc9dcee
#define COLOR_PILL 0x144a75
#define GRAD_TOP 0x6ebee4
#define GRAD_BOTTOM 0x176ebe
#define STALE_S 180   /* data older than this is shown dim, as last known */

static lv_obj_t *s_badge;
static bool s_badge_scaled;
static lv_obj_t *s_route_lbl;
static lv_obj_t *s_count_lbl;
static lv_obj_t *s_arrival_lbl;
static lv_obj_t *s_second_lbl;
static lv_obj_t *s_mock_lbl;

static char s_last_route[16] = "";
static char s_last_count[16] = "";
static char s_last_arrival[32] = "";
static char s_last_second[48] = "";
static bool s_last_mock;
static bool s_last_stale;
static bool s_built;

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

static void set_text(lv_obj_t *lbl, char *cache, const char *text)
{
    if (strcmp(cache, text) != 0) {
        strcpy(cache, text);
        lv_label_set_text(lbl, text);
    }
}

void muse_bus_ui_build(lv_obj_t *tile)
{
    if (!tile || s_built) {
        return;
    }
    s_built = true;
    s_text_16 = lv_font_unscii_16;
    s_text_16.fallback = &muse_font_cjk_16;

    lv_obj_set_style_bg_color(tile, lv_color_hex(GRAD_BOTTOM), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

    /* Plain blue vertical gradient behind everything (mockup-bus-v3),
     * drawn by LVGL — no full-screen image any more. Kept out of the
     * anchored layout with IGNORE_LAYOUT. */
    static lv_grad_dsc_t grad;
    memset(&grad, 0, sizeof(grad));
    grad.dir = LV_GRAD_DIR_VER;
    grad.stops_count = 2;
    grad.stops[0].color = lv_color_hex(GRAD_TOP);
    grad.stops[0].opa = LV_OPA_COVER;
    grad.stops[0].frac = 0;
    grad.stops[1].color = lv_color_hex(GRAD_BOTTOM);
    grad.stops[1].opa = LV_OPA_COVER;
    grad.stops[1].frac = 255;
    lv_obj_t *bg = lv_obj_create(tile);
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad(bg, &grad, 0);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(bg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* Small round bus badge at the top (its circular alpha and white
     * ring are baked into the asset). Scaled relative to the tile
     * width in the tick, once the layout has settled: 110 px on the
     * 360 px panel. */
    s_badge = lv_image_create(tile);
    lv_image_set_src(s_badge, &muse_bg_bus_badge);
    lv_obj_remove_flag(s_badge, LV_OBJ_FLAG_CLICKABLE);

    /* Small lines follow the system language: Chinese needs the 16 px
     * CJK font, English can use Montserrat 16. Language is a config, set
     * before the UI starts, so picking the fonts here is enough. */
    const lv_font_t *small = muse_bus_zh() ? FONT_TEXT_16 : &lv_font_montserrat_16;
    s_route_lbl = make_label(tile, &lv_font_montserrat_28, COLOR_LIT);
    lv_obj_set_style_bg_color(s_route_lbl, lv_color_hex(COLOR_PILL), 0);
    lv_obj_set_style_bg_opa(s_route_lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_route_lbl, 20, 0);
    lv_obj_set_style_pad_left(s_route_lbl, 16, 0);
    lv_obj_set_style_pad_right(s_route_lbl, 16, 0);
    lv_obj_set_style_pad_top(s_route_lbl, 4, 0);
    lv_obj_set_style_pad_bottom(s_route_lbl, 4, 0);
    lv_label_set_text(s_route_lbl, "BUS");
    s_count_lbl = make_label(tile, FONT_BIG, COLOR_LIT);
    lv_label_set_text(s_count_lbl, "--");
    s_arrival_lbl = make_label(tile, &lv_font_montserrat_20, COLOR_LIT);
    lv_label_set_text(s_arrival_lbl, "");
    s_second_lbl = make_label(tile, small, COLOR_DIM);
    lv_label_set_text(s_second_lbl, "");
    s_mock_lbl = make_label(tile, small, COLOR_DIM);
    lv_label_set_text(s_mock_lbl, "");

    /* Layout (mockup-bus-v4): no flex column. The countdown is
     * anchored dead on the screen centre; the badge + pill stack
     * above it via align_to (verified centred in the simulator).
     * The labels below the countdown do NOT chain align_to off it:
     * in this LVGL version a follower's position goes stale when
     * the base label's text changes width (the arrival/next lines
     * drifted right and clipped in testing). They anchor to the
     * screen centre independently, with fixed offsets sized from
     * the countdown's font height — deterministic for every text. */
    lv_obj_align(s_count_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_align(s_arrival_lbl, LV_ALIGN_CENTER, 0, 52);
    lv_obj_align(s_second_lbl, LV_ALIGN_CENTER, 0, 82);
    lv_obj_align(s_mock_lbl, LV_ALIGN_CENTER, 0, 108);
    lv_obj_align_to(s_route_lbl, s_count_lbl, LV_ALIGN_OUT_TOP_MID, 0, -10);
    lv_obj_align_to(s_badge, s_route_lbl, LV_ALIGN_OUT_TOP_MID, 0, -12);
}

void muse_bus_ui_tick(void)
{
    if (!s_built) {
        return;
    }
    if (!s_badge_scaled && s_badge) {
        int w = lv_obj_get_width(lv_obj_get_parent(s_badge));
        if (w > 0) {
            /* The asset is drawn at its design size on a 360 px
             * panel; scale it with the tile (412 px in the sim). */
            lv_image_set_scale(s_badge, (int32_t)w * 256 / 360);
            s_badge_scaled = true;
        }
    }
    muse_bus_snapshot_t snap;
    muse_bus_snapshot(&snap);
    time_t now = time(NULL);
    bool zh = muse_bus_zh();

    if (!snap.valid || snap.bus_count == 0) {
        set_text(s_route_lbl, s_last_route, "BUS");
        set_text(s_count_lbl, s_last_count, "--");
        set_text(s_arrival_lbl, s_last_arrival, "");
        set_text(s_second_lbl, s_last_second, zh ? "暂无班次" : "No buses");
        if (s_last_mock) {
            s_last_mock = false;
            lv_label_set_text(s_mock_lbl, "");
        }
        return;
    }

    const muse_bus_entry_t *b0 = &snap.buses[0];
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", b0->route);
    set_text(s_route_lbl, s_last_route, buf);

    int64_t left = b0->arrival_epoch - (int64_t)now;
    if (left <= 0) {
        set_text(s_count_lbl, s_last_count, zh ? "快到了" : "DUE");
    } else if (left < 60) {
        set_text(s_count_lbl, s_last_count, zh ? "<1 分钟" : "<1 min");
    } else if (zh) {
        snprintf(buf, sizeof(buf), "%d 分钟", (int)((left + 59) / 60));
        set_text(s_count_lbl, s_last_count, buf);
    } else {
        snprintf(buf, sizeof(buf), "%d min", (int)((left + 59) / 60));
        set_text(s_count_lbl, s_last_count, buf);
    }

    if (zh) {
        snprintf(buf, sizeof(buf), "%s 到", b0->arrival);
    } else {
        snprintf(buf, sizeof(buf), "Arrives %s · to New York", b0->arrival);
    }
    set_text(s_arrival_lbl, s_last_arrival, buf);

    if (snap.bus_count > 1) {
        const muse_bus_entry_t *b1 = &snap.buses[1];
        if (zh) {
            snprintf(buf, sizeof(buf), "下一班 %s · %s", b1->route, b1->arrival);
        } else {
            snprintf(buf, sizeof(buf), "Next %s · %s", b1->route, b1->arrival);
        }
    } else {
        buf[0] = '\0';
    }
    set_text(s_second_lbl, s_last_second, buf);

    bool stale = snap.updated_epoch > 0 && (int64_t)now - snap.updated_epoch > STALE_S;
    if (stale != s_last_stale) {
        s_last_stale = stale;
        uint32_t c = stale ? COLOR_DIM : COLOR_LIT;
        lv_obj_set_style_text_color(s_count_lbl, lv_color_hex(c), 0);
        lv_obj_set_style_text_color(s_arrival_lbl, lv_color_hex(c), 0);
    }
    if (snap.mock != s_last_mock) {
        s_last_mock = snap.mock;
        lv_label_set_text(s_mock_lbl, snap.mock ? (zh ? "模拟数据" : "Mock data") : "");
    }
}
