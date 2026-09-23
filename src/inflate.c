/*
 * inflate.c -- deflate, d'apres la RFC 1951.
 *
 * Codes de Huffman canoniques decodes par comptage (methode de "puff" de
 * Mark Adler) : pas de grande table, peu de memoire, chaque acces borne.
 */
#include "inflate.h"
#include "libc.h"

#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30

typedef struct {
    short count[MAXBITS + 1];
    short symbol[MAXLCODES];
} HUFF;

typedef struct {
    HUFF lencode, distcode;
    short lengths[MAXLCODES + MAXDCODES];
} WORK;

typedef char inflate_work_fits[(sizeof(WORK) <= INFLATE_WORK_BYTES) ? 1 : -1];

typedef struct {
    const unsigned char *in;
    long inlen, incnt;
    unsigned char *out;
    long outlen, outcnt;
    unsigned long bitbuf;
    int bitcnt;
    int err;
} STATE;

static int bits(STATE *s, int need)
{
    unsigned long val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt >= s->inlen) { s->err = 1; return 0; }
        val |= (unsigned long)s->in[s->incnt++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1UL << need) - 1));
}

static int stored(STATE *s)
{
    unsigned len;
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->incnt + 4 > s->inlen) return -1;
    len = s->in[s->incnt] | (s->in[s->incnt + 1] << 8);
    if ((s->in[s->incnt + 2] ^ 0xff) != (len & 0xff) || (s->in[s->incnt + 3] ^ 0xff) != (len >> 8))
        return -1;
    s->incnt += 4;
    if (s->incnt + (long)len > s->inlen || s->outcnt + (long)len > s->outlen) return -1;
    memcpy(s->out + s->outcnt, s->in + s->incnt, len);
    s->incnt += len;
    s->outcnt += len;
    return 0;
}

static int decode(STATE *s, const HUFF *h)
{
    int code = 0, first = 0, index = 0, len, count;
    for (len = 1; len <= MAXBITS; len++) {
        code |= bits(s, 1);
        if (s->err) return -1;
        count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

/* Construit h ; 0 si complet, >0 si incomplet (permis pour un seul code),
 * <0 si sur-souscrit. */
static int construct(HUFF *h, const short *length, int n)
{
    short offs[MAXBITS + 1];
    int symbol, len, left;
    for (len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (symbol = 0; symbol < n; symbol++) {
        if (length[symbol] < 0 || length[symbol] > MAXBITS) return -1;
        h->count[length[symbol]]++;
    }
    if (h->count[0] == n) return 0;
    left = 1;
    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (symbol = 0; symbol < n; symbol++)
        if (length[symbol] != 0) h->symbol[offs[length[symbol]]++] = (short)symbol;
    return left;
}

static int codes(STATE *s, const HUFF *lencode, const HUFF *distcode)
{
    static const short lbase[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const short lext[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    static const unsigned short dbase[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
        8193, 12289, 16385, 24577 };
    static const short dext[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
    int symbol;
    long len, dist;
    for (;;) {
        symbol = decode(s, lencode);
        if (symbol < 0) return -1;
        if (symbol < 256) {
            if (s->outcnt >= s->outlen) return -1;
            s->out[s->outcnt++] = (unsigned char)symbol;
        } else if (symbol == 256) {
            return 0;
        } else {
            symbol -= 257;
            if (symbol >= 29) return -1;
            len = lbase[symbol] + bits(s, lext[symbol]);
            symbol = decode(s, distcode);
            if (symbol < 0 || symbol >= 30) return -1;
            dist = dbase[symbol] + bits(s, dext[symbol]);
            if (s->err || dist > s->outcnt || s->outcnt + len > s->outlen) return -1;
            while (len--) {
                s->out[s->outcnt] = s->out[s->outcnt - dist];
                s->outcnt++;
            }
        }
    }
}

static int fixed(STATE *s, WORK *w)
{
    int symbol;
    for (symbol = 0; symbol < 144; symbol++) w->lengths[symbol] = 8;
    for (; symbol < 256; symbol++) w->lengths[symbol] = 9;
    for (; symbol < 280; symbol++) w->lengths[symbol] = 7;
    for (; symbol < 288 && symbol < MAXLCODES; symbol++) w->lengths[symbol] = 8;
    construct(&w->lencode, w->lengths, MAXLCODES);
    for (symbol = 0; symbol < MAXDCODES; symbol++) w->lengths[symbol] = 5;
    construct(&w->distcode, w->lengths, MAXDCODES);
    return codes(s, &w->lencode, &w->distcode);
}

static int dynamic(STATE *s, WORK *w)
{
    static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    int nlen, ndist, ncode, index, err;
    nlen = bits(s, 5) + 257;
    ndist = bits(s, 5) + 1;
    ncode = bits(s, 4) + 4;
    if (s->err || nlen > MAXLCODES || ndist > MAXDCODES) return -1;
    for (index = 0; index < ncode; index++) w->lengths[order[index]] = (short)bits(s, 3);
    for (; index < 19; index++) w->lengths[order[index]] = 0;
    if (s->err) return -1;
    err = construct(&w->lencode, w->lengths, 19);
    if (err != 0) return -1;
    index = 0;
    while (index < nlen + ndist) {
        int symbol = decode(s, &w->lencode), len;
        if (symbol < 0) return -1;
        if (symbol < 16) {
            w->lengths[index++] = (short)symbol;
        } else {
            len = 0;
            if (symbol == 16) {
                if (index == 0) return -1;
                len = w->lengths[index - 1];
                symbol = 3 + bits(s, 2);
            } else if (symbol == 17) {
                symbol = 3 + bits(s, 3);
            } else {
                symbol = 11 + bits(s, 7);
            }
            if (s->err || index + symbol > nlen + ndist) return -1;
            while (symbol--) w->lengths[index++] = (short)len;
        }
    }
    if (w->lengths[256] == 0) return -1;
    err = construct(&w->lencode, w->lengths, nlen);
    if (err < 0 || (err > 0 && nlen - w->lencode.count[0] != 1)) return -1;
    err = construct(&w->distcode, w->lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - w->distcode.count[0] != 1)) return -1;
    return codes(s, &w->lencode, &w->distcode);
}

int inflate_raw(const unsigned char *in, long insize, unsigned char *out, long outsize,
                void *work)
{
    STATE s;
    int last, type, err;
    memset(&s, 0, sizeof s);
    s.in = in;
    s.inlen = insize;
    s.out = out;
    s.outlen = outsize;
    do {
        last = bits(&s, 1);
        type = bits(&s, 2);
        if (s.err) return -1;
        if (type == 0) err = stored(&s);
        else if (type == 1) err = fixed(&s, work);
        else if (type == 2) err = dynamic(&s, work);
        else err = -1;
        if (err || s.err) return -1;
    } while (!last);
    return s.outcnt == outsize ? 0 : -1;
}

unsigned long crc32_update(unsigned long crc, const unsigned char *p, long n)
{
    int k;
    crc = ~crc & 0xffffffffUL;
    while (n-- > 0) {
        crc ^= *p++;
        for (k = 0; k < 8; k++) crc = (crc & 1) ? (crc >> 1) ^ 0xedb88320UL : crc >> 1;
    }
    return ~crc & 0xffffffffUL;
}
