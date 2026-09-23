/*
 * lzh.c -- lecture des archives LHA, decompression -lh5-.
 *
 * -lh5- : LZ77 (fenetre de 8 Ko, correspondances de 3 a 256 octets) et
 * codes de Huffman statiques par blocs, comme le LHA de Haruhiko Okumura et
 * Haruyasu Yoshizaki. On decompresse directement dans le tampon de sortie
 * (le fichier entier), qui sert de fenetre : pas de dictionnaire a part.
 */
#include "lzh.h"
#include "libc.h"

#define DICBIT 13
#define MAXMATCH 256
#define THRESHOLD 3
#define NC (256 + MAXMATCH - THRESHOLD + 1)     /* 510 */
#define NP (DICBIT + 1)                          /* 14 */
#define NT 19
#define NPT NT
#define CBIT 9
#define PBIT 4
#define TBIT 5

typedef struct {
    const unsigned char *src;
    long pos, end;
    unsigned short bitbuf;
    unsigned int subbitbuf;
    int bitcount;
    int err;
    unsigned int blocksize;
    unsigned char c_len[NC], pt_len[NPT + 1];
    unsigned short c_table[4096], pt_table[256];
    unsigned short left[2 * NC - 1], right[2 * NC - 1];
} LH;

/* La memoire de travail annoncee dans lzh.h doit contenir LH : sinon la
 * compilation echoue (tableau de taille negative). */
typedef char lzh_work_fits[(sizeof(LH) <= LZH_WORK_BYTES) ? 1 : -1];

static unsigned short le16(const unsigned char *p) { return (unsigned short)(p[0] | (p[1] << 8)); }
static long le32(const unsigned char *p)
{
    return (long)p[0] | ((long)p[1] << 8) | ((long)p[2] << 16) | ((long)p[3] << 24);
}

int lzh_is_archive(const unsigned char *a, long n)
{
    return n >= 22 && a[2] == '-' && a[3] == 'l' && a[4] == 'h' && a[6] == '-';
}

int lzh_entry(const unsigned char *a, long n, long off, LZHENTRY *e)
{
    const unsigned char *h;
    int level, nl, i;
    long hsize;

    if (off >= n || a[off] == 0) return LZH_END;
    if (off + 22 > n) return LZH_BAD;
    h = a + off;
    level = h[20];
    memset(e, 0, sizeof *e);
    memcpy(e->method, h + 2, 5);
    e->packed = le32(h + 7);
    e->size = le32(h + 11);
    if (e->packed < 0 || e->size < 0) return LZH_BAD;
    if (level == 0 || level == 1) {
        hsize = h[0] + 2;
        nl = h[21];
        if (off + hsize > n || 22 + nl + 2 > hsize) return LZH_BAD;
        for (i = 0; i < nl && i < 63; i++) e->name[i] = (char)h[22 + i];
        e->crc = le16(h + 22 + nl);
        e->data = off + hsize;
        if (level == 1) {
            /* Extensions apres l'en-tete, comptees dans packed. */
            long p = e->data - 2, sz;
            while (p + 2 <= n && (sz = le16(a + p)) != 0) {
                if (sz < 3 || p + 2 + sz > n) return LZH_BAD;
                e->packed -= sz;
                p += sz;
            }
            e->data = p + 2;
            if (e->packed < 0) return LZH_BAD;
        }
    } else if (level == 2) {
        long p;
        hsize = le16(h);
        if (hsize < 26 || off + hsize > n) return LZH_BAD;
        e->crc = le16(h + 21);
        /* Nom dans l'extension de type 1. */
        p = off + 24;
        while (p + 3 <= off + hsize) {
            long sz = le16(a + p);
            if (sz == 0) break;
            if (sz < 3 || p + sz > off + hsize) return LZH_BAD;
            if (a[p + 2] == 1)
                for (i = 0; i < sz - 3 && i < 63; i++) e->name[i] = (char)a[p + 3 + i];
            p += sz;
        }
        e->data = off + hsize;
    } else {
        return LZH_BAD;
    }
    e->next = e->data + e->packed;
    if (e->next > n) return LZH_BAD;
    return LZH_OK;
}

/* ---- lecture des bits ---- */

