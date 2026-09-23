# Sécurité des données de TOSFC

La conservation des données est une exigence centrale. Les instructions
obligatoires pour les IA et les contributeurs se trouvent dans
[AGENTS.md](../AGENTS.md). Ce document décrit, pour la version 0.4.0, ce que
chaque opération peut écrire, comment elle se protège et comment c'est
vérifié. Ce n'est ni une garantie contre toute panne matérielle, ni une
preuve exhaustive de l'absence de défauts.

## Protections par opération

| Opération | Risque | Protection | Vérifié par |
| --- | --- | --- | --- |
| Copie C | `Fcreate` tronquerait une destination existante ; copie incomplète prise pour bonne | Sonde `Fsfirst` juste avant de créer ; destination existante renommée en `TOSFC.BAK`, nouvelle copie écrite, fermée, **relue et comparée** (option Verify, active par défaut), puis `TOSFC.BAK` supprimé. Sur toute erreur : fichier créé retiré, `TOSFC.BAK` remis en place. | `test_fsops` (disque plein, lecture, fermeture, corruption, annulation), `bench/ops.py`, `bench/data_safety.py` |
| `TOSFC.BAK` préexistant | L'écraser détruirait peut-être la seule version intacte d'un échec précédent | L'écrasement est refusé tant qu'il existe. | `test_fsops`, `bench/data_safety.py` |
| Déplacement V vers un autre lecteur | Source supprimée sans copie complète | Comparaison intégrale **toujours**, même si Verify est désactivé ; source supprimée ensuite seulement. Source en lecture seule : copiée, jamais supprimée. | `test_fsops`, `bench/ops.py` |
| Déplacement V sur le même lecteur | — | Fichiers : `Frename` après sonde, avec la même sauvegarde `TOSFC.BAK` en cas d'écrasement. Dossiers : copie vérifiée puis suppression (voir AGENTS.md sur `..`). | `test_fsops`, `bench/ops.py` |
| Arborescences | Niveau à moitié lu pris pour complet ; dossier source supprimé alors qu'un fichier n'a pas suivi | Chaque niveau est lu en entier avant d'agir ; toute erreur autre que « fin » arrête. Un nom illisible (non 8.3) arrête le parcours. Un dossier n'est supprimé que si tout son contenu l'a été. Profondeur limitée à 16, chemins à 127 caractères. | `test_fsops` (erreur au milieu d'un parcours, fichier passé, nom corrompu) |
| Parcours préalable | Découvrir une erreur au milieu d'une opération | Copie, déplacement et suppression parcourent d'abord toute la sélection : un dossier illisible annule l'opération **avant la première écriture**. | `test_fsops`, `bench/ops.py` |
| Suppression D | Effacer un fichier protégé ; confirmation implicite | Confirmation, bouton par défaut **Cancel**, avertissement pour les dossiers. Fichiers en lecture seule jamais supprimés (le dossier qui les contient reste). | `test_fsops`, `bench/ops.py` |
| Renommer R, créer K | Écraser un nom existant ; nom invalide | Nom 8.3 validé et mis en majuscules ; nom existant refusé. | `test_fsops`, `bench/ops.py` |
| Copie d'un dossier dans lui-même | Récursion infinie, disque rempli | Refusé avant toute écriture. | `test_fsops`, `bench/data_safety.py` |
| Disquette protégée, lecteur vide | Boîte d'alerte du GEM par-dessus l'écran, ou pire, aucune | Gestionnaire `etv_critic` de TOSFC : Retry/Cancel, puis l'erreur remonte à l'opération qui nettoie. | `bench/data_safety.py` |
| TOSFC.INF | Écrire sur une disquette que l'utilisateur n'a pas choisie ; fichier tronqué | Enregistrement **seulement sur demande** (Options, Save) ; écrit dans `TOSFC.NEW`, relu, puis remplace `TOSFC.INF`. Un `TOSFC.NEW` préexistant bloque. | `test_prefs`, `bench/prefs.py` |
| Visionneuses (texte, images) | Fichier malformé qui ferait lire ou écrire hors des tampons ; erreur de lecture prise pour la fin | Lecture seule, aucun appel d'écriture. Décodeurs bornés (chaque répétition PackBits vérifiée contre la ligne et le fichier) ; erreur de lecture signalée à l'écran. | `test_view` (fichiers tronqués, 3000 fichiers aléatoires sous AddressSanitizer), `bench/viewers.py` (disquette identique au bit près après la session) |
| Éditeur, enregistrement | Fichier tronqué par une écriture ratée ; édition d'une partie seulement ; octets modifiés sans que l'utilisateur y ait touché | N'ouvre que les fichiers lus en entier sans erreur, sans octet nul ni fins de ligne mélangées, hors 1st Word et lecture seule. Enregistre dans `TOSFC.$ED` (création exclusive), ferme, relit et compare ; l'original devient `TOSFC.BAK` puis est supprimé, et revient en place sur toute erreur. `TOSFC.$ED` ou `TOSFC.BAK` préexistants bloquent. En cas d'échec le texte reste ouvert. | `test_edit` (disque plein, erreurs d'écriture, de fermeture, de relecture, de renommage, corruption ; octets de l'original vérifiés), `bench/editor.py` (disquette protégée, fichier de 66 Ko réenregistré identique) |
| Musique | Écrire le port A du YM2149 (sélection du lecteur de disquette) pendant une copie ; laisser une interruption pointer dans la mémoire libérée | Registres 0 à 13 seulement, registre 7 toujours avec ses bits de port à 1 ; le TOS masque les interruptions quand il touche au port A. Accroche XBRA retirée à l'arrêt et en quittant ; si un autre programme s'est accroché après TOSFC sans XBRA, TOSFC refuse de quitter plutôt que de laisser un vecteur pendant. Décompresseurs bornés. Un SNDH est un programme : un morceau fautif peut planter la machine (limite documentée). | `test_music`, `bench/music.py` (vecteur restauré, sortie propre pendant la lecture) |
| Images et archives ouvertes (RETURN) | Écrire dedans ; extraire un fichier abîmé comme s'il était bon ; lire un conteneur remplacé entre-temps ; déborder d'un tampon sur une archive malformée | Lecture seule : déplacer, supprimer, renommer, créer, attributs, éditer et copier *vers* un conteneur sont refusés. L'extraction est la copie ordinaire (sonde, `TOSFC.BAK`, relecture) ; chaque fichier d'archive est décompressé et son CRC vérifié **avant** la première écriture ; méthode inconnue ou fichier chiffré refusés. Taille et date du conteneur relevées à l'ouverture et revérifiées à chaque lecture. Chaîne de clusters d'un fichier d'image parcourue à l'ouverture : exactement la longueur voulue par sa taille, terminée par une fin de chaîne. Décompresseurs et lecteur FAT12 bornés (BPB faux, pistes MSA tronquées). | `test_vfs` (listes et contenus comparés, extraction par `ops_copy`, octet changé au milieu, images abîmées, 2 500 conteneurs aléatoires sous AddressSanitizer, conteneur modifié, méthodes non gérées), `bench/archives.py` |
| Outils de disquette (F) | Écraser la disquette du programme ou la source d'une copie ; image incomplète prise pour bonne ; GEMDOS qui garde en cache l'ancienne disquette | Secteur de boot de la disquette de TOSFC relevé au démarrage : jamais la cible d'une écriture, d'un formatage ni d'une copie. Confirmation nommant le lecteur, bouton par défaut Cancel. Chaque piste écrite est relue et comparée ; la piste 0 en dernier (une copie interrompue n'a pas de secteur de boot neuf) ; interruption ou erreur signalées comme « floppy incomplete ». Copie à un lecteur : à chaque échange, la disquette insérée est reconnue (la source a le secteur de boot relevé, la cible pas avant la fin). Lecture en image : copie ordinaire, relecture comprise. Changement de disquette forcé pour le GEMDOS après écriture. | `test_disk` (secteurs illisibles, relecture qui change, écriture faible, protection, géométrie refusée, échanges oubliés ou annulés, disquette du programme dans l'un ou l'autre lecteur ; octets intacts vérifiés), `bench/disktools.py` |
| Mémoire | Débordement de pile dans les données | Pile de 16 Ko réservée ; pire cas statique calculé au lien depuis le graphe d'appels de GCC (récursions comprises) ; creux réel mesuré par un banc. | `tools/check_budget.py`, `bench/memory.py` |

