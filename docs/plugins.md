# Héberger les plugins de l'utilisateur (S4)

Ce document dit ce que le projet sait faire d'un plugin VST3 ou CLAP, et
pourquoi les choix sont ceux-là. Il complète `docs/command-bus.md` ; il ne le
répète pas.

## 1. Deux natures d'état, deux mécanismes

Un plugin met dans le projet deux choses qui n'ont ni la même taille ni le même
rythme. Les confondre coûterait soit l'undo, soit le journal.

| | `PluginParam` | `StateBlobRef` |
|---|---|---|
| quoi | une valeur normalisée par paramètre touché | le chunk binaire opaque du plugin |
| taille | quelques octets | jusqu'à des dizaines de mégaoctets |
| rythme | continu, pendant un geste | ponctuel : sauvegarde, fin de session |
| dans `ProjectState` | par valeur | par digest seulement |
| dans le journal | une commande par valeur, fusionnée par geste | `{digest, byteCount}`, ~80 octets |

`params` est **épars** : seuls les paramètres touchés au moins une fois y
figurent. Un Omnisphere expose des milliers de paramètres ; les mémoriser tous
mettrait des mégaoctets de JSON dans chaque `toValue()`, donc dans chaque
comparaison et dans chaque hash de la couche de versioning.

Un paramètre absent veut dire « celui du blob, sinon le défaut du plugin ».
L'ordre de réhydratation est fixe et appliqué à un seul endroit
(`ProjectProjector::reconcilePlugins`) : **le blob d'abord, les paramètres
par-dessus**.

## 2. Le magasin adressé par contenu

Les octets ne voyagent jamais dans un payload. Ils vont dans
`PluginStateStore`, sous le dossier utilisateur, nommés par leur digest
BLAKE3-256. Trois propriétés, et le modèle dépend des trois :

- **déduplication** : deux instances au même état ne sont stockées qu'une fois ;
- **immuabilité** : un digest ne désigne jamais d'autres octets, donc annuler une
  capture reste possible — les octets précédents sont toujours là ;
- **vérification** : une lecture recalcule le digest et refuse des octets abîmés
  au lieu de les donner à un plugin.

Rien n'est jamais supprimé. Récupérer les blobs non référencés demande de
connaître tout l'historique : c'est la couche de versioning de S5.

## 3. Ce que le domaine sait faire

Cinq commandes, toutes par le bus, aucune n'engendre d'identifiant dans
`apply()` :

| type | payload | undoRecord |
|---|---|---|
| `plugin.insert` | `trackId, plugin{id, ref, bypassed, params, state}, index` | `{pluginId}` |
| `plugin.remove` | `pluginId` | l'instance entière + son index |
| `plugin.set_bypassed` | `pluginId, bypassed` | la valeur précédente |
| `plugin.set_parameter` | `pluginId, paramId, value` | `{existed, value}` |
| `plugin.capture_state` | `pluginId, state{digest, byteCount}` | l'état précédent |

`plugin.set_parameter` est la seule à fusionner, et seulement avec un mouvement
du **même** paramètre du **même** plugin : deux boutons bougés dans un même
geste restent deux entrées d'historique, sinon annuler l'un déplacerait l'autre.

`undoRecord` de `set_parameter` porte `existed` : annuler la toute première
touche **supprime** l'entrée au lieu d'y écrire un défaut. Le paramètre est rendu
au blob, pas figé.

`plugin.move` (réordonner la chaîne) est hors S4.

## 4. Le scan

La liste des plugins installés est un état machine : jamais journalisée, jamais
annulée, hors de `ProjectState`. Elle vit dans un fichier que le projet possède
(`plugins.xml` sous le dossier utilisateur), donc lisible et supprimable à la
main.

- **Persistée** : un démarrage ne coûte aucun scan. Seuls les fichiers dont la
  date de modification a bougé sont rescannés.
- **Processus enfant** : `setUsesSeparateProcessForScanning(true)`, et
  `Main.cpp` commence par demander si ce processus a été lancé pour scanner.
- **Liste noire** : le fichier témoin nomme le plugin en cours d'ouverture. Celui
  qui emporte le scanner part en liste noire au démarrage suivant, au lieu
  d'être retenté sans fin.

Identité d'un plugin, telle que le projet la stocke : le format et un
identifiant stable, **jamais un chemin**. Un projet doit survivre à un plugin
déplacé sur le disque et à un changement de machine.

- VST3 : la chaîne d'identité de Tracktion (format, nom, fabricant, uid).
- CLAP : l'identifiant CLAP du plugin, par exemple `com.u-he.diva`.

## 5. L'hôte CLAP

