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

## J2 — visionneuses et éditeur

- Texte (avec 1st Word / 1st Word Plus) et hexadécimal, recherche.
- Éditeur de texte.
- Images plein écran : Degas (PI1-3, PC1-3 compressées), NEOchrome,
  Spectrum 512 (SPU/SPC) ; flèches pour feuilleter.
- Musique SNDH et YM en tâche de fond (interruption Timer), `P` pour la pause.

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
