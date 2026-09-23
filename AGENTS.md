# TOSFC : la préservation des données prime

Ces instructions s'appliquent à tout le dépôt et à toute IA qui intervient
sur TOS File Cmd. La sécurité des données est une exigence centrale du
logiciel, pas une amélioration facultative. Une fonctionnalité, un gain de
vitesse ou quelques octets économisés ne justifient jamais une perte de données.

## Règles obligatoires

- Avant de modifier une opération, identifier les fichiers, dossiers et
  secteurs qu'elle peut écrire, directement ou par un appel GEMDOS, BIOS ou
  XBIOS. Une écriture brute (Rwabs, Flopwr, Flopfmt) contourne le GEMDOS :
  elle doit être contrôlée explicitement.
- **`Fcreate` tronque un fichier existant et le GEMDOS n'a pas de création
  exclusive.** On sonde avec `Fsfirst` juste avant de créer, et seule une
  réponse « introuvable » (`EFILNF`, `ENMFIL`) autorise la création. Toute
  autre erreur de sonde interdit l'écriture.
- Une erreur de lecture, de recherche ou de fermeture n'est ni une fin de
  fichier normale, ni la fin d'un répertoire, ni la preuve qu'un chemin est
  libre. En cas de doute, refuser l'écriture ou la suppression.
- Ne supprimer que les fichiers effectivement créés par l'opération en cours.
- Pour remplacer un fichier, garder l'original récupérable (`TOSFC.BAK`)
  jusqu'à la fin de l'écriture, de la fermeture et de la relecture. Traiter
  les échecs de renommage, de restauration et de suppression de la sauvegarde.
  Ne jamais écraser un `TOSFC.BAK`, `TOSFC.NEW` ou autre temporaire préexistant.
- Ne supprimer une source déplacée qu'après comparaison complète de la copie.
  La taille affichée dans un panneau peut être périmée : relire les flux
  jusqu'au bout. Un dossier n'est supprimé que si tout son contenu a été
  déplacé.
- Lire tout un niveau de répertoire avant d'agir dessus : `Fsfirst`/`Fsnext`
  ne supportent pas d'être imbriqués, et un parcours à moitié lu ne doit
  jamais servir à effacer.
- Respecter les attributs (lecture seule, caché, système), les limites des
  chemins GEMDOS et des tampons, et l'identité source/destination.
- `Frename` d'un dossier vers un autre parent ne met pas à jour son entrée
  `..` sur les anciens TOS : un dossier se déplace par copie vérifiée puis
  suppression, jamais par renommage vers un autre dossier.
- Ne pas écrire sur un disque que l'utilisateur n'a pas choisi : au démarrage
  ou en quittant, la disquette du lecteur n'est peut-être plus celle du
  programme. Les préférences ne s'enregistrent que sur demande.
- Pour les opérations volontairement destructrices, identifier précisément
  la cible et obtenir la confirmation dans l'interface avant la première
  écriture. Le bouton par défaut d'une suppression est Cancel.
- Ajouter des tests de régression pour chaque risque corrigé : erreurs de
  lecture/écriture/fermeture, disque plein, annulation, collisions de noms,
  données malformées, tailles périmées et échecs de renommage selon le cas.
  Exécuter le vrai code C (`tests/fakedos.c`) et contrôler les octets
  conservés, pas seulement le message ou le code de retour.
- Après une modification native, `make` vérifie la taille chargée et les
  piles (`tools/check_budget.py`) : un débordement de pile écrit dans les
  données, et de là dans les fichiers. Ne jamais désactiver ces contrôles.
- Utiliser uniquement des images et dossiers jetables pour les essais
  destructifs (`bench/` copie toujours la disquette publiée). Ne pas tester
  sur les disques ou fichiers personnels de l'utilisateur.
- Décrire honnêtement les limites restantes, notamment les coupures pendant
  une écriture : FAT12 n'est pas transactionnel, ne pas promettre une
  atomicité que le GEMDOS ne fournit pas.

Ces règles autorisent les corrections et tests dans le périmètre demandé ;
elles n'ajoutent pas une étape de permission pour chaque correction réversible.

## Pour travailler

- `make` construit `build/TOSFC.PRG`, `make disk` les disquettes de `dist/`,
  `make test` les tests hôtes, `make bench` les bancs NeoST
  ([`bench/README.md`](bench/README.md)).
- Commentaires et documentation technique en français ; interface, README
  et manuel en anglais.
