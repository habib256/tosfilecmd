/*
 * ym.c -- lecture des fichiers YM.
 */
#include "ym.h"
#include "libc.h"

static long be32(const unsigned char *p)
{
    return ((long)p[0] << 24) | ((long)p[1] << 16) | ((long)p[2] << 8) | p[3];
}

static int be16(const unsigned char *p) { return (p[0] << 8) | p[1]; }

/* Chaine terminee par 0 a partir de *pos, sans deborder. */
static const char *ntstring(const unsigned char *d, long size, long *pos)
{
    const char *s = (const char *)d + *pos;
    while (*pos < size && d[*pos]) (*pos)++;
    if (*pos >= size) return 0;
    (*pos)++;
    return s;
}

int ym_parse(const unsigned char *d, long size, YMSONG *s)
{
    long pos;
    int i;

    memset(s, 0, sizeof *s);
    s->title = s->author = s->comment = "";
    s->hz = 50;
    s->clock = 2000000L;
    if (size < 4) return YM_NOT;
    memcpy(s->format, d, 4);
    if (!memcmp(d, "YM2!", 4) || !memcmp(d, "YM3!", 4)) {
        s->nregs = 14;
        s->interleaved = 1;
        s->frames = (size - 4) / 14;
        s->regs = d + 4;
        return s->frames > 0 ? YM_OK : YM_BAD;
    }
    if (!memcmp(d, "YM3b", 4)) {
        if (size < 8 + 14) return YM_BAD;
        s->nregs = 14;
        s->interleaved = 1;
        s->frames = (size - 8) / 14;
        s->loop = be32(d + size - 4);
        s->regs = d + 4;
        if (s->loop < 0 || s->loop >= s->frames) s->loop = 0;
        return YM_OK;
    }
    if (memcmp(d, "YM4!", 4) && memcmp(d, "YM5!", 4) && memcmp(d, "YM6!", 4))
        return YM_NOT;
    if (size < 34 || memcmp(d + 4, "LeOnArD!", 8)) return YM_BAD;
    s->nregs = 16;
    s->frames = be32(d + 12);
    s->interleaved = (int)(be32(d + 16) & 1);
    if (d[2] == '4') {
        /* YM4 : nombre de digidrums sur 32 bits, puis la boucle. */
        long nd = be32(d + 20);
        s->loop = be32(d + 24);
        pos = 28;
        for (i = 0; i < nd; i++) {
            long sz;
            if (pos + 4 > size) return YM_BAD;
            sz = be32(d + pos);
            if (sz < 0 || pos + 4 + sz > size) return YM_BAD;
            pos += 4 + sz;
        }
    } else {
        int nd = be16(d + 20);
        long extra;
        s->clock = be32(d + 22);
        s->hz = be16(d + 26);
        s->loop = be32(d + 28);
        extra = be16(d + 32);
        pos = 34 + extra;
        for (i = 0; i < nd; i++) {
            long sz;
            if (pos + 4 > size) return YM_BAD;
            sz = be32(d + pos);
            if (sz < 0 || pos + 4 + sz > size) return YM_BAD;
            pos += 4 + sz;
        }
    }
    if (!(s->title = ntstring(d, size, &pos)) || !(s->author = ntstring(d, size, &pos)) ||
        !(s->comment = ntstring(d, size, &pos)))
        return YM_BAD;
    if (s->frames <= 0 || s->frames > (size - pos) / 16) return YM_BAD;
    s->regs = d + pos;
    if (s->hz < 10 || s->hz > 1000) s->hz = 50;
    if (s->loop < 0 || s->loop >= s->frames) s->loop = 0;
    return YM_OK;
}

int ym_reg(const YMSONG *s, long f, int r)
{
    if (s->interleaved) return s->regs[(long)r * s->frames + f];
    return s->regs[f * s->nregs + r];
}
