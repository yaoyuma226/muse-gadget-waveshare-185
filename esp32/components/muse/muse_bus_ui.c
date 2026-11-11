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

#include "muse_busdata.h"

#if LV_FONT_MONTSERRAT_48
#define FONT_BIG (&lv_font_montserrat_48)
#else
#define FONT_BIG (&lv_font_montserrat_28)
#endif

extern const lv_font_t muse_font_cjk_16;
static lv_font_t s_text_16;
#define FONT_TEXT_16 (&s_text_16)

#define COLOR_LIT 0xf2efff
#define COLOR_DIM 0x8b84a8
#define COLOR_ACCENT 0xa77dff
#define STALE_S 180   /* data older than this is shown dim, as last known */

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

    lv_obj_set_style_bg_color(tile, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tile, 2, 0);

    /* Small lines follow the system language: Chinese needs the 16 px
     * CJK font, English can use Montserrat 16. Language is a config, set
     * before the UI starts, so picking the fonts here is enough. */
    const lv_font_t *small = muse_bus_zh() ? FONT_TEXT_16 : &lv_font_montserrat_16;
    s_route_lbl = make_label(tile, &lv_font_montserrat_28, COLOR_ACCENT);
    lv_label_set_text(s_route_lbl, "BUS");
    s_count_lbl = make_label(tile, FONT_BIG, COLOR_LIT);
    lv_label_set_text(s_count_lbl, "--");
    s_arrival_lbl = make_label(tile, &lv_font_montserrat_20, COLOR_LIT);
    lv_label_set_text(s_arrival_lbl, "");
    s_second_lbl = make_label(tile, small, COLOR_DIM);
    lv_label_set_text(s_second_lbl, "");
    s_mock_lbl = make_label(tile, small, COLOR_DIM);
    lv_label_set_text(s_mock_lbl, "");
}

void muse_bus_ui_tick(void)
{
    if (!s_built) {
        return;
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
