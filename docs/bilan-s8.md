# Bilan de fin de S8 — DAW IA

**Période :** semaine 8 sur 26. Rédigé le 22 septembre 2026.
**Dépôt :** `samflppp/daw-ia` (privé), branche `main`, 10 commits (`748ab9a` → `HEAD`).
**Volume :** 48 fichiers, +4 576 lignes, −75.
**Tests :** 199 cas hors audio (183 de domaine, 16 de persistance), 46 cas d'engine sous label
`audio`, 18 cas Python.
**Registry :** 28 types de commandes, inchangé depuis la S7bis — le copilote n'en a ajouté aucun.

Le logiciel s'appelait DAW IA et ne contenait aucune ligne d'IA. Il en contient.

## 1. Livrables demandés

| # | Livrable | État | Preuve |
|---|---|---|---|
| 1 | Transport : process Python séparé, JSON-RPC sur socket locale, surveillé | Livré | §3 ; mort du process dite dans le panneau et dans `daw.log` |
| 2 | Thread : une file vers le thread propriétaire | Livré, exposé et validé avant code | §2 |
| 3 | Groupe d'annulation | Livré, exposé et validé avant code | §4 ; journal réel §7 |
| 4 | Contexte : extraction du `ProjectState` en JSON | Livré, exposé et validé avant code | §5 |
| 5 | Outils : chaque commande avec schéma et description | Livré | 28 décrites, 27 offertes, §6 |
| 6 | Modèle derrière `IAProvider`, clé en variable d'environnement, coût journalisé | Livré | §8 ; coûts mesurés §9 |
| 7 | Panneau copilote déclaré au manifeste, interface jamais gelée | Livré | §10 |
| 8 | **La preuve** : les quatre phrases, une entrée par requête, Ctrl+Z | Livré | §7 ; Ctrl+Z §12 |
| 9 | Échecs propres | Livré, six cas | §11 |

## 2. Décision 1 — le thread : une file, jamais une passation

Une commande arrive d'une socket, donc d'un autre thread, et le bus appartient au thread message
depuis la S2 : le projecteur mutile un `Edit` Tracktion depuis les notifications, et Tracktion
l'attend là.

`rebindToCurrentThread()` existe pour donner le bus **une fois**, délibérément. S'en servir par
commande ferait alterner deux threads sur le même bus, c'est-à-dire écrire une course poliment.

`daw::domain::CommandQueue` est la réponse :

| Point | Choix |
|---|---|
| ce qui traverse | l'intention : un type, un payload. La forme qu'utilisent déjà le journal et JSON-RPC |
| ce qui ne traverse pas | un `Command` — non copiable ; le bus — inatteignable depuis la file |
| le réveil | une `std::function` que l'application remplit avec un `AsyncUpdater` ; le domaine ignore JUCE |
| la règle de thread | **celle du bus, pas une deuxième**. `drain()` depuis un mauvais thread est refusé par le bus avec `wrongThread`, et le projet n'est pas touché |
| la fermeture | `close()` répond à tous ceux qui attendaient, plutôt que de les laisser sur une file que personne ne videra |

Le test le montre : une requête soumise depuis un thread, drainée depuis un autre, rendue en
`wrongThread`, et aucune piste créée.

## 3. Le tuyau

Le DAW écoute sur `127.0.0.1` sur un port pris dans `[49500, 49599]`, lance le process et lui passe
le port. Deux DAW sur une machine ne se disputent donc rien, et l'enfant n'a aucune configuration à
lui.

Les deux côtés demandent :

| Sens | Méthodes |
|---|---|
| copilote → DAW | `state.get`, `tools.list`, `clip.notes`, `plugins.find`, `commands.execute` |
| DAW → copilote | `copilot.ask`, une fois par phrase tapée |

Une ligne de JSON par message : chaque message est petit, et un saut de ligne ne peut pas apparaître
dans une chaîne JSON sans être échappé. Un cadrage `Content-Length` n'aurait rien acheté.

Côté Python, les requêtes entrantes sont traitées sur un worker et jamais sur le thread lecteur :
répondre à `copilot.ask` veut dire rappeler le DAW et attendre, et un thread lecteur qui attend une
ligne qu'il doit lire lui-même est un interblocage.

La mort du process est vue par un minuteur, dite avec son code de sortie, et l'édition continue.

## 4. Décision 2 — le groupe d'annulation

