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

#include "time_sync.h"

#include <stdlib.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_mgr.h"

static const char *TAG = "time_sync";

/* America/New_York in POSIX form: EST5, DST from the second Sunday of
 * March to the first Sunday of November. */
#define GADGET_TZ "EST5EDT,M3.2.0/2,M11.1.0/2"

static void time_sync_task(void *arg)
{
    (void)arg;
    /* The timezone is useful even before the first sync (the morning
     * window check just stays closed until the epoch is sane). */
    setenv("TZ", GADGET_TZ, 1);
    tzset();

    while (!wifi_mgr_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) == ESP_OK) {
        time_t now = time(NULL);
        struct tm lt;
        localtime_r(&now, &lt);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M %Z", &lt);
        ESP_LOGI(TAG, "clock set: %s", buf);
    } else {
        ESP_LOGW(TAG, "SNTP sync timed out; will keep trying in background");
    }
    vTaskDelete(NULL);
}

void time_sync_start(void)
{
    static bool started;
    if (started) {
        return;
    }
    started = true;
    xTaskCreate(time_sync_task, "time_sync", 4096, NULL, 3, NULL);
}
