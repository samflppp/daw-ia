# Contexte produit et stratégique

Ce que le code et les bilans ne disent pas : l'objectif, le positionnement, la stratégie. Référence pour
toute session neuve, lue après `CLAUDE.md`.

## Objectif
- DAW de nouvelle génération, produit à vendre. Objectif n°1 : un MVP pour
  l'incubateur accessible via l'école. 3 ans d'études pour en profiter,
  visé : incubé d'ici 6 mois. Seul sur le projet.

## Règles cadres non négociables
- Ne PAS développer de moteur audio. Le DAW est bâti sur Tracktion Engine.
- Multi-workspace conservé (interface reconfigurable selon le profil).
- Windows seule plateforme cible du MVP (S5). Le job CI Linux reste vert en
  compilation ; aucune validation runtime Linux. Portage repris après
  incubation.
- UI : JUCE natif, pas de webview.
- Plugins hébergés : VST3 + CLAP.
- Command Bus centralisé : toute mutation passe par des commandes
  sérialisables. Aucune commande n'engendre d'identifiant.
- Versioning : arrangement sérialisé et hashé.
- Workspaces : système déclaratif dès le départ (manifestes JSON).

## Positionnement
- Branding « DAW IA » : permettre à des personnes étrangères à la musique de
  composer. Approche progressive : produit pro comme fondation, un débutant
  atteint un niveau professionnel très vite (référence Higgsfield).
- Refus d'une bibliothèque de presets fermée. Conditionnement paramétrique :
  signets, axes continus, extraction depuis une référence utilisateur.
- Marketing : attirer avec l'IA, convertir avec la qualité de service.
- Trois workspaces dans un outil unique, sans complexité visible.
- Face à ACE Studio (noté le 3 octobre 2026) : eux vendent un atelier de voix et
  de génération qui se branche sur un DAW (pas de plugins tiers, mixer
  basique) ; DAW IA est le DAW lui-même, piloté par l'IA. On ne les suit pas
  fonction par fonction. Ce qui distingue : tout ce que l'IA fait reste
  modifiable, annulable, expliqué, et le cœur tourne en local, sans attente
  ni crédits.
- Audio généré : accepté comme matière (timbre depuis des notes, coup de
  batterie, bruitage, voix depuis notes et paroles), jamais comme morceau
  fini ni comme boucle sans notes. Détail dans IDEES.md.

## Stratégie IA
- Deux couches : copilote (mixage, routing, paramètres via le Command Bus) et
  moteur génératif. Copilote d'abord.
- Inférence hybride : modèles locaux pour le temps réel, API distante pour
  l'asynchrone lourd, orchestrateur transparent.
- Licences « Flex-Model » : fonctionnalités API → abonnement ou crédits ;
  modèles locaux → achat one-shot. Le code doit permettre les deux.
- Modèle économique non tranché.

## Workspaces visés
- Musique de film : sync SMPTE, pistes orchestrales.
- Beatmaker rap : step-sequencer, drum rack, groove.
- Pub / UGC : formats courts, 9:16, export multi-stems.

Découverte n'est pas un workspace du logiciel : sorti le 3 octobre 2026, c'est un outil pour les ateliers de
présentation, développé plus tard. Son manifeste reste dans `workspaces/`, marqué réservé aux ateliers ; le
logiciel que tout le monde utilise ne le montre pas.

## Idées en réserve (non tranchées)
- Séparation de stems intégrée à la piste ; Bass & Groove Engine (808
  accordées au sample) ; traitement vocal IA ; scoring émotionnel ;
  auto-arrangement pub 15/30/60 s préservant l'arc narratif.
- L'utilisateur crée son propre plugin VST par IA depuis le logiciel.
- L'IA propose une structure d'arrangement une fois les patterns créés.
- Le DAW apprend les plugins souvent ouverts et les propose en début de
  session.
- Intégrer les outils de carrière du beatmaker (YouTube, mails, loop kits) —
  envisagé, non tranché.

