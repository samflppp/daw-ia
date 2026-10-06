Projet DAW IA — semaine 24 sur 26. Dépôt : samflppp/daw-ia, branche main,
push direct.

Lis d'abord : CLAUDE.md, docs/plan-s21-s26.md (section S24 : ce brief la
précise), docs/bilan-s23.md (§7 --verify-lecture, §11 ce qui est tombé,
§2.4 direction.amount), docs/bilan-s22.md (la direction), docs/bilan-s20.md
(le mixage par l'IA : mesure, garde-fous, essai à blanc, effets internes),
docs/bilan-s21.md (§2 la carte son), docs/bilan-s12.md (la recherche de
samples), docs/bilan-s18.md (§6 : la grammaire de navigation),
docs/command-bus.md, docs/plugins.md, IDEES.md (« Le mixer et les effets »,
le générateur de drum kit, « Recherche de samples par l'IA »), core/engine/
(EngineHost, AudioOutputKeeper, ProjectProjector, MeterTap, MixTap,
PitchDetection, PluginCatalogue), core/ui/src/panels/ (MixerPanel,
PluginChainPanel, BrowserPanel, ChannelRackPanel).

ÉTAT ACQUIS (S1–S23, ne rien rouvrir) :
- Tout CLAUDE.md §2. Registry de 61 types, schéma 7, enveloppe v3.
- Les commandes des effets et du routage existent : plugin.insert,
  plugin.remove, plugin.set_bypassed, plugin.set_parameter, bus.add,
  track.set_send, track.remove_send, track.set_output, track.add,
  track.set_sample. Aucune ne réordonne un effet.
- Les effets internes (daw.eq, daw.compressor) sont les seuls que l'IA
  règle ; les plugins de la personne restent opaques, jamais retirés ni
  contournés par l'IA (S20).
- Le mixage gardé est un seul groupe d'historique ; une proposition passe
  par l'essai à blanc et s'écoute avant d'être gardée (S20).
- La direction vit dans le projet, des nombres seulement (S22).
- Le jeu en direct (S23) : LiveInputPlugin en tête de chaque piste, la
  règle du dernier 1/32 de temps gardée telle quelle.
- La recherche de samples par les noms (S12) ; PitchDetection (YIN) existe,
  et sous 100 Hz il peut se tromper d'octave, jamais de classe de hauteur.
- Les quatre décisions du 3 octobre (CLAUDE.md §4).
- Windows seule cible. Job CI Linux en compilation, ne pas toucher.

CAP : jusqu'ici l'IA mixe à partir de nombres, et le routage ne se voit
nulle part : il se devine dans le mixer et la page de la chaîne. Cette
semaine, le son se VOIT. Le flux audio montre par où il passe dans tout le
morceau, et ce que chaque effet en fait, en direct. Et l'IA compose un kit
de batterie cohérent depuis les samples de la personne. La fenêtre
« Audio » donne enfin le réglage qui manquait au jeu de la S23.

MON OPTIQUE, À APPLIQUER ICI : le débutant doit comprendre en regardant.
Un graphe qui montre le vrai routage, jamais une illustration. Un kit qui
sonne ensemble, choisi par des mesures vraies, jamais par les seuls noms.
Recommande la meilleure voie et dis ce qu'elle coûte. La semaine porte
quatre chantiers et un panneau neuf : TOUT est fait dans cette S24, quitte
à ce qu'elle dure plus qu'une semaine (décidé le 6 octobre 2026). Une S est
un périmètre, pas une durée : rien ne tombe faute de temps, rien n'est
bâclé pour finir plus tôt.

RÈGLES DE SESSION (elles tiennent) :
- Une seule session dans ce dossier. Du travail non commité ou une branche
  inconnue : tu t'arrêtes et tu me le dis.
- Tu compiles par scripts/build.cmd (page de code 65001), jamais
  autrement.
- Vérifications avec --project et --layout dans un dossier jetable. La
  fenêtre « Audio » ajoute un réglage de la machine : une vérification ne
  doit jamais réécrire mes réglages audio, pas plus que ma disposition.
- Espace libre sur C: regardé avant une compilation complète.
- Machine au repos pour mesurer une latence ou un coût processeur ; sinon
  dis-le avant.
- AUCUN COMMIT POUSSÉ AVEC LA CI ROUGE. En S23 il y en a eu un (C4324,
  que clang ne voit pas). Attends la CI du commit précédent avant de
  pousser le suivant ; git pull avant de pousser.
- Mes décisions de modèle sont recopiées dans le bilan avec leur date.
- Un commit, un sujet. Un message de commit ne dit que ce qui est prouvé
  (en S23, un message disait cassés des tests qui ne l'étaient pas).
- Debug et Release recompilés dans mon dossier de build à la fin.
- Aucune étape n'attend un essai de ma part.

CHANTIER 0 — LES RESTES DE LA S23. Une journée au plus, dans cet ordre.
Tu codes, tu expliques après.
1. --verify-lecture, puis --verify-lecture --sans-jeu, sur cette machine.
   La S23 n'a pas pu les mesurer. Le jeu fait des lectures muettes et
   --sans-jeu non : c'est une régression, elle passe avant tout le reste ;
   trouve la cause, ou retire le jeu des chaînes (playLiveFrom) en
   attendant et dis-le.
2. Le --verify complet avec copilote, pas relancé depuis la S22. Corrige
   ce qui tombe.
3. --verify-jeu sur ma carte : la latence réelle (pilote, tampon, de la
   touche au premier échantillon), et la preuve que Raw Input est lancé
   pour de vrai (daw.log). C'est la mesure de départ de la fenêtre
   « Audio ».
4. Les décalages de couleur des règles du mixage ignorent direction.amount
   (bilan S23 §2.4) : à zéro, la référence ne doit plus rien déplacer.
   Corrige, test cassé une fois.

CHANTIER 1 — LE MODÈLE. EXPOSE AVANT DE CODER. Attends ma validation.

5. LA FENÊTRE « AUDIO ».
   - Ce qu'elle montre : le pilote, la sortie, le tampon, la fréquence, la
     latence. La latence affichée est celle que la machine mesure, pas
     seulement celle que la carte déclare : dis comment.
   - La recommandation à l'écran (« Windows Audio (Low Latency) » s'il est
     là, sinon le mode exclusif en prévenant que les autres logiciels se
     taisent ; 256 échantillons au plus). Dis ce que JUCE offre vraiment
     sous Windows, et ce qui n'existe pas.
   - Un réglage qui échoue à l'ouverture : revenir à l'ancien, le dire.
   - La cohabitation avec AudioOutputKeeper, qui revient à la première
     sortie quand la carte part : un choix de sortie fait dans la fenêtre
     devient-il « la première » ?
   - Où le réglage est gardé (la machine, jamais le projet), et comment une
     vérification le laisse intact.
   - Changer de tampon pendant la lecture ou le jeu : aucune note coincée,
     aucune lecture muette.

6. LES EFFETS DEPUIS LA TRANCHE DU MIXER.
   - Poser, retirer, réordonner, contourner, sans quitter le mixer. Par les
     commandes existantes ; montre la tranche avant / après.
   - Réordonner : il n'y a pas de commande. Dis s'il en faut une (un verbe,
     sa raison de démonstration, ce qu'elle fait de l'identité du plugin et
     de son état), ou si retirer + poser suffit (et ce que ça perd). Ne
     l'ajoute pas sans me le dire.
   - Ce que la tranche montre d'un plugin opaque, et d'un effet interne.

7. LE FLUX AUDIO, le panneau neuf de la semaine.
   - Un graphe, analogue au Flow de Dataiku : des états du son (un nœud =
     la forme d'onde à cet endroit, en direct) reliés par des effets (un
     nœud par plugin), avec les branches et les jonctions du routage réel :
     source, effets, fader, envois, bus, master. Le graphe est calculé
     depuis ProjectState ; sa disposition est un état d'écran, jamais
     stockée dans le projet. Montre un projet de six pistes, deux bus et
     des envois tel que le graphe le dessinera.
   - Le prélèvement du son entre deux effets : une prise par emplacement,
     sans allocation ni verrou sur le fil audio (comme MeterTap). Pour quels
     nœuds (tous, ou ceux qui sont à l'écran), ce que ça coûte en processeur,
     et comment c'est mesuré.
   - Le retard qu'un plugin ajoute : l'avant et l'après se lisent au même
     instant.
   - Ce qu'un nœud montre (la forme d'onde ; le niveau ; le spectre ?) et ce
     qu'un clic fait (l'écouter seul à cet endroit ?). Recommande le
     minimum qui fait comprendre un effet à un débutant.
   - Ce qu'on fait dans le graphe : glisser un effet sur un lien, tirer un
     lien vers un bus pour créer un envoi. Par quelles commandes
     existantes. La navigation suit la grammaire de la S18 (Ctrl+molette,
     clic-molette, F) : rien ne s'apprend deux fois.
   - Ce que le mixage par l'IA y montre : une proposition vue dans le graphe
     avant d'être gardée.
   - Son rapport avec le mixer et la page de la chaîne de plugins : ce qui
     reste, ce qui fait double emploi. Recommande, ne retire rien sans me
     le dire.
   - Il marche pour les plugins de la personne : on prélève le son, pas
     leurs réglages.

8. LE KIT, version « assembler ».
   - Ce qui est mesuré sur chaque sample de la bibliothèque (attaque,
     brillance, longueur, hauteur, niveau…), où l'index vit (la machine,
     pas le projet), quand il se calcule (hors du fil des messages, en
     tâche annulable avec sa progression), et ce qu'il coûte sur une
     bibliothèque de 10 000 fichiers.
   - Ce que « aller ensemble » veut dire, en nombres : même couleur, pas de
     recouvrement kick / 808, 808 accordée à la tonalité du projet (à
     quelques cents ; dis comment tu mesures la hauteur d'une 808 qui
     glisse). Local, déterministe à entrée égale.
   - Les axes continus (sombre ↔ brillant, sec ↔ ample, propre ↔ saturé),
     et ce que la direction des références y donne.
   - Le rôle du modèle : décide-t-il à partir des seuls nombres, comme le
     mixage de la S20, ou des règles suffisent-elles ? Sans clé, il marche.
   - L'écoute avant de poser : comment, sans rien écrire.
   - Poser : track.add et track.set_sample en un seul groupe, identifiants
     de l'appelant. Ce qui arrive quand la bibliothèque n'a pas de 808, ou
     pas de charley ouvert : le dire, ne rien inventer.

