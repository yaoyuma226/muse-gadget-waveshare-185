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

#include "bus_client.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "config_store.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "wifi_mgr.h"

static const char *TAG = "bus_client";

#define FETCH_MAX_BYTES (8 * 1024)
#define POLL_ACTIVE_MS 60000    /* morning window (or test mode) */
#define POLL_IDLE_MS 300000     /* outside it: weather stays fresh enough */

static SemaphoreHandle_t s_lock;
static muse_bus_snapshot_t s_snap;
static char s_url[192];

bool bus_client_test_mode(void)
{
    char v[8];
    if (config_get_str("bus_test", v, sizeof(v))) {
        return v[0] == '1';
    }
#if CONFIG_BUS_TEST_MODE
    return true;
#else
    return false;
#endif
}

bool bus_client_zh(void)
{
    char v[8];
    if (config_get_str("bus_lang", v, sizeof(v))) {
        if (strcmp(v, "en") == 0) {
            return false;
        }
        if (strcmp(v, "zh") == 0) {
            return true;
        }
    }
#if CONFIG_BUS_LANG_ZH
    return true;
#else
    return false;
#endif
}

int bus_client_rotate_ms(void)
{
    char v[16];
    if (config_get_str("bus_rotate_ms", v, sizeof(v))) {
        int ms = atoi(v);
        if (ms >= 500 && ms <= 30000) {
            return ms;
        }
    }
    return CONFIG_BUS_ROTATE_MS;
}

void bus_client_snapshot(muse_bus_snapshot_t *out)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    *out = s_snap;
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

/* Same window rule as the UI's rotation (muse_ui.c): weekday 08:00-12:00
 * local, or test mode. False until SNTP sets the clock. */
static bool window_active(void)
{
    if (bus_client_test_mode()) {
        return true;
    }
    time_t t = time(NULL);
    if (t < 1700000000) {
        return false;
    }
    struct tm lt;
    localtime_r(&t, &lt);
    return lt.tm_wday >= 1 && lt.tm_wday <= 5 && lt.tm_hour >= 8 && lt.tm_hour < 12;
}

static void copy_str(char *dst, size_t cap, const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(v) && v->valuestring) {
        strncpy(dst, v->valuestring, cap - 1);
        dst[cap - 1] = '\0';
    }
}

static bool parse_snapshot(const char *body, muse_bus_snapshot_t *out)
{
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    const cJSON *updated = cJSON_GetObjectItemCaseSensitive(root, "updated");
    if (cJSON_IsNumber(updated)) {
        out->updated_epoch = (int64_t)updated->valuedouble;
    }
    out->mock = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "mock"));

    const cJSON *buses = cJSON_GetObjectItemCaseSensitive(root, "buses");
    if (cJSON_IsArray(buses)) {
        int n = cJSON_GetArraySize(buses);
        if (n > MUSE_BUS_MAX) {
            n = MUSE_BUS_MAX;
        }
        for (int i = 0; i < n; i++) {
            const cJSON *b = cJSON_GetArrayItem(buses, i);
            if (!cJSON_IsObject(b)) {
                continue;
            }
            muse_bus_entry_t *e = &out->buses[out->bus_count];
            copy_str(e->route, sizeof(e->route), b, "route");
            copy_str(e->arrival, sizeof(e->arrival), b, "arrival");
            const cJSON *ep = cJSON_GetObjectItemCaseSensitive(b, "arrival_epoch");
            if (cJSON_IsNumber(ep)) {
                e->arrival_epoch = (int64_t)ep->valuedouble;
            }
            if (e->route[0] && e->arrival_epoch > 0) {
                out->bus_count++;
            }
        }
    }

    const cJSON *wx = cJSON_GetObjectItemCaseSensitive(root, "weather");
    if (cJSON_IsObject(wx)) {
        const cJSON *code = cJSON_GetObjectItemCaseSensitive(wx, "code");
        const cJSON *temp = cJSON_GetObjectItemCaseSensitive(wx, "temp_c");
        const cJSON *high = cJSON_GetObjectItemCaseSensitive(wx, "high_c");
        const cJSON *low = cJSON_GetObjectItemCaseSensitive(wx, "low_c");
        if (cJSON_IsNumber(code) && cJSON_IsNumber(temp)) {
            out->weather_code = code->valueint;
            out->temp_c = (float)temp->valuedouble;
            out->high_c = cJSON_IsNumber(high) ? (float)high->valuedouble : out->temp_c;
            out->low_c = cJSON_IsNumber(low) ? (float)low->valuedouble : out->temp_c;
            out->is_day = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(wx, "is_day"));
            out->weather_valid = true;
        }
    }
    out->valid = true;
    cJSON_Delete(root);
    return true;
}

static bool fetch_once(void)
{
    char *body = malloc(FETCH_MAX_BYTES);
    if (!body) {
        return false;
    }
    size_t len = 0;
    bool ok = false;
    esp_http_client_config_t cfg = {
        .url = s_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 8000,
    };
    esp_http_client_handle_t http = esp_http_client_init(&cfg);
    if (!http) {
        free(body);
        return false;
    }
    if (esp_http_client_open(http, 0) == ESP_OK) {
        esp_http_client_fetch_headers(http);
        if (esp_http_client_get_status_code(http) == 200) {
            while (len < FETCH_MAX_BYTES - 1) {
                int r = esp_http_client_read(http, body + len, FETCH_MAX_BYTES - 1 - len);
                if (r < 0) {
                    len = 0;
                    break;
                }
                if (r == 0) {
                    break;
                }
                len += (size_t)r;
            }
            ok = len > 0;
        }
    }
    esp_http_client_cleanup(http);
    if (ok) {
        body[len] = '\0';
        muse_bus_snapshot_t snap;
        if (parse_snapshot(body, &snap)) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_snap = snap;
            xSemaphoreGive(s_lock);
            ESP_LOGI(TAG, "updated: %d bus(es), weather %s", snap.bus_count,
                     snap.weather_valid ? "ok" : "n/a");
        } else {
            ESP_LOGW(TAG, "backend JSON did not parse");
            ok = false;
        }
    }
    free(body);
    return ok;
}

static void bus_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (wifi_mgr_is_connected() && s_url[0]) {
            fetch_once();
        }
        vTaskDelay(pdMS_TO_TICKS(window_active() ? POLL_ACTIVE_MS : POLL_IDLE_MS));
    }
}

void bus_client_start(void)
{
    static bool started;
    if (started) {
        return;
    }
    started = true;
    s_lock = xSemaphoreCreateMutex();
    if (!config_get_str("bus_url", s_url, sizeof(s_url))) {
        strncpy(s_url, CONFIG_BUS_BACKEND_URL, sizeof(s_url) - 1);
        s_url[sizeof(s_url) - 1] = '\0';
    }
    ESP_LOGI(TAG, "backend: %s (test mode %s)", s_url, bus_client_test_mode() ? "on" : "off");
    xTaskCreate(bus_task, "bus_client", 8192, NULL, 3, NULL);
}
