# Licences des dépendances de DAW IA

Ce document recense chaque logiciel ou modèle tiers que DAW IA livre, ou fait télécharger par la machine de la
personne, avec sa licence lue à la source (lien et date de lecture). Pour chacun, il dit ce que le texte impose pour
**distribuer un logiciel au code fermé, puis le vendre**. Il ne fait pas d'interprétation juridique au-delà du
texte : ce qui demande l'avis d'un juriste est marqué **⚖ juriste**.

Version concernée : DAW IA 0.1.0-alpha (prototype de la S26), dépôt `samflppp/daw-ia`. Toutes les lectures
datent du **9 octobre 2026**, sauf mention contraire. Les versions sont celles du dépôt à cette date : sous-modules
de `external/`, `cmake/DawThirdParty.cmake`, `services/uv.lock`.

## 1. En bref

- **Rien n'interdit de distribuer ce prototype gratuitement, au code fermé.**
- **Deux licences commerciales gratuites sous seuil** portent le produit : JUCE (Starter, **20 000 $** de revenus
  ou de financement sur 12 mois) et Tracktion Engine (Personal, **50 000 $** reçus, financement compris, et une
  mention « Powered by Tracktion Engine » obligatoire). Le jour où DAW IA reçoit davantage, elles deviennent
  payantes (§2.1, §2.2). La seule autre voie, sous AGPLv3 et GPLv3, obligerait à publier le code de DAW IA.
- **Une bibliothèque sous LGPL était compilée dans le binaire** : SoundTouch, l'étireur de Tracktion. Liée
  statiquement dans un binaire au code fermé, elle obligeait à livrer de quoi la relier. Elle est retirée
  (commit `b533ec4`, prouvé sans effet sur le son).
- **Ce qui est livré** (le programme, Python et son environnement de base, uv) est sous licences permissives :
  MIT, BSD, Apache-2.0, PSF, ISC, zlib, Boost, CC0, domaine public. Elles demandent de reproduire leurs mentions.
- **Ce qui se télécharge au premier usage** (PyTorch, demucs, sherpa-onnx, les poids) vient directement de sa
  source officielle sur la machine de la personne. DAW IA ne le redistribue pas. Deux cas demandent un juriste :
  **les poids HTDemucs**, sans licence affichée, et la vente d'un produit qui les fait télécharger.

## 2. Le programme (`DAW IA.exe`, lié statiquement)

