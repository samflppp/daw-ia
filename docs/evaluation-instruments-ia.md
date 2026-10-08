# Les instruments IA — une évaluation écrite (S25)

*Rédigée le 7 octobre 2026. Pas de code : une décision à prendre pendant l'incubation.*

## 1. De quoi on parle

Un **instrument IA** rend le son d'une piste à partir des notes écrites dans le projet : un violon, un saxophone,
un ensemble de cordes « joués » par un modèle plutôt que par un sampler. Les notes restent dans le projet ; le son
rendu est de la **matière** (la règle de la S14, reformulée le 3 octobre 2026 : « l'audio généré est accepté comme
matière, jamais comme morceau fini »). Ce n'est donc ni de la génération de morceau (Suno, Udio), ni une conversion
de voix (Kits.ai), ni un instrument virtuel classique : la partition est la tienne, le modèle n'en change que le
timbre et l'interprétation.

Comment il entrerait dans DAW IA, quel que soit le modèle :

- **Un rendu hors ligne par pattern, mis en cache**, rangé sous l'empreinte des notes, du tempo, de l'instrument
  et de la signature du modèle — comme les stems de la S22 (`StemSeparation::cacheFolder`). Une note qui change
  invalide le rendu de ce pattern, et lui seul.
- **Le projet ne garde que les notes et le choix de l'instrument.** Le rendu est dans le cache de la machine, jamais
  dans `ProjectState` (comme l'index des samples). Un projet ouvert sans le modèle joue les notes avec l'instrument
  de repli (4OSC aujourd'hui) et le dit ; il ne casse pas.
- **Jamais sur le fil audio.** Le rendu se fait dans un autre processus (le service Python), comme la séparation. La
  lecture lit un fichier, comme un clip audio.
- **La boucle d'écoute** : écrire une note, entendre le rendu quelques secondes plus tard. C'est ce délai qui décide
  si l'outil sert pendant la composition ou seulement au « bounce ».

## 2. Ce qui existe, vérifié à la source

| Modèle ou service | Ce qu'il fait | Licence du code | Licence des poids ou conditions | Où il tourne | État |
|---|---|---|---|---|---|
| **Magenta RealTime 2** (Google DeepMind, 2026) | Modèle de musique « live » : contexte audio, style (texte ou audio), et **un vecteur MIDI par trame de 40 ms** (chaque hauteur : éteinte, tenue, attaque, ou « le modèle décide ») | Apache-2.0 ([GitHub](https://github.com/magenta/magenta-realtime)) | **CC-BY-4.0**, « Google claims no rights in outputs » ([carte](https://huggingface.co/google/magenta-realtime-2)) ; entraîné sur ~71 000 h de musique de stock, droits non détaillés | Apple Silicon en temps réel ; GPU NVIDIA hors temps réel ; **le CPU n'est pas mentionné** | Actif |
| **MIDI-DDSP** (Magenta, 2022) | Notes → interprétation (vibrato, dynamique) → synthèse DDSP ; 13 instruments d'orchestre (URMP : violon, alto, violoncelle, contrebasse, flûte, hautbois, clarinette, saxophone, basson, trompette, cor, trombone, tuba) | Apache-2.0 ([GitHub](https://github.com/magenta/midi-ddsp)) | Poids téléchargeables, **licence non indiquée** | TensorFlow 2.7, Python 3.8 | **Archivé le 1er février 2024** |
| **Spectrogram Diffusion** (Magenta, ISMIR 2022) | MIDI multi-instruments → spectrogramme (transformeur + diffusion) → audio | Apache-2.0 ([GitHub](https://github.com/magenta/music-spectrogram-diffusion)) | Points de contrôle téléchargeables, **licence non indiquée** | « environ 5× plus lent que le temps réel » sur TPU Colab | **Archivé le 6 janvier 2026** |
| **ACE Studio** (AI Violin, saxophone, trompette, cordes) | Notes → interprétation réaliste, dans leur application | Fermé | **Conditions d'utilisation non trouvées à la source** : seule la page d'accueil répond (« Royalty-Free », musiciens rémunérés à l'usage) ; rendu dans leur cloud | Leur application, pas d'API publique trouvée | Commercial, abonnement (≈ 16,58 à 22 $/mois selon des revendeurs, non vérifié chez eux) |
| **RAVE** (IRCAM) / **Neutone** | Transfert de timbre audio → audio en temps réel : il faut d'abord rendre les notes avec un synthé, puis transformer | **CC BY-NC 4.0** ([LICENSE](https://github.com/acids-ircam/RAVE)) : **pas d'usage commercial** | Par modèle (navigateur de Neutone), à vérifier un par un | CPU, temps réel | Actif |
| Synthesizer V, Vocaloid | Voix chantées à partir de notes et de paroles | Fermé | EULA par banque de voix | Local | Hors sujet (des voix, pas des instruments) |

*Sources lues le 7 octobre 2026. Les licences « non indiquées » le sont sur la page du dépôt ; aucune n'est à
supposer ouverte.*

**Ce qu'on en entend.** Rien d'écouté cette semaine, et pas d'avis de seconde main rapporté ici : les démos se
jugent à l'oreille, et ton oreille est en phase d'essais (CLAUDE.md §4). Les démos publiées à écouter, dans cet
ordre, sont ajoutées à `docs/essais-apres-s26.md` : Magenta RealTime 2 (une partition donnée, pas un jam), ACE
Studio (le violon solo, comme étalon commercial), MIDI-DDSP (les solos d'orchestre).

## 3. Latence et coût, ce qu'on peut en dire sans mesurer

- **Sur ton i5-8365U sans carte graphique**, aucun de ces modèles n'est annoncé en temps réel. MRT2 et Spectrogram
  Diffusion sont faits pour l'accélérateur ; MIDI-DDSP est plus léger (DDSP est un synthé différentiable) mais son
  étage d'interprétation est un réseau récurrent. Un rendu hors ligne de 8 mesures se compterait en secondes à
  dizaines de secondes, à mesurer.
- **ACE Studio** : coût d'abonnement par utilisateur, rendu dans leur cloud (la partition quitte la machine), et
  aucune API trouvée : il ne s'intègre pas à DAW IA, il se place à côté.
- **Le coût d'intégration** dans DAW IA est le même pour tous les modèles locaux : un rendu par pattern dans le
  service, un cache, une piste qui lit un fichier. C'est la forme des stems ; environ une semaine de travail bien
  faite, plus le modèle lui-même.

## 4. Recommandation

**Ne rien intégrer pendant les 26 semaines, et ne pas construire sur MIDI-DDSP ni Spectrogram Diffusion** : archivés,
poids sans licence écrite, TensorFlow ancien. Ce serait refaire l'erreur des poids HTDemucs « for research purpose »
(bilan S22), en pire.

**Pendant l'incubation, la piste qui vaut l'essai est Magenta RealTime 2** :

1. Le seul modèle actif, aux poids **CC-BY-4.0** et sans droit revendiqué sur les sorties, qui prend des notes en
   entrée.
2. Un essai d'une semaine : 8 mesures de violon écrites, rendues par `mrt2_small` avec un style « solo violin »,
   **sur une machine à GPU NVIDIA** (location à l'heure, quelques euros), et la mesure de ce qui compte : est-ce que
   les notes rendues sont **les notes écrites** (transcrire le rendu en notes, comparer hauteur et attaque) ?
   Un instrument qui joue autre chose que la partition ne respecte pas la règle « matière, jamais morceau fini ».
3. Si les notes tiennent : le rendu par pattern en cache décrit au §1, local quand la machine a un GPU, et la voie
   « API en crédits » du produit pour les autres (un MRT2 hébergé par nous, puisque les poids le permettent).

**Ce que ça demanderait** : une semaine d'essai (GPU loué, ~10 à 30 € de calcul), puis environ deux semaines
d'intégration bien faite (service, cache, invalidation, piste, repli sans modèle, vérification). En parallèle, une
écoute d'ACE Studio comme étalon de qualité : si rien d'ouvert n'en approche, le dire, plutôt que livrer un violon
qui sonne faux.

**À trancher par toi** : l'essai MRT2 pendant l'incubation, ou rien sur les instruments IA avant le comité.
