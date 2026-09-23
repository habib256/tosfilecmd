# Sécurité des données de TOSFC

La conservation des données est une exigence centrale. Les instructions
obligatoires pour les IA et les contributeurs se trouvent dans
[AGENTS.md](../AGENTS.md). Ce document décrit, pour la version 0.1.0, ce que
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
- Tester sur une vraie machine : les bancs utilisent EmuTOS dans NeoST ;
  TOS 1.00 à 2.06 n'ont pas encore été essayés (voir [TODO.md](../TODO.md)).

## Résultats de la version 0.1.0

`make test` : **142** contrôles des opérations et **21** de TOSFC.INF sur le
faux GEMDOS à pannes injectées, l'outil FAT12 (fsck compris) et la table de
relocation de l'exécutable. Un test de mutation (retirer une protection)
fait échouer la suite pour chaque protection essayée.
`make bench` : voir [bench/README.md](../bench/README.md) pour le compte des
contrôles natifs.
