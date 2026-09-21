# Bilan de la S7bis — DAW IA

**Période :** passe de correction entre la S7 et la S8. Rédigé le 21 septembre 2026.
**Dépôt :** `samflppp/daw-ia` (privé), branche `main`, 7 commits (`93db836` → `d5c1320`).
**Tests :** 175 cas hors audio, 46 cas d'engine sous label `audio` (42 avant la passe).
**Origine :** vingt minutes d'usage réel du binaire, neuf défauts rapportés, deux captures.

C'est le §11 du bilan S7 qui réclamait cette passe : « aucun des contrôles de
cette semaine n'a été regardé à l'œil ». Neuf défauts en vingt minutes, dont
trois qu'aucun test n'aurait trouvés et deux qui dormaient depuis la S2.

## 1. Un mot sur l'outillage, qui a changé

Le §11 de la S7 disait que l'outillage ne savait pas ouvrir le binaire. C'était
une erreur de nom : l'application se demande sous `DAW IA.exe`, pas sous
« DAW IA ». Tout ce qui suit a donc été reproduit, corrigé et **revérifié à
l'écran** dans le binaire, sur un projet bac à sable et jamais sur le projet de
l'utilisateur. Reste une chose qu'aucun outillage ne remplace, §4.

## 2. Les neuf défauts

| # | Symptôme | Reproduit | Cause racine | Correction | Preuve |
|---|---|---|---|---|---|
| 1 | Réduire puis rouvrir : écran blanc figé | **Non**, voir §3 | non établie | repeint complet au retour, défensif et dit comme tel | à revérifier par toi |
| 2 | Le bouton play ne joue pas à chaque fois | Oui | `TransportController` comparait à une copie périmée ; Tracktion s'arrête seul en fin de matière et ne prévient personne, donc le domaine disait encore « playing » et le play suivant n'était pas un changement | l'intention est appliquée, jamais comparée ; `TransportSync` porte l'arrêt du moteur jusqu'au domaine | 2 tests d'engine + vu à l'écran |
| 3 | Fenêtre de plugin : bande noire, puis fenêtre minuscule et vide | Oui | trois défauts empilés, §5 | GUI CLAP créé une fois par instance, taille de repli posée avant tout, échelle de l'écran au lieu du facteur de zoom JUCE | mesuré, §5 |
| 4 | Latence visible au départ de la lecture | Oui, **mesurée** | le premier `play()` construisait le graphe de lecture et ouvrait le périphérique sur le thread message | contexte de lecture construit au démarrage | 3202,3 ms → 1,2 ms, §6 |
| 5 | Stop ne ramène pas la tête au début | Oui | même cause que 2, plus un choix de la S2 qui ne convient pas à un beatmaker | `transport.stop` ramène au début | 2 tests (domaine + engine) + vu à l'écran |
| 6 | Traînée de lignes blanches derrière le curseur | Oui | le minuteur n'invalidait que la colonne vers laquelle le curseur va, jamais celle qu'il quitte | deux colonnes repeintes, et rien quand la position n'a pas bougé | visuelle |
| 7 | Impossible de déplacer la tête de lecture à la souris | Oui | rien ne l'implémentait | clic et glissé sur la règle émettent `transport.set_position` | visuelle |
| 8 | La lecture continue au-delà de la dernière note | Oui | aucune boucle n'existait | `transport.set_loop`, boucle sur le clip édité en beatmaker, §7 | 2 tests d'engine + vu à l'écran |
| 9 | Les accents ont disparu de l'interface | Oui | deux causes empilées, §8 | `/utf-8` sur tous les binaires, littéraux accentués en `u8""` | visuelle |

## 3. Le bug 1 n'a pas été reproduit, et je le dis plutôt que de deviner

Neuf cycles réduction/restauration, dans quatre configurations :

| Configuration | Cycles | Résultat |
|---|---|---|
| projet vide | 6 | aucun écran blanc, interface vivante après restauration |
| projet chargé, fenêtre de plugin Vital ouverte | 3 | idem |

Après chaque restauration, un clic sur une piste sélectionnait bien la piste :
la fenêtre n'était donc ni blanche ni figée.

La correction posée est **défensive et ne prétend pas soigner une cause
diagnostiquée** : au retour de la barre des tâches, la fenêtre et son contenu
sont repeints. C'est la seule chose qu'un hôte peut faire pour une fenêtre
revenue sans qu'on lui ait demandé de peindre.

