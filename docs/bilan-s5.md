# Bilan de fin de S5 — DAW IA

**Période :** semaine 5 sur 26 (13 – 19 octobre 2026). Rédigé le 20 septembre 2026.
**Dépôt :** `samflppp/daw-ia` (privé), branche `main`, 8 commits (`8abfd45` → HEAD).
**Volume :** 53 fichiers, +3 700 lignes, −181. Nouveau module : `core/persistence`.
**Tests :** 106 cas hors audio (90 de domaine, 16 de persistance), 35 cas d'engine sous label `audio`.

## 1. Livrables demandés

| # | Livrable | État | Preuve |
|---|---|---|---|
| 1 | Provenance dans `CommandEnvelope`, v1 → v2 | Livré | 9 cas de domaine, dont lecture v1 et refus d'un v2 sans `origin` |
| 2 | Schéma SQLite, WAL, un seul écrivain, magasin BLAKE3 réutilisé | Livré | `docs/persistence.md` §2, aucune table de blobs |
| 3 | Sauvegarde et rechargement à l'identique, plugins réhydratés | Livré, mesuré en samples | RMS 0,424802 écrit / 0,424802 relu, dans deux processus |
| 4 | Un projet est un dossier copiable d'un bloc | Livré | dossier copié, rouvert, même état et même son |
| 5 | Version de schéma et chemin de migration dès la v1 | Livré | chaîne de migrations, refus d'un schéma venu du futur |
| 6 | Tests : aller-retour, undo après rechargement, provenances mêlées, gros projet, base corrompue, enveloppe v1 | Livré | 16 cas de persistance + 2 cas d'engine, tous par processus enfant |

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
| Projet remis au schéma 1 | migré vers 2, lignes intactes, `project_id` inchangé |
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

**Sur le binaire Windows lui-même.** Lancé avec `--project`, tué de force, puis
relancé :

```
project Demo: 5 commands replayed, 0 undone
```

Deux fois, et pour deux raisons différentes : la première sans aucun
checkpoint, le WAL portant tout ; la seconde après le battement d'autosauvegarde
de 30 s, le `-wal` mesuré à 0 octet pendant que la session tournait encore.

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

`core/persistence` ne lie ni JUCE ni Tracktion : ses 16 cas tournent dans le
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
| Fermeture forcée | **Payée.** `save()` vide le WAL sans fermer, l'application l'appelle toutes les 30 s et à la demande de fermeture. Vérifié sur le binaire : `-wal` à 0 octet pendant la session, 5 commandes rejouées après un `Stop-Process` |
| Une écriture ratée n'était visible qu'à la fermeture | **Payée.** Le même battement relit `status()` ; faute de panneau, le titre de la fenêtre porte le message |
| La chaîne de migration n'avait jamais tourné au-delà de `0→1` | **Payée.** Schéma 2 (index par acteur) et un cas qui remet un projet réel au schéma 1, le rouvre et vérifie lignes, version et identité |

Le portage Linux est hors périmètre du MVP : le job CI Linux reste vert en
compilation, et rien n'y est validé au runtime.

## 10. Suivi d'avancement

### 10.1 Les cinq semaines écoulées

| S | Visait | Livré | Ce qui en restait ouvert | État aujourd'hui |
|---|---|---|---|---|
| S1 | Socle : outillage, structure, CI | CI verte deux plateformes, application qui démarre, règles d'hygiène vérifiées | `setup-ubuntu.sh` hors `--ci` jamais lancé | **Reporté post-MVP** (Windows seule cible) |
| S2 | Command Bus | payload / undoRecord séparés, ULID, registry, coalescing par geste, rejeu depuis journal sérialisé | `journal()` est-il un log d'audit ? | **Tranché en S5 §2** |
| S3 | Le bus pilote un vrai moteur audio | Projection par réconciliation, son audible, règle de thread vérifiée et non documentée | — | Acquis |
| S4 | Hébergement VST3 et CLAP | Scan persisté en processus enfant, 5 commandes plugin, pont de paramètres sans allocation, fenêtre d'édition, VST3 et CLAP commerciaux audibles | Plugin sous Linux | **Reporté post-MVP** |
| S5 | Persistance et provenance | Journal SQLite append-only, dossier de projet, migration, provenance en enveloppe v2 | Aucun | — |

Aucune décision d'architecture n'a été rouverte depuis la S1.

