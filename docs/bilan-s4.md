# Bilan de fin de S4 — DAW IA

**Période :** semaine 4 sur 26 (6 – 12 octobre 2026). Rédigé le 19 septembre 2026.
**Dépôt :** `samflppp/daw-ia` (privé), branche `main`, 8 commits (`3542048` → `e3887ab`).
**Volume :** 46 fichiers, +7 000 lignes environ. `core/engine` passe de 11 à 22 fichiers.
**Tests :** 75 cas de domaine (sans JUCE), 33 cas d'engine sous label `audio`.

## 1. Livrables demandés

| # | Livrable | État | Preuve |
|---|---|---|---|
| 1 | Scan VST3 et CLAP, liste persistée, survit au crash d'un plugin | Livré | `PluginCatalogue`, 2 cas dédiés (liste noire, persistance) |
| 2 | Instanciation sur une piste, par le bus, id fourni par l'appelant | Livré | `plugin.insert`, 5 cas de domaine |
| 3 | Projection des plugins par identité, sans rien reconstruire sans raison | Livré | `ProjectProjector::reconcilePlugins`, digest comparé avant écriture |
| 4 | Modèle d'état du plugin — **le point de conception de la semaine** | Livré, exposé et validé avant d'écrire une ligne | `docs/plugins.md` §1–§2 |
| 5 | Fenêtre d'édition ouverte depuis l'app | Livré, vu à l'écran | `PluginWindow`, log : `plugin window open, 1232 by 436` |
| 6 | Preuve audible avec un plugin tiers réel | **Livré pour VST3**, partiel pour CLAP | AL-1 (VST3) : RMS mesuré, son entendu. CLAP : voir §5 |
| 7 | Tests d'engine sous label `audio` | Livré | 33 cas, dont 12 nouveaux sur les plugins |

Ajout non prévu lundi : un **vrai plugin CLAP compilé par le dépôt**
(`core/engine/tests/clap_fixture/`). Justification en §5.

## 2. Le modèle d'état, et la correction reçue lundi

La proposition initiale disait : l'état d'un plugin est un blob opaque lourd,
capturé ponctuellement, gardé par digest. Elle était incomplète, et la correction
l'a dit : refuser de perdre le suivi en temps réel de l'état, parce que
l'ergonomie visée ne le permet pas.

Le modèle retenu a donc **deux mécanismes, non redondants** :

| | `PluginParam` | `StateBlobRef` |
|---|---|---|
| quoi | une valeur normalisée par paramètre touché | le chunk binaire du plugin |
| rythme | continu, pendant un geste | ponctuel : sauvegarde, fin de session |
| dans le journal | une commande par valeur, fusionnée par geste | `{digest, byteCount}`, ~80 octets |

Trois décisions qui tiennent tout :

1. **`params` est épars.** Seuls les paramètres touchés y entrent. Un Omnisphere
   expose des milliers de paramètres ; les mémoriser tous mettrait des
   mégaoctets de JSON dans chaque `toValue()`, donc dans chaque comparaison et
   dans chaque hash de S5.
2. **Le blob est adressé par contenu (BLAKE3).** Déduplication, immuabilité —
   donc annuler une capture reste toujours possible — et vérification : une
   lecture recalcule le digest et refuse des octets abîmés au lieu de les donner
   à un plugin. Un état de sampleur de 12 Mo laisse 80 octets dans le projet.
3. **Ordre de réhydratation fixe :** le blob d'abord, les paramètres épars
   par-dessus. Un seul endroit l'applique, et un test le mesure en samples
   (§4, dernier point).

Conséquence assumée, écrite dans `docs/plugins.md` §7 : entre deux captures,
l'instance vivante porte un état interne que le domaine ignore (un preset chargé,
un échantillon choisi). **`ProjectState` est exact aux points de capture, pas
entre eux.** C'est pour cela que le projector n'injecte un blob que si son digest
a changé : le repousser écraserait ce que l'utilisateur vient de faire dans la
fenêtre du plugin.

## 3. Le pont de paramètres : rendre la main du geste au bus