Si le défaut survit, le suspect suivant est nommé : **JUCE 8.0.13 utilise
Direct2D par défaut sur Windows** (`Component::createNewPeer` choisit le moteur 1,
GDI étant le 0). Un écran blanc après restauration est le symptôme classique
d'une chaîne d'échange Direct2D recréée et jamais repeinte. Le test suivant
serait de forcer GDI sur la fenêtre principale — un changement à faire
sciemment, parce qu'il touche tout le rendu.

**Ce qu'il me faudrait de toi :** la manœuvre exacte. Bouton de réduction de la
fenêtre, ou clic sur la barre des tâches, ou `Win+D` ? Fenêtre de plugin
ouverte ? Lecture en cours ? Et si possible, la ligne du journal
`%APPDATA%\DAW IA\daw.log` au moment où l'écran blanchit.

## 4. Ce que l'outillage ne remplace toujours pas

Je peux ouvrir le binaire, cliquer, mesurer des tailles de fenêtre et lire des
pixels. Je ne peux pas **entendre**. Tout ce qui suit est donc vérifié comme
image et jamais comme son : que la boucle reboucle, que le pan s'entend à
gauche, que le plugin sonne. Les mesures RMS de la S7 restent la seule preuve
auditive du projet, et elles sont hors ligne.

## 5. Le bug 3, en trois défauts

| Défaut | Ce qu'il donnait |
|---|---|
| Le GUI CLAP était **détruit** à la fermeture de la fenêtre et **recréé** à l'ouverture suivante | la spécification l'autorise, mais un plugin a le droit de revenir d'un second `create()` dans un autre état ; il est maintenant créé une fois par instance, caché et remontré ensuite, et détruit avec l'instance |
| Un `create()` en échec laissait le composant à **zéro par zéro**, la taille n'étant lue qu'après | la fenêtre s'ouvrait minuscule et noire, sans un mot dans le journal ; une taille de repli est posée avant tout, et l'échec est dit |
| `set_scale` recevait `juce::Desktop::getGlobalScaleFactor()` | c'est un réglage de zoom de JUCE, il vaut 1 ici, l'écran est à 1,5 ; le plugin annonçait donc une taille dans les pixels d'une échelle et dessinait dans ceux d'une autre. **C'est la bande noire.** |

Mesure, Vital CLAP, écran 1280×720 à 150 % :

| | Fenêtre | Dessiné par le plugin |
|---|---|---|
| avant | 1489×901 à (−105, −102) | 997×613 — d'où la bande |
| après | 997×613 à (142, 42) | bord à bord |

À (−105, −102) la barre de titre était **au-dessus du bord haut de l'écran** :
le bouton de fermeture était inatteignable. La fenêtre est maintenant placée
dans la zone utile, hauteur du cadre natif comprise, et un plugin qui refuse
d'être redimensionné garde sa taille au lieu d'être rétréci dans un cadre qu'il
ne remplit pas.

Trouvé au passage, et corrigé : un plugin que le projet nomme et que l'Edit n'a
pas ne disait rien du tout — l'emplacement s'affichait et le clic ne faisait
rien. Il est maintenant écrit dans le journal.

## 6. La latence, mesurée

Périphérique Realtek en WASAPI, bloc 441 à 44 100 Hz, soit 10 ms de tampon.

| | Thread message tenu par `play()` | Commande → départ du curseur |
|---|---|---|
| avant, premier appui | **3202,3 ms** | ~95 ms en plus |
| avant, appuis suivants | 0,3 ms | ~95 ms |
| après, premier appui | **1,2 ms** | ~95 ms |

Les 3,2 secondes du premier appui étaient la construction du graphe de lecture
et l'ouverture du périphérique, faites sur le thread message : trois secondes
d'interface gelée, lues comme « le bouton ne marche pas » autant que comme une
latence. Le contexte de lecture est maintenant construit au démarrage —
**2584,7 ms de plus à l'ouverture, une fois**, là où l'utilisateur regarde une
fenêtre s'ouvrir et non un beat s'arrêter.

Restent **environ 95 ms** entre la commande et le premier mouvement du curseur,
dont au plus 33 ms de rafraîchissement d'affichage. Le reste est le démarrage du
moteur lui-même. C'est à la limite du visible ; si tu le vois encore, c'est le
prochain sujet, et il faudra descendre dans `TransportControl::play`.

