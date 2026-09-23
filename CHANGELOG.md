# Changelog

## 0.2.0 — 2026-09-23 — visionneuses (début de J2)

- **Lecteur de texte** (`T`, ou RETURN sur un fichier) : 80 colonnes,
  coupure entre les mots, CR/LF/CRLF/LFCR, tabulations, codes de contrôle en
  points ; documents **1st Word / 1st Word Plus** sans leurs lignes de format
  ni leurs codes de style ; pages, début et fin, **recherche** (`F`, `N`),
  **hexadécimal** (`H`, aller-retour à la même ligne). Aucun index : la
  ligne précédente se recalcule, un fichier de plusieurs centaines de Ko
  s'ouvre sans préparation. Fichier trop grand ou erreur de lecture :
  on montre le début, et on le dit.
- **Images** (`I`, ou RETURN sur une image) : Degas (PI1-3), Degas Elite
  compressé (PC1-3) et NEOchrome (NEO), plein écran dans leur résolution et
  leur palette ; **album** avec les flèches (les fichiers abîmés sont
  sautés, la sélection du panneau suit) ; image couleur sur moniteur
  monochrome **tramée** (Bayer 4 × 4, par tables : ~1,5 s à 8 MHz) ; image
  monochrome sur moniteur couleur en **gris** en moyenne résolution.
- Barre des touches : `T` Text et `I` Image ; libellés raccourcis.
- Disquette : `DEMO\PICTURES` (cinq images, une par format), et dans
  `DEMO\TEXTS` une lettre 1st Word, le manuel et un long texte.
- Outils : `tools/stpic.py` (fabrique et relit les images, conversions de
  référence), `tools/screenshots.py` (images et texte).
- Tests : `tests/test_view.c` (73 contrôles : formats, fichiers tronqués,
  3000 fichiers aléatoires sous AddressSanitizer, conversions, mise en page,
  aller-retour ligne suivante/précédente) ; banc `bench/viewers.py`
  (47 contrôles : mémoire vidéo et **chaque pixel** comparés aux images,
  en couleur et en monochrome ; texte ; disquette inchangée).

Corrigé pendant le développement : reculer d'une ligne depuis le milieu
d'un CRLF, ou juste après une ligne de format 1st Word, restait sur place ;
la luminance du blanc ST valait 14/15 ; les tables supposaient un `long`
de 32 bits (faux sur l'hôte des tests).

## 0.1.0 — 2026-09-23 — J1 : le gestionnaire

Première version : les deux panneaux et les opérations sur les fichiers, sur
le modèle d'A2 File Cmd.

- **Écran** : texte 80 × 25 dessiné directement en mémoire vidéo, moyenne
  résolution (police ROM 8 × 8, palette à 4 couleurs) et haute résolution
  monochrome (8 × 16) ; la basse résolution passe en moyenne le temps du
  programme. Cadres dessinés par TOSFC (glyphes propres), pointeur souris en
  cellule inversée. Seules les lignes modifiées sont comparées, et une cellule
  se dessine avec des masques précalculés : un déplacement de sélection prend
  environ 6 trames à 8 MHz, un écran complet une vingtaine.
- **Panneaux** : lecteurs (`Drvmap`), dossiers d'abord, tri par nom,
  extension, taille, date ou ordre du disque, fichiers cachés affichables,
  place libre, 1024 entrées au plus (signalé au-delà).
- **Opérations** (`src/fsops.c`, portable) : copier, déplacer, renommer,
  créer un dossier, supprimer, attributs, fichiers marqués et arborescences.
  Sonde avant création, `TOSFC.BAK` jusqu'à vérification, relecture
  comparée, parcours préalable, restauration sur erreur, ESC par blocs de
  32 Ko.
- **Erreurs disque** : gestionnaire `etv_critic` de TOSFC (Retry/Cancel,
  changement de disquette sur un lecteur unique).
- **Souris** : gestionnaire de paquets IKBD propre, clics sur les entrées,
  en-têtes, chemin, barre des touches et boutons.
- **TOSFC.INF** : enregistré sur demande seulement, via `TOSFC.NEW` relu.
- **Outils** : `elf2prg.py` (exécutable TOS relogeable depuis
  `m68k-elf-gcc`), `fat12.py` (fabrication, lecture et fsck des disquettes),
  `mkdisk.py` (disquettes 720 Ko et 360 Ko), `check_budget.py` (taille et
  piles depuis le graphe d'appels de GCC), `ppm2png.py`, `screenshots.py`.
- **Tests** : 142 contrôles des opérations et 21 de TOSFC.INF sur un faux
  GEMDOS à pannes injectées, FAT12, relocations.
- **Bancs NeoST** : smoke, ops, data_safety, mouse, prefs, memory —
  133 contrôles.

Corrigé pendant le développement, grâce aux bancs :
- plantage en quittant depuis `AUTO\` : le pointeur `INTIN` de la Line-A est
  nul avant l'ouverture du VDI ; TOSFC lui prête son propre tableau ;
- l'écran était entièrement comparé à chaque trame (75 % du temps processeur
  au repos) ;
- une réponse « Non » à l'écrasement ouvrait un résumé inutile.
