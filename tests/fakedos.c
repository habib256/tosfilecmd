/*
 * fakedos.c -- implementation de sys.h en memoire, avec pannes injectees.
 * Suit la semantique GEMDOS utile a fsops.c : Fcreate tronque (c'est bien
 * pour cela que fsops sonde avant), Frename refuse une cible existante et
 * les changements de lecteur, Ddelete refuse un dossier non vide, Fdelete
 * refuse un fichier en lecture seule.
 */
#include "fakedos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAXN 512
#define MAXH 16

typedef struct {
    int used, isdir, attr;
    char path[PATH_MAX_TOSFC];
    unsigned char *data;
    long size, cap;
    unsigned short time, date;
} NODE;

typedef struct { int used, node, write; long pos; } HANDLE;

static NODE nodes[MAXN];
static HANDLE handles[MAXH];

int  fd_fail[OP_COUNT];
long fd_fail_code[OP_COUNT];
long fd_calls[OP_COUNT];
long fd_capacity[16];
int  fd_corrupt_write;
int  fd_cancel_after;
static int writes_seen;

/* Recherche en cours. */
static int search_list[MAXN], search_n, search_i;

static int injected(int op, long *code)
{
    fd_calls[op]++;
    if (fd_fail[op] > 0 && --fd_fail[op] == 0) {
        *code = fd_fail_code[op] ? fd_fail_code[op] : ERROR;
        return 1;
    }
    return 0;
}

void fd_reset(void)
{
    int i;
    for (i = 0; i < MAXN; i++) {
        free(nodes[i].data);
        memset(&nodes[i], 0, sizeof nodes[i]);
    }
    memset(handles, 0, sizeof handles);
    memset(fd_fail, 0, sizeof fd_fail);
    memset(fd_fail_code, 0, sizeof fd_fail_code);
    memset(fd_calls, 0, sizeof fd_calls);
    for (i = 0; i < 16; i++) fd_capacity[i] = -1;
    fd_corrupt_write = 0;
    fd_cancel_after = 0;
    writes_seen = 0;
    search_n = search_i = 0;
}

static void upper(char *d, const char *s)
{
    while ((*d++ = (char)toupper((unsigned char)*s++)) != 0) {}
}

static int find(const char *path)
{
    char p[PATH_MAX_TOSFC];
    int i;
    upper(p, path);
    for (i = 0; i < MAXN; i++)
        if (nodes[i].used && !strcmp(nodes[i].path, p)) return i;
    return -1;
}

/* Le dossier parent (sans '\' final) existe-t-il ? La racine existe. */
static int parent_ok(const char *path)
{
    char p[PATH_MAX_TOSFC];
    char *s;
    int i;
    upper(p, path);
    s = strrchr(p, '\\');
    if (!s) return 0;
    if (s == p + 2) return 1;          /* "X:\NOM" */
    *s = 0;
    i = find(p);
    return i >= 0 && nodes[i].isdir;
}

static int alloc_node(const char *path)
{
    int i;
    for (i = 0; i < MAXN; i++)
        if (!nodes[i].used) {
            memset(&nodes[i], 0, sizeof nodes[i]);
            nodes[i].used = 1;
            upper(nodes[i].path, path);
            nodes[i].date = (26 << 9) | (9 << 5) | 23;
            return i;
        }
    fprintf(stderr, "fakedos: out of nodes\n");
    exit(2);
}

static long used_bytes(char drive)
{
    long u = 0;
    int i;
    for (i = 0; i < MAXN; i++)
        if (nodes[i].used && nodes[i].path[0] == drive) u += nodes[i].size;
    return u;
}

void fd_mkdir(const char *path)
{
    int i = alloc_node(path);
    nodes[i].isdir = 1;
    nodes[i].attr = FA_DIR;
}

void fd_put(const char *path, const void *data, long size, int attr)
{
    int i = find(path);
    if (i < 0) i = alloc_node(path);
    free(nodes[i].data);
    nodes[i].data = malloc(size ? size : 1);
    memcpy(nodes[i].data, data, size);
    nodes[i].size = nodes[i].cap = size;
    nodes[i].attr = attr;
}

const unsigned char *fd_get(const char *path, long *size)
{
    int i = find(path);
    if (i < 0 || nodes[i].isdir) return NULL;
    *size = nodes[i].size;
    return nodes[i].data ? nodes[i].data : (const unsigned char *)"";
}

int fd_exists(const char *path) { return find(path) >= 0; }
int fd_isdir(const char *path) { int i = find(path); return i >= 0 && nodes[i].isdir; }
int fd_attr(const char *path) { int i = find(path); return i >= 0 ? nodes[i].attr : -1; }