Les deux mesures sont instrumentées dans le code qui reste : le journal écrit
`transport: play() held the message thread for N ms` et `transport: playing
after N ms` à chaque lecture.

## 7. La fin de lecture : ce qui a été retenu

| Workspace | Comportement | Pourquoi |
|---|---|---|
| **beatmaker** | la lecture **boucle sur le clip affiché dans le piano-roll** | c'est le mode pattern de FL Studio, et c'est ce que le panneau sait déjà : le clip à l'écran est le motif sur lequel on travaille. Rien de nouveau à décider, rien de nouveau à afficher |
| **les autres** | pas de boucle : la lecture va au bout de la matière et s'arrête | aucun panneau n'émet la commande. C'est ce qu'attend un montage, et ce que voudra un arrangement |

La boucle est `transport.set_loop`, **transitoire** comme le reste du transport,
et elle vit dans `TransportState` et non dans le projet — pour la raison qui
vaut déjà pour la tête de lecture : elle n'est ni journalisée, ni annulable, et
deux fenêtres sur le même projet en auraient deux. Un copilote la lit et la pose
quand même, puisqu'elle passe par une commande comme tout le reste.

**Ce que je n'ai pas fait, et pourquoi :** pas de contrôle de boucle dans
l'interface (bornes déplaçables sur la règle, bouton de boucle dans le
transport). La boucle suit le clip, ce qui couvre le cas beatmaker sans rien
ajouter à l'écran. Le jour où l'on veut boucler quatre mesures au milieu d'un
clip de seize, il faudra les deux.

Une boucle vide ou à l'envers est refusée : une boucle de longueur nulle est un
transport qui n'avance jamais.

## 8. Les accents : deux causes, aucune n'était la police

Inter et JetBrains Mono contiennent les accents. Le problème était ailleurs,
deux fois :

1. **`/utf-8` n'était appliqué qu'à `daw_ui_workspace`**, par `daw_set_warnings`.
   `daw_app` et le binaire de tests d'engine compilent des sources JUCE et
   Tracktion, donc ne peuvent pas prendre nos avertissements — et se
   retrouvaient sans `/utf-8`. `daw_use_utf8_sources` existe maintenant pour ce
   seul drapeau.

2. **`juce::String(const char*)` décode de l'ASCII, pas de l'UTF-8.** C'est écrit
   dans un `jassert` de JUCE : un octet au-delà de 127 ne peut pas être converti,
   faute de savoir quel encodage l'a produit. Un littéral accentué arrivait donc
   double-décodé — « CHAÎNE » devenait « CHAÃŽNE ». Les littéraux accentués sont
   préfixés `u8`, ce qui appelle la surcharge `char8_t`, qui décode bien de
   l'UTF-8. Même piège pour `juce::String(pointeur, taille)` dans le panneau
   d'historique, remplacé par `juce::String::fromUTF8`.

Les libellés de geste restent en `char` nu : ils vont à un `std::string_view` du
domaine, qui porte des octets UTF-8 et ne les décode jamais lui-même.

Ce n'était donc **pas un choix délibéré** : c'était un contournement, fait une
fois et jamais dit.

## 9. Le transport, et un choix de la S2 qui change

Trois bugs (2, 4, 5) avaient une seule cause. `TransportController` gardait une
copie de ce qu'il avait poussé et sautait tout appel qui ne changeait rien. La
copie ment dans un sens que personne n'avait regardé : **Tracktion s'arrête seul
en fin de matière**. Le domaine continuait de dire « playing » au-dessus d'un
moteur muet, donc le `transport.play` suivant n'était pas un changement et
n'atteignait jamais le moteur. Même histoire pour la tête de lecture :
`transport.set_position(0)` sur un domaine qui tenait déjà 0 n'atteignait pas le
moteur, donc le bouton de retour au début était mort.

Plus de copie. Chaque verbe est appelé quand sa propre commande arrive, et il
atteint toujours le moteur. Une projection de projet ne touche plus au
transport : une image de fader ne peut pas redémarrer la lecture parce que rien
n'est appelé sur une image de fader.

