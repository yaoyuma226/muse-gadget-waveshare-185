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
 * The bus tile: next bus to New York from stop 20451. Route large,
 * countdown largest, arrival time under it, the following bus in 16 px
 * below. Runs entirely in the LVGL task; data comes from muse_busdata.h
 * and the countdown is computed locally from arrival_epoch, so it ticks
 * every second between the backend's 60 s polls.
 */

void muse_bus_ui_build(lv_obj_t *tile);

/* Call periodically from the LVGL task; updates the countdown on change. */
void muse_bus_ui_tick(void);
