#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成应用图标（甜甜圈 + 毛发），纯 Python zlib 手写 PNG，无第三方依赖。"""
import math
import os
import struct
import zlib

SIZE = 192
R_MAJOR = 0.62   # 主环半径（归一化）
R_MINOR = 0.30   # 管半径
BG = (11, 14, 20, 255)


def png_bytes(width, height, rows):
    raw = b''.join(b'\x00' + bytes(row) for row in rows)
    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff))
    ihdr = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def hash21(x, y):
    a = (math.sin(x * 127.1 + y * 311.7) * 43758.5453) % 1.0
    return a


def main():
    rows = []
    for py in range(SIZE):
        row = bytearray()
        for px in range(SIZE):
            nx = (px + 0.5) / SIZE * 2.0 - 1.0
            ny = (py + 0.5) / SIZE * 2.0 - 1.0
            d = math.sqrt(nx * nx + ny * ny)
            off = (d - R_MAJOR) / R_MINOR      # -1..1 沿管截面
            color = BG

            # 毛发：环外侧一圈随机丝，越外越稀
            fur_t = (d - R_MAJOR) / R_MINOR     # >1 表示环外
            if 1.0 < fur_t < 1.9:
                ang = math.atan2(ny, nx)
                cell = (int(ang * 26), int((fur_t - 1.0) * 9))
                rnd = hash21(cell[0] * 1.7, cell[1] * 2.3)
                keep = 1.0 - (fur_t - 1.0) / 0.9
                if rnd < keep * 0.85:
                    shade = 0.45 + 0.35 * (1.0 - (fur_t - 1.0) / 0.9)
                    color = (int(236 * shade) + 20, int(150 * shade) + 10, int(62 * shade), 255)

            if abs(off) <= 1.0:
                # 管面法线：n = (cos, sin) 绕截面
                ca = math.sqrt(max(0.0, 1.0 - off * off))
                # 光照：主光在左上
                nxv = nx / d if d > 0 else 0.0
                nyv = ny / d if d > 0 else 0.0
                ln = (-0.55 * (nxv * ca) + -0.75 * (nyv * ca) + 0.38 * off)
                diff = max(0.0, ln)
                spec = max(0.0, ln) ** 22
                base = (0.92, 0.48, 0.14)
                lum = 0.22 + 0.9 * diff
                r = min(255, int(255 * base[0] * lum + 255 * spec * 0.65))
                g = min(255, int(255 * base[1] * lum + 235 * spec * 0.60))
                b = min(255, int(255 * base[2] * lum + 200 * spec * 0.50))
                color = (r, g, b, 255)

            row += bytes(color)
        rows.append(row)

    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    out = os.path.join(root, 'entry', 'src', 'main', 'resources', 'base', 'media', 'app_icon.png')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, 'wb') as f:
        f.write(png_bytes(SIZE, SIZE, rows))
    print('icon written:', out, os.path.getsize(out), 'bytes')


if __name__ == '__main__':
    main()
