/*
 * glibc_compat.c
 *
 * Compatibility shim to allow the binary to run on glibc 2.35 (Ubuntu 22.04
 * Jammy Jellyfish) when compiled on glibc 2.38+ (Ubuntu 24.04 Noble).
 *
 * Symbols resolved:
 *   __isoc23_strtol   -> strtol   (C23 variant, added in glibc 2.38)
 *   __isoc23_strtoul  -> strtoul  (C23 variant, added in glibc 2.38)
 *   arc4random        -> xorshift32 impl (added in glibc 2.36)
 *   arc4random_buf    -> built on arc4random above
 *
 * Source: written for obj2msh Jammy compatibility
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

/* ── C23 strtol/strtoul redirects ──────────────────────────────────────── */
long __isoc23_strtol(const char *nptr, char **endptr, int base) {
    return strtol(nptr, endptr, base);
}

unsigned long __isoc23_strtoul(const char *nptr, char **endptr, int base) {
    return strtoul(nptr, endptr, base);
}

/* ── arc4random replacement ─────────────────────────────────────────────── */
static uint32_t _arc4_state  = 0;
static int      _arc4_seeded = 0;

static void _arc4_seed(void) {
    if (_arc4_seeded) return;
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        if (fread(&_arc4_state, sizeof(_arc4_state), 1, f) != 1)
            _arc4_state = (uint32_t)time(NULL);
        fclose(f);
    } else {
        _arc4_state = (uint32_t)time(NULL);
    }
    if (_arc4_state == 0) _arc4_state = 1;
    _arc4_seeded = 1;
}

uint32_t arc4random(void) {
    _arc4_seed();
    /* xorshift32 */
    _arc4_state ^= _arc4_state << 13;
    _arc4_state ^= _arc4_state >> 17;
    _arc4_state ^= _arc4_state << 5;
    return _arc4_state;
}

void arc4random_buf(void *buf, size_t n) {
    uint8_t *p = (uint8_t *)buf;
    size_t i = 0;
    while (i + 4 <= n) {
        uint32_t r = arc4random();
        memcpy(p + i, &r, 4);
        i += 4;
    }
    if (i < n) {
        uint32_t r = arc4random();
        memcpy(p + i, &r, n - i);
    }
}