9. LA CRÉATION DE BUS INTELLIGENTE.
   - Ce que veut dire « les mêmes réglages » pour un état opaque (digests
     du magasin de contenu, paramètres touchés, tolérance).
   - Un effet d'insertion ne se mutualise pas comme un effet d'envoi : dis
     lesquels le logiciel propose, et lesquels jamais.
   - La proposition passe par l'essai à blanc, s'écoute, se garde en un
     groupe, et se voit dans le flux audio.

CHANTIER 2 — CONSTRUIS-LE, dans cet ordre, une livraison à chaque étape :
10. La fenêtre « Audio ».
11. Les effets depuis la tranche, avec le réordonnancement tel que validé.
12. Le flux audio : le graphe du projet, les prises entre effets, les
    gestes.
13. Le kit : l'index de la bibliothèque, puis le choix, l'écoute, la pose.
14. Les bus intelligents.
15. --verify-flux et --verify-kit : tout ce qui précède, sans matériel ni
    bibliothèque de la personne (une bibliothèque construite, des samples
    connus).

TOUT EST DANS LE PÉRIMÈTRE : aucune étape ne tombe faute de temps. Une
étape ne se reporte que si elle sera mieux faite plus tard, avec plus de
moyens ; tu me le dis avant, avec la raison, et c'est moi qui tranche.

