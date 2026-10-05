# Bilan de fin de S22 — DAW IA

**Période :** semaine 22 sur 26. Travail du 4 octobre 2026 ; bilan rédigé le 5 octobre 2026.
**Dépôt :** `samflppp/daw-ia`, branche `main`. 19 commits S22, de `ed85197` à `84ab1ca`, plus ce bilan. La branche
`s21-piste-compagne` est inchangée.
**Volume S22 sur `main` (hors ce bilan) :** 96 fichiers, +6 917 lignes, −231.
**Tests :** suite du domaine, 447 cas (+30) ; persistance, 21 cas ; Python, 63 cas (+12). Moteur (label `audio`,
local) : 99 sur 99, l'identité CLAP comprise (§6).
**CI :** rouge sur `dd23ec2`, le dernier commit du 4 octobre (§9), corrigé par `84ab1ca` ; **verte** à `3dcfc82`
(linux-clang et windows-msvc).
**Vérifications de l'application (Release, sur ta machine, le 4 octobre) :**
- `--verify` complet avec copilote, 674 passées, **0 en échec**, mais avant les stems (`c91ac5e`, §6) ;
- `--verify-stems` (neuve), 27 sur 27 ;
- `--verify-mix`, 74 sur 74 à `5c105f4`, plus quatre étapes du panneau à `dd23ec2` ;
- `--verify-canvas`, 131 sur 131.

**Registry :** 61 types (+1, `direction.set`). **Schéma du projet :** 7 (+1). **Un verbe ajouté**, justifié au §3.

## En bref

- **Un fichier audio se sépare en quatre stems sur leurs pistes** : Alt+clic droit sur un clip, « Séparer en
  stems ». Voix, batterie, basse et le reste arrivent chacun sur sa piste, au même endroit. Un Ctrl+Z retire
  tout. Le calcul est local, annulable et mis en cache (§2).
- **Une référence donne une direction au projet** : tempo, tonalité, sections, équilibre des stems, couleur du
  master. Ce sont des nombres, jamais son audio ni ses notes (§3).
  - **Le mixage la lit** (§4) : le master à l'essai devient plus sombre vers une référence sombre.
  - **La génération la lit** : tonalité et densité.
  - **Le copilote la lit** dans l'état du projet.
  - **Un panneau (F11)** montre ce qui a été compris, et la correction se fait à la main.
- **L'objet périmé des S20 et S21 est trouvé** : c'était ton compilateur en français (§6).
- **Le `--verify` complet est passé à 0 échec pour la première fois depuis la S20**, avant l'arrivée des stems
  (§6).
- **L'arrangement ne lit la direction qu'à travers le copilote** : il n'existe pas de fonction d'arrangement à
  part. C'est dit au §5.

## 1. Le modèle

Les réponses aux quatre questions du plan, telles que les commits et le code les portent. L'échange où tu les
as tranchées n'est pas dans le dépôt.

| Question | Réponse retenue |
|---|---|
| Le modèle de séparation | **HTDemucs** (Meta), local, sur le CPU. Deux qualités : « rapide », `htdemucs` (177 s pour 3 minutes sur ton i5-8365U), et « meilleure », `htdemucs_ft` (688 s). SCNet XL IHF, plus lent, reste à trancher. |
| La licence | Le code est sous MIT, **les poids ne le sont pas** : leur auteur les donne « for research purpose » et a retiré la mention MIT de leur fiche Hugging Face le 31 août 2026. **C'est ton choix** de bâtir le prototype dessus et de chercher une licence. Les poids sont téléchargés au premier usage sous `%LOCALAPPDATA%\DAW IA\models`, jamais livrés, jamais dans le dépôt. |
| Où vit la direction | Dans `ProjectState`, sérialisée, par un seul verbe, `direction.set`. Une référence y est nommée par son empreinte et son nom, jamais par un chemin. |
| Plusieurs références | Une moyenne pondérée pour la pente, la crête, la largeur et l'équilibre. Un tempo ou une tonalité ne se moyenne jamais : deux références en désaccord le laissent vide et le disent en une phrase, jusqu'à ta correction. Les sections sont celles de la référence la plus lourde. |
| Le raccord avec « Référence… » de la S20 | Un seul chemin : « Référence… » ajoute une référence à la direction, et le curseur règle la part de direction (0 à 1). |

## 2. Le séparateur de stems

**Le service.** `daw-services separate --model best|fast|fake` écrit les quatre stems en WAV 24 bits et dit sa
progression en lignes JSON :
- `fake` est fait de filtres de bandes, avec numpy seul : c'est celui de la CI ;
- PyTorch et demucs vivent dans un extra `stems`, que la CI n'installe jamais.