int fd_count(char drive)
{
    int i, n = 0;
    for (i = 0; i < MAXN; i++) if (nodes[i].used && nodes[i].path[0] == drive) n++;
    return n;
}

int fd_open_handles(void)
{
    int i, n = 0;
    for (i = 0; i < MAXH; i++) n += handles[i].used;
    return n;
}

static void fill(int i, FINFO *out)
{
    const char *n = strrchr(nodes[i].path, '\\') + 1;
    memset(out, 0, sizeof *out);
    strncpy(out->name, n, 13);
    out->attr = (unsigned char)nodes[i].attr;
    out->size = nodes[i].isdir ? 0 : (unsigned long)nodes[i].size;
    out->time = nodes[i].time;
    out->date = nodes[i].date;
}

long sys_first(const char *pattern, int attr, FINFO *out)
{
    char p[PATH_MAX_TOSFC], dir[PATH_MAX_TOSFC];
    char *s;
    int i, dl;
    long code;
    (void)attr;
    if (injected(OP_FIRST, &code)) return code;
    upper(p, pattern);
    s = strrchr(p, '\\');
    if (!s) return EPTHNF;
    search_n = search_i = 0;
    if (!strcmp(s + 1, "*.*")) {
        dl = (int)(s - p) + 1;
        memcpy(dir, p, dl);
        dir[dl] = 0;
        if (dl > 3) {
            dir[dl - 1] = 0;
            i = find(dir);
            if (i < 0 || !nodes[i].isdir) return EPTHNF;
            dir[dl - 1] = '\\';
        }
        for (i = 0; i < MAXN; i++) {
            if (!nodes[i].used) continue;
            if (strncmp(nodes[i].path, dir, dl)) continue;
            if (strchr(nodes[i].path + dl, '\\')) continue;
            search_list[search_n++] = i;
        }
    } else {
        if (!parent_ok(p)) return EPTHNF;
        i = find(p);
        if (i >= 0) search_list[search_n++] = i;
    }
    if (search_n == 0) return EFILNF;
    fill(search_list[search_i++], out);
    return 0;
}

long sys_next(FINFO *out)
{
    long code;
    if (injected(OP_NEXT, &code)) return code;
    if (search_i >= search_n) return ENMFIL;
    fill(search_list[search_i++], out);
    return 0;
}

static int new_handle(int node, int write)
{
    int h;
    for (h = 0; h < MAXH; h++)
        if (!handles[h].used) {
            handles[h].used = 1;
            handles[h].node = node;
            handles[h].write = write;
            handles[h].pos = 0;
            return h + 6;
        }
    return -1;
}

static HANDLE *get_handle(int h)
{
    h -= 6;
    if (h < 0 || h >= MAXH || !handles[h].used) return NULL;
    return &handles[h];
}

long sys_open(const char *path, int mode)
{
    long code;
    int i, h;
    if (injected(OP_OPEN, &code)) return code;
    i = find(path);
    if (i < 0) return EFILNF;
    if (nodes[i].isdir) return EACCDN;
    if (mode != 0 && (nodes[i].attr & FA_RDONLY)) return EACCDN;
    h = new_handle(i, mode != 0);
    return h < 0 ? ENHNDL : h;
}

long sys_create(const char *path, int attr)
{
    long code;
    int i, h;
    if (injected(OP_CREATE, &code)) return code;
    if (!parent_ok(path)) return EPTHNF;
    i = find(path);
    if (i >= 0) {
        if (nodes[i].isdir || (nodes[i].attr & FA_RDONLY)) return EACCDN;
        nodes[i].size = 0;             /* GEMDOS tronque */
    } else {
        i = alloc_node(path);
    }
    nodes[i].attr = attr;
    h = new_handle(i, 1);
    return h < 0 ? ENHNDL : h;
}

long sys_close(int h)
{
    long code;
    HANDLE *hp = get_handle(h);
    if (!hp) return EIHNDL;
    hp->used = 0;       /* ferme meme en cas d'erreur, comme le GEMDOS */
    if (injected(OP_CLOSE, &code)) return code;
    return 0;
}

long sys_read(int h, long n, void *buf)
{
    long code, left;
    HANDLE *hp = get_handle(h);
    NODE *nd;
    if (!hp) return EIHNDL;
    if (injected(OP_READ, &code)) return code;
    nd = &nodes[hp->node];
    left = nd->size - hp->pos;
    if (n > left) n = left;
    memcpy(buf, nd->data + hp->pos, n);
    hp->pos += n;
    return n;
}

