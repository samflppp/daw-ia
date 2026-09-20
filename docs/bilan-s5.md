# Bilan de fin de S5 — DAW IA

**Période :** semaine 5 sur 26 (13 – 19 octobre 2026). Rédigé le 20 septembre 2026.
**Dépôt :** `samflppp/daw-ia` (privé), branche `main`, 6 commits (`8abfd45` → `8ae89a6`).
**Volume :** 53 fichiers, +3 700 lignes, −181. Nouveau module : `core/persistence`.
**Tests :** 104 cas hors audio (90 de domaine, 14 de persistance), 35 cas d'engine sous label `audio`.

## 1. Livrables demandés

| # | Livrable | État | Preuve |
|---|---|---|---|
| 1 | Provenance dans `CommandEnvelope`, v1 → v2 | Livré | 9 cas de domaine, dont lecture v1 et refus d'un v2 sans `origin` |
| 2 | Schéma SQLite, WAL, un seul écrivain, magasin BLAKE3 réutilisé | Livré | `docs/persistence.md` §2, aucune table de blobs |
| 3 | Sauvegarde et rechargement à l'identique, plugins réhydratés | Livré, mesuré en samples | RMS 0,424802 écrit / 0,424802 relu, dans deux processus |
| 4 | Un projet est un dossier copiable d'un bloc | Livré | dossier copié, rouvert, même état et même son |
| 5 | Version de schéma et chemin de migration dès la v1 | Livré | chaîne de migrations, refus d'un schéma venu du futur |
| 6 | Tests : aller-retour, undo après rechargement, provenances mêlées, gros projet, base corrompue, enveloppe v1 | Livré | 14 cas de persistance + 2 cas d'engine, tous par processus enfant |

## 2. La décision de la semaine : ce que le journal garde

Le journal SQLite n'est pas `journal()` du bus, et la question laissée ouverte
en S4 §9 se tranche ainsi : **deux objets différents**.

| | `CommandBus::journal()` | la table `journal` |
|---|---|---|
| contenu | les enveloppes qui construisent l'état courant, après fusion | toute la suite des événements, dans l'ordre d'écriture |
| annulées | absentes | présentes, avec leur ligne d'annulation |
| geste | une enveloppe fusionnée | une ligne `execute` et *n* lignes `coalesce` |
| rôle | rejeu, JSON-RPC, comparaison | vérité sur disque |

Trois conséquences, et chacune paie une exigence :

1. **Rien n'est effacé.** Une branche d'arrangement est un pointeur dans
   l'historique complet ; effacer les commandes annulées la rendrait
   impossible. La pile d'undo est une vue dérivée de la suite.
2. **Une fusion s'écrit en ligne nouvelle, pas en `UPDATE`.** Un balayage de
   fader laisse ses vingt valeurs sur disque — matière pour l'IA plus tard — et
   le chargeur ne garde que la dernière, qui est ce que le bus rejouerait. Le
   repli est sûr parce que le bus ne fusionne qu'avec le sommet de la pile :
   toute autre ligne intercalée casse la fusion. Un journal où ce n'est pas le
   cas est refusé, pas rejoué de travers.
3. **Les commandes transitoires ne sont pas écrites.** Sans cela, rouvrir un
   projet le ferait jouer.

## 3. La provenance, et pourquoi elle n'est pas une colonne

`CommandEnvelope` passe en v2 et porte `origin` : un acteur (`user`, `copilot`,
`generator`) et un contexte optionnel **nommé par digest** dans le magasin
existant. Une enveloppe v1 se lit encore et vaut `user` ; une v2 sans `origin`
est refusée — elle est malformée, pas ancienne.

La mettre seulement dans la table aurait suffi pour interroger « qu'a changé le
copilote mardi ». Elle n'aurait pas survécu à un rejeu depuis un journal
sérialisé, puisqu'un rejeu lit des enveloppes. Une provenance qui disparaît dès
que l'historique voyage en texte n'est pas une provenance.

Trois effets dans le bus, tous petits, tous nécessaires :

- un geste **ne fusionne pas** deux origines différentes : sinon une entrée
  d'historique aurait un auteur faux, et l'undo rendrait tout au premier venu ;
- `undo(by)` et `redo(by)` disent **qui demande le mouvement**, sans réécrire
  l'auteur de l'entrée — celui-ci reste dans la ligne `execute` ;
- le reçu porte la charge utile que le journal stockera, donc la forme fusionnée
  pour un geste : le magasin n'a rien à recalculer.

Le magasin, lui, reste aveugle : un rendu produit par le moteur génératif s'y
stocke exactement comme une prise de l'utilisateur.

## 4. Ce que la persistance a révélé, et qui était invisible avant

**Une piste ne venait d'aucune commande.** `Main.cpp` posait un `Track` dans
`ProjectState` directement. Tant que rien n'était rejoué, cela ne se voyait
pas ; le jour où le journal devient la vérité, un projet dont les pistes
viennent d'un appel direct **se rouvre vide**. `track.add` et `track.remove`
existent donc, avec l'index rendu par l'undo — remettre une piste au bon endroit
a demandé `insertTrack` et `trackIndex` à `ProjectState`.