Ni le JUCE épinglé ni le Tracktion épinglé ne contiennent une ligne sur CLAP. Ce
qui existe, c'est le SDK CLAP en en-têtes. L'adaptateur est donc posé au seul
endroit qui paie tout : un `juce::AudioPluginFormat` ajouté au
`pluginFormatManager` de Tracktion. À partir de là, `ExternalPlugin` héberge un
CLAP sans modification, et scan, instanciation, projection, pont de paramètres
et commandes n'ont aucune branche sur le format.

Porté : ports audio, notes MIDI (dialecte MIDI quand le plugin le prend, sinon
événements de notes CLAP), paramètres avec gestes, `clap.state`, `clap.gui`
embarquée en win32 et x11.

**Pas** porté, et écrit dans l'en-tête : note expressions, modulation
polyphonique, remote controls, layouts surround, extensions timer et fd,
découverte de presets. Chacune serait une promesse que le projet ne peut pas
encore tenir.

Thread audio : aucun verrou et aucune allocation dans `process()`. Les mouvements
de paramètres traversent la frontière par des anneaux à indices atomiques, dans
les deux sens.

Le *thread-check* de CLAP ne dit pas quel thread de l'OS tourne : il dit **quel
rôle a l'appel en cours**. `process()`, `start_processing()`, `stop_processing()`
et `reset()` appartiennent tous au rôle audio, et c'est l'hôte — seul à savoir
qu'aucun rappel audio ne tourne en parallèle — qui le déclare, par un
`ScopedAudioThreadRole` posé sur la portée exacte. Vital refuse ces appels
ailleurs, et il a raison de le faire.

## 6. Le pont de paramètres

Sans lui, la moitié du projet échappe à l'undo : l'utilisateur tourne un bouton
dans la fenêtre du plugin et `ProjectState` n'en sait rien. Avec lui, un
balayage de bouton fait une seule entrée d'historique — et c'est le mécanisme de
geste de S2 qui le fait, sans une ligne changée dans le bus.

Trois difficultés, traitées explicitement :

1. **Threads.** Un plugin rapporte depuis le thread audio ; le bus refuse tout
   thread autre que le sien. Le mouvement passe par un anneau sans verrou et
   devient commande plus tard, sur le message thread. Le domaine voit donc la
   trajectoire **échantillonnée** du bouton. La valeur de fin de geste, celle où
   l'undo revient, est exacte.
2. **Écho.** Projeter une valeur fait rapporter cette valeur par le plugin.
   Trois gardes : le pont se tait pendant que le projector écrit ; une valeur que
   le projet possède déjà ne produit aucune commande ; et le projector n'écrit
   que ce qui diffère.
3. **Identité.** Un `ExternalPlugin` expose ses propres paramètres **après** ceux
   de Tracktion (`dry level`, `wet level`). Adresser le premier paramètre par son
   index ou par son nom viserait celui de Tracktion. `HostedParameters.h` ne
   retient que les paramètres du plugin, trouvés par l'instance juce qui seule
   connaît leurs identifiants réels.

## 7. Conséquence assumée sur « ProjectState est la seule vérité »

Entre deux `plugin.capture_state`, l'instance vivante porte un état interne que
le domaine ignore : un preset chargé, un échantillon choisi, tout ce qui n'est
pas un paramètre. `ProjectState` est exact **aux points de capture**, pas entre
eux. C'est pour cela que le projector n'injecte un blob que si son digest a
changé : le repousser écraserait ce que l'utilisateur vient de faire dans la
fenêtre du plugin.

## 8. Preuves

Ce qui est mesuré est un signal sorti de l'Edit, jamais un pointeur non nul.

- `core/engine/tests/clap_fixture/` est un **vrai plugin CLAP** compilé par ce
  dépôt : un sinus, un paramètre de gain, un état binaire qui porte ce gain, et
  une annonce de geste autour de son propre changement de paramètre. Un hôte ne
  se teste pas contre un mock, et un plugin commercial ne se commite pas.
- Les tests d'engine (label `audio`, exclus de la CI) rendent l'Edit hors ligne
  et comparent une valeur RMS.
- Un plugin tiers réel se désigne par variable d'environnement :

```bash
DAW_TEST_VST3="C:\Program Files\Common Files\VST3\Naturi Audio\AL-1.vst3" \
  ctest --preset windows-msvc -L audio
```

Variable absente : le test est sauté et le dit. Variable présente et sans son :
le test est rouge. Jamais vert pour la mauvaise raison.

À l'oreille, depuis l'application :

```bash
"DAW IA.exe" --demo --plugin "C:\Program Files\Common Files\VST3\Naturi Audio\AL-1.vst3"
```
