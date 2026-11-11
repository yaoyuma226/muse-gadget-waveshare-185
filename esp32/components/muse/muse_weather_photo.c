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
 * Procedural placeholder "photos" — see muse_weather_photo.h for how
 * real photographs replace these. The scenes are painted per pixel with
 * soft falloffs (no hard icon edges): a vertical sky gradient, a sun or
 * moon disc with a radial glow, a few big soft cloud ellipses, and
 * group-specific rain streaks or snow specks from a fixed hash, so the
 * frame is stable between redraws.
 */

#include "muse_weather_photo.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"

#define PHOTO_W 360
#define PHOTO_H 360

enum {
    GRP_CLEAR_DAY = 0,
    GRP_CLEAR_NIGHT,
    GRP_CLOUD,
    GRP_FOG,
    GRP_RAIN,
    GRP_SNOW,
    GRP_STORM,
    GRP_COUNT,
};

typedef struct {
    uint8_t top[3], horizon[3];   /* sky gradient, RGB */
    uint8_t cloud[3];             /* cloud body colour */
    float cloud_dark;             /* 0..1 darkening towards cloud bottoms */
    int sun_x, sun_y;             /* glow centre; -1 = none */
    uint8_t sun[3];
    float sun_r, glow_r;
} scene_t;

static const scene_t SCENES[GRP_COUNT] = {
    /* clear day */  {{47, 111, 208}, {191, 227, 255}, {255, 255, 255}, 0.15f, 252, 84, {255, 207, 77}, 34, 150},
    /* clear night */{{9, 14, 48}, {58, 82, 122}, {148, 160, 190}, 0.35f, 252, 84, {226, 222, 255}, 26, 90},
    /* cloudy */     {{84, 116, 163}, {196, 209, 224}, {232, 236, 244}, 0.25f, 252, 76, {255, 214, 110}, 30, 110},
    /* fog */        {{116, 128, 142}, {196, 201, 206}, {206, 210, 214}, 0.12f, -1, 0, {0, 0, 0}, 0, 0},
    /* rain */       {{54, 70, 94}, {126, 146, 168}, {168, 180, 196}, 0.35f, -1, 0, {0, 0, 0}, 0, 0},
    /* snow */       {{116, 140, 178}, {226, 234, 244}, {240, 244, 250}, 0.15f, -1, 0, {0, 0, 0}, 0, 0},
    /* storm */      {{30, 38, 58}, {92, 102, 124}, {120, 128, 148}, 0.45f, -1, 0, {0, 0, 0}, 0, 0},
};

/* Cloud ellipses: centre x/y, radii, brightness offset. Shared layout,
 * tinted per scene. Centres sit in the upper half like a morning sky. */
static const struct { float x, y, rx, ry, shade; } CLOUDS[] = {
    {86, 118, 96, 34, 0.0f},
    {168, 96, 110, 40, 0.05f},
    {262, 122, 104, 36, -0.03f},
    {330, 92, 76, 28, 0.04f},
    {30, 88, 70, 26, 0.02f},
};
#define CLOUD_COUNT (sizeof(CLOUDS) / sizeof(CLOUDS[0]))

static uint16_t *s_buf;
static lv_image_dsc_t s_dsc;
static int s_group = -1;

int muse_weather_group(int weather_code, bool is_day)
{
    switch (weather_code) {
        case 0:
        case 1:
            return is_day ? GRP_CLEAR_DAY : GRP_CLEAR_NIGHT;
        case 2:
        case 3:
            return GRP_CLOUD;
        case 45:
        case 48:
            return GRP_FOG;
        case 71: case 73: case 75: case 77: case 85: case 86:
            return GRP_SNOW;
        case 95: case 96: case 99:
            return GRP_STORM;
        case 51: case 53: case 55: case 56: case 57:
        case 61: case 63: case 65: case 66: case 67:
        case 80: case 81: case 82:
            return GRP_RAIN;
        default:
            return GRP_CLOUD;
    }
}

