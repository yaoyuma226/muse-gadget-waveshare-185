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
 * Background photo for the weather tile, grouped by WMO weather code:
 * clear day / clear night / cloudy / fog / rain / snow / storm.
 *
 * PLACEHOLDER for now: each group's image is generated procedurally
 * (soft sky gradient, glowing sun or moon, layered soft clouds, rain
 * streaks / snow specks) into a 360x360 RGB565 buffer. Terry will source
 * real photographs per group; swapping them in means replacing the
 * generator behind muse_weather_photo() — e.g. embedded JPEG assets
 * decoded once into the same buffer, or a backend-served image fetched
 * with the image_fetch.c pattern. The weather UI only ever calls the
 * two functions below, so nothing else changes.
 */

/* Condition group for a WMO code (Open-Meteo), 0..6 as listed above. */
int muse_weather_group(int weather_code, bool is_day);

/* LVGL image for the group, generated on first use per group and cached
 * (one buffer, regenerated when the group changes). NULL on alloc
 * failure — the UI then keeps its plain background. LVGL task only. */
const lv_image_dsc_t *muse_weather_photo(int group);
