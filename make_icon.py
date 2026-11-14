# -*- coding: utf-8 -*-
"""生成 ctrlq 托盘图标（启用 / 停用两态，多尺寸 ICO）。

设计：蓝色圆角徽章 + 白色窗口轮廓 + 右上角珊瑚红关闭角标。
停用态整体去色并降低不透明度，浅色/深色任务栏都能一眼分辨。

用法：python make_icon.py
产出：ctrlq.ico、ctrlq_off.ico、icon_preview.png
ICO 内每帧都是 32bpp BGRA DIB（不是 PNG 帧），保证 MinGW windres 能正确
拆成 RT_GROUP_ICON / RT_ICON。
"""

import os
import struct

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]

# 归一化坐标（0..1），全部按同一几何比例绘制，保证各尺寸观感一致
BADGE = (0.055, 0.055, 0.945, 0.945)
BADGE_R = 0.235
WINDOW = (0.225, 0.285, 0.715, 0.780)
WINDOW_R = 0.055
WINDOW_W = 0.078
BADGE_DOT = (0.750, 0.270, 0.165)      # cx, cy, r
DOT_RING = 0.036
CROSS = 0.065                           # 叉号半长
CROSS_W = 0.044

THEMES = {
    "on": {
        "grad_top": (0x5C, 0x80, 0xFF),
        "grad_bottom": (0x2D, 0x42, 0xC4),
        "accent": (0xFF, 0x5A, 0x49, 255),
        "white": (255, 255, 255, 255),
        "alpha": 1.0,
    },
    "off": {
        "grad_top": (0x94, 0x9C, 0xAE),
        "grad_bottom": (0x55, 0x5E, 0x71),
        "accent": (0xB4, 0xBB, 0xC7, 255),
        "white": (0xEC, 0xEE, 0xF3, 255),
        "alpha": 0.88,
    },
}


def gradient(size, top, bottom):
    strip = Image.new("RGBA", (1, size))
    px = []
    for y in range(size):
        t = y / float(size - 1)
        px.append(tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,))
    strip.putdata(px)
    return strip.resize((size, size), Image.NEAREST)


def draw(size, state):
    th = THEMES[state]
    s = max(size * 8, 256)              # 超采样，小尺寸也不糊
    # 托盘的 16/20/24 三档是主战场：线加粗、角标放大，避免缩成一团
    small = size <= 24
    win_w = 0.100 if small else WINDOW_W
    dot_r = 0.185 if small else BADGE_DOT[2]
    ring_w = 0.030 if small else DOT_RING
    cross = 0.078 if small else CROSS
    cross_w = 0.056 if small else CROSS_W
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))

    # 徽章：渐变填充 + 圆角蒙版
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [BADGE[0] * s, BADGE[1] * s, BADGE[2] * s, BADGE[3] * s],
        radius=BADGE_R * s, fill=255)
    img.paste(gradient(s, th["grad_top"], th["grad_bottom"]), (0, 0), mask)

    d = ImageDraw.Draw(img)

    # 窗口轮廓
    d.rounded_rectangle(
        [WINDOW[0] * s, WINDOW[1] * s, WINDOW[2] * s, WINDOW[3] * s],
        radius=WINDOW_R * s, outline=th["white"], width=max(1, int(win_w * s)))

    # 右上角关闭角标：白环 + 强调色圆 + 白色圆头叉号
    cx, cy, r = BADGE_DOT[0] * s, BADGE_DOT[1] * s, dot_r * s
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=th["accent"],
              outline=th["white"], width=max(1, int(ring_w * s)))
    k, w = cross * s, max(1, int(cross_w * s))
    for (x1, y1, x2, y2) in ((cx - k, cy - k, cx + k, cy + k),
                             (cx + k, cy - k, cx - k, cy + k)):
        d.line([x1, y1, x2, y2], fill=th["white"], width=w)
        d.ellipse([x1 - w / 2, y1 - w / 2, x1 + w / 2, y1 + w / 2], fill=th["white"])
        d.ellipse([x2 - w / 2, y2 - w / 2, x2 + w / 2, y2 + w / 2], fill=th["white"])

    if th["alpha"] < 1.0:
        img.putalpha(img.getchannel("A").point(lambda v: int(v * th["alpha"])))

    return img.resize((size, size), Image.LANCZOS)


def dib(image):
    """RGBA -> ICO 帧：BITMAPINFOHEADER + 自下而上 BGRA + 全 0 AND mask。"""
    w, h = image.size
    rgba = image.convert("RGBA")
    xor = bytearray()
    for y in range(h - 1, -1, -1):
        for x in range(w):
            r, g, b, a = rgba.getpixel((x, y))
            xor += bytes((b, g, r, a))
    mask_row = ((w + 31) // 32) * 4
    and_mask = bytes(mask_row * h)
    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0,
                         len(xor) + len(and_mask), 0, 0, 0, 0)
    return header + bytes(xor) + and_mask


def write_ico(path, state):
    frames = [(n, dib(draw(n, state))) for n in SIZES]
    out = bytearray(struct.pack("<HHH", 0, 1, len(frames)))
    offset = 6 + 16 * len(frames)
    for n, data in frames:
        out += struct.pack("<BBBBHHII", n % 256, n % 256, 0, 0, 1, 32,
                           len(data), offset)
        offset += len(data)
    for _, data in frames:
        out += data
    with open(path, "wb") as f:
        f.write(out)
    return os.path.getsize(path)


def preview():
    """实际尺寸贴到浅/深任务栏底上，右侧再放 6 倍放大图。"""
    states = [("on", "ENABLED", (0x5B, 0x80, 0xFF)), ("off", "DISABLED", (0x94, 0x9C, 0xAE))]
    sizes = [16, 20, 24, 32, 48, 64]
    pad, row_h, big, big_gap = 26, 96, 96, 18
    width = 300 + len(sizes) * 58 + big_gap + big + 60
    height = pad + len(states) * (row_h * 2 + 34) + pad
    canvas = Image.new("RGB", (width, height), (0xF3, 0xF4, 0xF7))
    d = ImageDraw.Draw(canvas)

    for si, (state, label, _) in enumerate(states):
        top = pad + si * (row_h * 2 + 34)
        d.text((24, top + 2), label, fill=(0x22, 0x26, 0x33))
        for bi, bg in enumerate(((0xF6, 0xF7, 0xF9), (0x23, 0x26, 0x2E))):
            y = top + 24 + bi * row_h
            d.rectangle([24, y, width - 24, y + row_h - 12], fill=bg)
            x = 44
            for n in sizes:
                icon = draw(n, state)
                iy = y + (row_h - 12 - n) // 2
                canvas.paste(icon, (x, iy), icon)
                x += 58
        # 放大对照
        big_img = draw(64, state).resize((big, big), Image.LANCZOS)
        bx = width - big - 34
        by = top + 30
        canvas.paste(big_img, (bx, by), big_img)
        d.text((bx, by - 16), "64 px @ 1.5x", fill=(0x66, 0x6C, 0x7A))

    path = os.path.join(HERE, "icon_preview.png")
    canvas.save(path)
    return path


if __name__ == "__main__":
    for st, name in (("on", "ctrlq.ico"), ("off", "ctrlq_off.ico")):
        p = os.path.join(HERE, name)
        print("%-16s %7d bytes" % (name, write_ico(p, st)))
    print("preview        ", preview())
