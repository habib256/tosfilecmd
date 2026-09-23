# Changelog

## 0.4.0 — 2026-09-23 — J3 : images disque, archives, outils de disquette

- **Images et archives ouvertes comme des dossiers** (RETURN), en lecture
  seule : `.ST` et `.MSA` (FAT12 lu secteur par secteur, pistes MSA
  compressées), `.LZH`/`.LHA` (`-lh0-`, `-lh5-`, en-têtes 0 à 2, dossiers),
  `.ZIP` (stockés, deflate : `inflate.c` écrit pour TOSFC), `.ARC` (méthodes
  1 à 4, 8 et 9 : RLE, squeeze, LZW de compress 4.0). Noms ramenés en 8.3
  sans doublon (`~1`), 1 024 entrées, 16 niveaux.
- **Extraction = la copie ordinaire** : `fsops` lit désormais ses sources
  par une interface `SOURCE` (GEMDOS, image, archive, disquette) ; création
  exclusive, `TOSFC.BAK`, relecture — et CRC-16/CRC-32 vérifié avant la
  première écriture. Visionneuses et musique lisent dans les conteneurs.
  Écrire dedans (déplacer, supprimer, renommer, créer, attributs, éditer,
  copier vers) est refusé. Conteneur modifié depuis l'ouverture (taille,
  date) : refus. Chaîne de clusters d'un fichier d'image vérifiée à
  l'ouverture (boucle, chaîne coupée).
