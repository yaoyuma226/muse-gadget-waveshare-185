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
#include <stdint.h>

/*
 * Weather + bus data for the morning tiles (weather home page, bus page).
 *
 * The UI polls this snapshot; the gadget app (main/bus_client.c, bridged in
 * main/muse_glue.c) fills it from the small LAN backend's JSON. The backend
 * is the only place NJ Transit credentials ever live — the firmware just
 * reads pre-digested arrivals. The simulator stubs this with fake data.
 * All calls are safe from any task; the snapshot is copied out.
 */

#define MUSE_BUS_MAX 2

typedef struct {
    char route[8];          /* "159R", "158" */
    int64_t arrival_epoch;  /* local countdown is computed from this */
    char arrival[8];        /* "8:23", as served by the backend */
} muse_bus_entry_t;

typedef struct {
    bool valid;             /* at least one successful fetch */
    bool mock;              /* bus data is simulated (pre NJ Transit approval) */
    int64_t updated_epoch;  /* when the backend generated the data */
    int bus_count;          /* 0..MUSE_BUS_MAX */
    muse_bus_entry_t buses[MUSE_BUS_MAX];
    bool weather_valid;
    int weather_code;       /* WMO code; -1 = unknown */
    float temp_c;           /* always degrees C */
    float high_c;           /* today's high / low, degrees C */
    float low_c;
    bool is_day;
} muse_bus_snapshot_t;

void muse_bus_snapshot(muse_bus_snapshot_t *out);

/*
 * UI language for the morning tiles, following the gadget's system
 * language: true = Chinese, false = English. The firmware has no global
 * locale setting, so this is its own config (NVS key "bus_lang" =
 * "zh"/"en", else CONFIG_BUS_LANG_ZH; default Chinese, Terry's system
 * language). Condition names are mapped from the WMO code on-device, in
 * this language — the backend sends no text.
 */
bool muse_bus_zh(void);

/* Forces the morning window + rotation regardless of the clock (NVS key
 * "bus_test" or CONFIG_BUS_TEST_MODE), so the tiles can be tried at night. */
bool muse_bus_test_mode(void);

/* Auto-rotation dwell per tile, ms (NVS key "bus_rotate_ms" or
 * CONFIG_BUS_ROTATE_MS; default 2000). */
int muse_bus_rotate_ms(void);