Sans lui, la moitié du projet échappe à l'undo. Avec lui, un balayage de bouton
dans la fenêtre d'un plugin fait **une seule entrée d'historique** — et c'est le
mécanisme de geste de S2 qui le fait, sans une ligne changée dans le bus.

| Difficulté | Traitement |
|---|---|
| Threads | Un plugin rapporte depuis le thread audio ; le bus refuse tout autre thread que le sien (acquis S3). Le mouvement passe par un anneau à indices atomiques — aucun verrou, aucune allocation dans `process()` — et devient commande sur le message thread. |
| Écho | Trois gardes : le pont se tait pendant que le projector écrit ; une valeur que le projet possède déjà ne produit aucune commande ; le projector n'écrit que ce qui diffère. |
| Identité | Le paramètre est nommé par l'identifiant du format, jamais par un index. |

Le domaine voit donc la trajectoire **échantillonnée** du bouton. La valeur de
fin de geste, celle où l'undo revient, est exacte. C'était la contrepartie
acceptée lundi.

## 4. Les deux pièges trouvés en mesurant, pas en relisant

**Premier piège — le paramètre visé n'était pas celui du plugin.** Un
`ExternalPlugin` de Tracktion expose ses propres paramètres **après** les siens :

```
automatable 0  id="dry level"  name="Dry Level"
automatable 1  id="wet level"  name="Wet Level"
automatable 2  id="0"          name="Gain"      <- celui du plugin
```

Adresser le premier paramètre par son index, ou par son nom via
`getAutomatableParameterByID`, viserait celui de Tracktion — et un plugin dont un
paramètre s'appellerait `dry level` collisionnerait. `HostedParameters.h` ne
retient que les paramètres du plugin, trouvés par l'instance juce qui seule
connaît leurs identifiants réels.

**Deuxième piège — la valeur n'arrivait pas au rendu.** Symptôme : après une
commande de paramètre, le RMS rendu ne bougeait pas d'un chiffre
(`0.212401` avant, `0.212401` après, gain 0 comme gain 1). La mesure a montré que
le paramètre Tracktion valait bien `0` et que le paramètre juce valait encore
`0.5` : Tracktion n'écrit la valeur dans le `ValueTree` que **lorsqu'il notifie**,
et la projection notifiait `dontSendNotification` pour éviter l'écho. Le rendu
reconstruit depuis l'arbre utilisait donc l'ancienne valeur. La projection notifie
maintenant, et c'est le drapeau `projecting_` qui empêche l'écho — le rôle qu'il
avait dès la conception.

Les deux bogues ont été trouvés parce que les tests mesurent un signal. Un test
qui aurait vérifié « le paramètre du domaine vaut 0 » serait passé dans les deux
cas. **Leçon de la S3, appliquée et rentable dès la semaine suivante.**

## 5. CLAP : ce qui est prouvé, et ce qui ne l'est pas encore

Ni le JUCE épinglé ni le Tracktion épinglé ne contiennent une ligne sur CLAP —
vérifié, pas supposé. L'hôte est donc écrit ici, et posé au seul endroit qui paie
tout : un `juce::AudioPluginFormat` ajouté au `pluginFormatManager`. À partir de
là, `ExternalPlugin` héberge un CLAP sans modification, et scan, instanciation,
projection, pont et commandes n'ont **aucune branche sur le format**.

Porté : audio, notes MIDI, paramètres avec gestes, `clap.state`, `clap.gui`
embarquée (win32, x11). Pas porté, et écrit dans l'en-tête : note expressions,
modulation polyphonique, remote controls, surround, timer et fd, découverte de
presets.

**Preuve obtenue.** Aucun plugin CLAP n'est installé sur la machine Windows
(rien sous `Common Files\CLAP`, aucun `.clap` sur les disques). Le dépôt compile
donc son propre plugin CLAP : un sinus, un paramètre de gain, un état binaire qui
porte ce gain, et une annonce de geste autour de son propre changement de
paramètre. Un hôte ne se teste pas contre un mock, et un plugin commercial ne se
commite pas — il n'est pas installé partout, et beaucoup restent muets tant que
leur contenu n'est pas chargé, ce qui rendrait un test rouge muet lui aussi.