long sys_write(int h, long n, const void *buf)
{
    long code, room;
    HANDLE *hp = get_handle(h);
    NODE *nd;
    char drive;
    if (!hp || !hp->write) return EIHNDL;
    if (injected(OP_WRITE, &code)) return code;
    nd = &nodes[hp->node];
    drive = nd->path[0];
    if (fd_capacity[drive - 'A'] >= 0) {
        room = fd_capacity[drive - 'A'] - used_bytes(drive);
        if (room < 0) room = 0;
        if (n > room) n = room;          /* GEMDOS : ecriture courte */
    }
    if (hp->pos + n > nd->cap) {
        nd->cap = (hp->pos + n) * 2 + 16;
        nd->data = realloc(nd->data, nd->cap);
    }
    memcpy(nd->data + hp->pos, buf, n);
    if (++writes_seen == fd_corrupt_write && n > 0) nd->data[hp->pos] ^= 0x5a;
    hp->pos += n;
    if (hp->pos > nd->size) nd->size = hp->pos;
    return n;
}

long sys_delete(const char *path)
{
    long code;
    int i;
    if (injected(OP_DELETE, &code)) return code;
    i = find(path);
    if (i < 0) return EFILNF;
    if (nodes[i].isdir || (nodes[i].attr & FA_RDONLY)) return EACCDN;
    free(nodes[i].data);
    memset(&nodes[i], 0, sizeof nodes[i]);
    return 0;
}

long sys_rename(const char *from, const char *to)
{
    long code;
    int i;
    if (injected(OP_RENAME, &code)) return code;
    if (toupper((unsigned char)from[0]) != toupper((unsigned char)to[0])) return ENSAME;
    i = find(from);
    if (i < 0) return EFILNF;
    if (find(to) >= 0) return EACCDN;
    if (!parent_ok(to)) return EPTHNF;
    if (nodes[i].isdir) {
        /* Les enfants suivent. */
        char f[PATH_MAX_TOSFC], t[PATH_MAX_TOSFC];
        int k, fl;
        upper(f, from);
        upper(t, to);
        fl = (int)strlen(f);
        for (k = 0; k < MAXN; k++) {
            char tmp[PATH_MAX_TOSFC];
            if (!nodes[k].used || strncmp(nodes[k].path, f, fl) || nodes[k].path[fl] != '\\')
                continue;
            snprintf(tmp, sizeof tmp, "%s%s", t, nodes[k].path + fl);
            strcpy(nodes[k].path, tmp);
        }
    }
    upper(nodes[i].path, to);
    return 0;
}

long sys_mkdir(const char *path)
{
    long code;
    if (injected(OP_MKDIR, &code)) return code;
    if (!parent_ok(path)) return EPTHNF;
    if (find(path) >= 0) return EACCDN;
    fd_mkdir(path);
    return 0;
}

long sys_rmdir(const char *path)
{
    long code;
    char p[PATH_MAX_TOSFC];
    int i, k, l;
    if (injected(OP_RMDIR, &code)) return code;
    i = find(path);
    if (i < 0) return EPTHNF;
    if (!nodes[i].isdir) return EACCDN;
    upper(p, path);
    l = (int)strlen(p);
    for (k = 0; k < MAXN; k++)
        if (nodes[k].used && !strncmp(nodes[k].path, p, l) && nodes[k].path[l] == '\\')
            return EACCDN;
    memset(&nodes[i], 0, sizeof nodes[i]);
    return 0;
}

long sys_attrib(const char *path, int set, int attr)
{
    long code;
    int i;
    if (injected(OP_ATTRIB, &code)) return code;
    i = find(path);
    if (i < 0) return EFILNF;
    if (set) nodes[i].attr = (nodes[i].attr & FA_DIR) | (attr & ~FA_DIR);
    return nodes[i].attr;
}

long sys_settime(int h, unsigned short time, unsigned short date)
{
    HANDLE *hp = get_handle(h);
    if (!hp) return EIHNDL;
    nodes[hp->node].time = time;
    nodes[hp->node].date = date;
    return 0;
}

long sys_dfree(int drive, unsigned long *freeb, unsigned long *totalb)
{
    long cap = fd_capacity[drive];
    if (cap < 0) cap = 720L * 1024;
    *totalb = cap;
    *freeb = cap - used_bytes((char)('A' + drive));
    return 0;
}

void *sys_alloc(long n) { return malloc(n); }
long sys_avail(void) { return 4L * 1024 * 1024; }
void sys_free(void *p) { free(p); }
