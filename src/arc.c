/*
 * arc.c -- decompression ARC.
 *
 *   1, 2 : stocke                      3 : RLE ($90 = repetition)
 *   4    : "squeeze" (Huffman) + RLE    8 : "crunch" (LZW 12 bits) + RLE
 *   9    : "squash" (LZW 13 bits)
 *
 * Le LZW est celui de compress 4.0 : codes de 9 a n bits, code 256 =
 * remise a zero, codes lus par groupes de n_bits octets, un groupe entame
 * etant abandonne quand la largeur change. Les methodes 5 a 7 (anciens
 * "crunch") et les extensions PAK ne sont pas gerees : ARC_METHOD.
 */
#include "arc.h"
#include "libc.h"

static unsigned int le16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static long le32(const unsigned char *p)
{
    return (long)p[0] | ((long)p[1] << 8) | ((long)p[2] << 16) | ((long)p[3] << 24);
}

int arc_header(const unsigned char *h, long n, ARCHDR *a)
{
    int i;
    memset(a, 0, sizeof *a);
    if (n < 2 || h[0] != 0x1a) return ARC_BAD;
    a->method = h[1];
    if (a->method == 0) return ARC_END;
    a->hsize = a->method == 1 ? 25 : 29;
    if (n < a->hsize) return ARC_BAD;
    for (i = 0; i < 12 && h[2 + i]; i++) a->name[i] = (char)h[2 + i];
    a->packed = le32(h + 15);
    a->date = (unsigned short)le16(h + 19);
    a->time = (unsigned short)le16(h + 21);
    a->crc = (unsigned short)le16(h + 23);
    a->size = a->method == 1 ? a->packed : le32(h + 25);
    if (a->packed < 0 || a->size < 0) return ARC_BAD;
    return ARC_OK;
}

int arc_method_ok(int m)
{
    return m == 1 || m == 2 || m == 3 || m == 4 || m == 8 || m == 9;
}

/* ---- RLE $90 : sortie bornee, a travers un petit etat ---- */

typedef struct {
    unsigned char *out;
    long n, cap;
    int last, dle;
    int err;
} RLE;

static void rle_put(RLE *r, int c)
{
    if (r->dle) {
        r->dle = 0;
        if (c == 0) {
            if (r->n >= r->cap) { r->err = 1; return; }
            r->out[r->n++] = 0x90;
            r->last = 0x90;
        } else {
            int k;
            if (r->last < 0) { r->err = 1; return; }
            for (k = 1; k < c; k++) {
                if (r->n >= r->cap) { r->err = 1; return; }
                r->out[r->n++] = (unsigned char)r->last;
            }
        }
    } else if (c == 0x90) {
        r->dle = 1;
    } else {
        if (r->n >= r->cap) { r->err = 1; return; }
        r->out[r->n++] = (unsigned char)c;
        r->last = c;
    }
}

/* Sortie : directe (squash) ou a travers le RLE. */
typedef struct {
    RLE rle;
    int use_rle;
} SINK;

static void sink_put(SINK *s, int c)
{
    if (s->use_rle) rle_put(&s->rle, c);
    else {
        if (s->rle.n >= s->rle.cap) { s->rle.err = 1; return; }
        s->rle.out[s->rle.n++] = (unsigned char)c;
    }
}

/* ---- squeeze ---- */

static int unsqueeze(const unsigned char *in, long n, SINK *s)
{
    long numnodes, bitpos, i;
    int node = 0;
    if (n < 2) return ARC_BAD;
    numnodes = le16(in);
    if (numnodes > 256) return ARC_BAD;
    if (2 + numnodes * 4 > n) return ARC_BAD;
    if (numnodes == 0) return ARC_OK;           /* rien que la fin */
    bitpos = (2 + numnodes * 4) * 8;
    for (i = 0;; i++) {
        long byte = bitpos >> 3;
        int bit, child;
        if (byte >= n) return ARC_BAD;
        bit = (in[byte] >> (bitpos & 7)) & 1;
        bitpos++;
        child = (short)le16(in + 2 + node * 4 + bit * 2);
        if (child < 0) {
            int v = -(child + 1);
            if (v == 256) return ARC_OK;       /* fin */
            if (v > 256) return ARC_BAD;
            sink_put(s, v);
            if (s->rle.err) return ARC_BAD;
            node = 0;
        } else {
            if (child >= numnodes) return ARC_BAD;
            node = child;
        }
        if (i > n * 8) return ARC_BAD;
    }
}

/* ---- LZW (compress 4.0) ---- */

typedef struct {
    unsigned short prefix[8192];
    unsigned char suffix[8192];
    unsigned char stack[8192];
} LZW;

typedef char arc_work_fits[(sizeof(LZW) <= ARC_WORK_BYTES) ? 1 : -1];

typedef struct {
    const unsigned char *in;
    long n, pos;
    unsigned char buf[16];
    int offset, size, n_bits, maxbits, clear_flg;
    long maxcode, maxmaxcode, free_ent;
} GC;