Ce que le modèle laisse va au reste, donc la somme des stems rend la source.

Un bogue de demucs a été trouvé en route : sa ligne de commande rend à chaque stem le continu retiré du mélange,
et la somme le portait trois fois. Il n'est rendu qu'une fois désormais. La somme vers le mélange passe de
15,2 dB à 24,5 dB.

**Dans le projet.** `domain::stems::commandsFor` pose une piste par stem, nommée (« Voix — Chanson.wav »), avec
son rôle de mixage. Il n'ajoute aucun verbe : `track.add`, `track.set_role`, `audio.place` et `audio.remove`,
en un seul groupe d'annulation.

**Dans l'application.**
- Un processus par séparation ; « Annuler » le tue.
- Le cache est rangé par empreinte de la source et signature du modèle. Il est écrit à côté puis déplacé : il ne
  reste jamais un cache à trois stems.
- Les stems entrent dans le magasin hors du fil des messages.
- Le copilote peut le demander : l'outil `stems.separate`.

**Preuve, `--verify-stems` (modèle rapide), 27 sur 27,** sur 20 s de sources connues mélangées :

| Stem | Vers sa source | Vers le mélange |
|---|---|---|
| Voix | 12,6 dB | 0,4 dB |
| Batterie | 19,3 dB | 2,7 dB |
| Basse | 19,2 dB | 2,9 dB |
| Reste | 8,1 dB | 0,2 dB |

- Chaque stem est bien plus proche de sa source que du mélange ; la somme est à 121 dB du mélange.
- La séparation prend 31,7 s. La fenêtre ne s'arrête jamais plus de 218 ms pendant le calcul.
- L'annulation est rendue en 481 ms, sans rien écrire.
- Un Ctrl+Z rend le projet à l'octet.
- Une seconde séparation vient du cache en 0,60 s.

La vérification a trouvé deux défauts avant le correctif : 656 ms de gel à l'import des stems sur le fil des
messages, et un contrôle d'installation qui chargeait PyTorch (plus de 5 s depuis le cache).

## 3. La direction : lire une référence, la garder dans le projet

**Lire.** Pour ajouter une référence :
1. son empreinte est calculée ;
2. elle est séparée par le modèle rapide, en cache ;
3. `domain::direction::read` lit ses stems sur un fil à part.

Ses stems ne servent qu'à mesurer : ils n'entrent pas dans le projet. Ce qui est lu :
- **Le tempo** : flux spectral du stem batterie sous 1,5 kHz, puis autocorrélation. Sous 0,35 de confiance, il
  est absent.
- **La tonalité** : chroma des stems harmoniques, profils de Krumhansl-Kessler. Si elle est trop faible ou trop
  proche de la suivante, aucune n'est devinée et les deux meilleures te sont offertes.
- **Les sections** : mesure par mesure, qui joue et à quelle intensité ; la même lettre pour deux sections qui
  sonnent pareil.
- **Par stem** : intensité, équilibre et part jouée ; et pour le tout, pente, crête et largeur, comme la mesure
  de la S20.

Sur des références construites : 120,09 BPM lu pour 120 et 92,10 pour 92. La mineur et mi majeur sont reconnus.
Deux relatives indiscernables par les notes ne donnent aucune tonalité devinée. Du bruit ne donne ni tempo ni
tonalité.

**Garder.** Le verbe `direction.set` écrit la direction entière : les références, tes corrections (tempo,
tonalité) et la part. Chaque changement du panneau est un Ctrl+Z. Son enregistrement d'annulation garde la
direction d'avant, qu'on ne saurait recalculer sans les fichiers.

Une direction vide n'est pas écrite : un projet sans direction se sérialise comme en S21, à l'octet. Le schéma
passe à 7 pour qu'un build S21 refuse le projet à l'ouverture, au lieu de s'arrêter à mi-relecture.
**Le projet rouvert garde sa direction** : c'est vérifié par le scénario « écrit par un autre processus » de la
persistance.

## 4. Qui lit la direction

- **Le mixage.** Sa cible (pente, crête, largeur) vient de la direction combinée, et sa part de
  `direction.amount`. Preuve à l'effet, dans `--verify-mix` : le même morceau mixé par les règles, sans
  direction puis vers une référence sombre à fond. La brillance du master à l'essai passe de −78,3 à −80,3 dB.
