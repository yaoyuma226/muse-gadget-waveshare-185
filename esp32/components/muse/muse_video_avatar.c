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

/*
 * Muse as video (CONFIG_MUSE_VIDEO_AVATAR): your avatar's own clips, played
 * from the "avatar" flash partition in place of the pixel art. The clips are
 * JPEG frames that tools/muse/avatar_video.py makes from the Muse app's
 * avatar videos and flashes there; no artwork is in the firmware or the repo.
 *
 * The pixel renderer is built alongside under muse_pixel_default_* names and
 * draws Muse when the partition holds no clips.
 *
 * A task of its own decodes each frame (the ROM's TJpgDec) into a back
 * buffer; muse_pixel_render() swaps it in on the UI task, so the UI never
 * waits for a decode and a frame never changes mid-draw.
 */

#include <math.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp32s3/rom/tjpgd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "muse_pixel.h"

#define MAGIC 0x5456414Du       /* "MAVT" */
#define VERSION 1
#define PART_SUBTYPE 0x42
#define CLIP_HAPPY MUSE_MODE_COUNT   /* the clip after the modes' */
#define CLIPS (MUSE_MODE_COUNT + 1)
#define WORK_BYTES 4096         /* TJpgDec's pool: 3100 bytes for its tables and buffers */
#define NO_FRAME UINT32_MAX

uint32_t muse_pixel_default_accent(muse_mode_t mode);
void muse_pixel_default_render(const muse_pose_t *pose);
void muse_pixel_default_set_size(int px);
void muse_pixel_default_scale(uint16_t *dst, int stride_px, int x0, int x1, int y0, int y1);

static const char *TAG = "muse_video";

/* The partition's layout, written by tools/muse/avatar_video.py. Little-endian. */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;       /* frames are size x size */
    uint16_t fps_x100;
    uint16_t nclips;
    uint32_t nframes;
} header_t;

typedef struct {
    uint32_t first;      /* index into the frame table */
    uint32_t count;      /* 0: play the idle clip */
    uint32_t loop;       /* else it plays once and holds its last frame */
} clip_t;

typedef struct {
    uint32_t offset;     /* from the start of the partition */
    uint32_t len;
} frame_t;

static bool s_tried, s_ok;
static const uint8_t *s_base;
static header_t s_hdr;
static clip_t s_clips[CLIPS];
static const frame_t *s_frames;

static int s_size;               /* on screen */
static uint16_t *s_front, *s_back;   /* s_hdr.size squared, RGB565 */
static TaskHandle_t s_task;
static volatile uint32_t s_want = NO_FRAME;   /* frame for the task to decode */
static volatile uint32_t s_ready = NO_FRAME;  /* frame decoded into s_back */
static uint32_t s_shown = NO_FRAME;
static bool s_changed;
static float s_happy_t0 = -1;

/* ---- Decoding ---- */

typedef struct {
    const uint8_t *src;
    size_t len, at;
    uint16_t *dst;
    int w;
} jpeg_io_t;

static UINT jpeg_in(JDEC *jd, BYTE *buf, UINT n)
{
    jpeg_io_t *io = jd->device;
    size_t left = io->len - io->at;
    n = n < left ? n : (UINT)left;
    if (buf) {
        memcpy(buf, io->src + io->at, n);
    }
    io->at += n;
    return n;
}

static UINT jpeg_out(JDEC *jd, void *bitmap, JRECT *r)
{
    jpeg_io_t *io = jd->device;
    const BYTE *p = bitmap;   /* RGB888 */
    for (int y = r->top; y <= r->bottom; y++) {
        uint16_t *d = io->dst + y * io->w + r->left;
        for (int x = r->left; x <= r->right; x++, p += 3) {
            *d++ = (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
        }
    }
    return 1;
}

static bool decode(uint32_t frame, uint16_t *dst, void *work)
{
    const frame_t *f = &s_frames[frame];
    jpeg_io_t io = {.src = s_base + f->offset, .len = f->len, .dst = dst, .w = s_hdr.size};
    JDEC jd;
    JRESULT r = jd_prepare(&jd, jpeg_in, work, WORK_BYTES, &io);
    if (r == JDR_OK && (jd.width != s_hdr.size || jd.height != s_hdr.size)) {
        r = JDR_PAR;
    }
    if (r == JDR_OK) {
        r = jd_decomp(&jd, jpeg_out, 0);
    }
    if (r != JDR_OK) {
        ESP_LOGW(TAG, "frame %u: TJpgDec error %d", (unsigned)frame, r);
    }
    return r == JDR_OK;
}

static void decode_task(void *arg)
{
    void *work = heap_caps_malloc(WORK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);   /* internal RAM is scarce */
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t want = s_want;
        if (!work || want == NO_FRAME || s_ready != NO_FRAME) {
            continue;   /* the last one hasn't been shown yet */
        }
        if (decode(want, s_back, work)) {
            s_ready = want;
        }
    }
}

/* ---- The partition ---- */

