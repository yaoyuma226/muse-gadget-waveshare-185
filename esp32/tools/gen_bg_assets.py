#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Regenerate the bus-notifier tile backgrounds (muse_bg_assets.h +
one muse_bg_img_<name>.c per image).

Weather tiles use full-screen backgrounds in Terry's final style: a
condition-coloured gradient with one big smooth matte icon (flat
silhouette, a few large rounded shapes, soft gradient shading, no
gloss), the lower third kept empty for the LVGL text overlays. One
render per WMO group (13 images: clear/partly split day/night, overcast,
fog, light rain, rain, heavy rain, sleet, snow, storm, hail); the
mapping from WMO code to image lives in muse_weather_photo.c.

The images are stored at 320x320 RGB565, not the panel's native 360:
13 x 360x360 x 2 B = 3.21 MiB would leave only ~0.74 MiB of the 4 MiB
app slot for the rest of the firmware, which is known to need more
than 2 MiB on its own. At 320 the set is 2.54 MiB and the UI scales it
to the tile width at runtime (a mild 1.125x upscale).

The bus tile no longer uses a full-screen background; it only needs a
small round badge (110x110 ARGB8888 with a baked circular alpha mask
and white ring) cropped out of the 3D bus artwork.

Each image goes in its own muse_bg_img_<name>.c (not one monolith):
the assets are pushed to GitHub as text snapshots, and a single
~10 MB C file exceeds the push request size limit.

Usage:
    python3 tools/gen_bg_assets.py [assets_dir] [out_dir]

