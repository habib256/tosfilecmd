/*
 * test_edit.c -- le tampon de l'editeur et l'enregistrement sur. Un fichier
 * ouvert puis enregistre sans modification doit etre identique octet pour
 * octet ; une panne a n'importe quelle etape de l'enregistrement laisse
 * l'original intact et aucun reste.
 */
#include "fakedos.h"
#include "../src/edbuf.h"
#include "../src/fsops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests, failures;
static const char *current;
#define CHECK(c) do { tests++; if (!(c)) { failures++; \
    printf("FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, current, #c); } } while (0)

static char mem[200000];
static char work[16384];

static int load(EDBUF *b, const char *s, long n)
{
    static char copy[100000];
    memcpy(copy, s, n);
    return eb_load(b, (unsigned char *)copy, n, mem, sizeof mem);
}

static long export_all(EDBUF *b, char *out, long cap)
{
    long at = 0, n;
    /* Par petits morceaux, pour exercer la reprise au milieu d'un CRLF. */
    while ((n = eb_export(b, at, out + at, 3)) > 0 && at < cap) at += n;
    return at;
}

static void t_round_trips(void)
{
    static const char *const samples[] = {
        "one\r\ntwo\r\n\r\nthree", "a\nb\n\nc\n", "mac\rstyle\r", "no line end",
        "", "\r\n", "tabs\there\r\n\tand there\r\n"
    };
    static char out[1000];
    EDBUF b;
    int i;
    current = "load then export gives the same bytes";
    for (i = 0; i < (int)(sizeof samples / sizeof samples[0]); i++) {
        long n = (long)strlen(samples[i]);
        CHECK(load(&b, samples[i], n) == EB_OK);
        CHECK(eb_file_size(&b) == n);
        CHECK(export_all(&b, out, sizeof out) == n && !memcmp(out, samples[i], n));
        CHECK(!b.modified);
    }
}

static void t_refusals(void)
{
    EDBUF b;
    current = "refusals";
    CHECK(load(&b, "a\r\nb\nc", 6) == EB_MIXED);
    CHECK(load(&b, "a\rb\nc", 5) == EB_MIXED);
    CHECK(load(&b, "bin\0ary", 7) == EB_BINARY);
    CHECK(eb_load(&b, (const unsigned char *)"12345", 5, mem, 100) == EB_NOROOM);
}

static void t_editing(void)
{
    EDBUF b;
    static char out[200];
    long n;
    current = "editing";
    load(&b, "first\r\nsecond\r\n", 15);
    CHECK(eb_len(&b) == 13);                        /* LF interne */
    CHECK(eb_insert(&b, 0, ">> ", 3) == EB_OK && b.modified);
    CHECK(eb_insert(&b, eb_len(&b), "third\n", 6) == EB_OK);
    eb_delete(&b, 3, 5);                            /* "first" */
    CHECK(eb_insert(&b, 3, "1st", 3) == EB_OK);
    n = export_all(&b, out, sizeof out);
    out[n] = 0;
    CHECK(!strcmp(out, ">> 1st\r\nsecond\r\nthird\r\n"));
    CHECK(eb_file_size(&b) == n);

    current = "lines and columns";
    CHECK(eb_line_start(&b, 5) == 0 && eb_line_end(&b, 0) == 6);
    CHECK(eb_next_line(&b, 2) == 7 && eb_prev_line(&b, 8) == 0 && eb_prev_line(&b, 3) == -1);
    CHECK(eb_line_number(&b, 0) == 1 && eb_line_number(&b, 8) == 2 && eb_line_number(&b, 14) == 3);
    CHECK(eb_next_line(&b, eb_len(&b)) == -1);
    CHECK(b.nlines == 3);
    eb_delete(&b, 0, 8);                            /* ">> 1st\n" et le "s" */
    CHECK(b.nlines == 2 && eb_file_size(&b) == n - 9);
    load(&b, "\tx\ty\n", 5);
    CHECK(eb_col(&b, 1) == 8 && eb_col(&b, 2) == 9 && eb_col(&b, 3) == 16);
    CHECK(eb_pos_at_col(&b, 0, 0) == 0 && eb_pos_at_col(&b, 0, 5) == 0);
    CHECK(eb_pos_at_col(&b, 0, 8) == 1 && eb_pos_at_col(&b, 0, 40) == 4);

    current = "full buffer";
    {
        static char small[1100];
        CHECK(eb_load(&b, (const unsigned char *)"abc", 3, small, sizeof small) == EB_OK);
        CHECK(eb_insert(&b, 1, small, 2000) == EB_FULL);
        CHECK(eb_len(&b) == 3);
    }
}

/* ---- enregistrement ---- */

typedef struct { const char *s; long n; } SRC;

static long gen(void *ctx, long at, char *out, long cap)
{
    SRC *src = ctx;
    long n = src->n - at;
    if (n > cap) n = cap;
    if (n < 0) n = 0;
    memcpy(out, src->s + at, n);
    return n;
}

static int same(const char *path, const char *s)
{
    long sz;
    const unsigned char *d = fd_get(path, &sz);
    return d && sz == (long)strlen(s) && !memcmp(d, s, sz);
}

static int clean(char drive)
{
    char p[32];
    sprintf(p, "%c:\\TOSFC.$ED", drive);
    if (fd_exists(p)) return 0;
    sprintf(p, "%c:\\TOSFC.BAK", drive);
    return !fd_exists(p);
}

static void t_save(void)
{
    SRC src = { "new text\r\n", 10 };
    current = "save replaces the file";
    fd_reset();
    fd_put("C:\\A.TXT", "old", 3, FA_HIDDEN);
    CHECK(ops_save_stream("C:\\", "A.TXT", 0, gen, &src, work, 4) == 0);
    CHECK(same("C:\\A.TXT", "new text\r\n") && clean('C'));
    CHECK(fd_attr("C:\\A.TXT") & FA_HIDDEN);

    current = "save creates a new file";
    CHECK(ops_save_stream("C:\\", "B.TXT", 1, gen, &src, work, 4) == 0);
    CHECK(same("C:\\B.TXT", "new text\r\n") && clean('C'));
    CHECK(ops_save_stream("C:\\", "B.TXT", 1, gen, &src, work, 4) == TE_EXISTS);

    current = "refusals leave everything untouched";
    fd_reset();
    fd_put("C:\\A.TXT", "old", 3, FA_RDONLY);
    CHECK(ops_save_stream("C:\\", "A.TXT", 0, gen, &src, work, 4) == TE_READONLY);
    CHECK(same("C:\\A.TXT", "old"));
    fd_reset();
    fd_put("C:\\A.TXT", "old", 3, 0);
    fd_put("C:\\TOSFC.$ED", "left over", 9, 0);
    CHECK(ops_save_stream("C:\\", "A.TXT", 0, gen, &src, work, 4) == TE_BAKEXIST);
    CHECK(same("C:\\A.TXT", "old") && same("C:\\TOSFC.$ED", "left over"));
    fd_reset();
    fd_put("C:\\A.TXT", "old", 3, 0);
    fd_put("C:\\TOSFC.BAK", "backup", 6, 0);
    CHECK(ops_save_stream("C:\\", "A.TXT", 0, gen, &src, work, 4) == TE_BAKEXIST);
    CHECK(same("C:\\A.TXT", "old") && same("C:\\TOSFC.BAK", "backup"));
}

static void t_save_faults(void)
{
    SRC src = { "0123456789abcdef0123456789", 26 };
    struct { const char *what; int op, at; long code; long expect; } f[] = {
        { "disk full",            OP_WRITE,  0, 0,      TE_SHORTW },
        { "write error",          OP_WRITE,  2, EWRITF, EWRITF },
        { "close error",          OP_CLOSE,  1, EWRITF, EWRITF },
        { "read-back error",      OP_READ,   1, EREADF, EREADF },
        { "rename original fails", OP_RENAME, 1, EACCDN, EACCDN },
        { "rename new fails",     OP_RENAME, 2, EACCDN, EACCDN },
        { "create fails",         OP_CREATE, 1, EWRPRO, EWRPRO },
    };
    int i;
    for (i = 0; i < (int)(sizeof f / sizeof f[0]); i++) {
        long r;
        current = f[i].what;
        fd_reset();
        fd_put("C:\\A.TXT", "original", 8, 0);
        if (f[i].op == OP_WRITE && f[i].code == 0) fd_capacity['C' - 'A'] = 20;
        else {
            fd_fail[f[i].op] = f[i].at;
            fd_fail_code[f[i].op] = f[i].code;
        }
        r = ops_save_stream("C:\\", "A.TXT", 0, gen, &src, work, 4);
        CHECK(r == f[i].expect);
        CHECK(same("C:\\A.TXT", "original"));
        CHECK(clean('C'));
        CHECK(fd_open_handles() == 0);
    }

    current = "corrupted write is caught by the read-back";
    fd_reset();
    fd_put("C:\\A.TXT", "original", 8, 0);
    fd_corrupt_write = 3;
    CHECK(ops_save_stream("C:\\", "A.TXT", 0, gen, &src, work, 4) == TE_VERIFY);
    CHECK(same("C:\\A.TXT", "original") && clean('C'));

    current = "old version left when it cannot be removed";
    fd_reset();
    fd_put("C:\\A.TXT", "original", 8, 0);
    fd_fail[OP_DELETE] = 1;
    CHECK(ops_save_stream("C:\\", "A.TXT", 0, gen, &src, work, 4) == TE_RESTORE);
    CHECK(same("C:\\A.TXT", "0123456789abcdef0123456789"));
    CHECK(same("C:\\TOSFC.BAK", "original"));
}

int main(void)
{
    t_round_trips();
    t_refusals();
    t_editing();
    t_save();
    t_save_faults();
    printf("test_edit: %d checks, %d failed\n", tests, failures);
    return failures ? 1 : 0;
}