Le coalescing ne fusionne que des commandes compatibles. « Ajoute une piste Basse et mets-y Vital »
en émet deux de types différents, et c'est **une** action pour qui l'a demandée.

`CommandBus::executeGroup` :

| Propriété | Comment |
|---|---|
| une entrée, un Ctrl+Z | une `Entry` porte N étapes ; l'undo les rend en ordre inverse — le plugin quitte la piste avant que la piste quitte le projet |
| rien à moitié | tout est appliqué avant qu'un seul observateur soit prévenu ; un échec défait les précédentes en ordre inverse, n'émet aucune notification et n'écrit aucune ligne |
| journal intact | **une notification par commande**, toutes portant le même groupe : le journal garde une ligne par commande, append-only, chacune rejouable seule |
| pas de greffe | une gesture ne fusionne jamais dans une entrée groupée : ce serait une ligne qui annule deux choses sans rapport |
| transitoire | une commande transient entre dans un groupe sans entrer dans l'historique ; un groupe qui n'en contient que ne laisse aucune entrée et ne tue pas la branche redo |

Enveloppe **v3**, champ `group`, **à côté de `gesture` et non à sa place** : une gesture fusionne des
commandes identiques et n'en garde qu'une, un groupe garde toutes les siennes et leur donne une
entrée. Un seul champ pour les deux rendrait le journal incapable de dire lequel des deux a eu lieu.
v1 et v2 restent lisibles.

SQLite : schéma **3**, colonnes `group_id` et `group_label`, index `journal_by_group`. Les deux
colonnes sont ajoutées depuis le code et non depuis le SQL, parce que SQLite ne sait pas dire
`ADD COLUMN IF NOT EXISTS` et qu'une migration incapable de tourner deux fois casse un projet déjà
réparé.

Au rejeu, les lignes consécutives de même groupe repartent ensemble : sans ça un projet rouvert
rendait le bon état avec la mauvaise histoire — trois Ctrl+Z là où l'utilisateur en avait laissé un.

**Un cas que ce bus ne sait pas rendre propre**, et qui est écrit dans le code plutôt que caché : si
un `revert` échoue **au milieu** d'un undo de groupe, les étapes déjà défaites le restent. L'entrée
reste sur la pile, l'erreur est rendue, et l'utilisateur peut redemander. Aucune commande du registry
ne peut refuser son `revert` aujourd'hui ; le jour où l'une le pourra, ce paragraphe sera la dette à
payer.

## 5. Décision 3 — le contexte : deux niveaux

Le problème n'est pas le format — `ProjectState` se sérialise déjà — c'est la taille.

