# Changelog

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