- **La génération.** Une source neuve, « référence », se place entre les notes du projet et le défaut ; ce que
  tu écris passe toujours avant.
  - Tonalité : avec une référence en mi majeur, toutes les notes de huit variantes sont en mi majeur ; sans
    référence, le défaut en donne hors de mi majeur.
  - Densité : une voix jouée un cinquième de la référence donne 104 notes sur huit variantes ; jouée presque
    tout le temps, 179.
- **Le copilote.** Le résumé du projet porte la direction quand il y en a une : tempo, tonalité, candidates,
  contradictions, sections et équilibre, en mots qu'il peut citer. Il porte aussi la direction entière, pour
  qu'il la corrige par `direction.set`.
- **Le panneau Direction (F11)** montre les références, ce qu'elles tranchent, les contradictions, les sections
  et la part. Le tempo se tape, la tonalité se choisit, « auto » rend la valeur aux références.
  `--verify-mix` gagne quatre étapes :
  - le tempo tapé (118 BPM) est dans le projet ;
  - la tonalité choisie (mi majeur) aussi ;
  - deux Ctrl+Z les rendent aux références ;
  - « Sans direction » retire la référence.

## 5. Ce qui n'est pas fait du plan, dit

- **L'arrangement.** Le plan dit que la direction est lue par « la génération, l'arrangement et le mixage ». Il
  n'y a pas de fonction d'arrangement dans le logiciel : arranger, c'est poser des blocs (`pattern.place`), à la
  main ou par le copilote. Le copilote voit les sections de la référence et peut les suivre. **Aucun code
  n'organise un morceau d'après ces sections, et aucune preuve n'existe que le copilote les suit.** Un
  arrangement guidé par les sections est une question de modèle (qui pose quoi, où, par quel verbe) : je ne
  l'écris pas sans te l'exposer. Il va au §11.
- **Le tempo de la direction n'est appliqué à rien.** La génération lit la tonalité et la densité, pas le tempo.
  Le projet garde le sien, et le copilote peut le changer s'il le juge bon. C'est voulu : changer le tempo du
  projet tout seul déplacerait tout ce que tu as posé.