## Méthode de travail

Décisions du fondateur, 3 octobre 2026 :

- **D. Un excellent logiciel, même si ça prend plus de temps et de moyens.** Quand une chose serait mieux faite
  plus tard, avec plus de moyens, on la reporte au lieu de la faire au rabais pour tenir une semaine.
  - Le fine-tune sort des 26 semaines : il se fera pendant l'incubation, avec de vraies machines et un jeu de
    données plus grand. La machine du fondateur (portable i5-8365U, sans carte graphique) n'entraîne pas un
    modèle de 421 M de paramètres, et un fine-tune bricolé ne vaut pas le détour. D'ici là, le journal continue
    de constituer le jeu de données ; ses lignes ne sont jamais purgées.
  - Une semaine trop chargée perd un chantier, elle ne le bâcle pas : dire ce qui tombe, et où il va.
  - Entre deux voies, recommander la meilleure pour le produit, et dire franchement ce qu'elle coûte en temps
    et en moyens. Le coût ne disqualifie pas une voie ; le fondateur tranche.
  - Ne contredit pas la décision B : la S26 livre un prototype aux fondations bien faites, pas un produit poli.
- **A. On construit les outils, puis on les essaie par phases.** Les 26 semaines servent à développer. Les
  essais du fondateur, à la main et à l'oreille, viennent après la S26, dans une phase consacrée à
  l'ergonomie.
  - Aucune semaine n'attend un essai du fondateur pour avancer ; aucun brief ni bilan ne met son essai en
    condition bloquante.
  - Les bilans gardent leur section « À essayer, dans l'ordre » : elle alimente la phase d'essais, rassemblée
    dans `docs/essais-apres-s26.md`.
  - Le piano-roll reste jusqu'à cette phase ; personne n'y touche d'ici là.
  - Ce qui reste exigé pendant les 26 semaines : ce que la machine peut prouver (rendu mesuré, `--verify`,
    tests cassés une fois).
- **B. Le niveau visé à la S26 : un premier prototype, pas un produit fini.** La qualité « très premium », la
  chasse complète aux bugs et le polissage se feront dans l'incubateur, avec des professionnels.
  - Pas de semaine de gel : la S26 emballe le prototype, elle ne le certifie pas.
  - Une cause non trouvée se dit, se note dans les dettes, et n'arrête pas la semaine — sauf si elle empêche
    de montrer la fonctionnalité.

Règles tenues depuis la S1 :

- Exposer l'architecture et demander validation AVANT de coder.
- Un test qui interroge l'état ne prouve pas l'effet : mesurer au rendu.
- Un test neuf qui passe du premier coup est cassé une fois pour prouver
  qu'il regarde quelque chose.
- Dire quand un choix contredit l'acquis au lieu de l'appliquer.
- IDEES.md se remplit, ne se met pas en œuvre.
- Commits atomiques, aucun commit cassé, zéro avertissement, CI verte.
- Bilan hebdomadaire dans docs/bilan-sNN.md.

## Préférence de livrables
- Visuels en couleurs claires, sobres et élégantes. Pas de palette sombre.

## Planning
- Démarrage 15 septembre 2026, plan de 26 semaines, jalon go/no-go en S8
  (atteint). Comité visé en mars 2027.
- S18-S19 : ergonomie (zoom continu, navigation). S20 : le mixage par l'IA.
- S21 à S26 (détail dans `docs/plan-s21-s26.md`) : S21 solidité et fin de
  l'ergonomie ; S22 séparateur de stems et direction par références ; S23
  jouer au clavier MIDI et au clavier AZERTY ; S24 le kit et le mixer ; S25
  le DAW à la voix ; S26 le prototype emballé.
- Après la S26 : la phase d'essais et d'ergonomie (`docs/essais-apres-s26.md`).
  Pendant l'incubation : le fine-tune, la qualité premium.
