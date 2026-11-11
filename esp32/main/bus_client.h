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

#include "muse_busdata.h"

/*
 * Poller for the bus-notifier backend (see bus-notifier/backend/ in the
 * project notes): GETs the backend JSON every 60 s while the morning
 * window is active (every 5 min outside it, so a manual swipe still
 * shows fresh weather), and keeps the latest snapshot for the UI.
 *
 * Backend URL: NVS key "bus_url", else CONFIG_BUS_BACKEND_URL.
 * Test mode (forces the window + rotation at any hour): NVS key
 * "bus_test" = "1", else CONFIG_BUS_TEST_MODE.
 * Rotation dwell: NVS key "bus_rotate_ms", else CONFIG_BUS_ROTATE_MS.
 *
 * No NJ Transit credentials ever reach the device; the backend holds
 * them and serves pre-digested arrivals.
 */

void bus_client_start(void);

/* Copies the latest snapshot out; safe from any task. */
void bus_client_snapshot(muse_bus_snapshot_t *out);

bool bus_client_test_mode(void);
int bus_client_rotate_ms(void);
/* UI language for the tiles: NVS key "bus_lang" ("zh"/"en"), else
 * CONFIG_BUS_LANG_ZH. True = Chinese. */
bool bus_client_zh(void);