**`transport.stop` ramène maintenant la tête de lecture au début.** Cela
contredit un commentaire écrit en S2 — « stopping keeps the position, the way
every DAW behaves ». Ce commentaire avait tort pour un beatmaker : stop est la
façon de revenir en haut du pattern, et le bouton de retour au début existe pour
l'autre cas, revenir au début sans arrêter. Je le signale au lieu de l'effacer.

`TransportSync` porte l'arrêt du moteur jusqu'au domaine, par une commande
transitoire. C'est un `Timer` et non un observateur du bus parce qu'un
observateur n'a pas le droit de rappeler le bus — la règle de la S2 tient.

## 10. Le channel rack : chiffrage, pas construction

Demandé chiffré, pas construit. Voici l'estimation honnête.

### 10.1 Ce que c'est, et ce qu'il n'est pas

Un channel rack FL Studio est **deux choses dans un seul panneau**, et les
confondre est la première façon de se tromper sur le coût :

1. une **liste d'instruments**, un par ligne, chacun jouant une hauteur fixe ;
2. une **grille de pas** — seize cases par ligne, allumées ou éteintes.

Le projet a déjà la première : une piste avec un plugin *est* un canal. Ce qui
manque est la seconde, et la question n'est pas « comment dessiner une grille »
mais **ce qu'une case allumée est dans le modèle**.

### 10.2 La décision qui commande tout le reste

| Option | Ce que c'est | Coût | Ce que ça casse |
|---|---|---|---|
| **A. Une case est une note** | la grille est une vue du clip : case allumée = `note.add` à ce beat, éteinte = `note.remove` | **faible** | rien. Le piano-roll et la grille montrent le même clip, par deux fenêtres |
| B. Un motif de pas dans l'état | `StepPattern { trackId, steps[] }` à côté des clips | moyen | deux vérités pour « ce qui sonne » : un clip et un motif. Il faudrait décider qui gagne |
| C. Des pas qui se compilent en notes | l'état porte les pas, la projection fabrique les notes | élevé | l'édition au piano-roll ne peut plus remonter vers les pas |

**Recommandation : A, sans hésiter.** Une case allumée est une note de longueur
un pas, à la hauteur du canal. Le channel rack devient une **deuxième vue du
même clip**, comme le piano-roll — et tout ce que la S7 a construit sert
directement : `note.add`, `note.remove`, `note.set_velocity`, `note.quantize`,
le sélecteur de clip, la boucle sur le clip.

L'option B est ce que fait FL, et c'est pour cela qu'un pas et une note y sont
deux mondes qui communiquent mal. Ne pas reproduire ça.

### 10.3 Les commandes nécessaires

Avec l'option A, **aucune commande nouvelle n'est strictement nécessaire.** La
grille est un appelant de plus des verbes existants.

Deux seraient confortables, et aucune n'est bloquante :

| Commande | Pourquoi | Peut-on s'en passer |
|---|---|---|
| `note.set_many` — pose et efface un lot de notes en une entrée d'historique | peindre une ligne de charleston est un geste, donc une entrée | oui : un geste ouvert autour de N `note.add` fait déjà exactement cela |
| `track.set_channel_pitch` — la hauteur fixe d'un canal | une piste de kick joue toujours C1 | oui : la hauteur peut vivre dans l'interface tant qu'un seul écran la lit — mais alors un copilote ne la connaît pas, ce qui est exactement le genre de raccourci que la S7 a refusé partout |

Ma lecture : **`track.set_channel_pitch` est le seul ajout vraiment justifié au
domaine**, parce qu'il est ce qui rend la grille compréhensible à autre chose
qu'elle-même.

### 10.4 L'articulation avec l'existant

| Pièce existante | Ce qu'elle devient |
|---|---|
| clip | inchangé. La grille édite le clip sélectionné, celui que le sélecteur de la S7 nomme déjà |
| piano-roll | inchangé. Deux vues du même clip, cohérentes parce qu'elles lisent le même état |
| boucle sur le clip | sert telle quelle : le pattern qui boucle est le clip que la grille montre |
| liste de pistes | devient la colonne de gauche du rack, ou reste à côté — décision d'écran, pas de modèle |
| `note.quantize` | sans objet dans la grille : un pas est déjà sur la grille. Reste utile au piano-roll |
| vélocité | la case porte la teinte, comme la note au piano-roll ; `note.set_velocity` sert telle quelle |

