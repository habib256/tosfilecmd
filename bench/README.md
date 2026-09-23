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
| `viewers.py` | Images en couleur (mémoire vidéo = bitmap du fichier, résolution, et **chaque pixel** de la capture = couleur de la palette), monochrome sur écran couleur (gris), couleur sur moniteur mono (tramage identique à la référence Python), album et sélection qui suit, images abîmées refusées et sautées ; texte : 1st Word, pages, fin, recherche, hexa aller-retour, fichier vide, binaire ; disquette inchangée au bit près. **47 contrôles.** |
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

Les bancs tournent sous **EmuTOS** : les TOS 1.00 à 2.06 d'Atari n'ont pas
encore été essayés, faute de ROM dans l'arbre de NeoST. Un vrai lecteur de
disquette n'a pas non plus été essayé. Voir [TODO.md](../TODO.md).