static void fillbuf(LH *l, int n)
{
    while (n > l->bitcount) {
        n -= l->bitcount;
        l->bitbuf = (unsigned short)((l->bitbuf << l->bitcount) +
                                     (l->subbitbuf >> (8 - l->bitcount)));
        if (l->pos < l->end) l->subbitbuf = l->src[l->pos];
        else {
            l->subbitbuf = 0;
            /* Quelques octets de marge : le decodeur lit en avance. */
            if (l->pos > l->end + 4) l->err = 1;
        }
        l->pos++;
        l->bitcount = 8;
    }
    l->bitcount -= n;
    l->bitbuf = (unsigned short)((l->bitbuf << n) + (l->subbitbuf >> (8 - n)));
    l->subbitbuf = (l->subbitbuf << n) & 0xff;
}

static unsigned int getbits(LH *l, int n)
{
    unsigned int x;
    if (n == 0) return 0;
    x = l->bitbuf >> (16 - n);
    fillbuf(l, n);
    return x;
}

/* ---- tables de Huffman (make_table de LHA, bornes verifiees) ---- */

static int make_table(LH *l, int nchar, const unsigned char *bitlen, int tablebits,
                      unsigned short *table)
{
    unsigned long count[17], weight[17], start[18];
    unsigned int i, k, len, ch, jutbits, avail, nextcode, mask, tsize = 1U << tablebits;
    unsigned short *p;

    for (i = 1; i <= 16; i++) count[i] = 0;
    for (i = 0; i < (unsigned)nchar; i++) {
        if (bitlen[i] > 16) return -1;
        count[bitlen[i]]++;
    }
    start[1] = 0;
    for (i = 1; i <= 16; i++) start[i + 1] = start[i] + (count[i] << (16 - i));
    if (start[17] != (1UL << 16)) return -1;            /* codes incoherents */
    jutbits = 16 - tablebits;
    for (i = 1; i <= (unsigned)tablebits; i++) {
        start[i] >>= jutbits;
        weight[i] = 1UL << (tablebits - i);
    }
    while (i <= 16) {
        weight[i] = 1UL << (16 - i);
        i++;
    }
    /* Les entrees des codes longs recevront les racines des arbres : elles
     * doivent partir de zero (une table sert pour plusieurs blocs). */
    i = (unsigned int)(start[tablebits + 1] >> jutbits);
    while (i < tsize) table[i++] = 0;
    avail = nchar;
    mask = 1U << (15 - tablebits);
    for (ch = 0; ch < (unsigned)nchar; ch++) {
        len = bitlen[ch];
        if (len == 0) continue;
        k = (unsigned int)start[len];
        nextcode = k + (unsigned int)weight[len];
        if ((int)len <= tablebits) {
            if (nextcode > tsize) return -1;
            for (i = k; i < nextcode; i++) table[i] = (unsigned short)ch;
        } else {
            if ((k >> jutbits) >= tsize) return -1;
            p = &table[k >> jutbits];
            i = len - tablebits;
            while (i != 0) {
                if (*p == 0) {
                    if (avail >= 2 * NC - 1) return -1;
                    l->right[avail] = l->left[avail] = 0;
                    *p = (unsigned short)avail++;
                }
                if (*p >= 2 * NC - 1) return -1;
                p = (k & mask) ? &l->right[*p] : &l->left[*p];
                k <<= 1;
                i--;
            }
            *p = (unsigned short)ch;
        }
        start[len] = nextcode;
    }
    return 0;
}

static int read_pt_len(LH *l, int nn, int nbit, int i_special)
{
    int i, c, n;
    unsigned int mask;

    n = (int)getbits(l, nbit);
    if (n == 0) {
        c = (int)getbits(l, nbit);
        if (c >= nn) return -1;
        for (i = 0; i < nn; i++) l->pt_len[i] = 0;
        for (i = 0; i < 256; i++) l->pt_table[i] = (unsigned short)c;
        return 0;
    }
    if (n > nn) return -1;
    i = 0;
    while (i < n) {
        c = l->bitbuf >> 13;
        if (c == 7) {
            mask = 1U << 12;
            while (mask & l->bitbuf) {
                mask >>= 1;
                c++;
                if (c > 16) return -1;
            }
        }
        fillbuf(l, (c < 7) ? 3 : c - 3);
        l->pt_len[i++] = (unsigned char)c;
        if (i == i_special) {
            c = (int)getbits(l, 2);
            while (--c >= 0 && i < nn) l->pt_len[i++] = 0;
        }
    }
    while (i < nn) l->pt_len[i++] = 0;
    return make_table(l, nn, l->pt_len, 8, l->pt_table);
}