static float clamp01(float v)
{
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

/* Deterministic 0..1 hash of a pixel, for rain/snow texture. */
static float hash2(int x, int y)
{
    uint32_t h = (uint32_t)(x * 374761393 + y * 668265263);
    h = (h ^ (h >> 13)) * 1274126177;
    return (float)((h ^ (h >> 16)) & 0xffffff) / (float)0x1000000;
}

static void render(const scene_t *s, int group)
{
    for (int y = 0; y < PHOTO_H; y++) {
        float v = (float)y / (PHOTO_H - 1);
        for (int x = 0; x < PHOTO_W; x++) {
            float r = s->top[0] + (s->horizon[0] - s->top[0]) * v;
            float g = s->top[1] + (s->horizon[1] - s->top[1]) * v;
            float b = s->top[2] + (s->horizon[2] - s->top[2]) * v;

            /* Sun / moon glow, then the disc itself. */
            if (s->sun_x >= 0) {
                float dx = (float)(x - s->sun_x), dy = (float)(y - s->sun_y);
                float d = sqrtf(dx * dx + dy * dy);
                float glow = clamp01(1.0f - d / s->glow_r);
                glow = glow * glow * 0.55f;
                r += (s->sun[0] - r) * glow;
                g += (s->sun[1] - g) * glow;
                b += (s->sun[2] - b) * glow;
                if (d < s->sun_r) {
                    float a = 0.92f;
                    r += (s->sun[0] - r) * a;
                    g += (s->sun[1] - g) * a;
                    b += (s->sun[2] - b) * a;
                }
            }

            /* Clouds: soft ellipses, darker underneath. Clear skies get
             * only a whisper of the two smallest ones. */
            for (size_t i = 0; i < CLOUD_COUNT; i++) {
                if ((group == GRP_CLEAR_DAY || group == GRP_CLEAR_NIGHT) && i > 1) {
                    continue;
                }
                float ex = (x - CLOUDS[i].x) / CLOUDS[i].rx;
                float ey = (y - CLOUDS[i].y) / CLOUDS[i].ry;
                float e = ex * ex + ey * ey;
                if (e >= 1.0f) {
                    continue;
                }
                float a = clamp01(1.0f - e);
                a = a * a * (3 - 2 * a);   /* smoothstep edge */
                if (group == GRP_CLEAR_DAY || group == GRP_CLEAR_NIGHT) {
                    a *= 0.5f;
                }
                float dark = 1.0f - s->cloud_dark * clamp01(ey * 0.5f + 0.5f);
                float cr = s->cloud[0] * (dark + CLOUDS[i].shade);
                float cg = s->cloud[1] * (dark + CLOUDS[i].shade);
                float cb = s->cloud[2] * (dark + CLOUDS[i].shade);
                r += (cr - r) * a;
                g += (cg - g) * a;
                b += (cb - b) * a;
            }

            if (group == GRP_FOG) {
                /* Soft horizontal bands. */
                float band = 0.5f + 0.5f * sinf((float)y * 0.055f);
                float a = band * 0.10f * clamp01((float)y / PHOTO_H);
                r += (235 - r) * a;
                g += (238 - g) * a;
                b += (240 - b) * a;
            } else if (group == GRP_RAIN || group == GRP_STORM) {
                /* Short diagonal streaks: bright pixels along x+y lines. */
                float h = hash2((x + y / 3) / 14, y / 26);
                if (h > 0.86f && hash2(x / 14, (y + 13) / 26) > 0.4f) {
                    r += (190 - r) * 0.35f;
                    g += (210 - g) * 0.35f;
                    b += (235 - b) * 0.35f;
                }
            } else if (group == GRP_SNOW) {
                float h = hash2(x / 9, y / 9);
                if (h > 0.93f) {
                    r += (255 - r) * 0.6f;
                    g += (255 - g) * 0.6f;
                    b += (255 - b) * 0.6f;
                }
            }

            uint32_t ri = (uint32_t)(r < 0 ? 0 : r > 255 ? 255 : r);
            uint32_t gi = (uint32_t)(g < 0 ? 0 : g > 255 ? 255 : g);
            uint32_t bi = (uint32_t)(b < 0 ? 0 : b > 255 ? 255 : b);
            s_buf[y * PHOTO_W + x] = (uint16_t)(((ri >> 3) << 11) | ((gi >> 2) << 5) | (bi >> 3));
        }
    }
}

const lv_image_dsc_t *muse_weather_photo(int group)
{
    if (group < 0 || group >= GRP_COUNT) {
        group = GRP_CLOUD;
    }
    if (group == s_group && s_buf) {
        return &s_dsc;
    }
    if (!s_buf) {
        s_buf = heap_caps_malloc(PHOTO_W * PHOTO_H * sizeof(uint16_t),
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_buf) {
            s_buf = malloc(PHOTO_W * PHOTO_H * sizeof(uint16_t));
        }
        if (!s_buf) {
            return NULL;
        }
    }
    render(&SCENES[group], group);
    s_group = group;
    s_dsc = (lv_image_dsc_t){
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565,
            .w = PHOTO_W,
            .h = PHOTO_H,
            .stride = PHOTO_W * 2,
        },
        .data_size = PHOTO_W * PHOTO_H * 2,
        .data = (const uint8_t *)s_buf,
    };
    return &s_dsc;
}