### 10.2 Le jalon du 9 novembre

| Ce qui est demandé | État | Depuis |
|---|---|---|
| Un binaire **Windows** | Atteint | S1 |
| qui joue de l'audio | Atteint, mesuré en RMS | S3 |
| qui charge un plugin | Atteint, VST3 et CLAP, tiers réels | S4 |
| et accepte des commandes annulables | Atteint | S2 |

Le jalon est **atteint sur ses quatre membres, trois semaines avant la date**,
et il porte deux propriétés qui n'étaient pas demandées : le projet survit à la
fermeture, et chaque commande dit qui l'a demandée.

Ce que cela veut dire concrètement : les trois semaines qui restent avant le
9 novembre ne servent plus à *atteindre* le jalon, mais à décider ce qu'il
montre. Un binaire qui joue sans interface ne se démontre pas devant un comité.

### 10.3 Ce qui n'existe toujours pas

| Manquant | Conséquence aujourd'hui |
|---|---|
| UI | Une fenêtre vide. Tout passe par `--demo` et par les tests |
| Services Python JSON-RPC | Le bus n'est pilotable que depuis le processus |
| IA | Aucune ligne, par périmètre. Le bus et la provenance l'attendent |
| Automation, tempo variable | Le tempo est un scalaire qu'aucune commande ne change |
| Branches d'arrangement | Le journal les rend possibles, rien ne les écrit |

### 10.4 Les trois semaines restantes

Proposition, à arbitrer lundi :

| S | Candidat | Pourquoi maintenant | Pourquoi pas |
|---|---|---|---|
| S6 | **UI minimale** : les quatre écrans déclarés en S1, pilotés par le bus | Sans elle le jalon n'est pas démontrable devant un comité | Ne découvre aucun inconnu technique |
| S6 | Branches d'arrangement | Le schéma a été conçu pour, et c'est le différenciateur produit | Invisible sans UI |
| S7 | Services Python JSON-RPC | Socle du MCP, donc de tout le copilote | La règle de thread du bus devra être rouverte — une commande arrivant d'une socket |
| S7 | **Tempo en séquence** | Coûte presque rien aujourd'hui (aucun journal n'a de commande de tempo), coûte cher après les clips audio | Ne se voit pas |
| S8 | Gel, démonstration, bilan de jalon | — | — |

Mon avis, une ligne : **UI en S6**, parce qu'un jalon atteint et non montrable
est un jalon à moitié atteint ; **tempo en séquence en S7**, parce que c'est la
seule dette dont le prix augmente avec le temps.

### 10.5 Les risques, classés par ce qu'ils coûteraient

| Risque | Coût s'il se réalise | Ce qui le tient aujourd'hui |
|---|---|---|
| La règle de thread du bus face à une socket Python | Moyen : la règle est vérifiée en release, donc l'erreur sera une erreur, pas un crash | `rebindToCurrentThread()` existe pour la passation délibérée |
| Le tempo scalaire rencontre un clip audio | Élevé : change la forme de payloads déjà écrits sur des projets réels | Rien. C'est la dette à payer tôt |
| Le rejeu intégral devient lent | Faible : 5 002 commandes en 2,4 s, et la chaîne de migration peut ajouter une table `snapshot` | Mesuré, pas supposé |
| Une session longue souffre d'une transaction par commande | Faible | Mesuré sur 5 000 écritures |
| Le portage Linux | Nul avant le MVP | Décision de périmètre, CI de compilation gardée verte |

### 10.6 Ce qui a tenu depuis cinq semaines

Trois méthodes, et chacune a payé la semaine suivante :

1. **Exposer la conception avant d'écrire.** Le modèle d'état des plugins a été
   corrigé lundi de la S4 avant une ligne de code ; le schéma SQLite de cette
   semaine a été validé de la même façon.
2. **Un test qui interroge l'état ne prouve pas l'effet.** Trouvé en S3, payé en
   S4 (deux bogues de paramètre invisibles autrement), payé encore en S5 : c'est
   la fermeture du processus, pas la relecture, qui a montré qu'une piste ne
   venait d'aucune commande.
3. **Dire quand un choix contredit l'acquis au lieu de l'appliquer.** Le magasin
   de blobs déplacé dans le projet contredisait un commentaire de S4 ; signalé,
   validé, puis appliqué.
