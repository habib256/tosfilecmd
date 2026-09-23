# Feuille de route

Les jalons suivent le plan initial, calqué sur la qualité d'A2 File Cmd :
chaque fonction arrive avec ses tests hôtes, ses bancs NeoST et sa ligne dans
[docs/DATA-SAFETY.md](docs/DATA-SAFETY.md).

## J1 — le gestionnaire ✅ (0.1.0)

Panneaux, tri, marquage, copier/déplacer/renommer/supprimer/créer, attributs,
progression et ESC, souris, aide, préférences, erreurs critiques.

### Reste à faire pour clore J1 sur du vrai matériel

- **TOS d'Atari** : les bancs tournent sous EmuTOS. Passer `smoke`, `ops` et
  `data_safety` sous TOS 1.00, 1.04, 1.62 et 2.06 (`NEOST_ROM=…`), en
  particulier : `Frename` de dossiers sur TOS 1.00, `Fdatime` sur TOS 1.00,
  cache GEMDOS et changement de disquette (`Mediach`).
- **Machine réelle** : un 520/1040 ST avec lecteur de disquette.
- **Un seul lecteur** : NeoST déclare toujours deux lecteurs ; le dialogue
  « Insert disk B: » n'est vérifié que par le code, pas par un banc.
- **Redessin plein écran** : ~20 trames à 8 MHz. Dessiner deux cellules par
  mot de 16 bits et traiter les espaces à part devrait le diviser par deux.
- **Sélection par la souris** : glisser pour marquer une série.
- **Lancer un programme** (`.PRG`, `.TOS`, `.TTP`) depuis un panneau, et
  revenir à TOSFC ensuite.
- Archives LHA : `src/lzh.c` sait déjà lire les en-têtes et décompresser
  `-lh5-` (pour les YM) ; l'ouverture d'un `.LZH` comme dossier est pour J3.

## J2 — visionneuses et éditeur ✅ (0.2.0, 0.3.0)

Texte, 1st Word, hexadécimal, recherche ; Degas, Degas Elite, NEOchrome,
Spectrum 512 ; éditeur ; musique YM et SNDH en tâche de fond.

### Plus tard (hors du plan initial de J2)

- Images Tiny, IFF ILBM, palettes STE 4 bits ; copie d'écran.
- Effets spéciaux des YM4-YM6 (digidrums, voix SID).
- Lecture par fenêtres des fichiers plus grands que la mémoire libre (texte, édition).
- Musique pendant l'affichage Spectrum 512 : la routine garde le processeur,
  le morceau hoquette ; la routine pourrait appeler elle-même le lecteur
  pendant les lignes de bordure.
- Le TOS d'Atari (1.00 à 2.06) et une vraie machine, pour tout J2 aussi
  (la routine Spectrum est réglée sur l'émulation de NeoST, fidèle à Hatari).

## J3 — images disque et archives

- `.ST` et `.MSA` ouvertes comme des dossiers en lecture seule, extraction.
- Lire une disquette en image, écrire une image sur disquette (`Floprd`,
  `Flopwr`), copier une disquette (un ou deux lecteurs), formater (`Flopfmt`,
  9/10/11 secteurs) : confirmation, cible affichée, volume du programme protégé.
- Archives ARC, LZH (lh5) et ZIP (stockés et deflate) : extraction avec
  création exclusive.

## J4 — outils

Désassembleur 68000, CRC-32, recherche sur un volume, VERIFY, comparaison de
disques, vue de la FAT et des secteurs, UNDELETE FAT (`$E5`), TREE, SYNC,
IDENT — en plugins chargés par `Pexec`, avec un SDK.

## Côté NeoST (à signaler dans son TODO)

- Des ROM TOS d'Atari dans l'arbre de test, ou une procédure pour les fournir.
- Une option pour ne déclarer qu'un lecteur de disquette (`_nflops = 1`).
