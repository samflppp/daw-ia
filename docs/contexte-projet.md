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
- S18-S19 : ergonomie (zoom continu, navigation). Puis fonctionnalités IA.
  Puis fine-tune du modèle.