static int read_c_len(LH *l)
{
    int i, c, n;
    unsigned int mask;

    n = (int)getbits(l, CBIT);
    if (n == 0) {
        c = (int)getbits(l, CBIT);
        if (c >= NC) return -1;
        for (i = 0; i < NC; i++) l->c_len[i] = 0;
        for (i = 0; i < 4096; i++) l->c_table[i] = (unsigned short)c;
        return 0;
    }
    if (n > NC) return -1;
    i = 0;
    while (i < n) {
        int guard = 0;
        c = l->pt_table[l->bitbuf >> 8];
        if (c >= NT) {
            mask = 1U << 7;
            do {
                if (c >= 2 * NC - 1 || ++guard > 16) return -1;
                c = (l->bitbuf & mask) ? l->right[c] : l->left[c];
                mask >>= 1;
            } while (c >= NT);
        }
        fillbuf(l, l->pt_len[c]);
        if (c <= 2) {
            if (c == 0) c = 1;
            else if (c == 1) c = (int)getbits(l, 4) + 3;
            else c = (int)getbits(l, CBIT) + 20;
            if (i + c > NC) return -1;
            while (--c >= 0) l->c_len[i++] = 0;
        } else {
            if (c - 2 > 16) return -1;
            l->c_len[i++] = (unsigned char)(c - 2);
        }
    }
    while (i < NC) l->c_len[i++] = 0;
    return make_table(l, NC, l->c_len, 12, l->c_table);
}

static int decode_c(LH *l)
{
    int j, guard = 0;
    unsigned int mask;
    if (l->blocksize == 0) {
        l->blocksize = getbits(l, 16);
        if (l->blocksize == 0) return -1;
        if (read_pt_len(l, NT, TBIT, 3) || read_c_len(l) || read_pt_len(l, NP, PBIT, -1))
            return -1;
    }
    l->blocksize--;
    j = l->c_table[l->bitbuf >> 4];
    if (j < NC) {
        fillbuf(l, l->c_len[j]);
    } else {
        fillbuf(l, 12);
        mask = 1U << 15;
        do {
            if (j >= 2 * NC - 1 || ++guard > 16) return -1;
            j = (l->bitbuf & mask) ? l->right[j] : l->left[j];
            mask >>= 1;
        } while (j >= NC);
        fillbuf(l, l->c_len[j] - 12);
    }
    return j;
}

static long decode_p(LH *l)
{
    int j, guard = 0;
    unsigned int mask;
    j = l->pt_table[l->bitbuf >> 8];
    if (j < NP) {
        fillbuf(l, l->pt_len[j]);
    } else {
        fillbuf(l, 8);
        mask = 1U << 15;
        do {
            if (j >= 2 * NC - 1 || ++guard > 16) return -1;
            j = (l->bitbuf & mask) ? l->right[j] : l->left[j];
            mask >>= 1;
        } while (j >= NP);
        fillbuf(l, l->pt_len[j] - 8);
    }
    if (j != 0) return (1L << (j - 1)) + (long)getbits(l, j - 1);
    return 0;
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

int lzh_extract(const unsigned char *a, long n, const LZHENTRY *e,
                unsigned char *out, void *work)
{
    LH *l = work;
    long count = 0;

    if (e->data + e->packed > n) return LZH_BAD;
    if (!memcmp(e->method, "-lh0-", 5)) {
        if (e->packed != e->size) return LZH_BAD;
        memcpy(out, a + e->data, e->size);
    } else if (!memcmp(e->method, "-lh5-", 5)) {
        memset(l, 0, sizeof *l);
        l->src = a + e->data;
        l->end = e->packed;
        fillbuf(l, 16);
        while (count < e->size) {
            int c = decode_c(l);
            if (c < 0 || l->err) return LZH_BAD;
            if (c < 256) {
                out[count++] = (unsigned char)c;
            } else {
                long len = c - 256 + THRESHOLD, from = count - decode_p(l) - 1;
                if (from < 0 || l->err) return LZH_BAD;
                if (count + len > e->size) return LZH_BAD;
                while (len-- > 0) out[count++] = out[from++];
            }
        }
    } else {
        return LZH_METHOD;
    }
    return crc16(out, e->size) == e->crc ? LZH_OK : LZH_CRC;
}
