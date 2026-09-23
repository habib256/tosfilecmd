/* icetool.c -- outil hote de TOSFC autour d'unice68 (sc68, GPL v3+) :
 * compresse ou decompresse un fichier ICE!, pour les tests et la disquette
 * de demonstration. N'est jamais compile pour l'Atari.
 *   icetool p in out     compresse
 *   icetool d in out     decompresse
 */
#include <stdio.h>
#include <stdlib.h>
#include "unice68.h"

int main(int argc, char **argv)
{
    FILE *f;
    long n;
    char *in, *out;
    int r;
    if (argc != 4) { fprintf(stderr, "usage: icetool p|d in out\n"); return 2; }
    f = fopen(argv[2], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    in = malloc(n + 16);
    if (fread(in, 1, n, f) != (size_t)n) return 2;
    fclose(f);
    if (argv[1][0] == 'p') {
        out = malloc(n * 2 + 1024);
        r = unice68_packer(out, (int)(n * 2 + 1024), in, (int)n);
        if (r <= 0) { fprintf(stderr, "pack failed\n"); return 1; }
        out[1] = 'C';               /* identifiant ICE! 2.4, comme l'outil unice68 */
        out[2] = 'E';
    } else {
        int csize = (int)n;
        int dsize = unice68_depacked_size(in, &csize);
        if (dsize < 0) { fprintf(stderr, "not ICE\n"); return 1; }
        out = malloc(dsize + 16);
        if (unice68_depacker(out, in)) { fprintf(stderr, "depack failed\n"); return 1; }
        r = dsize;
    }
    f = fopen(argv[3], "wb");
    fwrite(out, 1, r, f);
    fclose(f);
    return 0;
}
