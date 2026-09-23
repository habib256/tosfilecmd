/*
 * test_fsops.c -- le vrai code des operations (src/fsops.c) sur le faux
 * GEMDOS, pannes comprises. On controle les octets conserves, pas seulement
 * les codes de retour.
 */
#include "fakedos.h"
#include "../src/fsops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests, failures;
static const char *current;

#define CHECK(c) do { tests++; if (!(c)) { failures++; \
    printf("FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, current, #c); } } while (0)

/* ---- Rappels de test ---- */

static int answer = ANS_YES, asked, merge_answer = ANS_YES;
static int progress_calls, errors_seen;
static long last_error;

static int t_ask(OPS *o, int q, const char *p, const FINFO *s, const FINFO *d)
{
    (void)o; (void)p; (void)s; (void)d;
    asked++;
    return q == Q_MERGE ? merge_answer : answer;
}
static void t_progress(OPS *o, const char *n) { (void)o; (void)n; progress_calls++; }
static int t_cancel(OPS *o)
{
    (void)o;
    return fd_cancel_after && progress_calls >= fd_cancel_after;
}
static int t_error(OPS *o, const char *p, long e, int more)
{
    (void)o; (void)p; (void)more;
    errors_seen++;
    last_error = e;
    return 1;
}

static OPS ops;

static void begin(const char *name)
{
    current = name;
    fd_reset();
    memset(&ops, 0, sizeof ops);
    ops.ask = t_ask;
    ops.progress = t_progress;
    ops.cancel = t_cancel;
    ops.error = t_error;
    ops.verify = 1;
    answer = ANS_YES;
    merge_answer = ANS_YES;
    asked = progress_calls = errors_seen = 0;
    last_error = 0;
    if (ops_begin(&ops)) { printf("ops_begin failed\n"); exit(2); }
    ops.bufsize = 1024;            /* plusieurs tours de boucle */
}

static void end(void)
{
    CHECK(fd_open_handles() == 0);
    ops_end(&ops);
}

static unsigned char *pattern(long n, int seed)
{
    unsigned char *p = malloc(n ? n : 1);
    long i;
    for (i = 0; i < n; i++) p[i] = (unsigned char)(i * 7 + seed);
    return p;
}

static int same(const char *path, const unsigned char *data, long n)
{
    long sz;
    const unsigned char *d = fd_get(path, &sz);
    return d && sz == n && !memcmp(d, data, n);
}

static FINFO item(const char *path)
{
    FINFO f;
    char pat[PATH_MAX_TOSFC];
    strcpy(pat, path);
    sys_first(pat, 0x17, &f);
    return f;
}

/* ---- Cas ---- */

static void t_copy_plain(void)
{
    unsigned char *a = pattern(5000, 1);
    FINFO f;
    begin("copy plain");
    fd_mkdir("C:\\SRC");
    fd_mkdir("D:\\DST");
    fd_put("C:\\SRC\\A.TXT", a, 5000, 0);
    f = item("C:\\SRC\\A.TXT");
    CHECK(ops_copy(&ops, "C:\\SRC\\", &f, 1, "D:\\DST\\", 0) == 0);
    CHECK(same("D:\\DST\\A.TXT", a, 5000));
    CHECK(same("C:\\SRC\\A.TXT", a, 5000));
    CHECK(ops.files_done == 1 && ops.errors == 0);
    CHECK(fd_calls[OP_WRITE] >= 5);
    end();
    free(a);
}

static void t_copy_empty(void)
{
    FINFO f;
    begin("copy empty file");
    fd_put("C:\\E", "", 0, 0);
    fd_mkdir("D:\\X");
    f = item("C:\\E");
    CHECK(ops_copy(&ops, "C:\\", &f, 1, "D:\\X\\", 0) == 0);
    CHECK(fd_exists("D:\\X\\E"));
    CHECK(same("D:\\X\\E", (const unsigned char *)"", 0));
    end();
}

static void t_overwrite_keeps_backup_until_verified(void)
{
    unsigned char *a = pattern(3000, 1), *old = pattern(700, 9);
    FINFO f;
    begin("overwrite yes");
    fd_put("C:\\A.TXT", a, 3000, 0);
    fd_mkdir("D:\\D");
    fd_put("D:\\D\\A.TXT", old, 700, 0);
    f = item("C:\\A.TXT");
    CHECK(ops_copy(&ops, "C:\\", &f, 1, "D:\\D\\", 0) == 0);
    CHECK(asked == 1);
    CHECK(same("D:\\D\\A.TXT", a, 3000));
    CHECK(!fd_exists("D:\\D\\TOSFC.BAK"));
    end();
    free(a); free(old);
}

static void t_overwrite_no(void)
{
    unsigned char *a = pattern(3000, 1), *old = pattern(700, 9);
    FINFO f;
    begin("overwrite no");
    fd_put("C:\\A.TXT", a, 3000, 0);
    fd_put("D:\\A.TXT", old, 700, 0);
    answer = ANS_NO;
    f = item("C:\\A.TXT");
    CHECK(ops_copy(&ops, "C:\\", &f, 1, "D:\\", 0) == 0);
    CHECK(same("D:\\A.TXT", old, 700));
    CHECK(ops.skipped == 1);
    CHECK(fd_calls[OP_CREATE] == 0);
    end();
    free(a); free(old);
}

static void t_existing_bak_blocks(void)
{
    unsigned char *a = pattern(300, 1), *old = pattern(200, 9), *bak = pattern(100, 5);
    FINFO f;
    begin("TOSFC.BAK already there");
    fd_put("C:\\A.TXT", a, 300, 0);
    fd_put("D:\\A.TXT", old, 200, 0);
    fd_put("D:\\TOSFC.BAK", bak, 100, 0);
    f = item("C:\\A.TXT");
    ops_copy(&ops, "C:\\", &f, 1, "D:\\", 0);
    CHECK(last_error == TE_BAKEXIST);
    CHECK(same("D:\\A.TXT", old, 200));
    CHECK(same("D:\\TOSFC.BAK", bak, 100));
    end();
    free(a); free(old); free(bak);
}

static void t_disk_full_restores_old(void)
{
    unsigned char *a = pattern(9000, 1), *old = pattern(800, 9);
    FINFO f;
    begin("disk full during overwrite");
    fd_put("C:\\A.TXT", a, 9000, 0);
    fd_put("D:\\A.TXT", old, 800, 0);
    fd_capacity['D' - 'A'] = 4000;
    f = item("C:\\A.TXT");
    ops_copy(&ops, "C:\\", &f, 1, "D:\\", 0);
    CHECK(last_error == TE_SHORTW);
    CHECK(same("D:\\A.TXT", old, 800));
    CHECK(!fd_exists("D:\\TOSFC.BAK"));
    CHECK(same("C:\\A.TXT", a, 9000));
    CHECK(fd_count('D') == 1);
    end();
    free(a); free(old);
}

static void t_read_error_removes_partial(void)
{
    unsigned char *a = pattern(5000, 1);
    FINFO f;
    begin("read error mid-file");
    fd_put("C:\\A.TXT", a, 5000, 0);
    fd_fail[OP_READ] = 3;
    fd_fail_code[OP_READ] = EREADF;
    f = item("C:\\A.TXT");
    ops_copy(&ops, "C:\\", &f, 1, "D:\\", 0);
    CHECK(last_error == EREADF);
    CHECK(!fd_exists("D:\\A.TXT"));
    CHECK(same("C:\\A.TXT", a, 5000));
    end();
    free(a);
}

static void t_close_error(void)
{
    unsigned char *a = pattern(500, 1), *old = pattern(80, 3);
    FINFO f;
    begin("close error on destination");
    fd_put("C:\\A.TXT", a, 500, 0);
    fd_put("D:\\A.TXT", old, 80, 0);
    fd_fail[OP_CLOSE] = 1;
    fd_fail_code[OP_CLOSE] = EWRITF;
    f = item("C:\\A.TXT");
    ops_copy(&ops, "C:\\", &f, 1, "D:\\", 0);
    CHECK(last_error == EWRITF);
    CHECK(same("D:\\A.TXT", old, 80));
    CHECK(!fd_exists("D:\\TOSFC.BAK"));
    end();
    free(a); free(old);
}

static void t_verify_catches_corruption(void)
{
    unsigned char *a = pattern(3000, 1), *old = pattern(80, 3);
    FINFO f;
    begin("verify catches a corrupted write");
    fd_put("C:\\A.TXT", a, 3000, 0);
    fd_put("D:\\A.TXT", old, 80, 0);
    fd_corrupt_write = 2;
    f = item("C:\\A.TXT");
    ops_copy(&ops, "C:\\", &f, 1, "D:\\", 0);
    CHECK(last_error == TE_VERIFY);
    CHECK(same("D:\\A.TXT", old, 80));
    end();
    free(a); free(old);
}

static void t_cancel_removes_partial(void)
{
    unsigned char *a = pattern(20000, 1);
    FINFO f;
    begin("ESC during a copy");
    fd_put("C:\\BIG", a, 20000, 0);
    fd_cancel_after = 4;
    f = item("C:\\BIG");
    CHECK(ops_copy(&ops, "C:\\", &f, 1, "D:\\", 0) == TE_CANCEL);
    CHECK(!fd_exists("D:\\BIG"));
    CHECK(same("C:\\BIG", a, 20000));
    end();
    free(a);
}

static void t_move_cross_drive(void)
{
    unsigned char *a = pattern(4000, 1);
    FINFO f;
    begin("move to another drive");
    fd_put("C:\\A.TXT", a, 4000, FA_HIDDEN);
    f = item("C:\\A.TXT");
    ops.verify = 0;               /* move verifie quand meme */
    CHECK(ops_copy(&ops, "C:\\", &f, 1, "D:\\", 1) == 0);
    CHECK(same("D:\\A.TXT", a, 4000));
    CHECK(!fd_exists("C:\\A.TXT"));
    CHECK(fd_attr("D:\\A.TXT") & FA_HIDDEN);
    CHECK(fd_calls[OP_READ] > 8);       /* relecture des deux cotes */
    end();
    free(a);
}

static void t_move_verify_fail_keeps_source(void)
{
    unsigned char *a = pattern(4000, 1);
    FINFO f;
    begin("move: verify failure keeps the source");
    fd_put("C:\\A.TXT", a, 4000, 0);
    fd_corrupt_write = 1;
    f = item("C:\\A.TXT");
    ops_copy(&ops, "C:\\", &f, 1, "D:\\", 1);
    CHECK(last_error == TE_VERIFY);
    CHECK(same("C:\\A.TXT", a, 4000));
    CHECK(!fd_exists("D:\\A.TXT"));
    end();
    free(a);
}

static void t_move_readonly_source_kept(void)
{
    unsigned char *a = pattern(100, 1);
    FINFO f;
    begin("move: read-only source is copied, not deleted");
    fd_put("C:\\A.TXT", a, 100, FA_RDONLY);
    f = item("C:\\A.TXT");
    ops_copy(&ops, "C:\\", &f, 1, "D:\\", 1);
    CHECK(last_error == TE_READONLY);
    CHECK(same("C:\\A.TXT", a, 100));
    CHECK(same("D:\\A.TXT", a, 100));
    end();
    free(a);
}

static void t_move_same_drive_rename(void)
{
    unsigned char *a = pattern(100, 1);
    FINFO f;
    begin("move on the same drive");
    fd_put("C:\\A.TXT", a, 100, 0);
    fd_mkdir("C:\\SUB");
    f = item("C:\\A.TXT");
    CHECK(ops_copy(&ops, "C:\\", &f, 1, "C:\\SUB\\", 1) == 0);
    CHECK(same("C:\\SUB\\A.TXT", a, 100));
    CHECK(!fd_exists("C:\\A.TXT"));
    CHECK(fd_calls[OP_CREATE] == 0);
    end();
    free(a);
}

static void t_move_same_drive_rename_fails(void)
{
    unsigned char *a = pattern(100, 1), *old = pattern(40, 2);
    FINFO f;
    begin("move same drive: rename failure restores the old file");
    fd_put("C:\\A.TXT", a, 100, 0);
    fd_mkdir("C:\\SUB");
    fd_put("C:\\SUB\\A.TXT", old, 40, 0);
    fd_fail[OP_RENAME] = 2;         /* 1 = vers TOSFC.BAK, 2 = le deplacement */
    f = item("C:\\A.TXT");
    ops_copy(&ops, "C:\\", &f, 1, "C:\\SUB\\", 1);
    CHECK(errors_seen >= 1);
    CHECK(same("C:\\SUB\\A.TXT", old, 40));
    CHECK(same("C:\\A.TXT", a, 100));
    CHECK(!fd_exists("C:\\SUB\\TOSFC.BAK"));
    end();
    free(a); free(old);
}

static void t_tree_copy(void)
{
    unsigned char *a = pattern(1500, 1), *b = pattern(2500, 2);
    FINFO f;
    begin("copy a tree");
    fd_mkdir("C:\\T");
    fd_mkdir("C:\\T\\S1");
    fd_mkdir("C:\\T\\S1\\S2");
    fd_put("C:\\T\\A", a, 1500, 0);
    fd_put("C:\\T\\S1\\S2\\B", b, 2500, 0);
    f = item("C:\\T");
    CHECK(ops_scan(&ops, "C:\\", &f, 1) == 0);
    CHECK(ops.files_total == 2 && ops.bytes_total == 4000);
    CHECK(ops_copy(&ops, "C:\\", &f, 1, "D:\\", 0) == 0);
    CHECK(same("D:\\T\\A", a, 1500));
    CHECK(same("D:\\T\\S1\\S2\\B", b, 2500));
    CHECK(fd_exists("C:\\T\\S1\\S2\\B"));
    end();
    free(a); free(b);
}

static void t_tree_move_partial_keeps_source_dir(void)
{
    unsigned char *a = pattern(100, 1), *b = pattern(200, 2), *old = pattern(10, 3);
    FINFO f;
    begin("move a tree with one skipped file");
    fd_mkdir("C:\\T");
    fd_put("C:\\T\\A", a, 100, 0);
    fd_put("C:\\T\\B", b, 200, 0);
    fd_mkdir("D:\\T");
    fd_put("D:\\T\\B", old, 10, 0);
    answer = ANS_NO;
    f = item("C:\\T");
    ops_copy(&ops, "C:\\", &f, 1, "D:\\", 1);
    CHECK(same("D:\\T\\A", a, 100));
    CHECK(!fd_exists("C:\\T\\A"));
    CHECK(same("C:\\T\\B", b, 200));     /* passe : reste a la source */
    CHECK(fd_isdir("C:\\T"));
    CHECK(same("D:\\T\\B", old, 10));
    end();
    free(a); free(b); free(old);
}

static void t_tree_into_itself(void)
{
    FINFO f;
    begin("copy a folder into itself");
    fd_mkdir("C:\\T");
    fd_mkdir("C:\\T\\IN");
    f = item("C:\\T");
    ops_copy(&ops, "C:\\", &f, 1, "C:\\T\\IN\\", 0);
    CHECK(last_error == TE_SELF);
    CHECK(!fd_exists("C:\\T\\IN\\T"));
    end();
}

static void t_same_file(void)
{
    unsigned char *a = pattern(100, 1);
    FINFO f;
    begin("copy a file onto itself");
    fd_put("C:\\A", a, 100, 0);
    f = item("C:\\A");
    ops_copy(&ops, "C:\\", &f, 1, "C:\\", 0);
    CHECK(last_error == TE_SELF);
    CHECK(same("C:\\A", a, 100));
    end();
    free(a);
}

static void t_listing_error_no_write(void)
{
    FINFO f;
    begin("unreadable folder: nothing deleted");
    fd_mkdir("C:\\T");
    fd_put("C:\\T\\A", "x", 1, 0);
    fd_put("C:\\T\\B", "y", 1, 0);
    fd_fail[OP_NEXT] = 1;
    fd_fail_code[OP_NEXT] = EREADF;
    f = item("C:\\T");
    CHECK(ops_scan(&ops, "C:\\", &f, 1) == EREADF);
    CHECK(fd_exists("C:\\T\\A") && fd_exists("C:\\T\\B"));
    /* Et si l'erreur arrive pendant la suppression elle-meme : */
    fd_fail[OP_NEXT] = 1;
    fd_fail_code[OP_NEXT] = EREADF;
    ops_delete(&ops, "C:\\", &f, 1);
    CHECK(last_error == EREADF);
    CHECK(fd_exists("C:\\T\\A") && fd_exists("C:\\T\\B"));
    end();
}

static void t_delete_tree_with_readonly(void)
{
    FINFO f;
    begin("delete a tree with a read-only file");
    fd_mkdir("C:\\T");
    fd_put("C:\\T\\A", "x", 1, 0);
    fd_put("C:\\T\\LOCK", "y", 1, FA_RDONLY);
    f = item("C:\\T");
    ops_delete(&ops, "C:\\", &f, 1);
    CHECK(!fd_exists("C:\\T\\A"));
    CHECK(fd_exists("C:\\T\\LOCK"));
    CHECK(fd_isdir("C:\\T"));
    CHECK(last_error == TE_READONLY);
    end();
}

static void t_delete_tree(void)
{
    FINFO f;
    begin("delete a tree");
    fd_mkdir("C:\\T");
    fd_mkdir("C:\\T\\U");
    fd_put("C:\\T\\U\\A", "x", 1, 0);
    fd_put("C:\\KEEP", "k", 1, 0);
    f = item("C:\\T");
    CHECK(ops_delete(&ops, "C:\\", &f, 1) == 0);
    CHECK(!fd_exists("C:\\T"));
    CHECK(fd_exists("C:\\KEEP"));
    CHECK(fd_count('C') == 1);
    end();
}

static void t_bad_name_in_dir(void)
{
    FINFO f;
    begin("a corrupted name stops the walk");
    fd_mkdir("C:\\T");
    fd_put("C:\\T\\A", "x", 1, 0);
    fd_put("C:\\T\\B*D", "y", 1, 0);
    f = item("C:\\T");
    ops_delete(&ops, "C:\\", &f, 1);
    CHECK(last_error == TE_BADDIR);
    CHECK(fd_exists("C:\\T\\A"));
    end();
}

static void t_rename_and_mkdir(void)
{
    begin("rename and mkdir");
    fd_put("C:\\A.TXT", "a", 1, 0);
    fd_put("C:\\B.TXT", "b", 1, 0);
    CHECK(ops_rename("C:\\", "A.TXT", "b.txt") == TE_EXISTS);
    CHECK(fd_exists("C:\\A.TXT") && fd_exists("C:\\B.TXT"));
    CHECK(ops_rename("C:\\", "A.TXT", "new.doc") == 0);
    CHECK(fd_exists("C:\\NEW.DOC") && !fd_exists("C:\\A.TXT"));
    CHECK(ops_rename("C:\\", "NEW.DOC", "TOOLONGNAME") == TE_BADNAME);
    CHECK(ops_mkdir("C:\\", "work") == 0);
    CHECK(fd_isdir("C:\\WORK"));
    CHECK(ops_mkdir("C:\\", "WORK") == TE_EXISTS);
    CHECK(ops_mkdir("C:\\", "a*b") == TE_BADNAME);
    end();
}

static void t_names(void)
{
    char out[13];
    current = "8.3 names";
    CHECK(name_normalize("readme.txt", out) == 0 && !strcmp(out, "README.TXT"));
    CHECK(name_normalize("  A  ", out) == 0 && !strcmp(out, "A"));
    CHECK(name_normalize("NOM.", out) == 0 && !strcmp(out, "NOM"));
    CHECK(name_normalize("ABCDEFGH.IJK", out) == 0);
    CHECK(name_normalize("ABCDEFGHI", out) == TE_BADNAME);
    CHECK(name_normalize("A.BCDE", out) == TE_BADNAME);
    CHECK(name_normalize(".HIDDEN", out) == TE_BADNAME);
    CHECK(name_normalize("A.B.C", out) == TE_BADNAME);
    CHECK(name_normalize("A B", out) == TE_BADNAME);
    CHECK(name_normalize("A\\B", out) == TE_BADNAME);
    CHECK(name_normalize("", out) == TE_BADNAME);
}

static void t_overwrite_readonly_refused(void)
{
    unsigned char *a = pattern(50, 1), *old = pattern(20, 2);
    FINFO f;
    begin("overwrite onto a read-only file is refused");
    fd_put("C:\\A", a, 50, 0);
    fd_put("D:\\A", old, 20, FA_RDONLY);
    f = item("C:\\A");
    ops_copy(&ops, "C:\\", &f, 1, "D:\\", 0);
    CHECK(last_error == TE_READONLY);
    CHECK(same("D:\\A", old, 20));
    end();
    free(a); free(old);
}

static void t_overwrite_all(void)
{
    FINFO f[2];
    begin("overwrite all");
    fd_put("C:\\A", "1", 1, 0);
    fd_put("C:\\B", "2", 1, 0);
    fd_put("D:\\A", "x", 1, 0);
    fd_put("D:\\B", "y", 1, 0);
    answer = ANS_ALL;
    f[0] = item("C:\\A");
    f[1] = item("C:\\B");
    CHECK(ops_copy(&ops, "C:\\", f, 2, "D:\\", 0) == 0);
    CHECK(asked == 1);
    CHECK(same("D:\\A", (const unsigned char *)"1", 1));
    CHECK(same("D:\\B", (const unsigned char *)"2", 1));
    end();
}

static void t_probe_error_refuses(void)
{
    unsigned char *a = pattern(50, 1);
    FINFO f;
    begin("a failing probe forbids the write");
    fd_put("C:\\A", a, 50, 0);
    f = item("C:\\A");
    fd_fail[OP_FIRST] = 1;
    fd_fail_code[OP_FIRST] = EDRVNR;
    ops_copy(&ops, "C:\\", &f, 1, "D:\\", 0);
    CHECK(last_error == EDRVNR);
    CHECK(fd_calls[OP_CREATE] == 0);
    end();
    free(a);
}

static void t_restore_failure_reported(void)
{
    unsigned char *a = pattern(3000, 1), *old = pattern(30, 2);
    FINFO f;
    begin("failed restore leaves TOSFC.BAK and says so");
    fd_put("C:\\A", a, 3000, 0);
    fd_put("D:\\A", old, 30, 0);
    fd_capacity['D' - 'A'] = 1000;
    fd_fail[OP_RENAME] = 2;        /* la remise en place echoue */
    f = item("C:\\A");
    ops_copy(&ops, "C:\\", &f, 1, "D:\\", 0);
    CHECK(same("D:\\TOSFC.BAK", old, 30));
    CHECK(ops.warnings >= 1);
    CHECK(!strcmp(ops.warn_path, "D:\\TOSFC.BAK"));
    end();
    free(a); free(old);
}

int main(void)
{
    t_names();
    t_copy_plain();
    t_copy_empty();
    t_overwrite_keeps_backup_until_verified();
    t_overwrite_no();
    t_overwrite_all();
    t_existing_bak_blocks();
    t_disk_full_restores_old();
    t_read_error_removes_partial();
    t_close_error();
    t_verify_catches_corruption();
    t_cancel_removes_partial();
    t_move_cross_drive();
    t_move_verify_fail_keeps_source();
    t_move_readonly_source_kept();
    t_move_same_drive_rename();
    t_move_same_drive_rename_fails();
    t_tree_copy();
    t_tree_move_partial_keeps_source_dir();
    t_tree_into_itself();
    t_same_file();
    t_listing_error_no_write();
    t_delete_tree_with_readonly();
    t_delete_tree();
    t_bad_name_in_dir();
    t_rename_and_mkdir();
    t_overwrite_readonly_refused();
    t_probe_error_refuses();
    t_restore_failure_reported();
    printf("test_fsops: %d checks, %d failed\n", tests, failures);
    return failures ? 1 : 0;
}
