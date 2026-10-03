# Idées

Décisions de roadmap consignées, pas des chantiers. Rien ici n'est construit tant qu'une semaine ne
le prend pas explicitement dans son périmètre.

## Couche décision (routage du copilote)

- Laya (Convai Innovations, 421M paramètres, ModernBERT-large, Apache 2.0, décisions typées non
  autorégressives, contexte 512 tokens) comme routeur d'intention DEVANT le copilote : classer la
  phrase de l'utilisateur (commande exécutable / demande créative / hors de portée / ambiguë) avant
  d'appeler le modèle distant.
  Ne voit que la phrase, jamais l'état du projet — 9 000 tokens ne tiennent pas dans 512.
  Gain attendu honnête : 10-20 % du coût, pas 80 % — le gros du coût est l'état du projet renvoyé à
  chaque tour, pas les requêtes inutiles.
- Nécessite un fine-tune : le modèle de base est à peine au-dessus de l'aléatoire (0,36 contre
  0,318) et monte à 0,766 une fois affiné.
  **Reporté à l'incubation** (3 octobre 2026) : la machine du fondateur (portable i5-8365U, sans carte
  graphique) n'entraîne pas un modèle de 421 M de paramètres, et un fine-tune bricolé ne vaut pas le
  détour. Il se fera pendant l'incubation, avec de vraies machines et un jeu de données plus grand.
- Le jeu de données se constitue DÉJÀ tout seul dans le journal SQLite : `group_label` porte la
  phrase, `origin=copilot` marque la provenance, et les commandes produites sont l'étiquette. Ne pas
  purger ces lignes.
- Jev (TypeSafe) écarté : service hébergé, aucun poids public. Contredit l'argument « inférence
  locale, coût marginal nul » et ajoute un second fournisseur dans le chemin critique d'une démo.

## Couche générative (plus tard, S11+)

- Séparer JUSTESSE et STYLE, ne jamais les confondre :
  - le moteur harmonique énumère les candidats légaux — règles, déterministe, explicable, jamais
    faux ;
  - un modèle classe ces candidats selon le corpus personnel — style.

  Laisser un modèle statistique décider de la justesse donne du plausible-mais-faux ; laisser des
  règles décider du style donne du correct-et-plat.
- Tester une chaîne de Markov d'ordre 3-4 sur le corpus AVANT tout modèle neuronal. Sur un
  vocabulaire aussi contraint, ça donne souvent l'essentiel pour quelques centaines de lignes de
  Python.
- Laya est un candidat pour la couche CLASSEMENT, pas pour la génération de séquence : non
  autorégressif, donc N appels pour N notes, et ×4 si hauteur, durée, vélocité et position sont des
  questions séparées. Une question à choix doit rester sous ~20 options.
- LIVE : aucune inférence ne touche le thread audio. Même 13 ms, c'est dix fois le budget temps
  réel, et une allocation dans le callback est un craquement garanti (règle tenue depuis la S2). Le
  système propose la suite en avance de phase, il ne l'insère jamais au moment où elle doit sonner.

## Recherche de samples par l'IA (après la S12)

La S12 livre une recherche par les noms : mots, préfixes, une faute de frappe, et une table de
familles écrite à la main (house ≈ club ≈ techno, kick ≈ bd). Elle ne trouve que ce que les noms
disent. Ce qu'un modèle ajouterait :

- des familles apprises plutôt qu'écrites, en plongeant les noms de fichiers et de dossiers dans un
  espace de vecteurs (un petit modèle d'embeddings local) : « kick house » trouverait « Thump 04 »
  rangé dans « Deep Tech » ;
- une recherche par le son, pas par le nom : des descripteurs mesurés sur l'audio (attaque,
  brillance, longueur, hauteur) pour « un kick sec et court », « un 808 qui glisse » ;
- le copilote comme interface : « trouve-moi un clap plus clair que celui du pattern 2 ».

Garder le principe S12 : local, déterministe à entrée égale, et l'index calculé hors du thread
message. Un appel au modèle distant par frappe de clavier serait trop lent et trop cher.

## Réserve sur les sources

Chiffres issus d'un blog commercial (vendeur de NAS) reprenant la documentation du projet Laya. À
revérifier sur le dépôt Hugging Face avant tout engagement.

## À trancher fin S14 — la stratégie du moteur génératif