## Fichiers de récupération

Ne pas effacer automatiquement `TOSFC.BAK` ou `TOSFC.NEW` : un reste peut
être la seule version intacte après un échec de renommage. TOSFC le signale
(« Old version left as TOSFC.BAK ») et bloque les opérations qui
l'écraseraient. Examiner ou copier son contenu avant de le renommer.

## Limites connues

- FAT12 et le GEMDOS n'ont pas de transaction : une coupure de courant
  pendant une écriture peut laisser un fichier partiel ou des clusters
  perdus. La relecture valide ce que rend le pilote, pas sa persistance
  après une coupure.
- Pas de création exclusive dans le GEMDOS : la sonde précède la création
  de quelques instructions. Sous TOS monotâche c'est sans conséquence ; sous
  MiNT multitâche, un autre programme pourrait créer le même nom entre les
  deux.
- Les dates de dossiers ne sont pas conservées par une copie (le GEMDOS ne
  permet pas de les fixer). Celles des fichiers le sont (`Fdatime`).
- Les attributs de dossiers ne sont pas modifiables depuis TOSFC.
- Un panneau affiche au plus 1024 entrées ; au-delà il le dit. Les
  opérations sur les arborescences lisent au plus 640 entrées par niveau
  cumulé et refusent au-delà plutôt que d'agir sur une liste incomplète.
- Une ancienne copie d'une disquette a le même secteur de boot que sa
  source : avec un seul lecteur, TOSFC ne peut pas les distinguer et refuse
  de copier sur elle (la formater d'abord).
- Le formatage 10 et 11 secteurs n'est vérifié que sur l'hôte : NeoST, comme
  Hatari, refuse de formater une image .ST dans une autre géométrie. La copie
  à un seul lecteur non plus n'est vérifiée que sur l'hôte (NeoST ne sait pas
  changer de disquette en cours de banc).
- Tester sur une vraie machine : les bancs utilisent EmuTOS dans NeoST ;
  TOS 1.00 à 2.06 n'ont pas encore été essayés (voir [TODO.md](../TODO.md)).

## Résultats de la version 0.4.0

`make test` : **142** contrôles des opérations et **21** de TOSFC.INF sur le
faux GEMDOS à pannes injectées, **73** des visionneuses, **95** de l'éditeur,
**181** des décompresseurs et formats de musique, **227** des images et
archives, **108** des outils de disquette (fausses disquettes à pannes),
l'outil FAT12 (fsck compris) et la table de relocation de l'exécutable. Un test de mutation (retirer une protection)
fait échouer la suite pour chaque protection essayée ; en 0.4.0 : garde de
la disquette du programme, relecture des pistes, piste 0 en dernier,
reconnaissance des disquettes à l'échange, image sur la disquette cible,
CRC ZIP et ARC, conteneur modifié, chaîne de clusters (ce dernier essai a
trouvé un vrai trou, corrigé : une boucle dans la chaîne d'un petit fichier
passait sans erreur).
`make bench` : voir [bench/README.md](../bench/README.md) pour le compte des
contrôles natifs.
