// gpu/shaders.cpp Hash, for tools/host/xbtex.py through ctypes
#include <stdint.h>
#include <string.h>
static uint64_t mix(uint64_t a, uint64_t b) {
    unsigned __int128 r = (unsigned __int128)a * b;
    return (uint64_t)r ^ (uint64_t)(r >> 64);
}
uint64_t rt_hash(const uint8_t* b, size_t n, uint64_t h) {
    const uint64_t k0 = 0x9E3779B97F4A7C15ull, k1 = 0xE7037ED1A0B428DBull;
    uint64_t seed = h ^ k0;
    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        uint64_t x, y;
        memcpy(&x, b + i, 8);
        memcpy(&y, b + i + 8, 8);
        seed = mix(x ^ k1, y ^ seed);
    }
    size_t r = n - i;
    if (r) {
        uint64_t x = 0, y = 0;
        if (r >= 8) {
            memcpy(&x, b + i, 8);
            memcpy(&y, b + n - 8, 8);
        } else {
            memcpy(&x, b + i, r);
        }
        seed = mix(x ^ k1, y ^ seed ^ r);
    }
    return mix(seed ^ k0, (uint64_t)n ^ k1);
}