Point de friction à connaître : un clip de seize pas en croches fait **8 temps**,
un clip de seize pas en doubles fait **4 temps**. La résolution de la grille est
donc un réglage d'écran qui décide combien de pas tiennent dans le clip, et il
faut choisir entre « la grille suit la longueur du clip » et « la grille fixe la
longueur du clip ». Je recommande la première : le clip reste la vérité.

### 10.5 L'estimation

| Lot | Contenu | Estimation |
|---|---|---|
| 1 | Panneau `channel_rack` : grille, un canal par piste, lecture de l'état, dessin | **2 jours** |
| 2 | Édition : clic pose et efface une note, glissé peint une ligne dans un geste | **1 jour** |
| 3 | Hauteur de canal : `track.set_channel_pitch`, champ, commande, projection, tests | **1 jour** |
| 4 | Vélocité à la case, résolution de grille, colonne de sélection de canal | **1 jour** |
| 5 | Manifeste `beatmaker` refait pour loger le rack, et le regarder | **1 jour** |
| | **Total** | **6 jours, soit une semaine pleine** |

C'est une semaine **si et seulement si l'option A est retenue**. L'option B en
coûte deux, et paie une dette de cohérence pour toujours.

Je ne recommande pas de la placer avant la S8 : le copilote a dix verbes à
utiliser et aucun écran ne lui manque pour le démontrer. Le channel rack est ce
qui rend le produit crédible **devant un beatmaker**, et cette démonstration-là
est celle de mars, pas celle du 9 novembre.

## 11. Ce qui n'a pas pu être corrigé

| Quoi | État |
|---|---|
| **Bug 1**, écran blanc après restauration | non reproduit en 9 cycles ; correction défensive posée, cause non établie, §3 |
| Les ~95 ms restantes au départ de la lecture | mesurées, non expliquées ; il faudrait descendre dans `TransportControl::play` |
| La preuve auditive | aucun outillage ici n'entend. Tout est vérifié comme image, §4 |

## 12. Tes vérifications, dans l'ordre

Une session, une dizaine de minutes. Les six premières sont celles qui comptent.

1. **Play, stop, play.** Le curseur doit repartir à chaque fois, et sans attendre.
   Avant, le deuxième play ne faisait rien dès que la lecture avait atteint la
   fin de la matière.
2. **Stop.** La tête de lecture doit revenir à `001.1.01`.
3. **Le bouton de retour au début** (`|◀`), pendant la lecture puis à l'arrêt.
   Il doit ramener la tête dans les deux cas.
4. **Laisse jouer une minute sur le workspace beatmaker.** Le compteur doit
   revenir en arrière à la fin du clip au lieu de continuer. Et **le piano-roll
   doit rester propre** : un seul trait vertical, jamais une traînée.
5. **Clique et glisse sur la règle du piano-roll** (la bande des numéros de
   mesure, au-dessus des notes). La tête de lecture doit suivre la souris.
6. **Regarde les accents** : « CHAÎNE », « Chaîne vide », « Note ajoutée »,
   « Plugin inséré », « Rétablir », « Sélectionnez une piste ».
7. **Ouvre Vital, ferme sa fenêtre, rouvre-la, referme, rouvre encore.** À chaque
   fois : la barre de titre doit être visible, et le plugin doit remplir sa
   fenêtre sans bande noire.
8. **Le bug 1** : réduis la fenêtre et rouvre-la, comme tu l'avais fait. Si le
   blanc revient, note la manœuvre exacte et envoie-moi la fin de
   `%APPDATA%\DAW IA\daw.log` — les questions précises sont au §3.
9. **Le démarrage est plus long d'environ 2,5 secondes**, une fois. C'est voulu :
   c'est la latence du premier play déplacée là où elle ne coupe pas un beat.
   Dis-moi si c'est le mauvais échange.

## 13. Périmètre

Respecté. Aucune décision d'architecture rouverte : tout passe toujours par le
bus, le déplacement de la tête de lecture et la boucle sont des commandes de
transport, transitoires comme les autres, et le domaine reste la seule vérité.

Une seule sémantique a changé, et elle est signalée deux fois plutôt qu'une :
`transport.stop` ramène au début, contre un commentaire de la S2.

Aucune ligne d'IA, aucun service Python, aucun channel rack — chiffré, pas
construit.
