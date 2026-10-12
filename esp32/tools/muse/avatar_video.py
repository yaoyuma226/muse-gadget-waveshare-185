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

"""Put your avatar's own videos on a board with CONFIG_MUSE_VIDEO_AVATAR.

Ask your Muse for its avatar's files (the square MP4s: idle, working,
waking, milestone level-up), then:

    python3 tools/muse/avatar_video.py --idle idle.mp4 --thinking working.mp4 \\
        --boot waking.mp4 --happy levelup.mp4 --out avatar.bin --flash

Each clip becomes circle-cropped baseline JPEG frames, which the board's ROM
decoder plays (components/muse/muse_video_avatar.c). Modes without a clip
play idle. --flash writes the file to the "avatar" partition, so the
firmware and your settings stay as they are. Needs ffmpeg and Pillow.
"""

import argparse
import glob
import io
import os
import struct
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFilter

MAGIC = 0x5456414D       # "MAVT"
VERSION = 1
MODES = ["boot", "idle", "listening", "thinking", "speaking", "error", "off"]   # muse_mode_t
CLIPS = MODES + ["happy"]
LOOPS = {"idle", "listening", "thinking", "speaking", "error"}   # the rest play once
PART_OFFSET = 0x830000   # partitions_muse.csv
PART_SIZE = 0x700000


def frames(path, fps, tmp):
    """The clip's frames as PIL images, at fps."""
    out = os.path.join(tmp, os.path.basename(path) + "-%05d.png")
    subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-vf", f"fps={fps}", out], check=True)
    return [Image.open(f).convert("RGB") for f in sorted(glob.glob(out.replace("%05d", "*")))]


def mask(size, feather):
    """A circle, soft at the edge, so the round screen's black shows around it."""
    big = size * 4
    m = Image.new("L", (big, big), 0)
    ImageDraw.Draw(m).ellipse((0, 0, big - 1, big - 1), fill=255)
    return m.resize((size, size), Image.LANCZOS).filter(ImageFilter.GaussianBlur(feather))


def jpeg(img, size, zoom, circle, quality):
    w, h = img.size
    side = int(min(w, h) / zoom)
    left, top = (w - side) // 2, (h - side) // 2
    img = img.crop((left, top, left + side, top + side)).resize((size, size), Image.LANCZOS)
    if circle is not None:
        img = Image.composite(img, Image.new("RGB", (size, size)), circle)
    buf = io.BytesIO()
    # Baseline, 4:2:0: what the ROM's TJpgDec reads.
    img.save(buf, "JPEG", quality=quality, optimize=True, progressive=False, subsampling=2)
    return buf.getvalue()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for c in CLIPS:
        ap.add_argument(f"--{c}", metavar="VIDEO", help=f"clip for {c}" + (" (required)" if c == "idle" else ""))
    ap.add_argument("--size", type=int, default=192, help="frame size in px: Muse's size on screen (192 on the 1.85)")
    ap.add_argument("--fps", type=float, default=12)
    ap.add_argument("--quality", type=int, default=85)
    ap.add_argument("--zoom", type=float, default=1.0, help="crop in on the centre, e.g. 1.2")
    ap.add_argument("--square", action="store_true", help="no circle mask")
    ap.add_argument("--out", default="avatar.bin")
    ap.add_argument("--preview", metavar="DIR", help="also save each clip's first frame as a PNG here")
    ap.add_argument("--flash", action="store_true", help="write it to the board's avatar partition")
    ap.add_argument("--port", help="the board's serial port, if --flash can't find it")
    a = ap.parse_args()
    if not a.idle:
        ap.error("--idle is required")

    circle = None if a.square else mask(a.size, a.size / 96)
    table, data = [], []
    clips = []
    with tempfile.TemporaryDirectory() as tmp:
        for c in CLIPS:
            path = getattr(a, c)
            if not path:
                clips.append((0, 0, 0))
                continue
            imgs = frames(path, a.fps, tmp)
            clips.append((len(table), len(imgs), int(c in LOOPS)))
            for i, img in enumerate(imgs):
                j = jpeg(img, a.size, a.zoom, circle, a.quality)
                table.append(len(j))
                data.append(j)
                if a.preview and i == 0:
                    os.makedirs(a.preview, exist_ok=True)
                    Image.open(io.BytesIO(j)).save(os.path.join(a.preview, f"{c}.png"))
            print(f"{c:9} {len(imgs):4} frames from {os.path.basename(path)}")

    head = struct.pack("<IHHHHI", MAGIC, VERSION, a.size, round(a.fps * 100), len(CLIPS), len(table))
    head += b"".join(struct.pack("<III", *c) for c in clips)
    offset = len(head) + 8 * len(table)
    index = b""
    for n in table:
        index += struct.pack("<II", offset, n)
        offset += n
    blob = head + index + b"".join(data)
    if len(blob) > PART_SIZE:
        sys.exit(f"{len(blob)} bytes is more than the {PART_SIZE}-byte partition: lower --fps, --size or --quality")
    with open(a.out, "wb") as f:
        f.write(blob)
    print(f"{a.out}: {len(table)} frames, {len(blob) / 1e6:.2f} MB of {PART_SIZE / 1e6:.1f} MB, "
          f"{len(blob) // max(len(table), 1) // 1024} KB a frame")

    if a.flash:
        port = a.port or (glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))[0]
        subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", port, "-b", "460800",
                        "write-flash", hex(PART_OFFSET), a.out], check=True)


if __name__ == "__main__":
    main()
