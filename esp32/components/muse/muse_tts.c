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

#include "muse_tts.h"

#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifndef CONFIG_MUSE_TTS_OPENAI_KEY
#define CONFIG_MUSE_TTS_OPENAI_KEY ""
#endif
#ifndef CONFIG_MUSE_TTS_OPENAI_MODEL
#define CONFIG_MUSE_TTS_OPENAI_MODEL "gpt-4o-mini-tts"
#endif
#ifndef CONFIG_MUSE_TTS_OPENAI_VOICE
#define CONFIG_MUSE_TTS_OPENAI_VOICE "alloy"
#endif

#define URL "https://api.openai.com/v1/audio/speech"
#define TEXT_MAX 1024         /* a reply, as the chat session keeps it */
#define BODY_MAX (TEXT_MAX * 6 + 512)   /* the text with every byte escaped, and the rest */
#define MP3_RING (64 * 1024)  /* ~8 s at 64 kbps; the fetch waits while it's full */
#define CHUNK 1024
#define RETRY_MS 20

static const char *TAG = "muse_tts";

typedef enum { ST_IDLE, ST_RUNNING, ST_DONE, ST_FAILED } state_t;

static TaskHandle_t s_task;
static SemaphoreHandle_t s_lock;   /* s_job, s_state, s_text and the ring's contents */
static StreamBufferHandle_t s_ring;
static uint32_t s_job;             /* bumped by each start and cancel */
static state_t s_state;
static char *s_text;

bool muse_tts_configured(void)
{
    return CONFIG_MUSE_TTS_OPENAI_KEY[0] != '\0';
}

/* text as a JSON string body, without the quotes. */
static size_t json_escape(const char *text, char *out, size_t cap)
{
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p && o + 7 < cap; p++) {
        if (*p == '"' || *p == '\\') {
            out[o++] = '\\';
            out[o++] = (char)*p;
        } else if (*p == '\n') {
            out[o++] = '\\';
            out[o++] = 'n';
        } else if (*p < 0x20) {
            o += snprintf(out + o, cap - o, "\\u%04x", *p);
        } else {
            out[o++] = (char)*p;
        }
    }
    out[o] = '\0';
    return o;
}

/* Hands the ring what it has room for; false once the job is stale. */
static bool deliver(uint32_t job, const uint8_t *data, size_t len)
{
    while (len) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (job != s_job) {
            xSemaphoreGive(s_lock);
            return false;
        }
        size_t sent = xStreamBufferSend(s_ring, data, len, 0);
        xSemaphoreGive(s_lock);
        data += sent;
        len -= sent;
        if (len) {
            vTaskDelay(pdMS_TO_TICKS(RETRY_MS));   /* the player's behind */
        }
    }
    return true;
}

static void finish(uint32_t job, bool ok)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (job == s_job) {
        s_state = ok ? ST_DONE : ST_FAILED;
    }
    xSemaphoreGive(s_lock);
}

static bool fetch(uint32_t job, const char *text, char *body)
{
    int n = snprintf(body, BODY_MAX,
                     "{\"model\":\"" CONFIG_MUSE_TTS_OPENAI_MODEL "\",\"voice\":\"" CONFIG_MUSE_TTS_OPENAI_VOICE
                     "\",\"response_format\":\"mp3\",\"input\":\"");
    n += json_escape(text, body + n, BODY_MAX - n - 3);
    n += snprintf(body + n, BODY_MAX - n, "\"}");

    esp_http_client_config_t cfg = {
        .url = URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        return false;
    }
    esp_http_client_set_header(c, "Authorization", "Bearer " CONFIG_MUSE_TTS_OPENAI_KEY);
    esp_http_client_set_header(c, "Content-Type", "application/json");
    bool ok = false;
    int64_t got = 0;
    uint8_t buf[CHUNK];
    if (esp_http_client_open(c, n) != ESP_OK || esp_http_client_write(c, body, n) != n) {
        ESP_LOGW(TAG, "can't reach OpenAI");
        goto out;
    }
    esp_http_client_fetch_headers(c);
    int status = esp_http_client_get_status_code(c);
    if (status != 200) {
        int r = 0, k;
        while (r < (int)sizeof(buf) - 1 && (k = esp_http_client_read(c, (char *)buf + r, sizeof(buf) - 1 - r)) > 0) {
            r += k;   /* the error's JSON arrives in pieces */
        }
        buf[r] = '\0';
        for (char *p = (char *)buf; *p; p++) {
            *p = *p == '\n' ? ' ' : *p;
        }
        ESP_LOGW(TAG, "OpenAI said %d: %.200s", status, (const char *)buf);
        goto out;
    }
    for (;;) {
        int r = esp_http_client_read(c, (char *)buf, sizeof(buf));
        if (r < 0) {
            ESP_LOGW(TAG, "read failed after %lld bytes", got);
            goto out;
        }
        if (r == 0) {
            break;
        }
        got += r;
        if (!deliver(job, buf, r)) {
            goto out;   /* cancelled */
        }
    }
    ok = true;
    ESP_LOGI(TAG, "speech: %lld bytes of MP3", got);
out:
    esp_http_client_cleanup(c);
    return ok;
}

static void tts_task(void *arg)
{
    char *text = heap_caps_malloc(TEXT_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *body = heap_caps_malloc(BODY_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        uint32_t job = s_job;
        bool run = s_state == ST_RUNNING;
        if (run) {
            strlcpy(text, s_text, TEXT_MAX);
        }
        xSemaphoreGive(s_lock);
        if (run) {
            finish(job, text && body && fetch(job, text, body));
        }
    }
}

static bool ensure_task(void)
{
    if (s_task) {
        return true;
    }
    s_lock = xSemaphoreCreateMutex();
    s_ring = xStreamBufferCreateWithCaps(MP3_RING, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_text = heap_caps_malloc(TEXT_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_lock || !s_ring || !s_text) {
        return false;
    }
    /* TLS and the HTTP client keep their buffers in PSRAM; so can the stack. */
    return xTaskCreatePinnedToCoreWithCaps(tts_task, "muse_tts", 8 * 1024, NULL, 4, &s_task, 1,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS;
}

bool muse_tts_start(const char *text)
{
    if (!muse_tts_configured() || !text || !text[0] || !ensure_task()) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_job++;
    xStreamBufferReset(s_ring);   /* nothing blocks on it: the task sends without waiting */
    strlcpy(s_text, text, TEXT_MAX);
    s_state = ST_RUNNING;
    xSemaphoreGive(s_lock);
    xTaskNotifyGive(s_task);
    return true;
}

size_t muse_tts_read(uint8_t *buf, size_t cap)
{
    if (!s_task) {
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t n = xStreamBufferReceive(s_ring, buf, cap, 0);
    xSemaphoreGive(s_lock);
    return n;
}

bool muse_tts_finished(bool *ok)
{
    if (!s_task) {
        *ok = false;
        return true;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ended = s_state != ST_RUNNING && xStreamBufferIsEmpty(s_ring);
    *ok = s_state == ST_DONE;
    xSemaphoreGive(s_lock);
    return ended;
}

void muse_tts_cancel(void)
{
    if (!s_task) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_job++;
    xStreamBufferReset(s_ring);
    s_state = ST_IDLE;
    xSemaphoreGive(s_lock);
}
