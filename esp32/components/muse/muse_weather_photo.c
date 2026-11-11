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
 * (muse_bg_assets.c). Terry approved the full smooth matte set, so
 * every group has its own render now; clear and partly cloudy split
 * into day/night variants by the caller's is_day flag.
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

#include "muse_bg_assets.h"

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

static const lv_image_dsc_t *const GROUP_IMAGES[GRP_COUNT] = {
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

const lv_image_dsc_t *muse_weather_photo(int group)
{
    if (group < 0 || group >= GRP_COUNT) {
        group = GRP_OVERCAST;
    }
    return GROUP_IMAGES[group];
}
