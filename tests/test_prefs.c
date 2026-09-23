/*
 * test_prefs.c -- TOSFC.INF : format, relecture, et enregistrement sur
 * le faux GEMDOS, pannes comprises.
 */
#include "fakedos.h"
#include "../src/prefs.h"
#include <stdio.h>
#include <string.h>

static int tests, failures;
#define CHECK(c) do { tests++; if (!(c)) { failures++; \
    printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static void sample(PREFS *p)
{
    memset(p, 0, sizeof *p);
    strcpy(p->path[0], "A:\\DEMO\\");
    strcpy(p->path[1], "");
    p->sort[0] = 2;
    p->sort[1] = 4;
    p->active = 1;
    p->verify = 0;
    p->show_hidden = 1;
}

static void t_round_trip(void)
{
    PREFS a, b;
    char text[600];
    int n;
    sample(&a);
    n = prefs_format(&a, text, sizeof text);
    memset(&b, 0, sizeof b);
    b.verify = 1;
    prefs_parse(&b, text, n);
    CHECK(!strcmp(b.path[0], "A:\\DEMO\\"));
    CHECK(b.path[1][0] == 0);
    CHECK(b.sort[0] == 2 && b.sort[1] == 4);
    CHECK(b.active == 1 && b.verify == 0 && b.show_hidden == 1);
}

static void t_rejects_garbage(void)
{
    PREFS b;
    const char *bad = "TOSFC 1\r\nL=..\\..\\X\r\nR=C:NOSLASH\r\nSL=9\r\nA=7\r\n";
    const char *other = "NOT A TOSFC FILE\r\nL=C:\\\r\n";
    memset(&b, 0, sizeof b);
    strcpy(b.path[0], "A:\\");
    prefs_parse(&b, bad, (long)strlen(bad));
    CHECK(!strcmp(b.path[0], "A:\\"));
    CHECK(b.path[1][0] == 0);
    CHECK(b.sort[0] == 0 && b.active == 0);
    prefs_parse(&b, other, (long)strlen(other));
    CHECK(!strcmp(b.path[0], "A:\\"));
}

static void t_save_and_load(void)
{
    PREFS a, b;
    long sz;
    sample(&a);
    fd_reset();
    fd_put("A:\\TOSFC.INF", "old", 3, 0);
    CHECK(prefs_save(&a, "A:\\") == 0);
    CHECK(!fd_exists("A:\\TOSFC.NEW"));
    CHECK(fd_get("A:\\TOSFC.INF", &sz) && sz > 20);
    memset(&b, 0, sizeof b);
    prefs_load(&b, "A:\\");
    CHECK(!strcmp(b.path[0], "A:\\DEMO\\") && b.sort[1] == 4);
}

static void t_save_failures_keep_old(void)
{
    PREFS a;
    long sz;
    const unsigned char *d;
    sample(&a);

    /* Disque plein : l'ancien fichier reste, pas de TOSFC.NEW. */
    fd_reset();
    fd_put("A:\\TOSFC.INF", "old", 3, 0);
    fd_capacity[0] = 10;
    CHECK(prefs_save(&a, "A:\\") != 0);
    d = fd_get("A:\\TOSFC.INF", &sz);
    CHECK(d && sz == 3 && !memcmp(d, "old", 3));
    CHECK(!fd_exists("A:\\TOSFC.NEW"));

    /* Relecture differente : rien n'est remplace. */
    fd_reset();
    fd_put("A:\\TOSFC.INF", "old", 3, 0);
    fd_corrupt_write = 1;
    CHECK(prefs_save(&a, "A:\\") != 0);
    d = fd_get("A:\\TOSFC.INF", &sz);
    CHECK(d && sz == 3 && !memcmp(d, "old", 3));
    CHECK(!fd_exists("A:\\TOSFC.NEW"));

    /* Un TOSFC.NEW preexistant n'est jamais ecrase. */
    fd_reset();
    fd_put("A:\\TOSFC.NEW", "keep", 4, 0);
    CHECK(prefs_save(&a, "A:\\") != 0);
    d = fd_get("A:\\TOSFC.NEW", &sz);
    CHECK(d && sz == 4 && !memcmp(d, "keep", 4));

    /* Ecriture protegee. */
    fd_reset();
    fd_fail[OP_CREATE] = 1;
    fd_fail_code[OP_CREATE] = EWRPRO;
    CHECK(prefs_save(&a, "A:\\") == EWRPRO);
}

int main(void)
{
    t_round_trip();
    t_rejects_garbage();
    t_save_and_load();
    t_save_failures_keep_old();
    printf("test_prefs: %d checks, %d failed\n", tests, failures);
    return failures ? 1 : 0;
}