Ce que les tests établissent contre ce binaire réel, chargé par le vrai point
d'entrée CLAP :

- le scan le reconnaît, et son identité est son identifiant CLAP, pas un chemin ;
- trois notes envoyées par le bus donnent un signal ;
- une commande de paramètre change le son, dans les deux sens ;
- un état capturé, remis sur une **nouvelle** instance sans aucun paramètre,
  ramène le son qu'il avait — c'est l'ordre « blob puis paramètres » mesuré en
  samples ;
- un bouton tourné dans le plugin fait une seule entrée d'historique, et un undo
  rend le paramètre au plugin.

**Ce qui reste ouvert :** un CLAP **commercial** n'a pas encore été chargé, faute
d'en avoir un sur la machine. Décision prise avec toi : tu installes un CLAP
gratuit (Surge XT ou Vital) et le test se lance avec `DAW_TEST_CLAP`. Le chemin de
code est le même que celui déjà prouvé ; ce qui reste à vérifier, c'est la
tolérance d'un plugin réel à un hôte jeune.

## 6. VST3 : preuve audible

| Plugin | Résultat |
|---|---|
| AL-1 (Naturi Audio) | **Son mesuré et entendu.** RMS non nul sur un rendu hors ligne, trois notes envoyées par le bus |
| Kontakt 8, Omnisphere | Chargés sans erreur, **muets** — attendu : aucun instrument ni patch chargé dans leur propre contenu |

Le log de l'application, sur la machine Windows :

```
demo: hosting AL-1 (VST3)
demo: plugin window open, 1232 by 436
demo: transport running, 3 notes on track 01M2X7DQGPSMKJ5H7HDFGDAH3E
```

Kontakt muet n'est pas un échec de l'hôte : c'est un sampleur sans échantillon.
La distinction est faite ici plutôt que masquée par un seuil de test complaisant.

## 7. Le scan, et la stabilité non négociable

- **Processus enfant** : `Main.cpp` commence par demander si ce processus a été
  lancé pour scanner. Un scanner qui construirait un `Engine`, un `Edit` et une
  fenêtre serait un second DAW se battant pour le périphérique audio.
- **Liste noire** : le fichier témoin nomme le plugin en cours d'ouverture. Celui
  qui emporte le scanner part en liste noire au démarrage suivant, au lieu d'être
  retenté sans fin. Un test écrit un témoin à la main et vérifie la mise en liste
  noire.
- **Liste persistée** : un démarrage ne coûte aucun scan ; seuls les fichiers dont
  la date de modification a bougé sont rescannés.

## 8. Dette

| Dette | État |
|---|---|
| Lancer `scripts/setup-ubuntu.sh` en réel (hors `--ci`) | **Non payée.** La machine Ubuntu n'a pas été démarrée cette semaine. Hérité de S1, reporté à S5. |
| Vérifier l'hébergement de plugins sous Linux | **Non payée.** Le code compile sous Linux en CI ; les tests d'engine y sont exclus par label, et aucun plugin n'est installé sur le runner. À faire sur la machine Ubuntu. |
| Un CLAP commercial chargé | À faire dès que le plugin est installé (§5). |
| `plugin.move` (réordonner la chaîne) | Hors périmètre S4, validé comme tel. |

## 9. La question non tranchée

`journal()` n'est pas un log d'audit. Elle reste ouverte, comme convenu : elle
décide de ce que SQLite stocke, et se tranche en S5 au moment du versioning. Rien
de cette semaine ne la préempte — le magasin de blobs est déjà la table
adressée par contenu que S5 utilisera, quelle que soit la réponse.

## 10. Où en est le jalon S8

Ce qui restait d'inconnu technique majeur avant le jalon était l'hébergement de
plugins tiers. Il ne l'est plus : un VST3 tiers réel joue, un vrai binaire CLAP
joue, l'état survit à une capture et à une restauration, et un bouton du plugin
entre dans l'historique.

Restent, pour le 9 novembre : le versioning SQLite (S5), l'UI, les services
Python et l'IA — du travail, mais du travail dont la forme est connue.