C'est la même leçon que la S3 et la S4, une couche plus haut : ce n'est pas la
relecture qui l'a trouvé, c'est le fait de fermer le processus.

## 5. Mesurer, pas interroger

Les deux binaires de test se relancent **en processus enfant**. L'enfant écrit
le projet et meurt ; le parent rouvre et mesure. Un chargeur appelé deux fois
dans le même processus peut lire ce que le premier appel a laissé en mémoire,
en cache ou dans une connexion ouverte : il ne prouve rien.

| Cas | Résultat |
|---|---|
| Aller-retour complet | l'état JSON du processus mort est celui que le parent reconstruit |
| Identifiants | piste, clip et notes retrouvés par leurs ULID, pas par leur forme |
| Undo après rechargement | 8 entrées, dont **une** pour un geste de 20 commandes |
| Commande annulée | reste annulée, et reste à un redo près ; sa ligne est sur disque |
| Provenances mêlées | `SELECT … WHERE actor = 'copilot'` rend 1 ligne, digest et taille compris |
| Projet volumineux | 5 002 commandes relues en **2,4 s** |
| Base tronquée à la moitié | `storageError` nommé, aucun crash |
| Fichier qui n'est pas une base | refusé à l'ouverture |
| Schéma `99` | « this project was written by a newer version », pas d'ouverture dégradée |
| Enveloppe v1 en base | rejouée comme `user` |
| Dossier copié d'un bloc | s'ouvre ailleurs, même état |

**Et avec un plugin**, sous label `audio` : l'enfant héberge le CLAP compilé par
le dépôt, monte son gain par le bus, capture son état opaque dans le magasin du
projet, joue trois notes et écrit le RMS rendu. Le parent rouvre le dossier avec
un moteur neuf, un Edit neuf et un bus neuf :

```
rms written = 0.424802, rms reopened = 0.424802
```

Blob d'abord, paramètres épars par-dessus, dans cet ordre, mesuré en samples et
non déduit de l'état.

**Sur le binaire Windows lui-même.** Lancé une fois avec `--project`, tué de
force — donc sans fermeture propre, sans checkpoint — puis relancé :

```
project Demo: 5 commands replayed, 0 undone
```

## 6. Le magasin change de place

`ContentStore` (ex-`PluginStateStore`) vit maintenant dans
`<projet>.dawproj/blobs/` et non plus sous `AppData`. Le commentaire de S4
disait « machine state, outlives a project » : c'était vrai tant qu'un projet
n'était pas un dossier. Signalé avant d'être appliqué, et validé.

Prix payé sciemment : plus de déduplication entre deux projets qui capturent le
même état de plugin. Gain : un dossier copié sur une clé USB s'ouvre et sonne.

`EngineHost` ne possède donc plus le magasin — il n'est pas un état machine.

## 7. SQLite

Amalgamation officielle 3.53.4, épinglée par SHA-256 comme nlohmann et doctest,
compilée comme notre propre bibliothèque statique. Deux options méritent d'être
nommées : `SQLITE_DQS=0` (une chaîne entre guillemets doubles est un
identifiant, jamais un littéral — une faute de frappe dans un nom de colonne
doit échouer) et `SQLITE_OMIT_LOAD_EXTENSION` (un fichier de projet ne peut
jamais demander de charger une bibliothèque).

`core/persistence` ne lie ni JUCE ni Tracktion : ses 14 cas tournent dans le
preset `domain-only`.

## 8. Périmètre

Respecté. Pas de branches d'arrangement, pas de ramasse-miettes, pas de couche
d'extraction pour LLM, pas d'UI, pas de services Python, aucune implémentation
d'IA. Le journal rend le ramasse-miettes possible — il n'est pas écrit. Le
schéma rend une branche possible — elle n'est pas écrite.

Une chose est conçue et non implémentée, et c'est volontaire : il n'y a pas de
table `snapshot`. Le rejeu intégral suffit à 5 000 commandes ; le jour où il ne
suffira plus, la chaîne de migration est là pour ajouter la table.

## 9. Dette

| Dette | État |
|---|---|
| `journal()` n'est pas un log d'audit (question ouverte en S4 §9) | **Tranchée.** Deux objets distincts, §2 |
| Tempo non modifiable par commande | Aucune commande ne change le tempo aujourd'hui, donc rien n'est perdu au rechargement. À traiter quand une commande de tempo arrivera |
| Écriture synchrone à chaque commande | Une transaction par commande. Mesuré acceptable (5 002 commandes écrites puis relues sans peine) ; à revoir si une session longue montre le contraire |
| Fermeture forcée | Le WAL garde tout, vérifié sur le binaire Windows §5. Le checkpoint propre n'a lieu qu'à la fermeture normale |

Le portage Linux est hors périmètre du MVP : le job CI Linux reste vert en
compilation, et rien n'y est validé au runtime.

## 10. Où en est le jalon S8

Le jalon du 9 novembre — un binaire **Windows** qui joue de l'audio, charge un
plugin et accepte des commandes annulables — est atteint sur ses trois membres
depuis cette semaine, et il gagne une quatrième propriété qui n'était pas
demandée : ce binaire ouvre un projet, le rejoue et l'écrit.

Restent l'UI, les services Python et l'IA. Rien de cela n'est un inconnu
technique pour le jalon.
