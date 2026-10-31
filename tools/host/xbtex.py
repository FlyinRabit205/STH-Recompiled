#!/usr/bin/env python3
"""Xbox RenderWare textures of Shadow the Hedgehog: parse TXD dictionaries, compute the runtime's replacement key
(gpu/texturedumper.cpp Key), decode to RGBA.

The key is Hash(level 0 bytes, seed=Hash(u32[format, width, height])), then Hash(palette 1024 bytes, key) for P8,
with gpu/shaders.cpp Hash (16 byte steps through a 64x64->128 multiply). Formats are NV2A codes (gpu/texture.cpp).
"""
import io
import struct

import numpy as np
from PIL import Image

M64 = (1 << 64) - 1
K0, K1 = 0x9E3779B97F4A7C15, 0xE7037ED1A0B428DB
FNV = 1469598103934665603


def _mix(a, b):
    r = (a & M64) * (b & M64)
    return (r ^ (r >> 64)) & M64


import ctypes
import os
_LIB = None
for _p in (os.environ.get("RTHASH_LIB", ""), "/home/claude/tools/librthash.so"):
    if _p and os.path.exists(_p):
        _LIB = ctypes.CDLL(_p)
        _LIB.rt_hash.restype = ctypes.c_uint64
        _LIB.rt_hash.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_uint64]
        break


def rt_hash(data, h=FNV):
    """gpu/shaders.cpp Hash (C version from rthash.c when built, else Python)."""
    if _LIB is not None:
        b = bytes(data)
        return _LIB.rt_hash(b, len(b), h & M64)
    return rt_hash_py(data, h)


def rt_hash_py(data, h=FNV):
    """gpu/shaders.cpp Hash."""
    b = bytes(data)
    n = len(b)
    seed = (h ^ K0) & M64
    i = 0
    full = n - n % 16
    # 16 bytes a step
    for i in range(0, full, 16):
        x, y = struct.unpack_from("<QQ", b, i)
        seed = _mix(x ^ K1, y ^ seed)
    i = full
    r = n - i
    if r:
        if r >= 8:
            x = struct.unpack_from("<Q", b, i)[0]
            y = struct.unpack_from("<Q", b, n - 8)[0]
        else:
            x = int.from_bytes(b[i:n] + b"\0" * (8 - r), "little")
            y = 0
        seed = _mix(x ^ K1, y ^ seed ^ r)
    return _mix(seed ^ K0, n ^ K1)


# NV2A texture formats (gpu/texture.cpp): name, bits per pixel or bytes per 4x4 block, compressed
FMT = {
    0x02: ("A1R5G5B5", 16, False), 0x03: ("X1R5G5B5", 16, False), 0x04: ("A4R4G4B4", 16, False),
    0x05: ("R5G6B5", 16, False), 0x06: ("A8R8G8B8", 32, False), 0x07: ("X8R8G8B8", 32, False),
    0x0B: ("P8", 8, False), 0x0C: ("DXT1", 8, True), 0x0E: ("DXT3", 16, True), 0x0F: ("DXT5", 16, True),
}


def level_size(fmt, w, h):
    name, bits, comp = FMT[fmt]
    if comp:
        return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * bits
    return w * h * bits // 8


# RenderWare raster format bits
PAL8, PAL4, MIPMAP, AUTOMIP = 0x2000, 0x4000, 0x8000, 0x1000
RW_PIX = {0x0100: 0x02, 0x0200: 0x05, 0x0300: 0x04, 0x0500: 0x06, 0x0600: 0x07, 0x0A00: 0x03}
RW_DXT = {0x0C: 0x0C, 0x0D: 0x0E, 0x0E: 0x0E, 0x0F: 0x0F, 0x10: 0x0F}


class Tex:
    __slots__ = ("name", "fmt", "w", "h", "levels", "data", "palette", "key", "source")

    def __repr__(self):
        return f"Tex({self.name} {FMT[self.fmt][0]} {self.w}x{self.h} key {self.key:016x})"


def _chunks(b, off, end):
    while off + 12 <= end:
        cid, size, ver = struct.unpack_from("<III", b, off)
        yield cid, off + 12, min(off + 12 + size, end)
        off += 12 + size


