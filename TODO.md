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

- ✅ Texte (avec 1st Word / 1st Word Plus) et hexadécimal, recherche (0.2.0).
- ✅ Images plein écran : Degas (PI1-3, PC1-3), NEOchrome ; album ;
  conversions couleur ↔ monochrome (0.2.0).
- Spectrum 512 (SPU/SPC) : 512 couleurs, palette réécrite à chaque ligne
  par une routine synchronisée au cycle près.
- Tiny, IFF ILBM et images STE (palette 4 bits) ; saisir une copie d'écran.
- Éditeur de texte.
- Musique SNDH et YM en tâche de fond (interruption Timer), `P` pour la pause.
- Lecture par fenêtres pour les fichiers plus grands que la mémoire libre.
- La place : 111,5 Ko chargés sur 112 Ko de budget. Avant J3, alléger la
  BSS (sauvegardes d'écran des boîtes, 24 Ko ; tableau des éléments, 24 Ko)
  ou charger les outils en surcouches.

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
