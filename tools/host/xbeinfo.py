#!/usr/bin/env python3
"""Dump the important parts of an Xbox XBE: header, sections, kernel imports, XDK libraries."""
import struct, sys, json

RETAIL_EP = 0xA8FC57AB
RETAIL_KT = 0x5B6D40B6
DEBUG_EP = 0x94859D4B
DEBUG_KT = 0xEFB1F152


def main(path, jsonout=None):
    d = open(path, "rb").read()
    assert d[:4] == b"XBEH", "not an XBE"
    base, = struct.unpack_from("<I", d, 0x104)
    (hdr_size, img_size, img_hdr_size, timestamp, cert_addr, nsec, sec_addr,
     init_flags, ep_enc) = struct.unpack_from("<IIIIIIIII", d, 0x108)
    tls_addr, = struct.unpack_from("<I", d, 0x12C)
    kt_enc, = struct.unpack_from("<I", d, 0x158)
    nlib, lib_addr = struct.unpack_from("<II", d, 0x160)

    def off(va):
        return va - base

    if (ep_enc ^ RETAIL_EP) - base < 0x10000000:
        ep, kt, kind = ep_enc ^ RETAIL_EP, kt_enc ^ RETAIL_KT, "retail"
    else:
        ep, kt, kind = ep_enc ^ DEBUG_EP, kt_enc ^ DEBUG_KT, "debug"

    title = d[off(cert_addr) + 0xC: off(cert_addr) + 0xC + 80].decode("utf-16le").rstrip("\0")
    title_id, = struct.unpack_from("<I", d, off(cert_addr) + 8)
    print(f"Title: {title}  TitleID: {title_id:08X}  ({kind})")
    print(f"Base {base:08X}  ImageSize {img_size:08X}  Entry {ep:08X}  KernelThunk {kt:08X}  TLS {tls_addr:08X}")

    secs = []
    print(f"\n{nsec} sections:")
    for i in range(nsec):
        o = off(sec_addr) + i * 56
        flags, va, vsz, raw, rsz, name_addr = struct.unpack_from("<IIIIII", d, o)
        nm = d[off(name_addr):d.index(b"\0", off(name_addr))].decode()
        fl = "".join(c for c, b in (("W", 1), ("P", 2), ("X", 4), ("I", 8)) if flags & b)
        secs.append(dict(name=nm, va=va, vsize=vsz, raw=raw, rsize=rsz, flags=flags))
        print(f"  {nm:<10} VA {va:08X} size {vsz:>8X} file {raw:08X}+{rsz:<8X} {fl}")

    def read_va(va, n):
        for s in secs:
            if s["va"] <= va < s["va"] + s["rsize"]:
                p = s["raw"] + va - s["va"]
                return d[p:p + n]
        if va - base < img_hdr_size:
            return d[va - base: va - base + n]
        return None

    kimports = []
    i = 0
    while True:
        b = read_va(kt + 4 * i, 4)
        if not b:
            break
        v, = struct.unpack("<I", b)
        if v == 0:
            break
        kimports.append(v & 0x7FFFFFFF)
        i += 1
    print(f"\n{len(kimports)} kernel imports (ordinals): {sorted(kimports)}")

    libs = []
    print(f"\n{nlib} XDK libraries:")
    for i in range(nlib):
        o = off(lib_addr) + i * 16
        nm = d[o:o + 8].rstrip(b"\0").decode()
        maj, mnr, bld, fl = struct.unpack_from("<HHHH", d, o + 8)
        libs.append(dict(name=nm, version=f"{maj}.{mnr}.{bld}"))
        print(f"  {nm:<8} {maj}.{mnr}.{bld}")

    if jsonout:
        json.dump(dict(title=title, title_id=f"{title_id:08X}", base=base, entry=ep, kernel_thunk=kt,
                       tls=tls_addr, sections=secs, kernel_imports=kimports, libraries=libs),
                  open(jsonout, "w"), indent=1)


if __name__ == "__main__":
    main(*sys.argv[1:])
