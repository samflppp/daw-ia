# Bilan de fin de S12 — DAW IA

**Période :** semaine 12 sur 26. Rédigé le 26 septembre 2026.
**Dépôt :** `samflppp/daw-ia`, branche `main`, 11 commits S12 (`7b4e3fb` → `491c7f0`), CI verte sur
chacun.
**Volume :** 49 fichiers, +3 530 lignes, −826.
**Tests :** 271 cas ctest (domaine, persistance, interface), 71 cas d'engine (rendus audio), 19 cas Python.
**Vérifications par l'application :** 268 sur les cinq passages, aucune en échec au dernier passage (§7).
**Registry :** 48 types de commandes, contre 47 : `project.set_time_signature`, dans la table du copilote.

## En bref

Six des sept points de la liste sont livrés, dans l'ordre validé : tempo et signature, navigateur,
vélocité, rack simplifié. **L'automation ne l'est pas** : son modèle devait être exposé avant de coder, et la
liste S13 (workflow, navigation, export, IA) est passée devant. Elle reste le prochain modèle à exposer.

## 1. Livrables

| # | Livrable | État |
|---|---|---|
| 1 | Tempo : molette ±1, clic pour taper ou automatiser | Livré |
| 2 | Signature : molette, saisie « 6/8 » | Livré, non projetée dans Tracktion (§2) |
| 3 | Clic droit sur un curseur : automation dessinée dans la playlist | **Reporté** (modèle à exposer) |
| 4 | Vélocité au piano-roll, comme l'éditeur d'événements de FL | Livré, prouvé au rendu |
| 5 | Rack simplifié : ajouter ou retirer samples et instruments, plus de notes | Livré |
| 6 | Navigateur : un clic ouvre un dossier | Livré |
| 7 | Recherche « kick house » qui trouve « kick club » | Livré, par les noms seulement |

## 2. Tempo et signature

- **Le tempo du projet** est le point d'origine de la ligne de tempo. La molette le change d'un BPM par
  cran ; un tour de molette est une seule entrée d'historique (geste fermé après 500 ms de repos).
- **Clic sur le tempo :** « Taper le tempo » (virgule acceptée) ou « Automatiser le tempo », qui ouvre une
  ligne de tempo sous la règle de la playlist : clic pour poser un point, glisser, clic droit ou
  double-clic pour retirer, molette sur un point.
- **La signature** (`TimeSignature`, numérateur 1–16, dénominateur 1, 2, 4, 8 ou 16) change la grille des
  mesures partout : playlist, piano-roll, afficheur de position. Elle n'est enregistrée que si elle diffère
  de 4/4.

**Limites dites :**
- le tempo change par paliers entre deux points, sans rampe ;
- un glisser de point bouge un seul axe par geste ;
- la signature n'est pas projetée dans Tracktion : Tracktion compterait alors un temps comme le dénominateur,
  et un 6/8 jouerait deux fois plus vite. Le rendu en 6/8 a la même durée qu'en 4/4, un test moteur le
  vérifie.

## 3. Navigateur et recherche

- Un clic ouvre un dossier ; un clic sur un sample le fait entendre (S11).
- La recherche compare les mots : même mot 1, préfixe 0,85, une faute de frappe 0,7, même famille 0,6
  (table écrite à la main : house ≈ club ≈ techno, kick ≈ bd…). Le nom du dossier compte à 0,8.
- L'index est construit hors du thread message.

**Limite :** elle ne trouve que ce que les noms disent. Les familles apprises et la recherche par le son sont
consignées dans `IDEES.md`.

## 4. Vélocité

- Une tige par note sous la grille. Un clic règle une note, un trait règle toutes celles qu'il croise, en une
  entrée d'historique au relâcher.
- Si deux notes ou plus sont prises, seules elles changent.
- **Au rendu**, un crescendo dessiné donne −29,7, −21,4, −14,5 et −8,6 dBFS.

**Limite :** la pente de vélocité est celle de l'instrument ; le synthé intégré de Tracktion en a une forte.

## 5. Rack simplifié

- Le rack liste les canaux et ce qui les joue : le sample, l'instrument, ou le synthé intégré.
- « + Sample », « + Instrument » (instruments VST ou CLAP seulement, pas les effets), glisser un sample,
  renommer, retirer avec ses notes. Chaque action est une entrée d'historique.
- La grille de pas est retirée, avec la résolution, la hauteur du canal au clic droit, la tête de lecture
  du rack et le copier-coller de lignes entières.
- **Le piano-roll devient la seule façon d'écrire :** une piste sans ligne montre une grille vide, et le
  premier clic ouvre la ligne et pose la note en un seul groupe.

**Limite :** « + Sample » ouvre la fenêtre de fichiers de Windows, que la vérification ne pilote pas. Ce
bouton n'est essayé qu'à la main.

**Commit combiné :** le rack et sa vérification sont dans le même commit (`491c7f0`), parce que la
vérification ne compilait pas sans le nouveau rack.

