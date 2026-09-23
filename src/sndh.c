/*
 * sndh.c -- lecture des etiquettes SNDH.
 */
#include "sndh.h"
#include "libc.h"

#define HEADER_MAX 2048L

static int is_tag(const unsigned char *p, const char *t)
{
    return p[0] == t[0] && p[1] == t[1] && p[2] == t[2] && p[3] == t[3];
}

/* Copie la chaine qui suit l'etiquette ; renvoie la position apres le 0. */
static long take_string(const unsigned char *d, long pos, long end, char *out, int cap)
{
    int n = 0;
    while (pos < end && d[pos]) {
        if (out && n < cap - 1) out[n++] = (char)(d[pos] >= 32 && d[pos] < 127 ? d[pos] : '?');
        pos++;
    }
    if (out) out[n] = 0;
    return pos + 1;
}

static int number(const unsigned char *d, long pos, long end)
{
    int v = 0, k = 0;
    while (pos < end && d[pos] >= '0' && d[pos] <= '9' && k < 5) {
        v = v * 10 + d[pos++] - '0';
        k++;
    }
    return k ? v : -1;
}

int sndh_parse(const unsigned char *d, long size, SNDHINFO *s)
{
    long pos = 16, end;
    memset(s, 0, sizeof *s);
    s->tunes = 1;
    s->hz = 50;
    s->timer = 'C';
    if (size < 20 || !is_tag(d + 12, "SNDH")) return SNDH_NOT;
    end = size < HEADER_MAX ? size : HEADER_MAX;
    while (pos + 4 <= end) {
        const unsigned char *p = d + pos;
        if (is_tag(p, "HDNS")) break;
        if (is_tag(p, "TITL")) pos = take_string(d, pos + 4, end, s->title, sizeof s->title);
        else if (is_tag(p, "COMM")) pos = take_string(d, pos + 4, end, s->composer, sizeof s->composer);
        else if (is_tag(p, "YEAR")) pos = take_string(d, pos + 4, end, s->year, sizeof s->year);
        else if (is_tag(p, "RIPP") || is_tag(p, "CONV") || is_tag(p, "FLAG"))
            pos = take_string(d, pos + 4, end, 0, 0);
        else if (p[0] == '#' && p[1] == '#') {
            int n = number(d, pos + 2, end);
            if (n > 0 && n < 100) s->tunes = n;
            pos = take_string(d, pos + 2, end, 0, 0);
        } else if (p[0] == 'T' && p[1] >= 'A' && p[1] <= 'D' && p[2] >= '0' && p[2] <= '9') {
            int n = number(d, pos + 2, end);
            if (n > 0) { s->hz = n; s->timer = (char)p[1]; }
            pos = take_string(d, pos + 2, end, 0, 0);
        } else if (p[0] == '!' && p[1] == 'V' && p[2] >= '0' && p[2] <= '9') {
            int n = number(d, pos + 2, end);
            if (n > 0) { s->hz = n; s->timer = 'V'; }
            pos = take_string(d, pos + 2, end, 0, 0);
        } else if (is_tag(p, "TIME") || is_tag(p, "!#SN")) {
            /* Un mot par sous-morceau, sans 0 final. */
            pos += 4 + 2L * s->tunes;
        } else {
            pos++;                  /* octet de remplissage ou etiquette inconnue */
        }
    }
    if (s->hz < 10 || s->hz > 1000) s->hz = 50;
    return SNDH_OK;
}
