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

#pragma once

#include <stdbool.h>

#include "lvgl.h"

/*
 * Background art for the weather tile, grouped by WMO weather code
 * (Open-Meteo). Thirteen groups, one render each:
 *
 *   0 clear day, 1 clear night, 2 partly day, 3 partly night,
 *   4 overcast, 5 fog, 6 light rain, 7 rain, 8 heavy rain, 9 sleet,
 *   10 snow, 11 storm, 12 hail
 *
 * Terry's final design: simple and clean, the visual doing the work —
 * a condition-coloured gradient with ONE big smooth matte icon (flat
 * silhouette, a few large rounded shapes, soft gradient shading, no
 * gloss), the temperature drawn centred over the icon. Each image is
 * a pre-rendered 320x320 RGB565 asset (muse_bg_img_<name>.c; the UI
 * scales it to the tile width), generated from the artwork in
 * ~/workspace/bus-notifier/assets by esp32/tools/gen_bg_assets.py.
 * 320 not 360: 13 images at 360x360 would be 3.21 MiB of the 4 MiB
 * app slot, leaving too little for the rest of the firmware.
 */

/* Condition group for a WMO code (Open-Meteo), 0..12 as listed above.
 * is_day picks the day/night variant for clear and partly cloudy. */
int muse_weather_group(int weather_code, bool is_day);

/* True for groups whose artwork is dark (storm, hail, heavy rain,
 * clear night, partly night): the weather UI draws the big
 * temperature in white there, dark navy over the light icons
 * otherwise. */
bool muse_weather_group_dark(int group);

/* LVGL image for the group (a flash-resident descriptor, always
 * valid). LVGL task only. */
const lv_image_dsc_t *muse_weather_photo(int group);
