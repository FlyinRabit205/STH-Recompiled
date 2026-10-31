// Shadow the Hedgehog "One Ver 0.60" archive extractor (Sega PRS compression).
// usage: one_extract <file.one> <outdir>
// Layout: 0x1C u32 file count; entries from 0xB0, 0x38 bytes each: name[0x2C], u32 unpacked size,
// u32 data offset (from 0x0C), u32 unknown. An entry's packed bytes run to the next entry's offset (or the end).
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t prs_decompress(const uint8_t* src, size_t n, uint8_t* out, size_t cap) {
    size_t i = 0, o = 0;
    int bits = 0;
    unsigned ctrl = 0;
#define GETBIT() (bits == 0 ? (ctrl = (i < n ? src[i++] : 0), bits = 8, 0) : 0, bits--, (int)((ctrl >> (7 - bits)) & 1))
    for (;;) {
        if (GETBIT()) {
            if (i >= n || o >= cap) break;
            out[o++] = src[i++];
            continue;
        }
        long offset;
        size_t size;
        if (GETBIT()) {
            if (i + 1 >= n) break;
            unsigned v = src[i] | src[i + 1] << 8;
            i += 2;
            if (v == 0) break;
            offset = (long)(v >> 3) - 0x2000;
            size = v & 7;
            if (size == 0) {
                if (i >= n) break;
                size = (size_t)src[i++] + 1;
            } else {
                size += 2;
            }
        } else {
            int a = GETBIT();
            int b = GETBIT();
            size = (size_t)((a << 1) | b) + 2;
            if (i >= n) break;
            offset = (long)src[i++] - 0x100;
        }
        for (size_t k = 0; k < size; k++) {
            if (o >= cap || (long)o + offset < 0) return o;
            out[o] = out[o + offset];
            o++;
        }
    }
    return o;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: one_extract file.one outdir\n");
        return 2;
    }
    FILE* f = fopen(argv[1], "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    size_t n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* d = malloc(n);
    if (fread(d, 1, n, f) != n) return 1;
    fclose(f);
    if (n < 0xB0 || memcmp(d + 0x0C, "One Ver 0.60", 12) != 0) {
        fprintf(stderr, "%s: not a One archive\n", argv[1]);
        return 1;
    }
    uint32_t count;
    memcpy(&count, d + 0x1C, 4);
    for (uint32_t e = 0; e < count; e++) {
        const uint8_t* ent = d + 0xB0 + (size_t)e * 0x38;
        if ((size_t)(ent - d) + 0x38 > n) break;
        char name[0x2D] = {0};
        memcpy(name, ent, 0x2C);
        uint32_t usize, off, next = (uint32_t)(n - 0x0C);
        memcpy(&usize, ent + 0x2C, 4);
        memcpy(&off, ent + 0x30, 4);
        if (e + 1 < count) memcpy(&next, ent + 0x38 + 0x30, 4);
        size_t start = (size_t)off + 0x0C, end = (size_t)next + 0x0C;
        if (start >= n || end > n || end < start || usize > (256u << 20)) continue;
        uint8_t* out = malloc(usize ? usize : 1);
        size_t got = prs_decompress(d + start, end - start, out, usize);
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", argv[2], name);
        FILE* w = fopen(path, "wb");
        if (w) {
            fwrite(out, 1, got, w);
            fclose(w);
        }
        printf("%s %u %zu\n", name, usize, got);
        free(out);
    }
    return 0;
}