Décision reportée volontairement jusqu'à ce que la S14 dise jusqu'où vont des règles et un Markov
sur le corpus personnel.

**Tranché en S15 sur un point (27 septembre 2026) : pas de fine-tune sur un corpus personnel du
fondateur.** Un DAW s'adresse à tout le monde ; un générateur enfermé dans le style d'un seul
producteur ne sert que lui. Le générateur apprend à la place de la personne qui l'utilise, à partir
de ce qu'elle produit dans le DAW, et tout reste sur sa machine (bilan S15). La voie « transformer
fine-tuné sur le corpus perso » du tableau ci-dessous et la « lecture retenue » qui la suit sont
donc écartées ; le reste de la section (audio écarté, empiler les couches) tient toujours.

Écarté d'emblée : la génération d'un morceau fini (Suno, Udio, Stability). Elle produit du fini, pas de la
matière éditable — ni BPM exact, ni grille, ni stems, ni notes déplaçables. Incompatible avec la
promesse de contrôle chirurgical.

**Reformulé le 3 octobre 2026, validé par le fondateur : l'audio généré est accepté comme matière, jamais
comme morceau fini.** La ligne d'origine disait « la génération audio », ce qui était trop large : elle visait
le morceau entier sorti d'une phrase. Accepté : un timbre rendu à partir de notes écrites (instrument IA), un
coup de batterie, un bruitage, une voix chantée à partir de notes et de paroles. Toujours refusé : un morceau
entier généré d'une phrase, une boucle audio sans notes ni grille. Le critère : ce que l'IA produit reste à sa
place sur la grille, remplaçable et supprimable pièce par pièce, et la musique reste des notes dans le projet.

Symbolique open-source : peu de choses utilisables en produit. Magenta à l'abandon ; les
transformers symboliques (Music Transformer, anticipatory, MMM) sont des travaux de recherche —
poids disponibles, qualité inégale, licences à vérifier une par une. Point de départ pour un
fine-tune, pas un produit.

Les quatre voies :

| Voie | Coût | Valeur devant un comité |
|---|---|---|
| Règles + Markov (S14) | déjà payé | faible seul, mais c'est la base de justesse |
| LLM distant qui écrit du MIDI sous contraintes | quasi nul | marche demain, mais c'est l'API d'un autre |
| Transformer open-source fine-tuné sur le corpus perso | quelques centaines d'euros de GPU, 3-4 semaines | le plus fort : modèle spécialisé, corpus propriétaire |
| Modèle from scratch | des mois | aucun intérêt à cette échelle |

Lecture retenue : l'avantage n'est ni le modèle ni l'algorithme, c'est huit ans de production dans
un genre précis avec des placements pour preuve. La vraie architecture n'est pas de choisir une
voie mais de les empiler — règles pour la justesse, modèle pour le style, LLM distant pour
comprendre l'intention en français. La couche de contrainte, qui est notre code, garantit le
résultat.

PRÉALABLE JURIDIQUE, hors du dépôt : vérifier ce que le contrat d'édition dit de l'exploitation
dérivée des compositions. Entraîner un modèle commercial sur des œuvres éditées n'est pas neutre.

## Le geste de génération, après la S16

Notés en construisant la fenêtre, l'écoute et les retouches, non construits :

- **Les variantes côte à côte.** Aujourd'hui une flèche et un compteur (« 2 / 5 »). Trois
  propositions en miniature sous la fenêtre, qu'on écoute l'une après l'autre, se choisiraient à
  l'oreille sans les parcourir.
- **Un modèle plus rapide pour lire le prompt.** La lecture passe par le modèle du copilote, à
  effort bas. Un modèle plus petit répondrait plus vite pour une simple mise en champs ; le choix
  du modèle reste à toi, et il se mesure sur de vrais prompts avant d'être changé.
- **Retoucher des notes choisies une à une,** sans toucher aux autres de la zone. Aujourd'hui la
  zone est la plage des notes choisies, et la retouche reprend toutes les notes de cette plage.
- **L'écoute en SONG sur un autre placement que le premier.** Elle boucle sur le premier
  placement du pattern ; la tête de lecture pourrait choisir celui qu'on regarde.
- **Des retouches combinées :** « plus sombre et plus calme » ne prend aujourd'hui que la première
  consigne reconnue.

## La playlist et le rangement, après la S17

