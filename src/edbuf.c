/*
 * edbuf.c -- tampon a trou de l'editeur.
 */
#include "edbuf.h"
#include "libc.h"

int eb_load(EDBUF *b, const unsigned char *data, long size, char *mem, long memcap)
{
    long i, n = 0, lf = 0, crlf = 0, cr = 0;

    for (i = 0; i < size; i++) {
        unsigned char c = data[i];
        if (c == 0) return EB_BINARY;
        if (c == '\r') {
            if (i + 1 < size && data[i + 1] == '\n') { crlf++; i++; }
            else cr++;
        } else if (c == '\n') {
            lf++;
        }
    }
    if ((lf != 0) + (crlf != 0) + (cr != 0) > 1) return EB_MIXED;
    b->eol = crlf ? EOL_CRLF : cr ? EOL_CR : lf ? EOL_LF : EOL_CRLF;
    if (size - crlf + 1024 > memcap) return EB_NOROOM;

    /* Texte en fin de memoire, trou devant : on ajoute surtout a la fin et
     * au debut, et deplacer le trou coute peu. Ici le texte va a la fin. */
    b->buf = mem;
    b->cap = memcap;
    for (i = 0; i < size; i++) {
        unsigned char c = data[i];
        if (c == '\r') {
            if (b->eol == EOL_CRLF) { i++; c = '\n'; }
            else c = '\n';
        }
        mem[n++] = (char)c;
    }
    /* Deplacement vers la fin du tampon (le trou au debut). */
    memmove(mem + memcap - n, mem, n);
    b->gap0 = 0;
    b->gap1 = memcap - n;
    b->modified = 0;
    b->nlines = lf + crlf + cr;
    return EB_OK;
}


static void move_gap(EDBUF *b, long pos)
{
    if (pos < b->gap0) {
        long n = b->gap0 - pos;
        memmove(b->buf + b->gap1 - n, b->buf + pos, n);
        b->gap0 -= n;
        b->gap1 -= n;
    } else if (pos > b->gap0) {
        long n = pos - b->gap0;
        memmove(b->buf + b->gap0, b->buf + b->gap1, n);
        b->gap0 += n;
        b->gap1 += n;
    }
}

int eb_insert(EDBUF *b, long pos, const char *s, long n)
{
    if (pos < 0 || pos > eb_len(b)) return EB_FULL;
    if (b->gap1 - b->gap0 < n) return EB_FULL;
    move_gap(b, pos);
    memcpy(b->buf + b->gap0, s, n);
    b->gap0 += n;
    if (n) b->modified = 1;
    while (n-- > 0) if (*s++ == '\n') b->nlines++;
    return EB_OK;
}

void eb_delete(EDBUF *b, long pos, long n)
{
    long len = eb_len(b);
    if (pos < 0 || pos >= len || n <= 0) return;
    if (pos + n > len) n = len - pos;
    move_gap(b, pos);
    {
        long i;
        for (i = 0; i < n; i++) if (b->buf[b->gap1 + i] == '\n') b->nlines--;
    }
    b->gap1 += n;
    b->modified = 1;
}

/* Les deux recherches parcourent directement les deux morceaux du tampon
 * (avant et apres le trou) : c'est le coeur de chaque deplacement. */
long eb_line_start(const EDBUF *b, long pos)
{
    long gap = b->gap1 - b->gap0;
    const char *s;
    if (pos > eb_len(b)) pos = eb_len(b);
    if (pos > b->gap0) {
        const char *lo = b->buf + b->gap1;
        s = b->buf + pos + gap;
        while (s > lo && s[-1] != '\n') s--;
        if (s > lo) return (long)(s - b->buf) - gap;
        pos = b->gap0;
    }
    s = b->buf + pos;
    while (s > b->buf && s[-1] != '\n') s--;
    return (long)(s - b->buf);
}

long eb_line_end(const EDBUF *b, long pos)
{
    long gap = b->gap1 - b->gap0;
    const char *s, *e;
    if (pos < 0) pos = 0;
    if (pos < b->gap0) {
        s = b->buf + pos;
        e = b->buf + b->gap0;
        while (s < e && *s != '\n') s++;
        if (s < e) return (long)(s - b->buf);
        pos = b->gap0;
    }
    s = b->buf + pos + gap;
    e = b->buf + b->cap;
    while (s < e && *s != '\n') s++;
    return (long)(s - b->buf) - gap;
}

long eb_next_line(const EDBUF *b, long pos)
{
    long e = eb_line_end(b, pos);
    return e < eb_len(b) ? e + 1 : -1;
}

long eb_prev_line(const EDBUF *b, long pos)
{
    long s = eb_line_start(b, pos);
    return s > 0 ? eb_line_start(b, s - 1) : -1;
}

long eb_line_number(const EDBUF *b, long pos)
{
    long n = 1, i;
    for (i = 0; i < pos; i++)
        if (eb_at(b, i) == '\n') n++;
    return n;
}

int eb_col(const EDBUF *b, long pos)
{
    long i = eb_line_start(b, pos);
    int col = 0;
    for (; i < pos; i++)
        col = eb_at(b, i) == '\t' ? (col + 8) & ~7 : col + 1;
    return col;
}

long eb_pos_at_col(const EDBUF *b, long start, int col)
{
    long p = start, len = eb_len(b);
    int c = 0;
    while (p < len) {
        int ch = eb_at(b, p), w;
        if (ch == '\n') break;
        w = ch == '\t' ? 8 - (c & 7) : 1;
        if (c + w > col) break;
        c += w;
        p++;
    }
    return p;
}

long eb_file_size(const EDBUF *b)
{
    return eb_len(b) + (b->eol == EOL_CRLF ? b->nlines : 0);
}

long eb_export(const EDBUF *b, long at, char *out, long cap)
{
    /* at est une position dans le fichier : on retrouve la position logique
     * correspondante (en CRLF, chaque LF logique vaut deux octets). */
    long lpos = 0, fpos = 0, n = 0, len = eb_len(b);
    int half = 0;           /* 1 : le CR d'un CRLF est deja sorti */

    if (b->eol == EOL_CRLF) {
        while (lpos < len && fpos < at) {
            if (eb_at(b, lpos) == '\n') {
                if (fpos + 1 == at) { half = 1; fpos++; break; }
                fpos += 2;
            } else {
                fpos++;
            }
            lpos++;
        }
    } else {
        lpos = at;
    }
    while (n < cap && lpos < len) {
        int c = eb_at(b, lpos);
        if (c == '\n') {
            if (b->eol == EOL_CRLF) {
                if (!half) {
                    out[n++] = '\r';
                    half = 1;
                    continue;
                }
                out[n++] = '\n';
                half = 0;
            } else {
                out[n++] = b->eol == EOL_CR ? '\r' : '\n';
            }
        } else {
            out[n++] = (char)c;
        }
        lpos++;
    }
    return n;
}