HORS PÉRIMÈTRE : ASIO (après la S26), la synthèse de sons neufs (la
version « synthétiser » du kit), les fiches des VST connus, la piste
compagne (elle reste sur sa branche), l'enregistrement du bend et de la
modulation, les 32 voix de 4OSC, l'arrangement guidé par les sections, le
push-to-talk (S25), le piano-roll, le workspace découverte.

CRITÈRES DE RÉUSSITE, tous prouvables par la machine :
- La fenêtre « Audio » : un tampon changé est celui que la carte ouvre
  (relu au moteur) ; la latence affichée est celle que --verify-jeu
  mesure ; --verify-lecture reste à 0 sur 75 après le changement ; une
  vérification laisse mes réglages audio intacts.
- Les effets : chaque geste de la tranche et du graphe produit la commande
  attendue ; le contournement est vérifié au rendu.
- Le flux : un projet construit donne les nœuds et les liens attendus ; la
  forme d'onde d'un nœud est celle du rendu à cet endroit, mesurée ; un
  effet contourné donne la même forme avant et après ; le coût processeur
  des prises est mesuré et donné ; aucun bloc audio n'attend l'écran.
- Le kit : sur une bibliothèque construite, le kit choisi respecte ses
  contraintes mesurées (808 à la tonalité à quelques cents, pas de
  recouvrement kick / 808) ; le même choix à entrée égale ; le poser est
  un seul Ctrl+Z, à l'octet.
- Les bus : sur six pistes portant la même
  réverbération, la proposition est faite, essayée à blanc, gardée en un
  groupe ; le rendu avant / après est mesuré.
- --verify complet à zéro échec, --verify-lecture à 0 sur 75.

MÉTHODE :
- Chantier 0 : tu codes, tu expliques après. Chantier 1 : exposé et validé
  avant toute ligne de code.
- Un test qui interroge l'état ne prouve pas l'effet : une forme d'onde se
  prouve contre le rendu, pas contre un compteur.
- Un test neuf qui passe du premier coup est cassé une fois.
- Une cause non trouvée se dit « non trouvée ».
- scripts/check-all.sh avant chaque commit. Zéro avertissement, -Werror.
  CI verte avant de pousser le suivant. Commits atomiques.
- Si un choix contredit l'acquis, dis-le au lieu de l'appliquer.
- IDEES.md se remplit, ne se met pas en œuvre.
- Concis.

LIVRABLE DE FIN : docs/bilan-s24.md au format habituel : mes décisions de
modèle recopiées avec leur date, la fenêtre « Audio » et la latence
mesurée, le graphe d'un projet type, le coût des prises, les mesures du kit
et ce qu'« aller ensemble » veut dire, les bus intelligents, ce qui a été
reporté (s'il y en a) et pourquoi, les écarts à l'acquis. Sa section « À essayer, dans
l'ordre » va dans docs/essais-apres-s26.md. Mets à jour CLAUDE.md (cap,
décisions acquises, dettes) et docs/plan-s21-s26.md si la semaine déplace
quelque chose.

Commence par le chantier 0, puis expose le modèle.
