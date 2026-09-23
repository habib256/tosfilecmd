/*
 * textview.c -- mise en page du lecteur de texte.
 *
 * Pas d'index de lignes : la ligne suivante se calcule en avancant, la
 * precedente en reculant jusqu'au debut de la ligne du fichier puis en
 * refaisant la mise en page vers l'avant. Un fichier de plusieurs centaines
 * de Ko s'ouvre donc sans memoire en plus ni temps de preparation.
 */
#include "textview.h"
#include "libc.h"

/* Au-dela, un recul cherche une fin de ligne sans la trouver (fichier
 * binaire, ligne geante) : on recule alors par pas fixes. */
#define BACK_LIMIT 4096L

static int is_eol(unsigned char c) { return c == '\r' || c == '\n'; }

static int ext_is(const char *name, const char *ext)
{
    const char *d = strrchr(name, '.');
    if (!d) return 0;
    for (d++; *d && *ext; d++, ext++)
        if (toupper((unsigned char)*d) != *ext) return 0;
    return *d == 0 && *ext == 0;
}

int tv_is_firstword(const char *name, const unsigned char *data, long size)
{
    return size > 0 && data[0] == 0x1f && ext_is(name, "DOC");
}

/* Passe la fin de ligne en off (CR, LF, CRLF ou LFCR). */
static long skip_eol(const TEXTDOC *d, long off)
{
    if (off < d->size && is_eol(d->data[off])) {
        unsigned char c = d->data[off++];
        if (off < d->size && is_eol(d->data[off]) && d->data[off] != c) off++;
    }
    return off;
}

/* Au debut d'une ligne 1st Word, saute les lignes de format ($1F ...). */
static long skip_format(const TEXTDOC *d, long off)
{
    while (d->firstword && off < d->size && d->data[off] == 0x1f) {
        while (off < d->size && !is_eol(d->data[off])) off++;
        off = skip_eol(d, off);
    }
    return off;
}

static const char hexdig[] = "0123456789ABCDEF";

static long layout_hex(const TEXTDOC *d, long off, char *out)
{
    int i;
    long a = off;
    if (!out) return off + 16 < d->size ? off + 16 : d->size;
    memset(out, ' ', TV_COLS);
    out[TV_COLS] = 0;
    for (i = 7; i >= 0; i--) { out[i] = hexdig[a & 15]; a >>= 4; }
    for (i = 0; i < 16 && off + i < d->size; i++) {
        unsigned char c = d->data[off + i];
        out[10 + i * 3 + (i >= 8)] = hexdig[c >> 4];
        out[11 + i * 3 + (i >= 8)] = hexdig[c & 15];
        out[61 + i] = (c >= 32 && c < 127) ? (char)c : '.';
    }
    return off + 16 < d->size ? off + 16 : d->size;
}

long tv_layout(const TEXTDOC *d, long off, char *out)
{
    char line[TV_COLS];
    int col = 0, last_space_col = -1;
    long p, last_space_next = -1;

    if (d->hex) return layout_hex(d, off, out);
    if (out) { memset(out, ' ', TV_COLS); out[TV_COLS] = 0; }
    if (off >= d->size) return d->size;
    if (off == 0 || is_eol(d->data[off - 1])) off = skip_format(d, off);

    p = off;
    while (p < d->size) {
        unsigned char c = d->data[p];
        int w;
        if (is_eol(c)) {
            p = skip_eol(d, p);
            if (out) memcpy(out, line, col);
            return skip_format(d, p);
        }
        if (c == 0x0c) {                        /* saut de page : fin de ligne */
            if (out) memcpy(out, line, col);
            return p + 1;
        }
        if (d->firstword && c == 0x1b) {        /* style : ESC + un octet */
            p += 2;
            continue;
        }
        if (c == '\t') {
            w = 8 - (col & 7);
            if (col + w > TV_COLS) w = TV_COLS - col;
        } else {
            w = 1;
        }
        if (col + w > TV_COLS || col == TV_COLS) {
            /* Ligne pleine : couper au dernier espace s'il y en a un. */
            if (last_space_col > 0) {
                if (out) memcpy(out, line, last_space_col);
                return last_space_next;
            }
            if (out) memcpy(out, line, col);
            return p;
        }
        if (c == '\t') {
            int k;
            for (k = 0; k < w; k++) line[col++] = ' ';
        } else if (d->firstword && (c == 0x1c || c == 0x1d || c == 0x1e)) {
            line[col++] = ' ';                  /* espaces de justification */
        } else if (c < 32 || c == 127) {
            line[col++] = '.';                  /* $0F-$1F : glyphes des cadres */
        } else {
            line[col++] = (char)c;
        }
        if (c == ' ' || c == '\t' || (d->firstword && c >= 0x1c && c <= 0x1e)) {
            last_space_col = col;
            last_space_next = p + 1;
        }
        p++;
    }
    if (out) memcpy(out, line, col);
    return d->size;
}

long tv_line_of(const TEXTDOC *d, long pos)
{
    long q, start, next;
    if (pos <= 0 || d->size <= 0) return 0;
    if (pos >= d->size) pos = d->size - 1;
    if (d->hex) return pos & ~15L;
    /* Un vrai debut de ligne a ou avant pos : on remonte au-dela des fins de
     * ligne (pos peut etre au milieu d'un CRLF), puis jusqu'au debut du
     * texte qui les precede. Une ligne de format 1st Word n'est jamais un
     * debut affiche : on remonte encore. */
    q = pos;
    for (;;) {
        long lim;
        while (q > 0 && is_eol(d->data[q - 1])) q--;
        lim = q;
        while (q > 0 && !is_eol(d->data[q - 1]) && lim - q < BACK_LIMIT) q--;
        if (q > 0 && !is_eol(d->data[q - 1])) {
            /* Pas de fin de ligne a portee (fichier binaire) : on recule par
             * lignes pleines, sans promettre de retomber sur les coupures
             * de l'avance. */
            return pos - (pos % TV_COLS);
        }
        if (q == 0 || !(d->firstword && d->data[q] == 0x1f)) break;
        q--;
    }
    /* Puis la mise en page vers l'avant, jusqu'a la ligne d'ecran de pos. */
    start = q;
    for (;;) {
        next = tv_layout(d, start, 0);
        if (next > pos || next >= d->size || next <= start) return start;
        start = next;
    }
}

long tv_prev(const TEXTDOC *d, long off)
{
    if (off <= 0) return 0;
    if (d->hex) return off >= 16 ? ((off - 1) & ~15L) : 0;
    return tv_line_of(d, off - 1);
}

long tv_find(const TEXTDOC *d, long from, const char *needle)
{
    long n = (long)strlen(needle), i, k;
    int c0;
    if (n == 0) return -1;
    c0 = toupper((unsigned char)needle[0]);
    for (i = from < 0 ? 0 : from; i + n <= d->size; i++) {
        if (toupper(d->data[i]) != c0) continue;
        for (k = 1; k < n; k++)
            if (toupper(d->data[i + k]) != toupper((unsigned char)needle[k])) break;
        if (k == n) return i;
    }
    return -1;
}