def parse_native(b, start, end, source=""):
    """A texture native's struct chunk (Xbox, platform 5) -> Tex or None."""
    for cid, s, e in _chunks(b, start, end):
        if cid != 0x01:
            continue
        if e - s < 88:
            return None
        platform, filt = struct.unpack_from("<II", b, s)
        if platform != 5:
            return None
        name = b[s + 8:s + 40].split(b"\0")[0].decode("latin1")
        o = s + 72
        raster, has_alpha, unk = struct.unpack_from("<IHH", b, o)
        w, h = struct.unpack_from("<HH", b, o + 8)
        depth, nlev, rtype, comp = b[o + 12], b[o + 13], b[o + 14], b[o + 15]
        total = struct.unpack_from("<I", b, o + 16)[0]
        o += 20
        t = Tex()
        t.name, t.w, t.h, t.levels, t.source = name, w, h, max(1, nlev), source
        t.palette = None
        if comp:
            t.fmt = RW_DXT.get(comp)
        elif raster & (PAL8 | PAL4):
            # palettized, one byte per pixel either way: PAL8 has 256 colours, Shadow's PAL4 textures 32 (128 bytes).
            # The palette is whatever sits between the header and the pixels. The runtime hashes a 256 entry
            # buffer, zero past the palette's own size.
            t.fmt = 0x0B
            pal_bytes = (e - s) - 92 - total
            if pal_bytes not in (64, 128, 512, 1024):
                return None
            t.palette = bytes(b[o:o + pal_bytes]) + bytes(1024 - pal_bytes)
            o += pal_bytes
        else:
            t.fmt = RW_PIX.get(raster & 0x0F00)
        if t.fmt is None or t.fmt not in FMT:
            return None
        n0 = level_size(t.fmt, w, h)
        if o + n0 > e:
            return None
        t.data = bytes(b[o:o + n0])
        seed = rt_hash(struct.pack("<III", t.fmt, w, h))
        key = rt_hash(t.data, seed)
        if t.palette is not None:
            key = rt_hash(t.palette, key)
        t.key = key or 1
        return t
    return None


def parse_txd(b, source=""):
    """Every Xbox texture in a buffer: TXD dictionaries anywhere in it (DFF files embed them too)."""
    out = []
    view = memoryview(b)
    i = 0
    n = len(b)
    while True:
        i = b.find(b"\x15\x00\x00\x00", i)
        if i < 0 or i + 12 > n:
            break
        size, ver = struct.unpack_from("<II", b, i + 4)
        if (ver & 0xFFFF0000) == 0x1C020000 and i + 12 + size <= n:
            t = parse_native(b, i + 12, i + 12 + size, source)
            if t:
                out.append(t)
                i += 12 + size
                continue
        i += 4
    return out


def _unswizzle(arr, w, h):
    """NV2A swizzle (interleaved x/y bits, x first) -> linear."""
    def bits(n):
        return int(n - 1).bit_length()
    bw, bh = bits(w), bits(h)
    xs = np.arange(w)
    ys = np.arange(h)
    def spread(v, nbits, other, first):
        out = np.zeros_like(v)
        bit = 0
        for k in range(max(nbits, other)):
            for axis_first in ((True, False) if first else (False, True)):
                pass
        return out
    # build the swizzled index of each (x, y)
    xm = np.zeros(w, dtype=np.int64)
    ym = np.zeros(h, dtype=np.int64)
    out_bit = 0
    xb = yb = 0
    while xb < bw or yb < bh:
        if xb < bw:
            xm |= ((xs >> xb) & 1) << out_bit
            out_bit += 1
            xb += 1
        if yb < bh:
            ym |= ((ys >> yb) & 1) << out_bit
            out_bit += 1
            yb += 1
    idx = ym[:, None] | xm[None, :]
    return arr[idx]


def decode(t):
    """RGBA uint8 array (h, w, 4)."""
    w, h, fmt = t.w, t.h, t.fmt
    if FMT[fmt][2]:
        fourcc = {0x0C: b"DXT1", 0x0E: b"DXT3", 0x0F: b"DXT5"}[fmt]
        hdr = bytearray(128)
        struct.pack_into("<4sIIIIIII", hdr, 0, b"DDS ", 124, 0x1007, h, w, len(t.data), 0, 1)
        struct.pack_into("<II4s", hdr, 76, 32, 4, fourcc)
        struct.pack_into("<I", hdr, 108, 0x1000)
        im = Image.open(io.BytesIO(bytes(hdr) + t.data))
        return np.asarray(im.convert("RGBA"))
    raw = np.frombuffer(t.data, dtype=np.uint8)
    if fmt == 0x0B:
        pal = np.frombuffer(t.palette, dtype=np.uint8).reshape(256, 4)[:, [2, 1, 0, 3]]  # BGRA -> RGBA
        lin = _unswizzle(raw, w, h)
        return pal[lin]
    if fmt in (0x06, 0x07):
        px = raw.reshape(-1, 4)[:, [2, 1, 0, 3]].copy()
        if fmt == 0x07:
            px[:, 3] = 255
        return _unswizzle(px, w, h)
    v = raw.view("<u2").astype(np.uint32)
    if fmt == 0x05:
        r, g, b_, a = (v >> 11 & 31) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31, np.full_like(v, 255)
    elif fmt in (0x02, 0x03):
        r, g, b_ = (v >> 10 & 31) * 255 // 31, (v >> 5 & 31) * 255 // 31, (v & 31) * 255 // 31
        a = (v >> 15) * 255 if fmt == 0x02 else np.full_like(v, 255)
    else:  # A4R4G4B4
        a, r, g, b_ = (v >> 12 & 15) * 17, (v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17
    px = np.stack([r, g, b_, a], -1).astype(np.uint8)
    return _unswizzle(px, w, h)