static bool load(void)
{
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, PART_SUBTYPE, "avatar");
    if (!p) {
        ESP_LOGI(TAG, "no avatar partition: pixel Muse");
        return false;
    }
    if (esp_partition_read(p, 0, &s_hdr, sizeof(s_hdr)) != ESP_OK || s_hdr.magic != MAGIC ||
        s_hdr.version != VERSION) {
        ESP_LOGI(TAG, "no clips in the avatar partition: pixel Muse");
        return false;
    }
    esp_partition_mmap_handle_t h;
    if (esp_partition_mmap(p, 0, p->size, ESP_PARTITION_MMAP_DATA, (const void **)&s_base, &h) != ESP_OK) {
        ESP_LOGW(TAG, "can't map the avatar partition");
        return false;
    }
    size_t table = sizeof(header_t) + s_hdr.nclips * sizeof(clip_t);
    if (!s_hdr.size || s_hdr.size > 480 || !s_hdr.fps_x100 || s_hdr.nclips > CLIPS || !s_hdr.nframes ||
        table + s_hdr.nframes * sizeof(frame_t) > p->size) {
        ESP_LOGW(TAG, "avatar partition header is damaged");
        return false;
    }
    memcpy(s_clips, s_base + sizeof(header_t), s_hdr.nclips * sizeof(clip_t));
    s_frames = (const frame_t *)(s_base + table);
    for (int i = 0; i < CLIPS; i++) {
        if (s_clips[i].first + s_clips[i].count > s_hdr.nframes) {
            s_clips[i].count = 0;
        }
    }
    for (uint32_t i = 0; i < s_hdr.nframes; i++) {
        if (s_frames[i].offset + s_frames[i].len > p->size) {
            ESP_LOGW(TAG, "avatar frame %u is outside the partition", (unsigned)i);
            return false;
        }
    }
    if (!s_clips[MUSE_MODE_IDLE].count) {
        ESP_LOGW(TAG, "the avatar has no idle clip");
        return false;
    }
    size_t px = (size_t)s_hdr.size * s_hdr.size * sizeof(uint16_t);
    s_front = heap_caps_calloc(1, px, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_back = heap_caps_calloc(1, px, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_front || !s_back ||
        xTaskCreatePinnedToCoreWithCaps(decode_task, "muse_video", 4096, NULL, 3, &s_task, 1,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGW(TAG, "out of memory for the video avatar");
        return false;
    }
    ESP_LOGI(TAG, "video Muse: %u frames, %u px, %.1f fps", (unsigned)s_hdr.nframes, s_hdr.size,
             s_hdr.fps_x100 / 100.0);
    return true;
}

static bool ready(void)
{
    if (!s_tried) {
        s_tried = true;
        s_ok = load();
    }
    return s_ok;
}

/* ---- muse_pixel.h ---- */

uint32_t muse_pixel_accent(muse_mode_t mode)
{
    return muse_pixel_default_accent(mode);
}

void muse_pixel_set_size(int px)
{
    s_size = px;
    muse_pixel_default_set_size(px);
}

/* The frame of `c` that's showing `t` seconds in. */
static uint32_t frame_at(const clip_t *c, float t)
{
    uint32_t i = (uint32_t)(t * s_hdr.fps_x100 / 100.0f);
    i = c->loop ? i % c->count : (i < c->count ? i : c->count - 1);
    return c->first + i;
}

void muse_pixel_render(const muse_pose_t *p)
{
    if (!ready()) {
        muse_pixel_default_render(p);
        return;
    }
    if (s_ready != NO_FRAME) {
        uint16_t *t = s_front;
        s_front = s_back;
        s_back = t;
        s_shown = s_ready;
        s_ready = NO_FRAME;
        s_changed = true;
    }

    const clip_t *c = &s_clips[p->mode < MUSE_MODE_COUNT ? p->mode : MUSE_MODE_IDLE];
    float t = c->loop ? p->t : p->mode_t;   /* loops keep going across a change of mode */
    if (p->happy > 0.05f && s_clips[CLIP_HAPPY].count) {
        if (s_happy_t0 < 0) {
            s_happy_t0 = p->t;
        }
        c = &s_clips[CLIP_HAPPY];
        t = p->t - s_happy_t0;
    } else {
        s_happy_t0 = -1;
    }
    if (!c->count) {
        c = &s_clips[MUSE_MODE_IDLE];
        t = p->t;
    }
    uint32_t want = frame_at(c, t);
    if (want != s_shown && want != s_want) {
        s_want = want;
        xTaskNotifyGive(s_task);
    } else if (want != s_shown && s_ready == NO_FRAME) {
        xTaskNotifyGive(s_task);   /* it skipped this one while the last waited */
    }
}

int muse_pixel_changed(void)
{
    if (!s_ok) {
        return -1;
    }
    bool c = s_changed;
    s_changed = false;
    return c;
}

void muse_pixel_scale(uint16_t *dst, int stride_px, int x0, int x1, int y0, int y1)
{
    if (!s_ok) {
        muse_pixel_default_scale(dst, stride_px, x0, x1, y0, y1);
        return;
    }
    int n = s_hdr.size, size = s_size > 0 ? s_size : n;
    for (int y = y0; y <= y1; y++, dst += stride_px) {
        const uint16_t *row = s_front + (y * n / size) * n;
        if (size == n) {
            memcpy(dst, row + x0, (x1 - x0 + 1) * sizeof(uint16_t));
            continue;
        }
        for (int x = x0; x <= x1; x++) {
            dst[x - x0] = row[x * n / size];
        }
    }
}