static long getcode(GC *g)
{
    int r_off, bits, k;
    long code;
    const unsigned char *bp;
    if (g->clear_flg > 0 || g->offset >= g->size || g->free_ent > g->maxcode) {
        if (g->free_ent > g->maxcode) {
            g->n_bits++;
            g->maxcode = (g->n_bits == g->maxbits) ? g->maxmaxcode : (1L << g->n_bits) - 1;
        }
        if (g->clear_flg > 0) {
            g->n_bits = 9;
            g->maxcode = (1L << 9) - 1;
            g->clear_flg = 0;
        }
        if (g->pos >= g->n) return -1;
        g->size = 0;
        for (k = 0; k < g->n_bits && g->pos < g->n; k++) g->buf[g->size++] = g->in[g->pos++];
        g->offset = 0;
        g->size = (g->size << 3) - (g->n_bits - 1);
        if (g->size <= 0) return -1;
    }
    r_off = g->offset;
    bits = g->n_bits;
    bp = g->buf + (r_off >> 3);
    r_off &= 7;
    code = *bp++ >> r_off;
    bits -= 8 - r_off;
    r_off = 8 - r_off;
    if (bits >= 8) {
        code |= (long)*bp++ << r_off;
        r_off += 8;
        bits -= 8;
    }
    code |= (long)(*bp & ((1 << bits) - 1)) << r_off;
    g->offset += g->n_bits;
    return code;
}

static int unlzw(const unsigned char *in, long n, int maxbits, SINK *s, LZW *w)
{
    GC g;
    long code, oldcode, incode;
    int finchar, sp;

    memset(&g, 0, sizeof g);
    g.in = in;
    g.n = n;
    g.maxbits = maxbits;
    g.n_bits = 9;
    g.maxcode = (1L << 9) - 1;
    g.maxmaxcode = 1L << maxbits;
    g.free_ent = 257;
    for (code = 0; code < 256; code++) {
        w->prefix[code] = 0;
        w->suffix[code] = (unsigned char)code;
    }
    oldcode = getcode(&g);
    if (oldcode < 0) return ARC_OK;                 /* vide */
    if (oldcode > 255) return ARC_BAD;
    finchar = (int)oldcode;
    sink_put(s, finchar);
    while ((code = getcode(&g)) > -1) {
        if (code == 256) {
            for (code = 255; code >= 0; code--) w->prefix[code] = 0;
            g.clear_flg = 1;
            g.free_ent = 256;
            if ((code = getcode(&g)) == -1) break;
        }
        incode = code;
        sp = 0;
        if (code >= g.free_ent) {
            if (code > g.free_ent) return ARC_BAD;
            w->stack[sp++] = (unsigned char)finchar;
            code = oldcode;
        }
        while (code >= 256) {
            if (code >= g.maxmaxcode || sp >= 8191) return ARC_BAD;
            w->stack[sp++] = w->suffix[code];
            code = w->prefix[code];
        }
        finchar = w->suffix[code];
        w->stack[sp++] = (unsigned char)finchar;
        while (sp > 0) sink_put(s, w->stack[--sp]);
        if (s->rle.err) return ARC_BAD;
        if (g.free_ent < g.maxmaxcode) {
            w->prefix[g.free_ent] = (unsigned short)oldcode;
            w->suffix[g.free_ent] = (unsigned char)finchar;
            g.free_ent++;
        }
        oldcode = incode;
    }
    return ARC_OK;
}

static unsigned short crc16(const unsigned char *p, long n)
{
    unsigned short crc = 0;
    int k;
    while (n-- > 0) {
        crc ^= *p++;
        for (k = 0; k < 8; k++) crc = (crc & 1) ? (unsigned short)((crc >> 1) ^ 0xa001) : (unsigned short)(crc >> 1);
    }
    return crc;
}

int arc_extract(int method, const unsigned char *in, long insize,
                unsigned char *out, long outsize, unsigned short crc, void *work)
{
    SINK s;
    int r;
    memset(&s, 0, sizeof s);
    s.rle.out = out;
    s.rle.cap = outsize;
    s.rle.last = -1;
    s.use_rle = method != 9 && method != 1 && method != 2;
    switch (method) {
    case 1: case 2:
        if (insize < outsize) return ARC_BAD;
        memcpy(out, in, outsize);
        s.rle.n = outsize;
        r = ARC_OK;
        break;
    case 3: {
        long i;
        for (i = 0; i < insize && !s.rle.err; i++) rle_put(&s.rle, in[i]);
        r = s.rle.err ? ARC_BAD : ARC_OK;
        break;
    }
    case 4:
        r = unsqueeze(in, insize, &s);
        break;
    case 8:
        if (insize < 1 || in[0] != 12) return ARC_BAD;
        r = unlzw(in + 1, insize - 1, 12, &s, work);
        break;
    case 9:
        r = unlzw(in, insize, 13, &s, work);
        break;
    default:
        return ARC_METHOD;
    }
    if (r != ARC_OK || s.rle.err || s.rle.n != outsize) return ARC_BAD;
    return crc16(out, outsize) == crc ? ARC_OK : ARC_CRC;
}