## 6. Pièges de la vérification trouvés cette semaine

- Un `PopupMenu` se ferme tout seul quand l'application n'est pas au premier plan : l'ouvrir et y répondre
  dans la même étape.
- La `Selection` notifie de façon asynchrone : `dispatchPendingMessages()` avant d'agir sur un panneau qui
  doit l'avoir entendue.
- `ListBox::getComponentForRowNumber` rend null pour une ligne peinte : viser par `getComponentAt`.
- Les onsets faibles d'un crescendo : seuil de `listen` descendu à 0,02 pour cette étape.

## 7. Vérifications

| Passage | Vérifications |
|---|---|
| liste | 243 |
| réouverture | 5 |
| ancien projet | 4 |
| fichier | 11 |
| copie | 5 |
| **total** | **268** |

**Échec intermittent :** l'étape 41 (un Ctrl+Z pendant la lecture retire le bloc posé) a échoué une fois
sur environ cinq passages. Non reproduit. Le rapport note désormais la dernière entrée avant le Ctrl+Z, pour
trouver la cause au prochain échec.

## 8. Reste à faire

- L'automation : le modèle (lignes dans la playlist, points et courbes, projection sur les paramètres,
  commandes du copilote, clic droit sur un curseur) à exposer avant de coder.
- La rampe de tempo entre deux points.

## 9. Prochaines étapes

Ordre validé le 26 septembre 2026. L'IA passe au centre du DAW. Chaque modèle est exposé et validé avant d'être
codé.

### S13 : workflow, navigation, export (livrés)

| Point | Commit |
|---|---|
| Le pattern se choisit dans la barre de transport ; le piano-roll a un menu des canaux du rack ; « + Ligne » retiré | `c6f0a86` |
| Molette sur la règle : zoom autour du pointeur ; clic molette : déplacer la vue (piano-roll et playlist) | `fb67015` |
| Fichier > Exporter… : WAV, FLAC, MP3, AAC (MP3 et AAC par Media Foundation, voie A) | `e1a0311` |

310 vérifications. Un nouvel échec intermittent : un clic sur S pendant la lecture est resté sans effet une
fois, et il a entraîné 7 échecs à sa suite. Le passage suivant n'a eu aucun échec. C'est la même famille que
l'étape 41 : la cause est à chercher.

### S14 : la génération locale dans le piano-roll

- **Notes fantômes.** On choisit une plage, on lance la génération (Ctrl+G, ou un champ dans l'en-tête), et
  les notes proposées s'affichent en gris.
  - Tab accepte la proposition : un seul groupe d'historique « copilote : … ».
  - Échap la rejette.
  - Alt+molette passe d'une variante à l'autre.
  - Tant qu'une proposition n'est pas acceptée, elle ne crée aucune entrée d'historique.
- **`harmony` en règles déterministes.** Tonalité, gamme et accords, déduits ou imposés. Il énumère les
  candidats justes : c'est la justesse.
- **Chaîne de Markov d'ordre 3 ou 4** sur le corpus personnel. Elle classe ces candidats : c'est le style.
- Tout est local, sans API.
- **Preuve au rendu :** les attaques sont aux bonnes positions, et une note hors gamme est un échec.

### S15 : le copilote distant dans le workflow

- **Portée.** Le modèle distant reçoit la portée du piano-roll (canal, pattern, plage), pas le projet entier.
- **Trois niveaux de latence :**

  | Niveau | Budget | Ce qui y tourne |
  |---|---|---|
  | T0 | < 16 ms | règles et Markov |
  | T1 | < 300 ms | ONNX local |
  | T2 | secondes | le modèle distant, annulable |

- **Version du projet.** Chaque requête la porte. Si le projet a changé entre-temps, la réponse reste en notes
  fantômes au lieu d'être appliquée.
- **Beatmaking.** La génération couvre plusieurs canaux à la fois (kick, snare, hat) dans le pattern en cours.

### S16 : sound design et recherche

- **Effets par le copilote.** Il règle les paramètres des effets par des commandes. Il manque encore les noms,
  les unités et les plages des paramètres : c'est le besoin `plugin.parameters` de la S11.
- **Recherche de samples.** Elle passe par des embeddings locaux, puis par des descripteurs audio (voir
  `IDEES.md`).

### Plus tard

- **L'automation.** Son modèle est à exposer : lignes dans la playlist, points et courbes, projection sur les
  paramètres, commandes du copilote, clic droit sur un curseur. Elle conditionne aussi `automation.write`
  pour le copilote.
- **La rampe de tempo** entre deux points.
- **Le routeur d'intention Laya**, quand le journal SQLite aura assez d'exemples pour l'affiner.
- **L'export sans bloquer l'interface** : le rendu passe sur un thread, avec une barre de progression.
- **L'écoute d'une proposition** sans l'écrire dans le projet : il faut un chemin moteur séparé.