- **Outils de disquette** (`F`) : lire une disquette en image `.ST` (par la
  copie vérifiée), écrire une image `.ST`/`.MSA` sur disquette (chaque piste
  formatée, écrite, relue), copier une disquette à deux lecteurs ou à un seul
  (échanges reconnus d'après le secteur de boot, piste 0 écrite en dernier),
  formater 360/720/800/880 Ko (numéro de série neuf). La disquette d'où
  TOSFC a démarré n'est jamais une cible. Changement de disquette forcé pour
  le GEMDOS après écriture (`hdv_mediach`). Appels XBIOS dans `flop.S`.
- **Disquette** : `DEMO\ARCHIVES` (TEXTS.LZH, PICTURES.ZIP, OLDIES.ARC,
  OLDDISK.MSA). `BIG.BIN` ramené à 90 000 octets pour garder de la place à
  l'enregistrement de `LONG.TXT` (un enregistrement sûr écrit une copie).
- Chemins trop longs pour le titre d'un panneau : on en montre la fin.
  Erreurs TOS inconnues affichées avec leur numéro.
- **Tests** : `test_vfs` (230 : listes et contenus, extraction, corruptions,
  2 500 conteneurs aléatoires, méthodes non gérées), `test_disk` (108 :
  fausses disquettes à pannes). Tests de mutation des nouvelles protections.
  **Bancs** : `archives.py` (42), `disktools.py` (25). Les bancs refusent un
  `neost-headless` plus vieux que ses sources. `tools/arcpack.py` (ARC,
  vérifié contre unar), `fat12.msa`.
- Budget : 154 Ko sur 160 ; pile principale 11,6 Ko sur 16 (les sources
  appelées par pointeur sont désormais comptées).

Corrigé pendant le développement : la table des pistes MSA allouée en mots
de 32 bits (débordement sur l'hôte 64 bits) ; l'en-tête LZH relu dans un
tampon de 1 Ko refusait les fichiers plus longs ; la copie à un lecteur
abandonnait si l'utilisateur n'avait pas encore changé de disquette ; une
boucle dans la chaîne FAT d'un petit fichier aurait donné des données
fausses sans erreur (trouvé par test de mutation). Côté bancs : deux échecs
venaient de NeoST (un binaire périmé ; avec `--fastfdc`, une écriture après
un formatage lit une mémoire altérée — signalé, `disktools.py` tourne au
rythme réel du contrôleur).

## 0.3.0 — 2026-09-23 — J2 terminé : éditeur, Spectrum 512, musique

- **Éditeur de texte** (`E`, `F4`) : tampon à trou, défilement horizontal,
  lignes et colonnes, coupe de ligne, nouveau fichier. Fins de ligne
  ramenées à LF en mémoire et reconstituées dans le style du fichier : un
  fichier ouvert puis enregistré sans changement est identique au bit près
  (66 Ko vérifiés par le banc). Refus de ce qu'une édition abîmerait
  (octets nuls, fins mélangées, 1st Word, lecture seule, fichier plus grand
  que la mémoire, erreur de lecture). **Enregistrement sûr**
  (`ops_save_stream`) : `TOSFC.$ED` exclusif, écrit, fermé, relu et comparé ;
  l'original en `TOSFC.BAK` jusqu'au bout ; toute panne le remet en place.
- **Spectrum 512** (`.SPU`, `.SPC`) : décodage (RLE du SPC, masques de
  palette), et routine d'affichage 68000 dans la file VBL : 24
  `move.l (a0)+,(aN)+` par ligne sur trois registres, synchronisation sur le
  compteur vidéo, écran à 50 Hz, souris IKBD coupée pendant l'affichage.
  Réglée au cycle par le banc : **chacun des 64 000 points** a la couleur que
  donne la formule du format. Tramage point par point sur moniteur mono.
- **Musique en tâche de fond** : YM2 à YM6 (LHA `-lh5-` ou non), SNDH
  (ICE! 2.4 ou non) ; accroche à `etv_timer` en XBRA (50 Hz quelle que soit
  la fréquence de l'écran), Timer A pour les autres fréquences ; registre 7
  jamais privé de ses bits de port (le port A choisit le lecteur de
  disquette), ports jamais écrits ; pause, arrêt, sous-morceaux, boîte
  Musique, note dans la barre des touches ; arrêt et décrochage en quittant.
- **Décompresseurs portables** : `lzh.c` (niveaux d'en-tête 0 à 2, `-lh0-`,
  `-lh5-`, CRC-16), `ice.c` (ICE! 2.4 et « Ice! », transformation image),
  `ym.c`, `sndh.c` — bornes vérifiées partout.
- **Disquette** : `DEMO\MUSIC` (WELCOME.YM et un SNDH composés pour TOSFC,
  le SNDH en clair et compressé), `DEMO\PICTURES\RAINBOW.SPC` et `.SPU`.
- **Outils** : `tools/lha.py` (compresseur lh5, vérifié contre lhasa),
  `tools/chiptune.py` (la musique, et le lecteur SNDH en assembleur),
  unice68 dans `tests/ext/` (compresseur ICE! de référence, hôte seulement).
- **Tests** : `test_edit` (95), `test_music` (181 : LHA et ICE contre leurs
  références, fichiers tronqués, abîmés et aléatoires sous AddressSanitizer,
  YM, SNDH), `test_lha` (le compresseur contre lhasa). **Bancs** :
  `editor.py` (25, dont la vitesse de frappe), `music.py` (16, dont un vrai son mesuré dans la sortie
  audio de NeoST), Spectrum ajouté à `viewers.py` (53).
- Budget de taille porté à 160 Ko (129 Ko utilisés).

Corrigé pendant le développement : la table de travail du décompresseur LHA
annoncée trop petite (débordement vu par AddressSanitizer, taille désormais
vérifiée à la compilation) ; un trou d'alignement entre `.text` et `.data`
refusé par elf2prg ; les bancs qui attendaient TOSFC après l'avoir quitté ;
dans l'éditeur, un saut lointain (fin du texte) ne descendait la vue que
d'une ligne par touche, et chaque touche reparcourait le texte (8 s par
caractère en fin de fichier de 66 Ko, 0,15 s maintenant : accès en ligne au
tampon, recherche des fins de ligne par segments, nombre de lignes tenu à
jour) — le banc mesure désormais cette vitesse.

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