- **Les sections sont en secondes, pas en mesures du projet.** Pour l'instant, rien ne les pose sur la grille.
- **La « couleur » de la génération** (timbre, choix d'instrument) n'est pas lue : seul le mixage lit la
  couleur.

## 6. Ce qui s'est fermé en route

- **L'objet périmé des S20 et S21.** MSVC est en français sur ta machine, et ses lignes `/showIncludes`
  commencent par « Remarque : » avec des espaces insécables. Ces lignes sont écrites dans la page de code de la
  console, que Ninja ne reconnaît pas. Résultat : 35 objets compilés sans aucune dépendance, dont
  `Main.cpp.obj`. Un en-tête changé ne les recompilait pas.
  - **Correctif :** `scripts/build.cmd` compile sous la page de code 65001. La configuration prévient quand le
    préfixe sort de l'ASCII. Recompilé ainsi, `Main.cpp.obj` passe de 0 à 1 001 dépendances.
- **Les tests du moteur et l'identité CLAP.** Chaque processus de test a maintenant son dossier de réglages
  jetable : 99 sur 99.
- **Les jetons d'un mixage** sont lus à un seul endroit (`domain::copilot::Usage`), et testés.
- **Les étapes 96, 150, 171 et 183 à 185 de `--verify`.**
  - 96 était un vrai défaut : un trait de vélocité ré-attrapait une tige déjà réglée.
  - Sur une zone vide, un mot de retouche est désormais dit ignoré.
  - Les autres étapes ont été remises d'accord avec le comportement voulu, chacune expliquée dans `c91ac5e`.
  - **`--verify` complet : 674 passées, 0 en échec.**

## 7. Les écarts à l'acquis, dits

- **Un verbe ajouté au domaine, `direction.set`** (61 types), et le schéma 7. La démonstration l'exige : la
  direction doit se rouvrir avec le projet et s'annuler.
- **Des poids sous licence de recherche** dans le chemin d'une fonction du prototype, par ton choix (§1). Ils ne
  sont pas livrés. L'installeur de la S26 devra les télécharger, ou attendre une licence.
- **PyTorch entre dans les services**, en extra optionnel : jamais dans la CI, installé au premier usage.
- **Un geste neuf, Alt+clic droit sur un clip audio** : le clic droit seul garde son sens de la S17.
- **Deux options neuves de la ligne de commande** : `--verify-stems` (une vérification), et `--stems-model`
  (`fake` implicite pour toute vérification, sauf `--verify-stems`).
- **Un commit poussé avec la CI rouge** (`dd23ec2`), rattrapé par `84ab1ca` (§9).

## 8. La règle du test cassé une fois

Chaque test neuf passé du premier coup a été cassé une fois, et tous sont passés au rouge :
- le reste faux, les stems échangés et le résidu non rendu (pytest) ;
- la source non retirée, l'ordre non imposé (la pose) ;
- « Annuler » qui ne tue plus le processus (`--verify-stems`, 5 échecs) ;
- le routage de l'outil `stems.separate` coupé ;
- le flux du tempo sur tout le spectre (92 lu 184) ;
- l'annulation qui ne rend plus la direction ;
- la cible du mixage qui ignore la direction (3 échecs, dont la brillance inchangée) ;
- la tonalité de la direction ignorée par la génération ;
- la direction hors du résumé du copilote ;
- le tempo tapé sans écriture (étapes 25 et 26 de `--verify-mix`) ;
- le dossier de réglages du moteur ramené à l'ancien ;
- la clé des jetons ramenée à `input_tokens` ;
- la garde du mot ignoré retirée.

Le correctif `84ab1ca` ne change qu'un nombre attendu dans un test existant : ce n'est pas un test neuf.

## 9. Ce qui s'est mal passé, dit

- **La CI est restée rouge sur le dernier commit du 4 octobre.** Le panneau Direction ajoute une 11e fenêtre au
  beatmaker, et `WorkspaceLayoutTests` en comptait 10. La session s'est arrêtée avant de le voir et avant ce
  bilan. Le test est corrigé le 5 octobre.
- **Ce correctif n'a pas pu être compilé avant d'être poussé.** Il a été fait depuis un environnement Linux en
  ligne dont le proxy refuse le téléchargement de doctest. `./scripts/check-all.sh` y est vert ; la CI compile
  et lance les tests.
- **Deux commits poussés avec la CI rouge plus tôt dans la journée** (`c259ea2`, `e99ece0`) :
  `-Wimplicit-int-float-conversion` sous clang, que MSVC ne signale pas. Ils ont été rattrapés par `6b4ad9b`.
- **Le `--verify` complet n'a pas été relancé après les stems et la direction.** Les 674 sur 674 datent de
  `c91ac5e`. `--verify-stems` et `--verify-mix` couvrent ce qui a changé, pas le reste du parcours.

## 10. À essayer, dans l'ordre

Sur `main`, Release recompilé par `scripts/build.cmd`. Ta clé dans `DAW_IA_ANTHROPIC_API_KEY`.

1. **Séparer une vraie chanson.** Pose un morceau sur la playlist, Alt+clic droit, « Séparer en stems
   (rapide) ». La première fois, l'installation et le téléchargement des poids prennent du temps. Compte environ
   la durée du morceau pour le calcul. Écoute chaque stem seul : dis-moi où ça bave (la voix dans le reste, la
   basse dans la batterie).
2. **« Annuler » pendant la séparation** : rien ne doit rester, ni piste ni fichier.
3. **La meilleure qualité** sur le même morceau (environ quatre fois plus long) : l'écart s'entend-il assez pour
   la proposer ?
4. **Une référence.** F11, ajoute un morceau que tu connais : le tempo et la tonalité lus sont-ils justes ? Les
   sections tombent-elles aux bons endroits ?
5. **Deux références qui ne s'accordent pas** sur le tempo : la contradiction doit être dite, le tempo laissé
   vide jusqu'à ce que tu le tapes.
6. **Le mixage vers la référence** : F10, « Mixer », part de direction à fond puis à zéro. Écoute l'avant/après
   à niveau égal.
7. **Générer avec une référence** : un pattern vide, Ctrl+G. Les notes doivent être dans la tonalité de la
   référence, et plus denses si sa voix joue tout le temps.
8. **Demander au copilote** de « suivre la structure de la référence » : dis-moi ce qu'il fait des sections.
   C'est la question du §5.

## 11. Reste à faire

- **L'arrangement guidé par les sections de la référence** (§5) : un modèle à t'exposer avant toute ligne, pour
  une semaine que tu choisiras, ou la phase d'essais.
- **Les sections en mesures du projet**, pour qu'elles se lisent sur la grille.
- **SCNet XL IHF** contre HTDemucs-ft : à trancher, licence comprise.
- **La licence des poids HTDemucs**, avant l'installeur de la S26.
- **Un `--verify` complet** sur `main` après les stems et la direction.
- **La piste compagne** (S21), toujours sur sa branche.