| Dépendance | Version | Licence | Source lue | Pour distribuer au code fermé et vendre |
|---|---|---|---|---|
| **JUCE** | 8.0.13 (+7, `37c894f83d`) | AGPLv3 **ou** licence JUCE 8 | `external/JUCE/LICENSE.md` ; [EULA JUCE 8](https://juce.com/legal/juce-8-licence/) | Licence JUCE 8, §2.1 |
| **Tracktion Engine** | v3.2.0 (+423, `00fe42753a9`) | GPLv3 **ou** licence commerciale Tracktion | `external/tracktion_engine/LICENSE.md` ; [plans](https://engine.tracktion.com) ; [contrat](https://engine.tracktion.com/agreement) | Licence Tracktion, §2.2 |
| SDK VST3 (Steinberg) | 3.8.1 build 84 | **MIT** | `external/vst3sdk/LICENSE.txt` | garder la mention de copyright et la licence |
| CLAP | 1.2.10 | MIT | `external/clap/LICENSE` | idem |
| BLAKE3 | 1.8.7 | CC0-1.0 **ou** Apache-2.0 (au choix) | `external/BLAKE3/LICENSE_CC0`, `LICENSE_A2` | sous CC0 : aucune obligation |
| nlohmann/json | 3.12.0 | MIT | `_deps/nlohmann_json-src/LICENSE.MIT` | mention |
| SQLite | 3.53.4 (amalgamation) | domaine public | [sqlite.org/copyright](https://www.sqlite.org/copyright.html) | aucune obligation écrite |

**Embarqué par JUCE et compilé** (`external/JUCE/LICENSE.md` liste chaque composant et son fichier de licence) :
FLAC (BSD), Ogg Vorbis (BSD), zlib (zlib), libpng (zlib), jpeglib (licence de l'Independent JPEG Group),
HarfBuzz (« Old MIT »), SheenBidi (Apache-2.0), le SDK VST3 de JUCE (MIT), les en-têtes LV2 (ISC, hébergement LV2
non activé). **Non compilés** : ASIO (`JUCE_ASIO` absent, donc 0), AAX, Oboe, l'AudioUnitSDK, le navigateur web
(`JUCE_WEB_BROWSER=0`), curl (`JUCE_USE_CURL=0`).

**Embarqué par Tracktion et compilé** (en-têtes des fichiers, lus dans `external/tracktion_engine/modules/`) :
CHOC (ISC), crill (Boost-1.0), expected (CC0), libsamplerate (BSD-2-Clause), magic_enum (MIT), rigtorp MPMCQueue
(MIT), rpmalloc (domaine public), farbot (MIT). **Non compilés** : SoundTouch (LGPL-2.1, retiré en S26), Elastique,
RubberBand, Signalsmith Stretch (MIT), AirWindows (`TRACKTION_AIR_WINDOWS=0`).

### 2.1 JUCE 8

- **Voie gratuite au code fermé : « Starter »** (§1.2). Gratuite jusqu'à **20 000 $ de revenus ou de financement
  sur 12 mois glissants**.
- §1.2.1 : pour une société, le seuil compte « the total revenue or funding received by the entity and all its
  Affiliates », que ce soit en lien avec JUCE ou non, sans déduction. Pour une personne : le revenu ou le
  financement tiré de son usage de JUCE.
- **Au-delà** : Indie (jusqu'à 300 000 $) à 800 $ perpétuel ou 40 $/mois ; Pro (sans limite) à 3 500 $ ou 175 $/mois.
- Aucune obligation d'écran de démarrage ni de mention dans le produit dans l'EULA lue. §1.17 interdit de
  distribuer le framework seul ; §2.3 interdit de le placer sous une licence libre.
- **⚖ juriste** : une bourse, un prêt d'honneur ou une levée de l'incubation comptent-ils dans les 20 000 $ ? Le
  texte dit « funding », lié à JUCE ou non pour une société. La personne et sa future société sont-elles un même
  licencié ?

### 2.2 Tracktion Engine

- **Voie gratuite au code fermé : « Personal »**. Gratuite tant que le revenu « generated or raised » reste sous
  **50 000 $** ; §12.15 : « any and all revenue raised, donated towards, earned, or otherwise received ».
- **Mention obligatoire** pour Personal, Indie et Education (§1.8) : « Powered by Tracktion Engine ». Le contrat ne
  dit pas où ; DAW IA la met dans l'écran « À propos » et dans l'installeur.
- **Au-delà** : Indie 35 $/poste/mois jusqu'à 200 000 $ (engagement de 12 mois, mention obligatoire) ; Pro 1
  50 $/poste/mois jusqu'à 400 000 $ (mention facultative) ; Pro 2 et 3, Enterprise au-dessus.
- §1.6 : au-delà du seuil sans plan payant, c'est la GPLv3 qui s'applique, et elle oblige à publier le code.
- §10.4 : sur demande, fournir ses comptes sous 30 jours ; Tracktion peut auditer.
- **Inscription au plan Personal** : le fondateur s'y inscrit (décision du 9 octobre 2026).
- **⚖ juriste** : la même question que pour JUCE sur le financement de l'incubation.
- 4OSC, l'instrument du projet de démonstration, fait partie de Tracktion Engine : il est couvert par cette licence.

## 3. Livré avec l'application : Python et les services

| Pièce | Version | Licence | Source lue | Obligations |
|---|---|---|---|---|
| CPython (python-build-standalone) | 3.12 | PSF-2.0 | `LICENSE.txt` de CPython ; [python-build-standalone, « Licensing »](https://gregoryszorc.com/docs/python-build-standalone/main/running.html) | garder la licence PSF et le copyright. La distribution est construite **sans readline ni GDBM** (GPLv3) ; elle porte OpenSSL (Apache-2.0), libffi (MIT), zlib, bzip2, xz, SQLite, Tcl/Tk et mpdecimal (permissives) — **liste à recopier depuis l'archive livrée à l'étape 14** |
| uv | 0.12.15 | MIT **ou** Apache-2.0 | métadonnées du paquet ; [astral-sh/uv](https://github.com/astral-sh/uv) | mention |
| numpy | 2.5.3 | BSD-3-Clause, avec ce qu'il embarque | `numpy-2.5.3.dist-info/licenses/LICENSE.txt` | mention. Embarque OpenBLAS (BSD-3), LAPACK (BSD-3-Open-MPI) et la **bibliothèque d'exécution de GCC** (GPL-3.0+ **avec l'exception GCC 3.1**, qui permet de l'embarquer dans un logiciel fermé) |
| `daw-services` | 0.1.0 | code de DAW IA | — | — |

Le groupe de développement (`pytest`, `ruff`, `jiwer`, `text2num`) n'est **pas** livré (`uv sync --no-dev`).

## 4. Téléchargé au premier usage, depuis sa source officielle

DAW IA ne livre pas ce qui suit : la machine de la personne le télécharge depuis PyPI, le dépôt de PyTorch, GitHub
ou Hugging Face, quand elle demande la fonction.

### 4.1 La voix (« Installer la voix »)

| Pièce | Version | Licence | Source lue | Notes |
|---|---|---|---|---|
| sherpa-onnx, sherpa-onnx-core | 1.13.8 | Apache-2.0 | métadonnées ; [k2-fsa/sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) | embarque **onnxruntime** (MIT) |
| **Parakeet TDT 0.6B v3** (poids NVIDIA, quantifiés en int8 par sherpa-onnx) | — | **CC-BY-4.0** | [fiche du modèle](https://huggingface.co/nvidia/parakeet-tdt-0.6b-v3) : « ready for commercial/non-commercial use » | **attribution** : citer NVIDIA, la licence avec son lien, et la modification (quantifié en int8). Fait dans « À propos » |

### 4.2 Les stems (première séparation)

| Pièce | Version | Licence | Notes |
|---|---|---|---|
| PyTorch (CPU) | 2.14.1 | licence de type BSD-3 ; l'ensemble déclaré : Apache-2.0 (dont avec l'exception LLVM), BSD-2, BSD-3, BSL-1.0, MIT | `torch-2.14.1+cpu.dist-info/licenses/` : la licence et une quarantaine de composants tiers (sleef, XNNPACK, fbgemm, oneDNN/ideep, protobuf, pybind11…) |
| torchaudio | 2.11.0 | BSD | |
| demucs | 4.1.0 | **MIT (le code)** | [facebookresearch/demucs](https://github.com/facebookresearch/demucs), archivé le 1er janvier 2025 ; maintenu sur [adefossez/demucs](https://github.com/adefossez/demucs) |
| **poids HTDemucs** (`htdemucs`, `htdemucs_ft`) | — | **aucune licence affichée** | [huggingface.co/adefossez/HTDemucs](https://huggingface.co/adefossez/HTDemucs) : aucun champ de licence. L'auteur les a dits « for research purpose » (issues #267, #327, #508 de demucs) et a retiré la mention MIT de leur fiche le 31 août 2026 (bilan S22) |
| lameenc | 1.8.4 | **LGPL-3.0-or-later** | dépendance de demucs ; embarque LAME |
| soundfile | 0.14.0 | BSD-3 ; embarque **libsndfile** (LGPL-2.1) en DLL | DLL chargée dynamiquement, téléchargée depuis PyPI |
| sphn | 0.2.1 | Apache-2.0 | dépendance de demucs |
| huggingface-hub, hf-xet, safetensors | 2.1.1 / 1.6.0 / 0.8.0 | Apache-2.0 | |
| einops, julius, PyYAML, filelock, typing-extensions, sympy, mpmath, networkx, Jinja2, MarkupSafe, fsspec, setuptools, tqdm, httpx2, httpcore2, anyio, idna, h11, truststore, click, colorama, packaging, cffi, pycparser | versions de `uv.lock` | MIT, BSD, Apache-2.0, PSF-2.0, MIT-0 ; **tqdm : MPL-2.0 et MIT** | métadonnées des paquets |

## 5. Les services en ligne

| Service | Conditions | Source lue | Notes |
|---|---|---|---|
| API d'Anthropic (copilote, mixage par le modèle) | Commercial Terms, en vigueur le 17 juin 2025 | [anthropic.com/legal/commercial-terms](https://www.anthropic.com/legal/commercial-terms) | A.1 : un client peut s'en servir pour des produits offerts à ses utilisateurs. D.3 : prévenir les utilisateurs que les réponses peuvent être inexactes. D.4 : pas de produit concurrent. La clé est celle de la personne : **⚖ juriste** pour le modèle où chacun apporte sa clé (H.1 : les frais sont au titulaire du compte) |

## 6. Les outils de fabrication

| Outil | Licence | Source lue | Notes |
|---|---|---|---|
| Inno Setup 6 | licence propre, usage commercial permis, gratuit | [jrsoftware.org/files/is/license.txt](https://jrsoftware.org/files/is/license.txt) | ses conditions portent sur la redistribution d'Inno Setup lui-même ; une mention dans la documentation est « appreciated but is not required » |
| doctest | MIT | `_deps/doctest-src` | tests seulement, jamais livré |

## 7. Ce que l'ensemble impose pour vendre DAW IA au code fermé

1. **Une licence JUCE payante dès 20 000 $ reçus sur 12 mois**, financement compris, et une licence Tracktion
   payante dès 50 000 $. En dessous, les deux sont gratuites, avec la mention « Powered by Tracktion Engine ».
2. **Reproduire les mentions** des licences MIT, BSD, Apache-2.0, ISC, zlib, Boost, PSF et IJG dans ce qui est
   livré : le fichier de licences installé avec l'application et ouvert depuis « À propos ».
3. **Citer NVIDIA** pour Parakeet (CC-BY-4.0).
4. **Aucune bibliothèque sous GPL ou LGPL** n'est liée au binaire ni livrée par l'installeur (SoundTouch retiré,
   CPython sans readline ni GDBM, l'exécution de GCC dans numpy sous exception).
5. **Aucune obligation de publier le code de DAW IA**, tant que les licences commerciales de JUCE et de Tracktion
   couvrent le revenu et le financement.

## 8. Pour un juriste

1. **Les seuils de JUCE et de Tracktion face au financement de l'incubation** (§2.1, §2.2) : une subvention ou un
   prêt d'honneur comptent-ils ? À partir de quand une personne et sa société sont-elles le même licencié ?
2. **Les poids HTDemucs** : aucune licence n'est affichée et l'auteur les dit « for research purpose ». Un produit
   vendu qui les fait télécharger et s'en sert peut-il le faire ? Sinon, quelle alternative (SCNet, un modèle sous
   licence explicite, une licence demandée à l'auteur) ?
3. **Les paquets sous LGPL téléchargés au premier usage** (lameenc, libsndfile par soundfile) : DAW IA les fait
   installer sans les livrer. Les obligations de la LGPL pèsent-elles sur DAW IA ?
4. **L'API d'Anthropic avec la clé de chaque utilisateur** : qui est le « Customer » des Commercial Terms, et que
   doit dire DAW IA à ses utilisateurs ?
5. **Le morceau de démonstration** (*addiction*, 2021) : son nom cite lawone et crvkit. Les droits de chaque
   co-auteur, et la licence de tout sample qu'il contient, doivent couvrir sa livraison dans l'installeur.
