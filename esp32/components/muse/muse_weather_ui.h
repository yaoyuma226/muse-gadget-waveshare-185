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

#include "lvgl.h"

/*
 * The weather tile: home page of the morning rotation. A full-screen
 * photo for the current condition (muse_weather_photo.h) with the time,
 * temperature and condition overlaid in white over dark scrims. The
 * clock comes from the SNTP-synced local time; the condition name is
 * mapped from the backend's WMO code on-device, in the gadget's system
 * language (muse_bus_zh()). Runs entirely in the LVGL task.
 */

void muse_weather_ui_build(lv_obj_t *tile);

/* Call periodically from the LVGL task; updates clock + weather on change. */
void muse_weather_ui_tick(void);
