# Plan des six dernières semaines — S21 à S26

Arrêté par le fondateur le 3 octobre 2026, à la fin de la S20. Ce fichier sert de base aux briefs de semaine ;
il ne les remplace pas. Chaque brief reprend la semaine, l'expose avant de coder, et peut la préciser ; un écart
à ce plan est dit dans le bilan de la semaine, comme tout écart à l'acquis.

## Le cadre

Quatre décisions du 3 octobre 2026 (détail dans `docs/contexte-projet.md`, « Méthode de travail ») :

- **Un excellent logiciel, même si ça prend plus de temps et de moyens.** Une chose mieux faite plus tard, avec
  plus de moyens, est reportée plutôt que faite au rabais. **Une semaine est un périmètre, pas une durée** (6 octobre 2026) :
  elle dure ce qu'il faut pour que tout son périmètre soit bien fait. Un chantier ne tombe plus faute de temps ;
  il ne se reporte que s'il sera mieux fait plus tard, et le bilan dit où il va.
- **On construit, puis on essaie par phases.** Les 26 semaines servent à développer. Les essais du fondateur, à la
  main et à l'oreille, viennent après la S26, dans une phase consacrée à l'ergonomie. Aucune semaine n'attend un
  de ses essais. Chaque bilan garde sa section « À essayer, dans l'ordre » et l'ajoute à
  `docs/essais-apres-s26.md`.
- **La S26 livre un premier prototype, pas un produit fini.** Pas de semaine de gel. Une cause non trouvée se dit,
  se note dans les dettes, et n'arrête pas la semaine — sauf si elle empêche de montrer la fonctionnalité.
- **Le fine-tune sort des 26 semaines** : il se fera pendant l'incubation. Le journal continue de constituer le jeu
  de données ; ses lignes ne sont jamais purgées.

Ce qui reste exigé chaque semaine : ce que la machine peut prouver. Rendu mesuré, `--verify`, tests cassés une
fois, `./scripts/check-all.sh` avant chaque commit, CI verte.

## S21 — Solidité, et fin de l'ergonomie

**But.** Fermer les dettes qui empêchent de montrer le logiciel sans crainte, et finir le chantier
d'ergonomie de la S19.

**Périmètre.**
- **La lecture muette** (dette S12, revue en S20 : une boucle jouée sans un son, pistes et master à −100 dBFS,
  moteur en lecture). Première priorité de la semaine.
- **La fermeture qui laissait le processus en vie** (cause non trouvée depuis la S18 ; garde S19 en place).
- **Les 17 étapes de `--verify` qui échouent quand le copilote tourne** (pages et fenêtres ; sans copilote, elles
  passent).
- **Les plugins de l'utilisateur qui ne traitent pas la piste compagne** (ses enregistrements), alors que les
  effets internes passent sur les deux.
- **Un premier vrai mixage par le modèle** : un appel réel, avec `DAW_IA_ANTHROPIC_API_KEY` (réglée sur la machine
  du fondateur), lancé par Claude sur le projet de `--verify-mix`, hors CI. But : prouver que le chemin marche et
  relever le coût réel dans `daw.log`. Ce n'est pas un essai à l'oreille.
- **Le critère kick / basse** : le recouvrement à 6 dB, qui ne peut pas baisser sur un son qui décroît (S20 §4),
  est remplacé par la marge du kick sur ses coups.
- **Le chantier 2 de la S19** : ancrage des fenêtres, défilement du channel rack et de l'historique.

