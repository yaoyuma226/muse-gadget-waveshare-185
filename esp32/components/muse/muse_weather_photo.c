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

/*
 * Maps WMO weather codes (Open-Meteo) to per-condition background art
 * (muse_bg_assets.c). Every group has its own render; clear and partly
 * cloudy split into day/night variants by the caller's is_day flag.
 *
 *   code(s)              image
 *   0                    clear day / clear night
 *   1, 2                 partly day / partly night
 *   3                    overcast
 *   45, 48               fog
 *   51, 53, 55, 56, 57   light rain (drizzle, incl. freezing)
 *   61, 80               rain
 *   63, 65, 81, 82       heavy rain
 *   66, 67               sleet (freezing rain)
 *   71, 73, 75, 77,
 *   85, 86               snow
 *   95                   storm
 *   96, 99               hail (thunderstorm with hail)
 */

#include "muse_weather_photo.h"

#include <string.h>

#include "muse_bg_assets.h"
#include "src/misc/cache/instance/lv_image_cache.h"

/* The S3's ROM TJpgDec, as muse_video_avatar.c uses. LVGL's own copy
 * (LV_USE_TJPGD) must stay off on the device: its jd_prepare/jd_decomp
 * override the ROM symbols at link time with a different JDEC layout,
 * which breaks the avatar's decoder. The simulator has no ROM, so it
 * builds LVGL's copy; the API is the same apart from the integer types. */
#ifdef ESP_PLATFORM
#include "esp32s3/rom/tjpgd.h"
typedef UINT jd_size_t;
typedef BYTE jd_byte_t;
typedef UINT jd_ret_t;
#else
#include "src/libs/tjpgd/tjpgd.h"
typedef size_t jd_size_t;
typedef uint8_t jd_byte_t;
typedef int jd_ret_t;
#endif

#define PHOTO_SIZE 320
#define TJPGD_POOL_BYTES 4096

enum {
    GRP_CLEAR_DAY = 0,
    GRP_CLEAR_NIGHT,
    GRP_PARTLY_DAY,
    GRP_PARTLY_NIGHT,
    GRP_OVERCAST,
    GRP_FOG,
    GRP_LIGHT_RAIN,
    GRP_RAIN,
    GRP_HEAVY_RAIN,
    GRP_SLEET,
    GRP_SNOW,
    GRP_STORM,
    GRP_HAIL,
    GRP_COUNT,
};

static const muse_bg_jpeg_t *const GROUP_IMAGES[GRP_COUNT] = {
    &muse_bg_clear_day,     /* clear day */
    &muse_bg_clear_night,   /* clear night */
    &muse_bg_partly_day,    /* partly cloudy, day */
    &muse_bg_partly_night,  /* partly cloudy, night */
    &muse_bg_overcast,      /* overcast */
    &muse_bg_fog,           /* fog */
    &muse_bg_light_rain,    /* light rain */
    &muse_bg_rain,          /* rain */
    &muse_bg_heavy_rain,    /* heavy rain */
    &muse_bg_sleet,         /* sleet */
    &muse_bg_snow,          /* snow */
    &muse_bg_storm,         /* storm */
    &muse_bg_hail,          /* hail */
};

int muse_weather_group(int weather_code, bool is_day)
{
    switch (weather_code) {
        case 0:
            return is_day ? GRP_CLEAR_DAY : GRP_CLEAR_NIGHT;
        case 1:
        case 2:
            return is_day ? GRP_PARTLY_DAY : GRP_PARTLY_NIGHT;
        case 3:
            return GRP_OVERCAST;
        case 45:
        case 48:
            return GRP_FOG;
        case 51: case 53: case 55: case 56: case 57:
            return GRP_LIGHT_RAIN;
        case 61: case 80:
            return GRP_RAIN;
        case 63: case 65: case 81: case 82:
            return GRP_HEAVY_RAIN;
        case 66: case 67:
            return GRP_SLEET;
        case 71: case 73: case 75: case 77: case 85: case 86:
            return GRP_SNOW;
        case 95:
            return GRP_STORM;
        case 96: case 99:
            return GRP_HAIL;
        default:
            return GRP_OVERCAST;
    }
}

