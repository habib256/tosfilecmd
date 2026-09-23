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
- ✅ Archives LHA ouvertes comme des dossiers (J3).

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

## J3 — images disque, archives, disquettes ✅ (0.4.0)

- ✅ `.ST` et `.MSA` ouvertes comme des dossiers en lecture seule (FAT12,
  pistes MSA compressées), extraction par la copie ordinaire.
- ✅ Archives LZH (`-lh0-`, `-lh5-`, niveaux 0 à 2), ZIP (stockés, deflate),
  ARC (méthodes 1 à 4, 8, 9) : CRC vérifié, noms ramenés en 8.3 sans doublon,
  conteneur modifié pendant l'ouverture → refus (`test_vfs`, `bench/archives.py`).
- ✅ Lire une disquette en image, écrire une image sur disquette, copier une
  disquette (un ou deux lecteurs), formater 9/10/11 secteurs, une ou deux
  faces : confirmation, cible affichée, disquette du programme protégée
  (`test_disk`, `bench/disktools.py`).
- Plus tard : écrire une disquette en `.MSA` compressé ; copie à un lecteur
  vérifiée dans NeoST (il faudrait changer de disquette en cours de banc) ;
  formatage 10/11 secteurs vérifié sur une vraie machine.
- Plus tard : archive dans une archive, LZH `-lh1-`/`-lh6-`/`-lh7-`, ZIP64,
  ZIP chiffrés (refusés proprement aujourd'hui : « Compression method not
  supported »).

## J4 — outils

Désassembleur 68000, CRC-32, recherche sur un volume, VERIFY, comparaison de
disques, vue de la FAT et des secteurs, UNDELETE FAT (`$E5`), TREE, SYNC,
IDENT — en plugins chargés par `Pexec`, avec un SDK.

## Côté NeoST (à signaler dans son TODO)

- Des ROM TOS d'Atari dans l'arbre de test, ou une procédure pour les fournir.
- Une option pour ne déclarer qu'un lecteur de disquette (`_nflops = 1`).
- Signalé dans le TODO de NeoST le 2026-09-23 : avec `--fastfdc`, une écriture
  qui suit un formatage lit une mémoire altérée (`bench/repro/fastfdc_format.py`) ;
  `bench/disktools.py` tourne sans `--fastfdc` en attendant. Demandé aussi : une
  commande serveur pour changer de disquette (copie à un lecteur).

### Livré par NeoST le 2026-09-23 (commit `c70dc1b`) — exploité

- **Identité de build** : `bench/neost.py` compare le `commit` de
  `neost-headless --version` à `git -C ../neost rev-parse --short=12 HEAD` et
  refuse un binaire périmé (`NEOST_ANY_BUILD=1` pour passer outre). C'est un
  binaire périmé qui avait fait échouer `bench/archives.py` (Fclose → EIHNDL).
- **Trace GEMDOS avec résultat** (`NEOST_GEMDOS_TRACE=1`, sortie dans
  `$BENCH_LOG`) : à utiliser tel quel pour tout souci de handle.
- **`disks/diskA.st` de NeoST** : les bancs ne lisent jamais `neost.cfg`
  (pas de `--from-cfg`) ; pour lancer l'interface de NeoST à la main, ne
  monter que des copies jetables de nos disquettes, en A: comme en B:.