Defaults: assets_dir = ~/workspace/bus-notifier/assets,
out_dir = components/muse (next to the UI sources).
"""

import math
import os
import sys

from PIL import Image

SIZE = 320          # weather backgrounds, RGB565
BADGE_SIZE = 110    # bus badge, ARGB8888 with circular alpha + ring

# descriptor name -> source file in the assets dir (weather, RGB565)
IMAGES = {
    "clear_day": "media-generation-weather-hero-smooth-clear-0-2093c131-68ae-438c-a518-0883dabc8800.webp",
    "clear_night": "media-generation-weather-hero-smooth-clear-nigh-0-2318b29d-eb37-47ae-b610-8398d31175d1.webp",
    "partly_day": "media-generation-weather-hero-smooth-partly-0-e63a5eb8-099f-4fe6-8a6c-89552d50c785.webp",
    "partly_night": "media-generation-weather-hero-smooth-partly-nig-0-99fcd8f4-d719-489f-806f-cb20f342a9f0.webp",
    "overcast": "media-generation-weather-hero-smooth-overcast-0-f35195ae-4790-41c5-aa83-d652b4033266.webp",
    "fog": "media-generation-weather-hero-smooth-fog-0-16f30b07-d199-4add-858e-4e5b53c64b15.webp",
    "light_rain": "media-generation-weather-hero-smooth-lightrain-0-3603cf6f-21fc-4e04-adc7-4d16f388aafd.webp",
    "rain": "media-generation-weather-hero-smooth-rain-0-cb55a4db-2f01-42f0-ab56-47d005641cf9.webp",
    "heavy_rain": "media-generation-weather-hero-smooth-heavyrain-0-40aa4137-4dcf-4a1f-857c-f00005147d75.webp",
    "sleet": "media-generation-weather-hero-smooth-sleet-0-44bda421-5ba4-4328-abff-2d9807dd5d77.webp",
    "snow": "media-generation-weather-hero-smooth-snow-0-1838ff8b-936a-4c10-b8d2-662ef46005bb.webp",
    "storm": "media-generation-weather-hero-smooth-storm-0-3532b287-41a2-488f-a5f2-8e2f278c2a57.webp",
    "hail": "media-generation-weather-hero-smooth-hail-0-e6406f2e-075e-49b3-8c50-d7c89f169cb8.webp",
}

BUS_ART = "media-generation-bus-hero-3d-0-8cfa4707-0b6a-47f8-94e5-9fedebf9ce9a.webp"
# Square crop around the bus in BUS_ART (fractions of the image size):
# the bus spans roughly x 24-76%, y 16-54%.
BADGE_CROP_CENTRE = (0.50, 0.35)
BADGE_CROP_SIDE = 0.60

HEADER = """/*
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
"""


def rgb565_pixels(path):
    im = Image.open(path).convert("RGB")
    w, h = im.size
    side = min(w, h)
    im = im.crop(((w - side) // 2, (h - side) // 2,
                  (w + side) // 2, (h + side) // 2))
    im = im.resize((SIZE, SIZE), Image.LANCZOS)
    px = im.load()
    out = []
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b = px[x, y]
            out.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    return out


def badge_pixels(path):
    """Crop the bus, resize to BADGE_SIZE, bake the circular badge:
    artwork inside, a white ring at the edge, transparent outside.
    Returns ARGB8888 words (0xAARRGGBB)."""
    im = Image.open(path).convert("RGB")
    w, h = im.size
    side = int(min(w, h) * BADGE_CROP_SIDE)
    cx = int(w * BADGE_CROP_CENTRE[0])
    cy = int(h * BADGE_CROP_CENTRE[1])
    im = im.crop((cx - side // 2, cy - side // 2,
                  cx - side // 2 + side, cy - side // 2 + side))
    im = im.resize((BADGE_SIZE, BADGE_SIZE), Image.LANCZOS)
    px = im.load()
    c = (BADGE_SIZE - 1) / 2.0
    out = []
    for y in range(BADGE_SIZE):
        for x in range(BADGE_SIZE):
            d = math.hypot(x - c, y - c)
            r, g, b = px[x, y]
            if d <= 49.0:
                a = 255
            elif d < 50.0:      # artwork -> ring blend
                t = d - 49.0
                r = int(r + (255 - r) * t)
                g = int(g + (255 - g) * t)
                b = int(b + (255 - b) * t)
                a = 255
            elif d <= 54.0:     # white ring
                r, g, b, a = 255, 255, 255, 255
            elif d < 55.0:      # ring fades out at the rim
                r, g, b = 255, 255, 255
                a = int(255 * (55.0 - d))
            else:
                r, g, b, a = 0, 0, 0, 0
            out.append((a << 24) | (r << 16) | (g << 8) | b)
    return out


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    assets_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser(
        "~/workspace/bus-notifier/assets")
    out_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        here, "..", "components", "muse")

    names = list(IMAGES) + ["bus_badge"]
    h_lines = [HEADER, "#pragma once\n", '#include "lvgl.h"\n',
               "/* Tile background art, generated by",
               " * esp32/tools/gen_bg_assets.py from the artwork in",
               " * ~/workspace/bus-notifier/assets. Do not edit by hand.",
               " *",
               " * Weather: full-screen 320x320 RGB565 (the UI scales to",
               " * the tile width), one image per WMO group; see",
               " * muse_weather_photo.c for the code -> image mapping.",
               " * Bus: a 110x110 ARGB8888 round badge (circular alpha,",
               " * white ring baked in); the bus tile draws its own",
               " * gradient behind it. */\n"]
    for n in names:
        h_lines.append(f"extern const lv_image_dsc_t muse_bg_{n};")
    h_lines.append("")
    with open(os.path.join(out_dir, "muse_bg_assets.h"), "w") as f:
        f.write("\n".join(h_lines))

    total = 0

    def emit(name, pixels, word, fmt, per_line, cf, size, bpp):
        nonlocal total
        total += len(pixels) * bpp
        path = os.path.join(out_dir, f"muse_bg_img_{name}.c")
        with open(path, "w") as f:
            f.write(HEADER)
            f.write('#include "muse_bg_assets.h"\n\n')
            f.write(f"static const {word} bg_{name}"
                    f"[{size} * {size}] = {{\n")
            for i in range(0, len(pixels), per_line):
                chunk = ",".join(fmt.format(p=p)
                                 for p in pixels[i:i + per_line])
                f.write(f"    {chunk},\n")
            f.write("};\n\n")
            f.write(f"const lv_image_dsc_t muse_bg_{name} = {{\n")
            f.write("    .header = {\n")
            f.write("        .magic = LV_IMAGE_HEADER_MAGIC,\n")
            f.write(f"        .cf = {cf},\n")
            f.write(f"        .w = {size},\n")
            f.write(f"        .h = {size},\n")
            f.write(f"        .stride = {size} * {bpp},\n")
            f.write("    },\n")
            f.write(f"    .data_size = {size} * {size} * {bpp},\n")
            f.write(f"    .data = (const uint8_t *)bg_{name},\n")
            f.write("};\n")

    for n in IMAGES:
        emit(n, rgb565_pixels(os.path.join(assets_dir, IMAGES[n])),
             "uint16_t", "0x{p:04x}", 12, "LV_COLOR_FORMAT_RGB565",
             SIZE, 2)
    emit("bus_badge", badge_pixels(os.path.join(assets_dir, BUS_ART)),
         "uint32_t", "0x{p:08x}", 6, "LV_COLOR_FORMAT_ARGB8888",
         BADGE_SIZE, 4)

    print(f"{len(names)} images, {total} bytes of image data "
          f"({total / 1048576:.2f} MiB)")


if __name__ == "__main__":
    main()
