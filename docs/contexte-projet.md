# Contexte produit et stratégique

Consigne d'origine ayant produit `CLAUDE.md`, conservée telle quelle comme référence.

Pour le projet daw ia Écris un CLAUDE.md à la racine du dépôt. Il doit permettre à une session
Claude Code neuve, sans historique et sans mémoire, de reprendre le projet
sans rien redemander. Il contient :

1. Ce qu'est le projet en cinq lignes, et le cap (incubateur, comité mars
   2027, plan de 26 semaines).
2. Les décisions d'architecture acquises, avec la semaine où chacune a été
   prise et la raison. Celles à ne jamais rouvrir : Command Bus, aucune
   commande n'engendre d'identifiant, ProjectState seule vérité, projection
   par réconciliation liée par identité, règle de thread du bus, journal
   append-only, Pattern/Placement/Clip/Lane séparés, Windows seule cible.
3. Les invariants vérifiés automatiquement : les trois règles d'hygiène,
   check_hygiene.py, DawGuards.cmake, le test croisé registry ↔ table du
   copilote.
4. Les règles de méthode, telles qu'elles ont tenu dix-huit semaines :
   exposer avant de coder ; un test qui interroge l'état ne prouve pas
   l'effet ; un test neuf qui passe du premier coup est cassé une fois ;
   dire quand un choix contredit l'acquis ; check-all.sh avant chaque
   commit ; commits atomiques ; IDEES.md se remplit et ne se met pas en
   œuvre.
5. Où trouver quoi : les bilans dans docs/, IDEES.md, les presets CMake,
   les variantes de --verify, la clé d'API en variable d'environnement.
6. Les dettes ouvertes, avec leur semaine d'origine.
