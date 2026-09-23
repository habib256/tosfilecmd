/*
 * prefs.c -- TOSFC.INF.
 *
 * Le fichier n'est ecrit que sur demande (Options, Save) : au moment de
 * quitter, la disquette du lecteur n'est peut-etre plus celle du programme,
 * et on n'ecrit pas sur un disque que l'utilisateur n'a pas choisi.
 * L'ecriture passe par TOSFC.NEW, relu octet par octet avant de remplacer
 * l'ancien TOSFC.INF ; un TOSFC.NEW preexistant bloque l'enregistrement.
 */
#include "prefs.h"
#include "fsops.h"
#include "libc.h"

#define INF_MAX 600

static int valid_path(const char *s)
{
    int l = (int)strlen(s);
    if (l == 0) return 1;
    if (l < 3 || l >= PATH_MAX_TOSFC - 14) return 0;
    if (toupper((unsigned char)s[0]) < 'A' || toupper((unsigned char)s[0]) > 'Z') return 0;
    return s[1] == ':' && s[2] == '\\' && s[l - 1] == '\\';
}

int prefs_format(const PREFS *p, char *out, int cap)
{
    char num[8];
    out[0] = 0;
    str_add(out, "TOSFC 1\r\nL=", cap);
    str_add(out, p->path[0], cap);
    str_add(out, "\r\nR=", cap);
    str_add(out, p->path[1], cap);
    str_add(out, "\r\nSL=", cap);
    fmt_ulong(num, (unsigned long)p->sort[0], 0);
    str_add(out, num, cap);
    str_add(out, "\r\nSR=", cap);
    fmt_ulong(num, (unsigned long)p->sort[1], 0);
    str_add(out, num, cap);
    str_add(out, p->active ? "\r\nA=1" : "\r\nA=0", cap);
    str_add(out, p->verify ? "\r\nV=1" : "\r\nV=0", cap);
    str_add(out, p->show_hidden ? "\r\nH=1\r\n" : "\r\nH=0\r\n", cap);
    return (int)strlen(out);
}

void prefs_parse(PREFS *p, const char *text, long len)
{
    char line[PATH_MAX_TOSFC + 8];
    long i = 0;
    int first = 1;

    while (i < len) {
        int n = 0;
        while (i < len && text[i] != '\r' && text[i] != '\n') {
            if (n < (int)sizeof line - 1) line[n++] = text[i];
            i++;
        }
        while (i < len && (text[i] == '\r' || text[i] == '\n')) i++;
        line[n] = 0;
        if (first) {
            if (strcmp(line, "TOSFC 1")) return;   /* pas notre format */
            first = 0;
            continue;
        }
        if ((line[0] == 'L' || line[0] == 'R') && line[1] == '=') {
            if (valid_path(line + 2) && strlen(line + 2) < PATH_MAX_TOSFC)
                strcpy(p->path[line[0] == 'R'], line + 2);
        } else if (line[0] == 'S' && (line[1] == 'L' || line[1] == 'R') && line[2] == '=') {
            int v = line[3] - '0';
            if (v >= 0 && v <= 4 && line[4] == 0) p->sort[line[1] == 'R'] = v;
        } else if (line[1] == '=' && (line[2] == '0' || line[2] == '1') && line[3] == 0) {
            int v = line[2] - '0';
            if (line[0] == 'A') p->active = v;
            if (line[0] == 'V') p->verify = v;
            if (line[0] == 'H') p->show_hidden = v;
        }
    }
}

void prefs_load(PREFS *p, const char *home)
{
    char path[PATH_MAX_TOSFC], buf[INF_MAX];
    long h, n;
    if (path_join(path, home, "TOSFC.INF", 0)) return;
    h = sys_open(path, 0);
    if (h < 0) return;
    n = sys_read((int)h, sizeof buf, buf);
    sys_close((int)h);
    if (n > 0) prefs_parse(p, buf, n);
}

long prefs_save(const PREFS *p, const char *home)
{
    char inf[PATH_MAX_TOSFC], tmp[PATH_MAX_TOSFC], text[INF_MAX], back[INF_MAX];
    FINFO f;
    long h, r, n;
    int len = prefs_format(p, text, sizeof text);

    if (path_join(inf, home, "TOSFC.INF", 0) || path_join(tmp, home, "TOSFC.NEW", 0))
        return TE_TOOLONG;
    r = sys_first(tmp, 0x07, &f);
    if (r == 0) return TE_EXISTS;
    if (r != EFILNF && r != ENMFIL) return r;

    h = sys_create(tmp, 0);
    if (h < 0) return h;
    n = sys_write((int)h, len, text);
    r = sys_close((int)h);
    if (n != len || r < 0) {
        sys_delete(tmp);
        return n < 0 ? n : (r < 0 ? r : TE_SHORTW);
    }
    h = sys_open(tmp, 0);
    if (h < 0) { sys_delete(tmp); return h; }
    n = sys_read((int)h, sizeof back, back);
    sys_close((int)h);
    if (n != len || memcmp(text, back, len)) {
        sys_delete(tmp);
        return n < 0 ? n : TE_VERIFY;
    }
    /* Le nouveau est bon : l'ancien peut partir. Si le renommage echoue,
     * TOSFC.NEW reste et sera signale au prochain enregistrement. */
    r = sys_first(inf, 0x07, &f);
    if (r == 0) {
        r = sys_delete(inf);
        if (r < 0) return r;
    } else if (r != EFILNF && r != ENMFIL) {
        return r;
    }
    return sys_rename(tmp, inf);
}
