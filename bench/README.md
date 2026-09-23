# Les bancs

Ils jouent de TOS File Cmd comme un utilisateur, sur un Atari ST émulé sans
fenêtre par [NeoST](https://github.com/habib256/neost) (`neost-headless
--server`), et vérifient ce que l'écran affiche et ce qui est écrit sur les
disques. Rien n'est ajouté au binaire livré pour cela : l'adresse du
programme vient de la basepage courante du GEMDOS, celles de ses variables
de la table de symboles du lien (`build/tosfc.sym`), et l'écran se lit dans
`scr_chr`/`scr_att`, le texte exact que TOSFC dessine. Pour savoir qu'une
opération est finie, les bancs regardent `in_idle_polls`, le compteur de
sondages clavier sans événement : il n'avance que quand TOSFC attend
l'utilisateur.

Tous les essais se font sur des copies jetables : la disquette publiée
(`dist/TOSFC-<v>.st`) est copiée, les disques GEMDOS sont des dossiers
temporaires. Un banc refuse une disquette qui ne porte pas le
`build/TOSFC.PRG` courant.

| Banc | Ce qu'il vérifie |
|---|---|
| `smoke.py` | La disquette démarre sur les panneaux depuis `AUTO\` : ST 1 Mo couleur, ST mono, **520 ST 512 Ko avec la 360 Ko simple face**, STE. Q demande, rend la main sans plantage, résolution d'origine restaurée. **40 contrôles.** |
| `ops.py` | Copie de fichiers marqués (octets et date conservés), fichier vide, écrasement Non puis Oui sans `TOSFC.BAK` restant, arborescence, renommage (et refus sur un nom existant), création de dossier, déplacement vers un autre lecteur puis sur le même, attributs écrits sur la disquette, refus de supprimer un fichier en lecture seule, suppression d'arborescence (RETURN = Cancel), ESC pendant une copie (fichier partiel retiré), tri, fsck final. **45 contrôles.** |
| `data_safety.py` | `TOSFC.BAK` préexistant, disque plein pendant un écrasement (ancienne version restaurée, fsck), dossier copié dans lui-même, disquette protégée en écriture (boîte critique de TOSFC, image inchangée au bit près), lecteur vide (Retry, Cancel). **19 contrôles.** |
| `mouse.py` | Pointeur, clic pour sélectionner puis ouvrir, clic droit pour marquer, tri par les en-têtes, remontée par le chemin, changement de panneau, barre des touches, boutons des boîtes. **13 contrôles.** |
| `prefs.py` | Options (fichiers cachés), enregistrement de `TOSFC.INF` sans `TOSFC.NEW` restant, panneaux, tri et options retrouvés au démarrage suivant. **12 contrôles.** |
| `viewers.py` | Images en couleur (mémoire vidéo = bitmap du fichier, résolution, et **chaque pixel** de la capture = couleur de la palette), monochrome sur écran couleur (gris), couleur sur moniteur mono (tramage identique à la référence Python), album et sélection qui suit, images abîmées refusées et sautées ; **Spectrum 512 : chacun des 64 000 points à sa couleur**, mono tramé comme la référence, souris revenue ; texte : 1st Word, pages, fin, recherche, hexa aller-retour, fichier vide, binaire ; disquette inchangée au bit près. **53 contrôles.** |
| `editor.py` | Taper, ligne nouvelle, coupe (Ctrl+Y), F10, ESC avec ou sans enregistrer, nouveau fichier, refus (1st Word, lecture seule, binaire), fichier de 66 Ko réenregistré identique au bit près, disquette protégée (rien de perdu), aucun `TOSFC.$ED`/`TOSFC.BAK` restant, frappe rapide en fin de fichier, fsck. **25 contrôles.** |
| `music.py` | YM : 50 appels/s sur un écran 60 Hz, registres identiques à la partition ; SNDH compressé ICE! et en clair (le lecteur 68000 avance), sous-morceau suivant, pause, boîte Musique, arrêt et sortie qui décrochent `etv_timer` ; morceaux abîmés refusés ; **un vrai son** dans la sortie audio de NeoST. **16 contrôles.** |
| `archives.py` | TEXTS.LZH, PICTURES.ZIP, OLDIES.ARC, OLDDISK.MSA ouverts par RETURN : listes en 8.3, description, « Read-only » ; fichiers et dossiers copiés vers C: identiques octet pour octet (lus en Python par zipfile, lha.py, arcpack.py, fat12.py) ; écriture refusée (supprimer, renommer, créer, attributs, éditer, copier vers, déplacer) ; texte, image (mémoire vidéo exacte) et musique lus dans l'archive ; ESC referme ; ZIP tronqué refusé, CRC faux sans fichier partiel ; disquette inchangée. **42 contrôles.** |
| `disktools.py` | Disquette B: lue en image sur C: octet pour octet ; formatage 720 Ko (panneau vide aussitôt : GEMDOS a relu, fsck, numéro de série neuf) ; image `.MSA` écrite puis relue identique ; 800 Ko refusé par le lecteur et signalé ; copie A: → B: exacte ; puis, TOSFC démarré de A: : écraser, formater ou copier sur la disquette du programme refusé, image inchangée au bit près. FDC au rythme réel (voir TODO). **25 contrôles.** |
| `memory.py` | Creux réel des deux piles après une session chargée, comparé à la réserve et à l'estimation statique de `tools/check_budget.py`. **4 contrôles.** |

`run_all.py` les enchaîne et résume ; `make bench` le lance après `make disk`.

## Les faire tourner

Il faut NeoST construit (`cmake -B build && cmake --build build --target
neost-headless` dans son dépôt). Par défaut les bancs le cherchent dans
`../neost/build/neost-headless`, avec la ROM EmuTOS 192 Ko US de NeoST :

```sh
make disk
NEOST=/chemin/vers/neost-headless python3 bench/run_all.py
NEOST_ROM=/chemin/vers/tos104.img python3 bench/smoke.py   # un autre TOS
```

`BENCH_LOG=/tmp/neost.log` garde le journal de l'émulateur ;
`BENCH_OUT=dossier` range les journaux des bancs.

## Les pilotes

- `neost.py` : le protocole serveur de NeoST (trames, touches, souris,
  lecture mémoire sans effet de bord, captures), la table de symboles et la
  lecture de l'écran de TOSFC.
- `tosfc.py` : au-dessus, la lecture des panneaux et des boîtes, la
  navigation au clavier (`go`, `select`, `tag`), l'attente de fin
  d'opération, et l'accès aux disques de l'hôte et aux images (`tools/fat12.py`).

## Limites honnêtes

Un banc refuse un `neost-headless` construit sur un autre commit que celui
de `../neost` (`NEOST_ANY_BUILD=1` pour passer outre). La copie de disquette
à un seul lecteur et le formatage 10/11 secteurs ne sont vérifiés que sur
l'hôte (`test_disk`) : NeoST ne change pas de disquette en cours de session
et refuse, comme Hatari, une géométrie différente de l'image.

Les bancs tournent sous **EmuTOS** : les TOS 1.00 à 2.06 d'Atari n'ont pas
encore été essayés, faute de ROM dans l'arbre de NeoST. Un vrai lecteur de
disquette n'a pas non plus été essayé. Voir [TODO.md](../TODO.md).