bool muse_weather_group_dark(int group)
{
    /* Dark artwork: the temperature renders in white. The dark set is
     * the storm family (storm, hail, heavy rain) and the two night
     * variants; everything else is light enough for the navy temp. */
    return group == GRP_CLEAR_NIGHT || group == GRP_PARTLY_NIGHT ||
           group == GRP_HEAVY_RAIN || group == GRP_STORM ||
           group == GRP_HAIL;
}

/* Two decode slots, used alternately: the image widget still points at
 * the previous slot while the next one is decoded, and LVGL caches by
 * descriptor pointer, so a slot is dropped from its cache before reuse. */
static lv_image_dsc_t s_slots[2];
static int s_next_slot;

typedef struct {
    const muse_bg_jpeg_t *jpg;
    size_t pos;
    uint16_t *out;
} decode_ctx_t;

static jd_size_t jpg_in(JDEC *jd, jd_byte_t *buf, jd_size_t n)
{
    decode_ctx_t *ctx = jd->device;
    size_t left = ctx->jpg->size - ctx->pos;
    if (n > left) {
        n = left;
    }
    if (buf) {
        memcpy(buf, ctx->jpg->data + ctx->pos, n);
    }
    ctx->pos += n;
    return n;
}

/* TJpgDec hands out RGB888 rectangles (JD_FORMAT 0); pack to RGB565. */
static jd_ret_t jpg_out(JDEC *jd, void *bitmap, JRECT *rect)
{
    decode_ctx_t *ctx = jd->device;
    const uint8_t *src = bitmap;
    for (int y = rect->top; y <= rect->bottom; y++) {
        uint16_t *dst = ctx->out + y * PHOTO_SIZE + rect->left;
        for (int x = rect->left; x <= rect->right; x++) {
            *dst++ = (uint16_t)(((src[0] & 0xf8) << 8) | ((src[1] & 0xfc) << 3) | (src[2] >> 3));
            src += 3;
        }
    }
    return 1;
}

const lv_image_dsc_t *muse_weather_photo(int group)
{
    if (group < 0 || group >= GRP_COUNT) {
        group = GRP_OVERCAST;
    }
    lv_image_dsc_t *dsc = &s_slots[s_next_slot];
    if (!dsc->data) {
        void *buf = lv_malloc(PHOTO_SIZE * PHOTO_SIZE * 2);
        if (!buf) {
            return NULL;
        }
        dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
        dsc->header.cf = LV_COLOR_FORMAT_RGB565;
        dsc->header.w = PHOTO_SIZE;
        dsc->header.h = PHOTO_SIZE;
        dsc->header.stride = PHOTO_SIZE * 2;
        dsc->data_size = PHOTO_SIZE * PHOTO_SIZE * 2;
        dsc->data = buf;
    } else {
        lv_image_cache_drop(dsc);
    }

    void *pool = lv_malloc(TJPGD_POOL_BYTES);
    if (!pool) {
        return NULL;
    }
    /* Static, not on the stack: this runs in the LVGL task. */
    static decode_ctx_t ctx;
    static JDEC jd;
    ctx = (decode_ctx_t){
        .jpg = GROUP_IMAGES[group],
        .out = (uint16_t *)dsc->data,
    };
    JRESULT res = jd_prepare(&jd, jpg_in, pool, TJPGD_POOL_BYTES, &ctx);
    if (res == JDR_OK && (jd.width != PHOTO_SIZE || jd.height != PHOTO_SIZE)) {
        res = JDR_FMT1;
    }
    if (res == JDR_OK) {
        res = jd_decomp(&jd, jpg_out, 0);
    }
    lv_free(pool);
    if (res != JDR_OK) {
        LV_LOG_WARN("weather photo %d: JPEG decode failed (%d)", group, (int)res);
        return NULL;
    }
    s_next_slot ^= 1;
    return dsc;
}