Notés en construisant les lignes libres, le classifieur de rôle et la zone multi-pistes, non
construits :

- **Couper le son d'une ligne,** comme FL le fait d'une piste de playlist. La ligne ne sonne pas
  aujourd'hui, par choix : c'est ce qui garde la projection et l'écoute intactes. Un mute de ligne
  serait un champ de plus et une règle de projection, à exposer avant.
- **Les automations en blocs,** rangées sur les lignes comme dans FL. Aujourd'hui ce sont des
  courbes de paramètre sous les lignes, sur toute la chanson.
- **Des lignes de couleur et de hauteur choisies.** C'est un état d'écran : il irait dans les
  préférences de la fenêtre, pas dans le projet.
- **La zone qui crée ses canaux.** Une ligne « Basse » sans piste de basse est laissée de côté,
  et la fenêtre le dit. Créer le canal, avec un instrument, demanderait un choix de plugin.
- **Le rôle d'une ligne corrigé à la main** dans la fenêtre de la zone (« non, celle-ci est une
  nappe »), sans renommer la ligne.
- **« Ranger le projet » par le copilote seul,** en une requête : il a les outils (lane.*,
  placement.move avec laneId) et voit les lignes nommées.

## La fluidité, après la S18 bis

Notés en mesurant, non construits :

- **Les vumètres à la cadence de l'écran.** Les panneaux le sont ; `LevelMonitor` lit encore les prises du moteur à
  30 Hz. Le lire à chaque image coûterait une lecture des prises par image et par tranche.
- **Le mixer entier en un repeint moins cher.** C'est le plus lourd (~7 ms en Direct2D) : chaque tranche redessine
  ses combos, ses curseurs et son texte. Une tranche mise en image, repeinte seulement quand elle change, est la
  piste à mesurer d'abord.
- **La réaction des panneaux à une commande,** ~3 ms par image de glissé de note : le piano-roll reconstruit son
  choix de canal à chaque changement, même quand les canaux n'ont pas changé.
- **Mesurer le déplacement natif de la fenêtre principale** (traces ETW de Windows), au lieu de le juger à l'œil.

## La toile, après la S19

Notés en construisant la toile, non construits :

- **Rendre un bloc unique** depuis la toile (« éditer seulement ici ») : cloner le pattern et reposer le bloc
  sur le clone. C'est un verbe de domaine de plus ; la règle de la S18 demande une raison de démonstration.
- **Retirer le piano-roll** une fois la toile essayée : F7 hors du manifeste, `PianoRoll*.cpp` supprimé, les
  étapes de `--verify` et `--verify-fluidite` qui passent par lui réécrites sur la toile.
- **La toile à la place de la playlist (F5),** le drapeau toile/playlist retiré.
- **Une bascule PAT/SONG propre à la toile,** si suivre le transport gêne à l'usage.
- **Le repeint du glissé d'un point d'automation** (12 à 14 ms de repeint par image, plus que la playlist entière) : la zone salie
  dépasse la bande d'automation. À trouver avant d'y toucher.
- **La toile chargée sous 8 ms au 95e centile en Direct2D :** 256 blocs donnent 9 à 13 ms au 95e centile selon
  la charge de la machine.

## Le mixer et les effets (noté le 3 octobre 2026, avant la S20)

Demandés par le fondateur, non construits, hors du périmètre de la S20 tant qu'une semaine ne les prend pas :

- **Poser des effets depuis la tranche du mixer.** Aujourd'hui une tranche liste le nom de ses inserts, en
  lecture seule ; on les ajoute depuis la page de la chaîne de plugins. Il faut un endroit dans chaque tranche
  pour ajouter, retirer, réordonner et contourner un effet, sans quitter le mixer. Les commandes existent
  (`plugin.insert`, `plugin.remove`, `plugin.set_bypassed`) : c'est de l'écran, pas du domaine.
  **Prise en S24** (`docs/plan-s21-s26.md`).
- **La création de bus intelligente.** Le logiciel remarque qu'un même plugin est posé plusieurs fois avec les
  mêmes réglages (la même réverbération sur six pistes, par exemple) et propose de le remplacer par un bus et
  des envois. À trancher avant d'y toucher : ce que veut dire « les mêmes réglages » pour un plugin dont l'état
  est un blob opaque (comparer les digests du magasin de contenu, comparer les paramètres touchés, tolérance) ;
  un effet d'insertion (compresseur, égaliseur) ne se mutualise pas comme un effet d'envoi (réverbération,
  délai) ; la proposition passe par l'essai à blanc et un seul groupe d'annulation, comme le reste. Les
  commandes existent (`bus.add`, `track.set_send`, `plugin.remove`).
  **Prise en S24** (`docs/plan-s21-s26.md`).