| Niveau | Contenu | Pourquoi |
|---|---|---|
| résumé, à chaque requête | tempo (séquence entière, courte par nature, **identifiant du point à l'origine nommé**), pistes (id, index, nom, volume, pan, mute), plugins de chaque chaîne (id, format, nom, bypass, **nombre** de paramètres touchés), clips (id, début, durée, **nombre** de notes, note la plus grave et la plus aiguë), transport, plugins machine | de quoi choisir une piste, un clip, un plugin : ce dont presque toute requête a besoin |
| à la demande | `clip.notes` — les notes d'un clip, avec leurs identifiants | `note.quantize` nomme ses notes une à une |
| à la demande | `plugins.find` — le catalogue filtré | une machine à 300 plugins dépenserait plus en catalogue qu'en projet |

Ce qui n'y est **jamais** : les notes dans le résumé, les paramètres de plugins, les empreintes
d'état. Un sampler expose des milliers de paramètres ; les envoyer coûterait plus que tout le projet
et dirait moins que le nom du plugin.

La liste machine est coupée à 60 entrées, **le total est annoncé**, et ce que la coupe laisse dehors
reste atteignable par la recherche.

Je n'ai **pas** autorisé `noteIds` vide à valoir « toutes les notes ». Ça rendrait le rejeu dépendant
de l'état : la même enveloppe rejouée sur un clip enrichi quantifierait d'autres notes. L'acquis a
raison, on paie le coût des identifiants.

## 6. Les outils, et les identifiants que le modèle ne sait pas écrire

Les 28 commandes du registry portent chacune un schéma de payload et une phrase en français. Un test
compare la table au registry **dans les deux sens** : une commande ajoutée sans description casse la
suite, une description orpheline aussi.

27 sont offertes au modèle. `plugin.capture_state` reste dans la table et sort de la liste : capturer
l'état d'un plugin demande ses octets, et le copilote n'en a aucun.

**Les identifiants créés.** Un ULID fait 26 caractères de base32 Crockford, dont l'alphabet exclut I,
L, O et U. Demander ça à un modèle, c'est fabriquer des payloads que le domaine refuse. Le modèle
écrit `$new:basse` ; le process copilote minte le ULID avant que le payload atteigne le bus, une fois
par nom — le même nom dans la même requête désigne la même chose, sinon le plugin atterrirait sur une
piste qui n'existe pas.

La règle de la S2 ne bouge pas : **l'appelant fournit l'identifiant, le bus n'en engendre aucun.**
Seule change la partie de l'appelant qui l'écrit.

**Aucun privilège.** Le payload du copilote passe par `CommandRegistry::create` comme une ligne de
journal rejouée, et un payload invalide est refusé aussi fort.

## 7. La preuve

Vrai binaire, vrai modèle, projet bac à sable ouvert avec `--demo`. Les quatre phrases, l'une après
l'autre. Journal SQLite relu après coup :

```
 1..5 execute  track.add, clip.create_midi, note.add ×3     user
 6    execute  track.set_volume    copilot  "monte le volume de la piste 1 de 3 dB"
 7    execute  track.add           copilot  "ajoute une piste Basse et mets-y Vital"
 8    execute  plugin.insert       copilot  "ajoute une piste Basse et mets-y Vital"
 9    execute  note.quantize       copilot  "quantifie les notes du clip en doubles-croches…"
10    execute  note.transpose      copilot  "quantifie les notes du clip en doubles-croches…"
11    execute  tempo.set_bpm       copilot  "passe le tempo à 140 et fais boucler la lecture…"
```

Quatre requêtes, quatre groupes, six commandes. `transport.set_loop` **n'est pas dans le journal** :
transitoire, comme le veut la S7bis — et la boucle est pourtant active dans l'état lu après coup.

Ce que le copilote a répondu, mot pour mot :

> Volume de la piste « Demo » monté de 0 dB à 3 dB.
> Piste « Basse » créée avec Vital inséré dessus.
> Les 3 notes du clip "Demo" ont été quantifiées sur des doubles-croches et transposées une octave plus bas.
> Tempo réglé à 140 BPM et boucle activée sur le clip « Demo » (0–4 temps).

Lors d'une session à la main, une cinquième phrase hors du scénario — « ne garde que la première note
dans le pattern » — a produit **six `note.remove` dans une seule entrée**. Le groupe ne tient pas
seulement sur les phrases prévues.

## 8. Le fournisseur

Claude Sonnet 5, par la Messages API, derrière `IAProvider`.

| Critère | Ce qui a décidé |
|---|---|
| tool use natif | plusieurs appels dans un tour : c'est exactement la forme d'une requête qui fait une entrée d'historique |
| français | sans conditionnement ; le copilote parle à l'utilisateur, pas au développeur |
| coût | un bloc `usage` à chaque réponse : le coût est **mesuré**, pas estimé |

La clé vient de `DAW_IA_ANTHROPIC_API_KEY`, jamais du dépôt, **jamais d'un argument de ligne de
commande** — un argument est lisible par tous les process de la machine. La CI n'appelle aucune API
payante : les tests tournent contre un `ScriptedProvider`.

Une contrainte du fournisseur a été trouvée à l'écran : l'API refuse un nom d'outil qui n'est pas
`^[a-zA-Z0-9_-]{1,128}$`, et toutes nos commandes portent un point. **Le point reste le nom du DAW** ;
renommer les commandes aurait mis la règle d'un fournisseur dans le domaine, et le journal, le rejeu
et les manifestes auraient suivi. La traduction `track.add` ↔ `track__add` se fait dans l'adaptateur
Anthropic, au seul endroit qui sait à qui il parle.

## 9. Les coûts, mesurés

Tarifs Sonnet : 3 $ / M jetons en entrée, 15 $ en sortie, 3,75 $ pour une écriture de cache, 0,30 $
pour une lecture.

**Avant le cache de prompt** (première session, à la main) :

| Requête | entrée | sortie | cache lu | ≈ coût |
|---|---|---|---|---|
| volume | 22 711 | 162 | 0 | 0,071 $ |
| piste + Vital | 34 578 | 460 | 0 | 0,111 $ |
| tempo + boucle | 11 550 | 146 | 0 | 0,037 $ |
| suppression de notes | 36 883 | 954 | 0 | 0,125 $ |

**Après** — le système et les 27 schémas ne changent pas d'un tour à l'autre, donc le dernier outil
porte `cache_control` et tout ce qui est au-dessus devient cacheable. L'état du projet est
volontairement laissé dehors : il change à chaque requête, et mettre en cache ce qui change coûte
plus que ça ne rapporte.

| Requête | entrée | sortie | cache écrit | cache lu | ≈ coût |
|---|---|---|---|---|---|
| monte le volume de la piste 1 de 3 dB | 9 027 | 160 | 6 683 | 6 683 | **0,057 $** |
| ajoute une piste Basse et mets-y Vital | 13 782 | 314 | 0 | 20 049 | **0,052 $** |
| quantifie… et transpose d'une octave | 15 105 | 570 | 0 | 20 049 | **0,060 $** |
| passe le tempo à 140 et fais boucler | 9 557 | 291 | 0 | 13 366 | **0,037 $** |

**Environ 0,05 $ par requête**, contre 0,09 $ avant. Pour le plan d'affaires : une session de
cinquante requêtes coûte 2,50 $ de modèle. Le reste de l'entrée est l'état du projet et la
conversation, qui grossissent avec le projet — c'est là que portera le prochain gain, pas sur les
outils.

## 10. L'interface

Le panneau `copilot` est déclaré dans `workspaces/beatmaker.json` comme les autres, six panneaux au
lieu de cinq, sous l'historique dans la colonne de droite.

Il lit un `CopilotHost` et ne sait rien du process ni de la socket — même forme que `PluginHost` et
`WorkspaceHost`. `ask()` rend la main tout de suite ; ce qui suit arrive par un `ChangeBroadcaster`,
qui diffère vers le thread message et regroupe les rafales. **L'interface ne gèle jamais pendant que
le modèle réfléchit** : le champ est fermé le temps de la réponse, la fenêtre reste vivante.

Tokens, `LookAndFeel`, règle 1 (aucune valeur visuelle en dur) et règle 2 (un panneau ignore où il
est) : `scripts/check_hygiene.py` passe.

## 11. Les six échecs, et ce qu'ils disent

| Cas | Ce qui se passe |
|---|---|
| clé absente | « Aucune clé d'API. Posez `DAW_IA_ANTHROPIC_API_KEY` dans l'environnement, puis relancez le copilote. » Rien n'est modifié |
| modèle injoignable ou refus HTTP | le code et le début du corps sont rendus en français ; rien n'est modifié |
| process Python mort | le minuteur le voit, le panneau dit le code de sortie, le bouton **Relancer** apparaît, l'édition continue |
| payload invalide | refusé par `CommandRegistry::create` avant qu'une seule commande soit appliquée |
| commande refusée par le domaine | le groupe entier est défait ; vérifié sur le binaire, la piste « Fantôme » n'a pas survécu |
| requête ambiguë ou hors de portée | le modèle le dit en une phrase et n'appelle aucun outil de commande ; aucune entrée d'historique |

Aucun ne laisse le projet à moitié modifié.

## 12. Ce qui n'a pas été vérifié

**Le Ctrl+Z n'existait pas, et le bouton « Annuler » l'a caché pendant huit semaines.** Aucun
raccourci clavier n'avait jamais été posé dans ce projet : seul le piano roll écoutait des touches,
pour Suppr et les octaves. Le défaut n'est apparu qu'au moment de vérifier le §8 à l'écran — aucun
test ne l'aurait trouvé, puisque la commande d'annulation, elle, marchait.

Le raccourci vit maintenant dans `WorkspaceView` et non dans un panneau, pour la raison qui fait
exister la règle d'hygiène 2 : annuler appartient au projet, pas à ce qui a le focus. JUCE fait
remonter une touche non traitée le long de la chaîne des parents, donc la vue est le dernier
composant à la voir. Un champ de texte qui gère son propre Ctrl+Z le garde : annuler un mot tapé au
copilote ne doit pas annuler une édition du projet. `Ctrl+Y` et `Ctrl+Maj+Z` rétablissent.

**Vérifié à l'écran par l'utilisateur** sur le projet de la preuve : trois Ctrl+Z rendent le tempo,
puis la quantification **et** la transposition ensemble, puis la piste Basse **et** Vital ensemble.

L'annulation reste volontairement absente de JSON-RPC : le panneau et le clavier sont les seules
surfaces qui l'offrent, et une méthode RPC ouverte pour faciliter un test est une surface de plus à
tenir.

**Rien n'a été entendu.** Comme en S7bis : je vois, je ne peux pas écouter. Que la boucle reboucle et
que Vital sonne restent des vérifications à l'oreille.

**Les formulations floues ne sont pas éprouvées.** Les quatre phrases du périmètre passent. « Rends-
le plus chaud » n'a pas été essayé et n'a aucune raison de marcher.

## 13. Dette

| Dette | Poids | Quand |
|---|---|---|
| Les lectures que le modèle ferait **après** une commande voient l'état d'avant : les commandes partent en bloc à la fin du tour | moyen — c'est le prix de l'atomicité, et le bon prix pour l'instant | S9 si une requête le demande |
| Un `revert` qui échoue au milieu d'un undo de groupe laisse les étapes déjà défaites (§4) | faible aujourd'hui, aucune commande ne le peut | le jour où une commande pourra refuser son revert |
| L'état du projet reparti entier à chaque tour (§9) | moyen : c'est désormais le gros du coût | S9 ou S10 |
| Le transcript du panneau est une ligne par message, sans retour à la ligne ni défilement | faible | quand une réponse dépassera une ligne |
| `MAX_TURNS = 6` en dur | faible | quand une requête le touchera |

## 14. Vérifications visuelles, dans l'ordre

Projet bac à sable, jamais le tien. Lance le binaire avec la clé posée, puis :

1. **Le panneau existe.** Workspace beatmaker : la colonne de droite porte CHAÎNE, HISTORIQUE et
   COPILOTE. Le point d'état est bleu et dit « Prêt ».
2. **L'interface ne gèle pas.** Tape `monte le volume de la piste 1 de 3 dB`, envoie, et pendant que
   le point est jaune : clique une piste, ouvre un clip. La fenêtre doit répondre.
3. **Une entrée, un auteur.** L'historique gagne **une** ligne, point bleu, mention « copilote », et
   le libellé est ta phrase — pas « Volume de piste ».
4. **Deux commandes, une ligne.** `ajoute une piste Basse et mets-y Vital` : la piste apparaît avec
   Vital dans sa chaîne, et l'historique gagne **une** ligne marquée `x2`.
5. **Le Ctrl+Z.** Appuie une fois. La piste Basse **et** Vital doivent partir ensemble. Ctrl+Y les
   ramène ensemble. *(Vérifié le 22 septembre.)*
6. **Le transport n'écrit pas d'histoire.** `passe le tempo à 140 et fais boucler la lecture sur le
   clip` : le tempo passe à 140, la lecture reboucle, et l'historique ne gagne **qu'une** ligne —
   celle du tempo.
7. **Les notes bougent.** `quantifie les notes du clip en doubles-croches et transpose d'une octave
   vers le bas` : dans le piano roll, les notes s'alignent sur la grille et descendent d'une octave.
8. **L'échec est propre.** Ferme le process Python à la main (Gestionnaire des tâches, `python.exe`).
   Le panneau doit dire que le copilote s'est arrêté, proposer **Relancer**, et l'édition doit
   continuer. Le projet ne bouge pas.
9. **Hors de portée.** Tape `rends le morceau plus chaud`. Le copilote doit dire qu'il ne sait pas, et
   l'historique ne doit gagner aucune ligne.

## 15. Périmètre

Hors périmètre et resté dehors : moteur génératif, moteur harmonique, MCP complet, entraînement,
channel rack, playlist, branches d'arrangement, automation, les trois autres workspaces,
duplication.

Aucune décision de cette semaine ne fige la question ouverte de la S10 — dans FL un pattern regroupe
plusieurs canaux, chez nous un clip appartient à une seule piste. Le groupe est une propriété de
**l'historique** et ne dit rien de l'appartenance d'un clip.

## 16. Suivi d'avancement

| S | Visait | État aujourd'hui |
|---|---|---|
| S1 | Socle : outillage, structure, CI | Acquis |
| S2 | Command Bus | Acquis |
| S3 | Le bus pilote un vrai moteur audio | Acquis |
| S4 | Hébergement VST3 et CLAP | Acquis |
| S5 | Persistance et provenance | Acquis |
| S6 | Une interface qui se montre | Acquis |
| S7 | Le vocabulaire du domaine | Acquis |
| S8 | Le copilote | — |

Aucune décision d'architecture n'a été rouverte depuis la S1. La règle de thread de la S2, la règle
d'identifiant de la S3 et le journal append-only de la S5 ont tous tenu face à un agent qui pousse
des commandes depuis un autre process : aucune n'a été assouplie pour lui.