**Hors périmètre.**
- Le retrait du piano-roll (décision A : il attend la phase d'essais après la S26).
- La cible de 8 ms de la toile chargée, le repeint du glissé d'un point d'automation, la mesure du mixage sous
  20 s sur une machine occupée : dettes ouvertes, aucune semaine ne les prend.

**Ce qui se prouve par la machine.**
- La lecture muette : un test ou une étape qui la reproduit avant le correctif (sinon, l'instrumentation qui
  dit où le son s'arrête), puis `--verify` relancé plusieurs fois sans les étapes 58 / 60 en échec.
- La fermeture : `scripts/verify-quit.ps1` — le processus disparaît, sans passer par la garde des 2 s.
- Les 17 étapes : `--verify` complet avec copilote, les mêmes étapes vertes.
- La piste compagne : un rendu mesuré, un plugin réel (label `audio`) qui traite les deux pistes.
- Le mixage par le modèle : `daw.log` contient la proposition, les refus des garde-fous, les jetons lus et
  écrits ; une vérification ne mixe toujours jamais par le modèle.
- Le critère kick / basse : la marge mesurée dans `--verify-mix`, cassée une fois.
- Le chantier 2 : `--verify-canvas` ou une étape neuve par geste (ancrer, défiler).

**Questions de modèle à exposer avant de coder.**
- Le chantier 2, tel qu'exposé en fin de S19 : ce qu'« ancrer » veut dire pour une fenêtre interne (bords,
  autres fenêtres, mémoire de la disposition), et la grammaire S18 appliquée au rack et à l'historique.
- Le chemin de la piste compagne dans la projection : où les plugins de la personne se placent pour traiter les
  deux pistes, sans les dupliquer (un plugin a un état ; deux instances divergent).
- Comment lancer le mixage réel hors CI et hors `--verify` (le copilote, « mixe le morceau », ou une option
  dédiée), puisqu'une vérification ne mixe jamais par le modèle.

## S22 — IA : séparateur de stems et direction par références

**But.** Un fichier audio se sépare en stems sur leurs pistes, et une ou plusieurs références donnent au projet
une direction partagée par la génération, l'arrangement et le mixage.

**Périmètre.**
- **Le séparateur de stems** : un fichier audio déposé sur une piste se sépare en voix, batterie, basse et le
  reste, chacun sur sa piste, calé au même endroit. Calcul hors du fil des messages, annulable, avec sa
  progression ; les stems entrent dans le magasin de contenu ; poser les pistes est un seul groupe d'annulation.
- **La direction par références** : la mesure de la S20, appliquée stem par stem à une ou plusieurs références,
  donne une direction (tonalité, tempo, densité, équilibre, couleur) que lisent la génération, l'arrangement et
  le mixage. Ce qui est extrait : des nombres, jamais l'audio ni les notes d'une référence.

**Hors périmètre.**
- L'audio vers MIDI (que les stems serviraient aussi).
- Toute génération audio.

**Ce qui se prouve par la machine.**
- Une séparation sur un signal construit (des sources connues mélangées) : chaque stem mesuré plus proche de sa
  source que du mélange, et la somme des stems proche du mélange.
- Les stems sur leurs pistes, au même endroit, un Ctrl+Z qui retire tout, à l'octet.
- La direction : deux références connues donnent la direction calculée ; la génération et le mixage lisent bien
  ces nombres (le brief du mixage, le prompt de la génération), vérifié à la couche qui les produit.
- Le projet rouvert garde sa direction.

**Questions de modèle à exposer avant de coder.**
- Le modèle de séparation : ouvert et local ou API ; licence du code **et** des poids, vérifiée avant de
  s'engager ; temps de calcul sur la machine du fondateur (i5-8365U, sans carte graphique) ; poids à livrer dans
  l'installeur de la S26.
- Où vit la direction : dans le projet (elle doit se rouvrir avec lui), donc une forme sérialisée, sans doute un
  verbe de domaine — à justifier par la démonstration.
- Comment plusieurs références se combinent (moyenne, poids, contradictions dites).
- Comment la direction se raccorde à la « Référence… » du mixage de la S20, qui existe déjà.

## S23 — Jouer : clavier MIDI et clavier AZERTY

**But.** On joue l'instrument d'une piste, au clavier MIDI ou au clavier de l'ordinateur, et on enregistre ce
qu'on joue dans le pattern en cours.

**Périmètre.**
- **Le clavier MIDI** : détection, choix de l'entrée, jeu de l'instrument de la piste choisie, enregistrement dans
  le pattern en cours.
- **Le clavier AZERTY comme clavier MIDI**, comme dans FL : les rangées de touches jouent des notes, avec
  changement d'octave. Disposition AZERTY, pas QWERTY.

**Hors périmètre.**
- L'enregistrement audio (micro), le MIDI vers d'autres logiciels, l'automation enregistrée au contrôleur.

**Ce qui se prouve par la machine.**
- Une prise simulée (des messages MIDI datés injectés à l'entrée) devient les bonnes notes, en un seul groupe
  d'annulation, et le rejouer donne le même projet.
- Le jeu n'écrit rien dans le projet tant qu'on n'enregistre pas.
- La latence, de l'événement d'entrée au son rendu, mesurée.
- Les touches AZERTY donnent les bonnes hauteurs ; les raccourcis existants marchent toujours hors du mode.

**Questions de modèle à exposer avant de coder.**
- La cohabitation du clavier AZERTY avec les raccourcis existants (Ctrl+C, F, flèches…), et quand le mode est
  actif.
- Le jeu passe par le moteur en temps réel et n'écrit rien ; une prise enregistrée devient des commandes
  (`note.add`, identifiants engendrés par l'appelant) en un groupe d'annulation, à la fin de la prise.
- Quantification à l'enregistrement ou non.
- Quelle piste joue quand aucune n'est choisie.

**Ce que la semaine a déplacé** (`docs/bilan-s23.md` §11).
- **La fenêtre « Audio »** (pilote, tampon, latence affichée) est tombée : **à la S24**, décidé le 6 octobre 2026.
- **Le choix d'une entrée MIDI** n'est pas fait : toutes les entrées jouent la piste choisie, validé le 6 octobre.

## S24 — IA : le kit et le mixer

**But.** Composer un kit cohérent depuis les samples de la personne, et régler les effets et les bus depuis le
mixer.

**Périmètre.**
- **Le générateur de drum kit, version « assembler »** : un kit cohérent composé depuis les samples de
  l'utilisateur, 808 accordée à la tonalité, des axes continus, écoute avant de poser, un seul groupe
  d'annulation.
- **Les effets depuis la tranche du mixer** : poser, retirer, réordonner et contourner un effet, sans quitter le
  mixer.
- **Le flux audio** (demandé le 6 octobre 2026, précisé le même jour) : une vue en graphe du trajet du son,
  analogue au Flow de Dataiku. Là-bas, des jeux de données reliés par des recettes ; ici, **des états du son**
  reliés par **des effets**. Chaque état est un nœud qui montre la forme d'onde du son à cet endroit, en direct ;
  chaque plugin est un nœud entre deux états ; les liens suivent le routage réel : la source d'une piste, ses
  effets l'un après l'autre, son fader, ses envois qui partent en branche vers un bus, les bus où plusieurs
  pistes se rejoignent, le master au bout. On lit d'un coup d'œil par où passe le son de tout le morceau, et ce
  que chaque étape en fait. Ce n'est pas une ligne par piste : c'est un graphe, avec ses branches et ses
  jonctions. C'est aussi l'endroit où l'on pose, retire, réordonne et contourne un effet (le point précédent).
- **La création de bus intelligente** : proposer un bus et des envois quand un même plugin revient avec les mêmes
  réglages.
- **La fenêtre « Audio »**, tombée en S23 (décidé le 6 octobre 2026) : le pilote, la sortie, le tampon, la
  latence affichée, et une recommandation (« Windows Audio (Low Latency) » s'il est là, sinon le mode exclusif
  en prévenant que les autres logiciels se taisent ; 256 échantillons au plus). Un réglage de la machine, pas du
  projet.

**Hors périmètre.**
- ASIO (le SDK de Steinberg et sa licence), après la S26.
- La synthèse de sons neufs par un modèle (la version « synthétiser » du kit).
- Les fiches des VST connus (la liste n'est pas donnée).

**Ce qui se prouve par la machine.**
- Le kit : sur une bibliothèque construite (samples connus), le kit choisi respecte ses contraintes mesurées
  (808 à la tonalité à quelques cents près, pas de recouvrement kick / 808), et le poser est un seul Ctrl+Z.
- Les effets : chaque geste de la tranche produit la commande attendue (`plugin.insert`, `plugin.remove`,
  `plugin.set_bypassed`, réordonner), vérifiée au rendu pour le contournement.
- Le flux : le graphe affiché est celui du projet (chaque piste, chaque effet, chaque envoi, chaque bus, le
  master ; un projet construit donne les nœuds et les liens attendus) ; la forme d'onde d'un nœud est celle du
  rendu à cet endroit, mesurée ; un effet contourné donne la même forme avant et après ; un geste dans le graphe
  produit la commande attendue ; la lecture n'en devient pas muette et aucun bloc audio n'attend l'écran.
- Les bus : sur un projet construit (la même réverbération sur six pistes), la proposition est faite, essayée à
  blanc, gardée en un seul groupe ; le rendu avant / après est mesuré.
- La fenêtre « Audio » : un tampon changé est celui que la carte ouvre (relu au moteur), la latence affichée est
  celle que `--verify-jeu` mesure, et la lecture n'en devient pas muette (`--verify-lecture` après le changement).

**Questions de modèle à exposer avant de coder.**
- Le kit : comment on choisit des samples qui « vont ensemble » sans recherche par le son — des descripteurs
  mesurés sur l'audio (attaque, brillance, longueur, hauteur) sont sans doute à construire d'abord.
- Réordonner un effet : il n'y a pas de commande pour ça aujourd'hui.
- Le flux : c'est un panneau neuf, un graphe. Sa disposition (calculée depuis le routage, jamais stockée dans
  le projet : un état d'écran, comme la vue de la toile) ; sa navigation (la grammaire de la S18 : Ctrl+molette,
  clic-molette, F) ; ce qu'un nœud d'état montre (la forme d'onde, peut-être le spectre et le niveau) et ce
  qu'un clic sur lui fait (l'écouter seul à cet endroit ?) ; comment le son est prélevé entre deux effets (une
  prise par emplacement, sans allocation ni verrou sur le fil audio, comme `MeterTap`), pour quels nœuds (tous,
  ou seulement ceux qui sont à l'écran) et ce que ça coûte en processeur ; le retard qu'un plugin ajoute, pour
  que l'avant et l'après se lisent au même instant ; ce qu'on peut faire dans le graphe (glisser un effet sur un
  lien pour l'insérer, tirer un lien vers un bus pour créer un envoi) et par quelles commandes existantes ; ce
  que le mixage par l'IA et les bus intelligents y montrent (une proposition vue dans le graphe avant d'être
  gardée) ; et son rapport avec le mixer et la page de la chaîne de plugins : ce qui reste, ce qui fait double
  emploi. Il marche pour les plugins de la personne, opaques, puisqu'on prélève le son et pas leurs réglages.
- Ce que veut dire « les mêmes réglages » pour un plugin dont l'état est un blob opaque ; un effet d'insertion
  ne se mutualise pas comme un effet d'envoi.
- La fenêtre « Audio » : ce qu'on fait quand le réglage choisi échoue à l'ouverture (revenir à l'ancien, le
  dire), et comment elle cohabite avec `AudioOutputKeeper`, qui revient à la première sortie quand la carte part.
- La semaine porte quatre chantiers, et le flux audio est un panneau neuf. **Tous sont faits dans la S24**,
  quitte à ce qu'elle dure plus qu'une semaine (décidé le 6 octobre 2026) : aucun ne tombe faute de temps.

**Ce que la semaine laisse à trancher** (`docs/bilan-s24.md` §6.4, §7.3).
- **La contrainte de couleur du kit sur une petite bibliothèque** : à 0,75, quatre kicks laissaient le kit à trois
  éléments. Garder, ou relâcher sous dix samples par rôle (recommandé).
- **La réverbération par envoi au rendu** : non éprouvée, faute de réverbération sur une machine de vérification.
  Un test au label `audio` avec `DAW_TEST_VST3` (recommandé pour la S25), ou une réverbération interne (une
  décision de modèle).

## S25 — Diriger le DAW à la voix

**But.** On tient une touche, on parle, on relâche : la phrase part au copilote, sans geste déclenché sur une
transcription douteuse.

**Périmètre.**
- **Le push-to-talk** : tenir une touche, parler, relâcher, la phrase part au copilote. Il faut une brique de
  reconnaissance vocale.
- **La place laissée par le fine-tune** va à la qualité du push-to-talk : ce qu'on voit pendant qu'on parle, une
  phrase mal comprise qui se corrige avant de partir, aucun geste déclenché sur une transcription douteuse. Pas
  à un chantier de plus.
- **Les instruments IA** : une évaluation écrite sérieuse (quels modèles ou services, licence, latence, coût,
  qualité à l'écoute de leurs démos), avec une recommandation. Pas de code.

**Hors périmètre.**
- Le fine-tune du routeur d'intention (incubation).
- Tout code d'instrument IA.

**Ce qui se prouve par la machine.**
- Des enregistrements de phrases connues (fichiers audio du dépôt de test) transcrits avec un taux d'erreur
  mesuré, en français et sur le vocabulaire de la musique.
- La latence, du relâchement de la touche à la phrase affichée, mesurée.
- Une transcription marquée douteuse ne déclenche aucune commande ; le micro ne passe pas par le moteur ; aucune
  inférence sur le fil audio.

**Questions de modèle à exposer avant de coder.**
- La brique de reconnaissance vocale : locale ou distante, licence, latence, taille, qualité en français et sur
  le vocabulaire de la musique. Laya ne transcrit pas (c'est un classeur de texte). Jev a été écarté
  (`IDEES.md`) : le reprendre serait un écart, à dire avec sa raison.
- Ce qui rend une transcription « douteuse », et ce que la personne voit alors.
- La touche du push-to-talk, et sa cohabitation avec le clavier AZERTY de la S23.

## S26 — Emballer le prototype

**But.** Livrer un premier prototype qui s'installe et se lance sur une machine vierge, avec son projet de
démonstration.

**Périmètre.**
- **Un installeur Windows**, un premier lancement sur une machine vierge.
- **La clé d'API et les crédits** : où l'utilisateur la met, ce qui marche sans elle (les règles, le local), en
  gardant possibles les deux modèles de licence (abonnement / crédits pour l'API, achat pour le local).
- **Un projet de démonstration** livré avec l'application.
- **Les plantages rattrapés** et écrits dans `daw.log`.
- **Le bilan du prototype** : `docs/bilan-prototype.md` — ce qui existe, ce qui est dette — et
  `docs/essais-apres-s26.md` prêt pour la phase d'essais.

**Hors périmètre.**
- Pas de gel, pas de chasse complète aux bugs (décision B).
- La facturation elle-même : seul le code qui garde les deux modèles possibles.

**Ce qui se prouve par la machine.**
- L'installeur sur une machine vierge (une machine virtuelle ou Windows Sandbox) : installation, premier
  lancement, projet de démonstration ouvert et rendu mesuré, désinstallation propre.
- Sans clé : les règles et le local marchent, et l'écran le dit.
- Un plantage provoqué est rattrapé et écrit dans `daw.log`.

**Questions de modèle à exposer avant de coder.**
- Ce que l'installeur emporte : les services Python, leur interpréteur, les poids des modèles locaux (stems,
  reconnaissance vocale), et leur taille.
- Où la clé est rangée sur la machine (jamais dans un fichier du dépôt), et comment elle est demandée.
- Ce qu'un plantage rattrapé fait ensuite (sauvegarde, message, relance).

**Ajouté par la S25 (8 octobre 2026).** Les téléchargements au premier usage sont listés au bilan S25 §3.3 (la voix :
sherpa-onnx et 487 Mo de poids, sans PyTorch ; les stems : PyTorch et HTDemucs). Les poids de Parakeet sont sous
CC-BY-4.0 : l'application doit citer NVIDIA (un écran « À propos » ou équivalent).

## Hors des 26 semaines

Noté comme tel, pour qu'aucun brief ne le prenne sans une décision du fondateur :

- **Composer sur une vidéo et les bruitages** : il faut d'abord une piste vidéo dans le DAW.
- **Les fiches des VST connus** : la liste des plugins est à donner.
- **Les workspaces film et pub / UGC** au-delà de leurs manifestes.
- **Le workspace découverte** : outil des ateliers de présentation, développé plus tard. Sorti du logiciel le
  3 octobre 2026 (manifeste gardé, réservé aux ateliers).
- **Le retrait du piano-roll** : il attend la phase d'essais.
- **La phase d'essais et d'ergonomie**, après la S26, sur `docs/essais-apres-s26.md`.
- **Pendant l'incubation** : le fine-tune du routeur d'intention, avec de vraies machines et un jeu de données
  plus grand ; la qualité premium, la chasse complète aux bugs et le polissage, avec des professionnels.
