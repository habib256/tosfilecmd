/*
 * sys_tos.c -- l'interface sys.h sur un vrai GEMDOS.
 */
#include "sys.h"
#include "tos.h"
#include "libc.h"

/* DTA GEMDOS : 21 octets reserves, puis attribut, heure, date, taille, nom. */
typedef struct {
    char           reserved[21];
    unsigned char  attr;
    unsigned short time;
    unsigned short date;
    unsigned long  size;
    char           name[14];
} DTA;

static DTA dta;

static void from_dta(FINFO *out)
{
    memcpy(out->name, dta.name, 14);
    out->name[13] = 0;
    out->attr = dta.attr;
    out->pad = 0;
    out->time = dta.time;
    out->date = dta.date;
    out->size = dta.size;
}

long sys_first(const char *pattern, int attr, FINFO *out)
{
    long r;
    void *old = Fgetdta();
    Fsetdta(&dta);
    r = Fsfirst(pattern, attr);
    Fsetdta(old);
    if (r == 0) from_dta(out);
    return r;
}

long sys_next(FINFO *out)
{
    long r;
    void *old = Fgetdta();
    Fsetdta(&dta);
    r = Fsnext();
    Fsetdta(old);
    if (r == 0) from_dta(out);
    return r;
}

long sys_open(const char *path, int mode)   { return Fopen(path, mode); }
long sys_create(const char *path, int attr) { return Fcreate(path, attr); }
long sys_close(int h)                       { return Fclose(h); }
long sys_read(int h, long n, void *buf)     { return Fread(h, n, buf); }
long sys_write(int h, long n, const void *buf) { return Fwrite(h, n, buf); }
long sys_seek(int h, long off, int mode)    { return Fseek(off, h, mode); }
long sys_delete(const char *path)           { return Fdelete(path); }
long sys_rename(const char *f, const char *t) { return Frename(f, t); }
long sys_mkdir(const char *path)            { return Dcreate(path); }
long sys_rmdir(const char *path)            { return Ddelete(path); }
long sys_attrib(const char *path, int set, int attr) { return Fattrib(path, set, attr); }

long sys_settime(int h, unsigned short time, unsigned short date)
{
    unsigned short td[2];
    td[0] = time;
    td[1] = date;
    return Fdatime(td, h, 1);
}

long sys_gettime(int h, unsigned short *time, unsigned short *date)
{
    unsigned short td[2];
    long r = Fdatime(td, h, 0);
    *time = td[0];
    *date = td[1];
    return r;
}

long sys_dfree(int drive, unsigned long *freeb, unsigned long *totalb)
{
    /* b_free, b_total, b_secsiz, b_clsiz */
    unsigned long info[4];
    long r = Dfree(info, drive + 1);
    if (r < 0) return r;
    *freeb = info[0] * info[2] * info[3];
    *totalb = info[1] * info[2] * info[3];
    return 0;
}

void *sys_alloc(long n)
{
    long r = Malloc(n);
    return r > 0 ? (void *)r : 0;
}

long sys_avail(void) { return Malloc(-1L); }
void sys_free(void *p) { if (p) Mfree(p); }
