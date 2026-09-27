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

Écarté d'emblée : la génération audio (Suno, Udio, Stability). Elle produit du fini, pas de la
matière éditable — ni BPM exact, ni grille, ni stems, ni notes déplaçables. Incompatible avec la
promesse de contrôle chirurgical.

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
