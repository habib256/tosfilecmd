/*
 * ice.c -- ICE! 2.4, d'apres la routine 68000 d'Axe (Delight).
 *
 * Le flux se lit a l'envers, depuis la fin des donnees compressees, et la
 * sortie se remplit aussi a l'envers, depuis la fin. Les bits viennent
 * d'un octet tampon a sentinelle (l'astuce add.b/addx.b de l'original).
 * Chaque lecture et chaque ecriture est bornee ; une erreur arrete tout.
 */
#include "ice.h"

typedef struct {
    const unsigned char *src;   /* debut des donnees (apres l'en-tete) */
    long in;                    /* prochain octet a lire : src[--in] */
    unsigned char *out;
    long o;                     /* prochain octet a ecrire : out[--o] */
    unsigned int bits;          /* octet tampon, sentinelle comprise */
    int err;
} ICE;

static long be32(const unsigned char *p)
{
    return ((long)p[0] << 24) | ((long)p[1] << 16) | ((long)p[2] << 8) | p[3];
}

long ice_size(const unsigned char *d, long size)
{
    long csize, dsize;
    if (size < 12) return -1;
    if (!((d[0] == 'I' && d[1] == 'C' && d[2] == 'E' && d[3] == '!') ||
          (d[0] == 'I' && d[1] == 'c' && d[2] == 'e' && d[3] == '!')))
        return -1;
    csize = be32(d + 4);
    dsize = be32(d + 8);
    if (csize < 12 || csize > size || dsize <= 0) return -1;
    return dsize;
}

static int getbyte(ICE *s)
{
    if (s->in <= 0) { s->err = 1; return 0; }
    return s->src[--s->in];
}

/* add.b d7,d7 ; bne ; move.b -(a5),d7 ; addx.b d7,d7 */
static int bit(ICE *s)
{
    unsigned int r = (s->bits & 0xff) << 1;
    if (r & 0xff) {
        s->bits = r & 0xff;
        return (int)(r >> 8);
    }
    r = ((unsigned int)getbyte(s) << 1) | (r >> 8);
    s->bits = r & 0xff;
    return (int)(r >> 8);
}

/* get_d0_bits : n + 1 bits, poids fort d'abord. */
static long bits(ICE *s, int n)
{
    long v = 0;
    while (n-- >= 0) v = (v << 1) | bit(s);
    return v;
}

static void put(ICE *s, unsigned char c)
{
    if (s->o <= 0) { s->err = 1; return; }
    s->out[--s->o] = c;
}

int ice_unpack(const unsigned char *d, long size, unsigned char *out, long outsize)
{
    /* Litteraux : combien de bits lire, et le maximum qui fait passer au
     * cas suivant ; la base a ajouter si l'on s'arrete a ce cas. */
    static const unsigned char lit_bits[5] = { 1, 1, 2, 7, 14 };
    static const unsigned short lit_max[5] = { 3, 3, 7, 255, 0x7fff };
    static const unsigned short lit_base[5] = { 1, 4, 7, 14, 269 };
    static const signed char len_bits[5] = { 9, 1, 0, -1, -1 };
    static const unsigned char len_base[5] = { 8, 4, 2, 1, 0 };
    ICE s;
    long csize = be32(d + 4), dsize = ice_size(d, size);

    if (dsize < 0 || dsize != outsize) return -1;
    s.src = d + 12;
    s.in = csize - 12;
    s.out = out;
    s.o = dsize;
    s.err = 0;
    s.bits = (unsigned int)getbyte(&s);

    for (;;) {
        /* normal_bytes : des litteraux ? */
        if (bit(&s)) {
            long n = 0;
            if (bit(&s)) {
                int k;
                for (k = 0; k < 5; k++) {
                    n = bits(&s, lit_bits[k]);
                    if (n != lit_max[k]) break;
                }
                if (k == 5) k = 4;
                n += lit_base[k];
            }
            /* n + 1 octets tels quels */
            while (n-- >= 0) put(&s, (unsigned char)getbyte(&s));
            if (s.err) return -1;
        }
        if (s.o <= 0) break;

        /* strings : une repetition */
        {
            int k = 3;
            long len, off;
            while (k >= 0 && bit(&s)) k--;
            /* k : 3 (0 bit a 1) .. -1 (quatre bits a 1) ; index = k + 1 */
            len = 0;
            if (len_bits[k + 1] >= 0) len = bits(&s, len_bits[k + 1]);
            len += len_base[k + 1];
            if (len == 0) {
                /* Deux octets, distance courte. */
                if (bit(&s)) off = bits(&s, 8) + 0x3f;
                else off = bits(&s, 5) - 1;
            } else {
                int j = 1;
                static const unsigned char off_bits[3] = { 11, 4, 7 };  /* j = -1, 0, 1 */
                static const short off_base[3] = { 0x11f, -1, 0x1f };
                while (j >= 0 && bit(&s)) j--;
                off = bits(&s, off_bits[j + 1]) + off_base[j + 1];
                if (off < 0) off -= len;
            }
            /* depack_bytes : len + 2 octets, pris a off + len + 2 plus loin. */
            {
                long from = s.o + 2 + len + off, i;
                if (s.err || from > dsize || from - (len + 2) < 0) return -1;
                for (i = 0; i < len + 2; i++) {
                    if (s.o <= 0) return -1;
                    s.out[--s.o] = s.out[--from];
                }
            }
        }
        if (s.err) return -1;
        /* Comme l'original : retour a normal_bytes, qui relit un bit meme si
         * la sortie est pleine (le compresseur y a ecrit un 0). */
    }
    if (s.o != 0 || s.err) return -1;

    /* Transformation "image" : quatre plans remis en mots. */
    if (bit(&s)) {
        long groups = 3999, p = dsize;
        if (bit(&s)) groups = bits(&s, 15);
        if (s.err) return -1;
        while (groups-- >= 0) {
            unsigned int d0 = 0, d1 = 0, d2 = 0, d3 = 0;
            int w, b;
            if (p < 8) return -1;
            for (w = 0; w < 4; w++) {
                unsigned int d4;
                p -= 2;
                d4 = ((unsigned int)out[p] << 8) | out[p + 1];
                for (b = 0; b < 4; b++) {
                    d0 = (d0 << 1) | ((d4 >> 15) & 1); d4 = (d4 << 1) & 0xffff;
                    d1 = (d1 << 1) | ((d4 >> 15) & 1); d4 = (d4 << 1) & 0xffff;
                    d2 = (d2 << 1) | ((d4 >> 15) & 1); d4 = (d4 << 1) & 0xffff;
                    d3 = (d3 << 1) | ((d4 >> 15) & 1); d4 = (d4 << 1) & 0xffff;
                }
            }
            out[p] = (unsigned char)(d0 >> 8); out[p + 1] = (unsigned char)d0;
            out[p + 2] = (unsigned char)(d1 >> 8); out[p + 3] = (unsigned char)d1;
            out[p + 4] = (unsigned char)(d2 >> 8); out[p + 5] = (unsigned char)d2;
            out[p + 6] = (unsigned char)(d3 >> 8); out[p + 7] = (unsigned char)d3;
        }
    }
    return 0;
}
