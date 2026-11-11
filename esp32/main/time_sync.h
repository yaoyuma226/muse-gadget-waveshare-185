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

/*
 * Wall-clock time for the morning tiles: sets the timezone to
 * America/New_York and starts SNTP once Wi-Fi is up. The firmware never
 * needed the time before the bus notifier; localtime_r() is only
 * meaningful after time_sync has run (callers check for a sane epoch).
 * Safe to call once at startup; the sync itself happens on its own task.
 */
void time_sync_start(void);