- **Apprendre au DAW les VST externes les plus connus.** Pour une liste de plugins que le fondateur donnera
  plus tard, le DAW sait ce que fait chaque paramètre (nom, unité, plage utile, rôle), au lieu de voir un
  plugin opaque. C'est ce qui permettrait au copilote et au mixage par l'IA de régler les plugins de
  l'utilisateur, que la S20 laisse volontairement de côté. Forme probable : une fiche déclarative par plugin,
  liée par l'identifiant stable du format (jamais par le nom ni par l'index d'un paramètre), versionnée avec
  le plugin. Liste des plugins : à fournir.

## Quatre idées d'IA (notées le 3 octobre 2026)

Demandées par le fondateur, non construites. Trois d'entre elles touchent une décision déjà écrite plus haut
dans ce fichier : c'est dit à chaque fois, et rien n'est rouvert tant qu'une semaine ne le prend pas.

- **La direction IA du projet, fondée sur des références.** L'utilisateur donne un ou plusieurs morceaux de
  référence, et c'est d'eux que l'IA tire sa direction pour tout le projet : tonalité, tempo, structure,
  densité, équilibre du mixage, couleur. Une seule direction partagée par la génération, l'arrangement et le
  mixage, au lieu d'une consigne redonnée à chaque geste. C'est le « conditionnement paramétrique par
  extraction depuis une référence utilisateur » du positionnement, et le prolongement de la piste de référence
  prévue pour le mixage en S21 : la même mesure, appliquée à un fichier, donne une cible. À trancher : ce qui
  est extrait d'une référence (des nombres, jamais son audio ni ses notes), et où vit la direction (dans le
  projet, puisqu'elle doit se rouvrir avec lui).
  **Prise en S22** (`docs/plan-s21-s26.md`).
- **Le push-to-talk.** Tenir une touche, parler, relâcher : la phrase part au copilote. Proposé avec Jev, ou
  Laya pour rester en local. Deux points à voir avant :
  - **Jev a été écarté** plus haut (« Couche décision ») : service hébergé, aucun poids public, un second
    fournisseur dans le chemin critique d'une démo. Le reprendre, c'est rouvrir cette ligne.
  - **Laya ne transcrit pas la voix** : c'est un classeur de texte (routeur d'intention, 512 tokens). Il manque
    donc une brique de reconnaissance vocale avant lui, locale si l'on veut tenir « inférence locale, coût
    marginal nul » ; Laya, lui, classerait la phrase transcrite. Aucune inférence sur le thread audio, et le
    micro du push-to-talk ne passe pas par le moteur.
  **Prise en S25** (`docs/plan-s21-s26.md`).
- **Des instruments IA** (vu chez ACE Studio, « AI Instruments » : des interprétations d'instruments réalistes,
  sans télécharger de banques de samples). Un instrument dont le son est rendu par un modèle à partir des notes
  écrites, au lieu d'un synthé ou d'un sampler. **Ce n'est pas la génération d'un morceau fini, écartée plus haut**
  (Suno, Udio) : ici les notes restent dans le projet, déplaçables, sur la grille ; seul le timbre vient du
  modèle. À trancher : rendu local ou distant, latence à la lecture (un rendu hors ligne mis en cache par
  pattern, invalidé quand ses notes changent, plutôt qu'une inférence en temps réel), et ce que devient le
  projet sans le modèle.
  **Prise en S25, en évaluation écrite seulement, sans code** (`docs/plan-s21-s26.md`).
- **Composer sur une vidéo** (vu chez ACE Studio, « Video Composer », https://acestudio.ai/video-composer/ :
  des musiques et des bruitages générés pour coller à une vidéo). Pour les workspaces pub / UGC et musique de
  film : l'IA lit la vidéo (coupes, rythme, durée, ambiance) et propose une musique et des bruitages calés
  dessus. **Dans la règle « matière, jamais morceau fini » (S14, reformulée)** : pour tenir la promesse de contrôle, la musique
  proposée devrait être des patterns et des notes posés sur des repères tirés de la vidéo, pas un fichier
  audio fini ; les bruitages, eux, sont de l'audio par nature (recherche de samples par l'IA, ou génération).
  Dépend d'une piste vidéo dans le DAW, qui n'existe pas.
- **Un séparateur de stems par IA** (noté le 3 octobre 2026, demandé par le fondateur ; ACE Studio en propose
  un). Un fichier audio déposé sur une piste se sépare en voix, batterie, basse et le reste, chaque stem sur sa
  propre piste, calé au même endroit. C'est la « séparation de stems intégrée à la piste » du contexte produit.
  Ce n'est pas de la génération : rien n'est inventé, donc la ligne « génération audio écartée » n'est pas
  touchée. À trancher : modèle ouvert en local (licence à vérifier avant de s'engager) ou API ; le calcul est
  long, donc hors du thread message, annulable, avec sa progression ; les stems entrent dans le magasin de
  contenu du projet comme n'importe quel sample ; poser les pistes est un seul groupe d'annulation. Sert aussi
  la direction par références (mesurer une référence stem par stem) et l'audio vers MIDI.
  **Prise en S22** (`docs/plan-s21-s26.md`).
- **Les bruitages sur une vidéo, par IA** (noté le 3 octobre 2026 ; précise « Composer sur une vidéo »
  ci-dessus). Le fondateur veut d'abord le volet bruitages (SFX) : l'IA lit la vidéo, repère ce qui demande un
  son (une coupe, un impact, un mouvement, une ambiance) et pose un bruitage à chaque repère, sur la timeline.
  Chaque bruitage est un clip audio à sa place : déplaçable, remplaçable, supprimable un par un. D'où vient le
  son, à trancher : cherché dans les samples de l'utilisateur (recherche par le son), ou généré — et là c'est
  de l'audio généré, accepté comme matière depuis la reformulation de la règle de la S14. Dépend toujours d'une piste vidéo dans le DAW, qui n'existe pas : c'est elle le premier chantier.
- **Un générateur de drum kit automatique** (noté le 3 octobre 2026, demandé par le fondateur). En un geste, le
  DAW compose un kit complet et cohérent — kick, caisse claire ou clap, charleys fermé et ouvert, percussions,
  808 — et le range dans le channel rack, un canal par élément. Deux lectures possibles, à trancher :
  - **assembler** un kit à partir des samples de l'utilisateur, choisis pour aller ensemble (même couleur,
    pas de recouvrement entre le kick et la 808, 808 accordée à la tonalité du projet). C'est la suite directe
    de la « recherche par le son » ci-dessus (section « Recherche de samples par l'IA ») et du Bass & Groove
    Engine du positionnement ; local, sans génération audio ;
  - **synthétiser** des sons neufs par un modèle. Un coup de batterie isolé est de la matière, acceptée depuis la
    reformulation de la règle de la S14 ; reste la question des droits sur ce que le modèle a appris.
  Dans les deux cas : pas une liste de kits tout faits, des axes continus (sombre ↔ brillant, sec ↔ ample,
  propre ↔ saturé) et une direction tirée des références du projet ; le kit s'écoute avant d'être posé, et le
  poser est un seul groupe d'annulation. Les commandes existent (`track.add`, `track.set_sample`).
  **Prise en S24, version « assembler »** (`docs/plan-s21-s26.md`).

## Jouer (noté le 3 octobre 2026)

Demandés par le fondateur :

- **Brancher un clavier MIDI.** Détection, choix de l'entrée, jouer l'instrument de la piste choisie, enregistrer
  ce qu'on joue dans le pattern en cours. Le jeu passe par le moteur en temps réel et n'écrit rien ; une prise
  enregistrée devient des commandes (`note.add`, identifiants engendrés par l'appelant) en un seul groupe
  d'annulation, à la fin de la prise.
  **Prise en S23** (`docs/plan-s21-s26.md`).
- **Le clavier AZERTY comme clavier MIDI,** comme dans FL : les rangées de touches jouent des notes, avec
  changement d'octave. Disposition AZERTY, pas QWERTY. À trancher : la cohabitation avec les raccourcis
  existants (Ctrl+C, F, flèches), et quand le mode est actif.
  **Prise en S23** (`docs/plan-s21-s26.md`).
