# S26 — le modèle de l'emballage, exposé avant toute ligne

Exposé le 9 octobre 2026, chantier 1 du brief de la S26. Rien de ce qui suit n'est codé. Chaque point finit
sur ce que je recommande et ce que ça coûte ; les décisions à prendre sont rassemblées au §8.

## 0. D'abord : ce qui peut bloquer la distribution

**Aucune dépendance n'interdit de distribuer ce prototype gratuitement, au code fermé.** Deux licences
posent des conditions qui comptent le *financement*, pas seulement le chiffre d'affaires (lues le
9 octobre 2026) :

| Dépendance | Voie gratuite au code fermé | Seuil | Au-delà |
|---|---|---|---|
| JUCE 8 ([EULA](https://juce.com/legal/juce-8-licence/), §1.2) | « Starter », gratuite | **20 000 $ de revenus *ou de financement* sur 12 mois glissants** (§1.2.1 : pour une société, tout ce qu'elle et ses affiliés reçoivent, lié à JUCE ou non) | Indie : 800 $ perpétuel ou 40 $/mois (jusqu'à 300 000 $) ; Pro : 3 500 $ ou 175 $/mois |
| Tracktion Engine ([plans](https://engine.tracktion.com), [contrat](https://engine.tracktion.com/agreement)) | « Personal », gratuite | **50 000 $ « raised, donated, earned or otherwise received »** (§12.15) | Indie 35 $/poste/mois (200 000 $, engagement 12 mois) ; Pro 1 : 50 $/poste/mois |

- Tracktion Personal **impose une mention « Powered by Tracktion Engine »** (§1.8, l'emplacement n'est pas dit).
  Elle ira dans l'écran « À propos » et dans l'installeur.
- **Le jour où l'incubation apporte plus de 20 000 $** (bourse, prêt d'honneur, levée), JUCE Starter ne couvre
  plus : Indie (800 $ une fois) devient nécessaire. Au-delà de 50 000 $, Tracktion Indie (420 $/an par poste).
  **À porter à un juriste** : si une subvention non liée au produit compte (le texte de JUCE dit « whether
  it be received in connection with the entity's use of the Framework or not »), et si une personne physique
  et sa future société sont un même licencié.
- **Les poids HTDemucs** (« for research purpose », bilan S22) ne sont pas livrés : la machine de la personne
  les télécharge. Que l'application les fasse télécharger et s'en serve pour un usage commercial est **une
  question pour un juriste** ; elle ne bloque pas une démonstration gratuite.

Le détail de chaque dépendance ira dans `docs/licences-tierces.md` (point 10, le premier construit).

## 1. L'installeur (point 4)

### L'outil : Inno Setup 6, par utilisateur, sans droits d'administrateur

| Outil | Par utilisateur sans admin | Questions à la désinstallation | Contre |
|---|---|---|---|
| **Inno Setup 6** | oui (`PrivilegesRequired=lowest`, installe dans `%LOCALAPPDATA%\Programs\DAW IA`) | oui (script Pascal : cases à cocher, tailles affichées) | un `.exe` d'installation, pas un `.msi` (les parcs d'entreprise préfèrent le `.msi`) |
| WiX (MSI) | possible, mal commode (`ALLUSERS=2`, `MSIINSTALLPERUSER`) | non : une désinstallation MSI n'a pas d'interface | deux à trois fois plus long à écrire |
| MSIX | oui | non | conteneur : `AppData` redirigé, écrire à côté de l'application interdit (les extras Python ne s'installeraient pas) ; **même installé à la main, un MSIX doit être signé**, auto-signé il demande d'importer un certificat |

**Recommandé : Inno Setup.** Gratuit, usage commercial permis ([licence](https://jrsoftware.org/files/is/license.txt),
lue le 9 octobre 2026 : seule obligation, garder ses mentions dans Inno Setup lui-même). Le compilateur
(`ISCC.exe`) se lance en ligne de commande depuis `scripts/package.cmd`. Coût : 0 €, environ une journée pour
l'installeur et sa désinstallation. Il s'installe sur ta machine (~10 Mo).

### Ce qu'il emporte

| Pièce | Taille installée (estimée, mesurée à l'étape 14) | Pourquoi |
|---|---|---|
| `DAW IA.exe` | 24 Mo (mesuré) | le runtime C++ est lié statiquement (`/MT`, `CMakeLists.txt`) : **pas de Visual C++ Redistributable** |
| Python 3.12 autonome (python-build-standalone, celui qu'uv installe) | ~60 Mo | la machine vierge n'a pas Python ; il porte ses propres DLL du runtime |
| `uv.exe` | 41 Mo (mesuré) | installe les extras au premier usage, depuis le même `uv.lock` que la CI : ce qui est éprouvé est ce qui s'installe |
| `services/` (source, `uv.lock`) et son environnement de base (numpy) déjà construit | ~60 Mo | le copilote démarre sans réseau ni installation |
| Projet de démonstration et sa petite bibliothèque de samples | ~30 Mo | §5 |
| `licences-tierces`, mentions | <1 Mo | §6 |

**Installeur estimé à 70–90 Mo** (compressé LZMA), **~250 Mo installés**.

### Ce qui se télécharge ensuite, fonction par fonction

| Fonction | Au premier usage | Téléchargé | Sur le disque |
|---|---|---|---|
| Toile, jeu, mixer, flux, kit, bus, mixage par les règles, génération locale | rien | 0 | 0 |
| Copilote, mixage par le modèle | une clé d'API (§3) | 0 | 0 |
| Voix | « Installer la voix » | sherpa-onnx ~28 Mo + Parakeet **487 Mo** | ~670 Mo |
| Stems (rapide) | première séparation | PyTorch CPU + demucs ~**500 Mo** + poids htdemucs ~80 Mo | ~1,6 Go |
| Stems (meilleure) | première séparation « meilleure » | poids htdemucs_ft ~320 Mo | +0,3 Go |

### Où vont les extras, et sans réseau

- **L'environnement Python** vit dans le dossier de l'application (`%LOCALAPPDATA%\Programs\DAW IA\services\.venv`,
  écrivable puisque l'installation est par utilisateur) ; les extras s'y ajoutent (`uv sync --frozen --inexact`).
  Ils partent avec l'application.
- **uv reçoit ses variables** : l'interpréteur livré (`UV_PYTHON`), aucun téléchargement de Python
  (`UV_PYTHON_DOWNLOADS=never`), et **son cache dans notre dossier, vidé après chaque installation** — sinon
  PyTorch resterait une seconde fois (~500 Mo) dans `%LOCALAPPDATA%\uv\cache`, hors de notre désinstallation.
- **Les poids** restent où ils sont (`%LOCALAPPDATA%\DAW IA\models`) : la désinstallation les demande.
- **Sans réseau** : l'application, le projet, le mixage par les règles, le jeu, le kit marchent. « Installer la
  voix » ou une séparation disent « pas de réseau : rien n'a été installé, réessaie une fois connecté » ; rien
  n'est à moitié installé (`uv sync` n'écrit qu'une fois tout résolu et téléchargé ; les poids de la voix sont
  déballés à côté puis déplacés, `parakeet.py`). Le copilote dit qu'il ne joint pas le service. **Prouvé par une
  vérification** : un proxy injoignable (`HTTPS_PROXY=http://127.0.0.1:9`) pour le processus.

### La signature du code

| Voie | Prix | Délai | Pour toi aujourd'hui | Ce que voit la personne |
|---|---|---|---|---|
| **Sans** | 0 | — | oui | le navigateur : « n'est pas couramment téléchargé » ; au lancement, SmartScreen « Windows a protégé votre ordinateur », éditeur inconnu ; il faut « Informations complémentaires » puis « Exécuter quand même » |
| Certificat OV (DigiCert, Sectigo…) | 150–300 $/an + jeton matériel (~120 $) | quelques jours de vérification d'identité | oui, à ton nom de personne | ton nom comme éditeur ; **l'avertissement reste** tant que la réputation n'est pas faite (des téléchargements) |
| Azure Artifact Signing | ~9,99 $/mois | quelques jours | **non** : une personne doit vivre aux États-Unis ou au Canada ; une **société** de l'UE y a droit | idem OV, sans jeton |
| Certificat EV | 400 $+/an | | | plus aucun avantage sur SmartScreen depuis 2024 |

Source : [Microsoft, Code signing options](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options),
mise à jour le 29 août 2026, lue le 9 octobre 2026.

**Recommandé : sans, et le dire** (une page « Installer DAW IA » qui montre les deux écrans et les deux clics).
**À la création de la société pendant l'incubation : Azure Artifact Signing** (120 $/an, branché sur la CI).
Aucun certificat ne fait disparaître l'avertissement du premier jour.

### La désinstallation

- **Part toujours** : le dossier de l'application (programme, Python, services, extras), le raccourci du menu
  Démarrer, l'entrée « Applications installées ».
- **Demandé, une case par ligne, taille affichée, décoché par défaut** : les poids des modèles
  (`%LOCALAPPDATA%\DAW IA\models`) ; les réglages de la machine (`%APPDATA%\DAW IA` : carte son, plugins
  scannés, micro, types de plugins, disposition, `daw.log`) ; l'index des samples (`samples-index.json`, dans le
  même dossier, case à part) ; la clé d'API rangée (§3).
- **Ne part jamais** : `Documents\DAW IA` (tes projets, et la copie du projet de démonstration), et tout projet
  ailleurs. La désinstallation ne regarde même pas ces dossiers.
- **Prouvé** sur la machine vierge : un projet créé, désinstallation, le projet est là et s'ouvre après réinstallation.

### Le numéro de version

Une seule source : `project(VERSION …)` du `CMakeLists.txt`, aujourd'hui **0.1.0**. Je propose **0.1.0** pour le
prototype de la S26, et le commit court (`99b1407`) à côté. Il se lit : écran « À propos », première ligne de
`daw.log` à chaque lancement, propriétés du `.exe` (onglet Détails), nom de l'installeur
(`DAW-IA-0.1.0-installation.exe`), « Applications installées » de Windows.

## 2. La machine vierge (point 5)

- **Ta machine** : Windows 10 **Professionnel** 22H2, virtualisation activée dans le micrologiciel, **aucun
  hyperviseur actif** : ni Windows Sandbox ni Hyper-V ne sont activés. Les activer demande tes droits
  d'administrateur et un redémarrage : **c'est ta main**, pas la mienne.
- **Windows Sandbox (recommandé)** : un Windows propre à chaque ouverture, du même build que le tien, jeté à la
  fermeture. Un script `.wsb` y monte l'installeur et un dossier de résultats, et lance l'épreuve tout seul.
  Place : ~1 à 2 Go pendant qu'il tourne, 4 Go de mémoire. Coût : 0 €.
- **Hyper-V ou VirtualBox** : une image de Windows 10 (~5,5 Go) et un disque virtuel de 20 à 40 Go.
  **Impossible aujourd'hui** : C: n'a que **8,6 Go libres** (98 % plein).
- **Il faut libérer de la place avant l'étape 14** : la construction de l'installeur et Sandbox demandent ~5 Go
  de marge. Ce qui est à moi et se reconstruit : `build/windows-msvc` (Debug, 7 Go), `build/domain-only` (1 Go).
  Je ne les efface pas sans ton accord.
- **Ce qui se prouve dans Sandbox** : l'installation sans droits d'administrateur, sans Python, uv ni runtime
  préinstallés ; le premier lancement ; le projet de démonstration ouvert ; le rendu hors ligne mesuré non muet ;
  `--verify-demo` ; un plantage et la reprise ; la désinstallation qui garde `Documents\DAW IA`. Sandbox a une
  sortie audio par défaut (celle de l'hôte) : la lecture en direct s'y essaie, sans valoir pour une vraie carte.
- **Ce qui ne s'y prouve pas** : une autre carte son, un casque Bluetooth, ASIO, le micro (désactivé par défaut),
  les temps (processeur et carte graphique partagés), SmartScreen tel qu'un inconnu le voit (Sandbox ne garde
  aucune réputation ; je pose la marque « téléchargé d'Internet » sur l'installeur pour voir l'écran), Windows 11.

## 3. La clé d'API et les deux licences (point 6)

- **Saisie** : là où elle manque. Le panneau du copilote sans clé dit « Pas de clé d'API : le copilote et le
  mixage par le modèle sont éteints ; tout le reste marche » et un bouton **« Saisir une clé… »** ; la même chose
  dans le menu Fichier. Une boîte : champ masqué, « Enregistrer », « Retirer la clé », un lien vers la console
  d'Anthropic. La clé est essayée tout de suite (une requête de 1 jeton) et la réponse dite.
- **Rangée** : dans le **Gestionnaire d'identification de Windows** (`CredWriteW`, identifiant générique
  `DAW IA/anthropic`, `CRED_PERSIST_LOCAL_MACHINE` : cette session Windows sur cette machine, pas d'itinérance),
  chiffré par Windows sous ta session (DPAPI). Jamais dans un fichier, jamais dans `daw.log`, jamais en argument
  de processus.
- **Ordre de lecture** : `DAW_IA_ANTHROPIC_API_KEY` d'abord (toi, la CI), puis le coffre.
- **Ce qui reste exposé, dit** : le processus Python du copilote reçoit la clé par son environnement (déjà le cas) ;
  un autre programme de la même session Windows peut la lire là. C'est la limite de toute clé utilisée en local.
- **Prouvé** : une vérification saisit une fausse clé engendrée, puis cherche ses octets dans `%APPDATA%\DAW IA`,
  `%LOCALAPPDATA%\DAW IA`, le dossier du projet, `%TEMP%` et `daw.log` : zéro occurrence ; la retire, le coffre ne
  l'a plus.
- **Sans clé** : marchent la toile, le jeu et l'enregistrement, le mixage par les règles (déjà le chemin sans clé
  de `--verify-mix`), la génération locale, les stems, la direction par références, la voix (la phrase s'affiche,
  et n'a nulle part où partir : elle le dit), le kit, les bus, le flux, la fenêtre « Audio ». Ne marchent pas :
  le copilote, le mixage par le modèle. Rien ne bloque : un bandeau discret, pas une boîte.
- **« Flex-Model », un seul endroit** : `core/app/Rights.h`, `bool Rights::allows(Feature, Route)` où
  `Feature` = copilote, mixage par le modèle, génération, stems, voix, kit… et `Route` = API (abonnement ou
  crédits) ou local (acheté une fois). **Il répond « oui » partout.** Chaque fonctionnalité le demande une fois, à
  son entrée ; sur un « non », elle dit « cette fonction n'est pas comprise dans ta licence » et rien ne change.
  Prouvé par une vérification qui le fait répondre « non » à tout : chaque fonctionnalité le dit, le projet à
  l'octet. Aucune facturation.

## 4. Les plantages (point 7)

- **Rattrapé** : un gestionnaire d'exception non gérée (`SetUnhandledExceptionFilter`, et `std::set_terminate`).
  Il écrit, sans allouer autant que possible : la version et le commit, le module fautif et son décalage, la pile
  (adresses ; les symboles se résolvent avec le PDB de cette version, **gardé par `package.cmd`, jamais livré**),
  la dernière commande appliquée (type, identifiant, heure) et le projet ouvert. Plus un minidump dans
  `%LOCALAPPDATA%\DAW IA\plantages\` (petit, sans le contenu de la mémoire). **Rien n'est envoyé.**
- **Ce qu'on perd au pire** : **rien de ce qui est passé par le bus** — chaque commande est écrite dans sa
  transaction SQLite avant de rendre la main (journal en WAL, `synchronous=NORMAL` : sûr contre un plantage de
  l'application ; une coupure de courant ou un plantage de Windows peut perdre les dernières transactions). Ce
  qui ne passe pas par le bus, et se perd, dit tel quel : **la prise en cours d'enregistrement** (elle devient des
  commandes à la fin, S23) ; **l'état interne d'un plugin tiers** changé dans sa fenêtre depuis la dernière
  capture (`plugin.capture_state` n'est pris qu'à l'enregistrement et à la fermeture ; ses paramètres exposés,
  eux, sont des commandes) ; une séparation ou une installation en cours ; le texte tapé et pas envoyé.
- **Au lancement suivant** : un marqueur « session ouverte » (le projet, l'heure), écrit au lancement et retiré à
  la fermeture normale. Présent : « DAW IA s'est fermé brutalement le 9 octobre à 14 h 32 (dans
  `ValhallaSupermassive.vst3`). Rouvrir « Mon morceau » là où il en était ? » — **Rouvrir** / **Rouvrir avec ce
  plugin contourné** (si le module fautif est un plugin : une commande `plugin.set_bypassed`, une entrée
  d'historique, un Ctrl+Z la défait) / **Non**.
- **Prouvé** : une option de vérification provoque un plantage après N commandes ; un second processus rouvre
  et compare `ProjectState` à l'octet avec celui d'avant le plantage, et lit `daw.log`.
- **Un service Python qui tombe** : le copilote le fait déjà (« Le copilote s'est arrêté. Le projet est intact. »,
  bouton « Relancer ») ; la voix aussi (« La reconnaissance vocale s'est arrêtée. ») ; la séparation le dit et
  n'écrit rien. À **prouver** : une vérification tue chaque processus (copilote, voix, séparation) pendant qu'il
  travaille ; le DAW reste debout, le dit, relance à la demande.
- **Un plugin qui plante** : Tracktion héberge les plugins **dans le processus** ; seul le **scan** est déjà fait
  dans un processus à part (`runAsPluginScannerIfAsked`). Ni Tracktion ni JUCE n'offrent l'hébergement isolé.
  **Recommandé pour le prototype** : dans le processus, le plugin fautif nommé et la réouverture avec ce plugin
  contourné ci-dessus. **L'hébergement isolé (un processus par plugin, comme Bitwig) va à l'incubation** : des
  semaines, une latence de plus, et le jeu en direct à repenser.

## 5. Le projet de démonstration et le parcours (point 8)

- **Les sons** : tout fait par le logiciel. Instruments : 4OSC (dans Tracktion Engine, sous sa licence) et le
  générateur ; effets internes (`daw.eq`, `daw.compressor`). **La batterie** : une trentaine de one-shots
  synthétisés par un script du dépôt (`tools/demo/`, numpy, déterministe : kick, 808 accordées, caisses claires,
  charleys, claps, en variantes de couleur), livrés comme petite bibliothèque pour que la page « Kit » ait à
  choisir. Aucun sample de ta bibliothèque, aucune chanson d'un tiers.
- **Pour les stems et la direction** : il faut un morceau mixé, avec une voix si la séparation doit en montrer
  une. Deux voies :
  - **A, fabriqué (recommandé)** : deux morceaux rendus par le DAW lui-même (batterie, basse, accords, une ligne
    principale), à deux tempos et deux tonalités. La séparation en montre quatre stems, dont « voix » vide ou
    presque : c'est dit à l'écran du parcours. La direction lit tempo, tonalité, sections : les nombres sont
    connus d'avance, donc vérifiables. Zéro question de licence.
  - **B, avec une vraie voix** : A, plus une a cappella sous **CC-BY** (ccMixter), vérifiée à la source, citée
    dans « À propos ». La séparation impressionne davantage ; une licence de plus à tenir, et la voix d'un tiers
    dans ta présentation.
- **Le parcours** (`docs/parcours-demonstration.md`) : dans l'ordre, environ dix minutes — ouvrir, jouer la
  toile ; jouer au clavier et enregistrer une prise ; le kit choisi ; le mixage (règles sans clé, modèle avec clé)
  écouté avant/après ; une référence et la direction ; séparer un morceau ; le flux audio et un bus intelligent ;
  parler au DAW ; Ctrl+Z qui défait tout. Chaque ligne : le geste, ce qui se voit, la fonction prouvée.
  **La veille de la présentation** : installer la voix et le séparateur (1,2 Go, une fois), écrit en tête.
- **`--verify-demo`** : le parcours joué par la machine sur l'application **installée**, sans clé (le copilote
  répond par une table, comme `--verify-voix`), la séparation par le séparateur factice de la CI, la voix par
  des fichiers du jeu d'essai donnés en argument (`--materiel <dossier>`, pris dans le dépôt : **rien de ce jeu
  n'est livré**, ses voix de synthèse viennent de Windows et leur redistribution n'est pas tranchée).

## 6. « À propos » et les licences des dépendances (point 9)

- **L'écran** : nom, version, commit, « Powered by Tracktion Engine », la citation de NVIDIA pour Parakeet
  (CC-BY-4.0 : auteur, licence, lien, modifications : quantifié en int8 par sherpa-onnx), les mentions des
  licences MIT, BSD et Apache des dépendances livrées, et un bouton qui ouvre le fichier des licences complet
  livré avec l'application.
- **`docs/licences-tierces.md`** : chaque dépendance livrée ou téléchargée — natif : JUCE (et ce qu'il embarque :
  FLAC, Ogg Vorbis, zlib, HarfBuzz, SheenBidi, ASIO non utilisé…), Tracktion Engine, SDK VST3 (MIT depuis 3.8 :
  celui du dépôt l'est, 3.8.1), CLAP (MIT), BLAKE3 (CC0 / Apache-2.0), nlohmann/json (MIT), SQLite (domaine
  public) ; Python : CPython (PSF), python-build-standalone et ce qu'il embarque (OpenSSL, etc.), uv, numpy,
  PyTorch, torchaudio, demucs et ses dépendances (dont `lameenc`, à lire : LAME est sous LGPL), sherpa-onnx ;
  poids : HTDemucs, Parakeet ; outils : Inno Setup. Pour chacune : la licence lue à la source (lien, date), ce
  qu'elle impose pour distribuer au code fermé puis vendre, et **« juriste »** là où le texte ne suffit pas.

## 7. Ce que le chantier 0 a trouvé, en bref

- **Les étapes 58 et 60** : pas une lecture muette. L'étape 58 lisait le vu-mètre au moment où elle arrivait ;
  au repos, à 0,69 temps (tempo 90), à 0,06 temps du moment où le kick s'est tu ; en S25, machine chargée, à 0,90
  et 0,93 temps. Corrigé et prouvé (`110972c`) : la lecture prise dans le temps fort ; un relevé pris exprès en fin
  de temps reproduit les trois échecs de la S25 à l'identique. Aucun commit n'en est la cause : la bissection
  n'avait rien à trouver.
- **Le micro absent ou refusé** : prouvé dans `--verify-voix` (`99b1407`), 215 sur 215, cassé une fois.
- **Le crédit de la clé d'API est épuisé** (« Your credit balance is too low ») : les 18 contrôles du `--verify`
  complet qui demandent au vrai copilote tombent. Le `--verify` complet à zéro échec attend du crédit.

## 8. À trancher par toi

1. **L'installeur** : Inno Setup, par utilisateur, sans droits d'administrateur. Oui ?
2. **La signature** : sans, et le dire ; Azure Artifact Signing à la création de la société. Oui ?
3. **La machine vierge** : activer Windows Sandbox (Fonctionnalités de Windows, droits d'administrateur,
   redémarrage) et libérer ~10 Go sur C:. Puis-je effacer `build/windows-msvc` (Debug, 7 Go, se reconstruit en
   ~40 min) ?
4. **Le morceau des stems et de la direction** : A (fabriqué) ou B (avec une a cappella CC-BY) ?
5. **Le crédit de l'API** : recharger pour le `--verify` complet (chaque passage appelle le vrai copilote une
   vingtaine de fois) ?
6. **Les licences** : t'inscrire au plan Personal de Tracktion Engine (gratuit) à ton nom ; et le seuil de 20 000 $
   de JUCE à garder en tête avant tout financement.
7. **La version** : 0.1.0 pour le prototype ?
8. **Un plugin qui plante** : dans le processus, nommé, réouverture avec ce plugin contourné ; l'isolement à
   l'incubation. Oui ?

## 9. Tes réponses, le 9 octobre 2026

1. **L'installeur** : « oui » — Inno Setup, par utilisateur, sans droits d'administrateur.
2. **La signature** : « sans » — et le parcours le dit.
3. **La machine vierge** : « la vm je le ferais juste après » — tu actives Windows Sandbox. L'effacement de
   `build/windows-msvc` (7 Go) n'est pas tranché : je n'y touche pas.
4. **Le morceau des stems et de la direction** : « prends un son à moi » — *addiction*, 107 BPM, la mineur,
   2021 (`lawone_crvkit_samgee.mp3_addiction_107_Bpm_Am_MIX_V1.mp3`, MP3 160 kbit/s, 2 min 51, 3,4 Mo). C'est un
   écart au brief (« aucun sample de ma bibliothèque »), décidé par toi. **À confirmer par toi avant toute
   distribution** : le nom cite lawone et crvkit ; s'ils sont co-auteurs, ou si des samples d'un kit y sont, leur
   accord ou la licence du kit couvre-t-il la livraison du morceau dans un installeur ?
5. **Le crédit de l'API** : « j'en rajouterai plus tard » — le `--verify` complet à zéro échec attend ; le bilan
   le dira.
6. **Les licences** : « je vais faire l'inscription Tracktion ».
7. **La version** : « c'est la version alpha » — **0.1.0-alpha** affiché ; 0.1.0.0 dans les propriétés du `.exe`
   (Windows n'y accepte que des nombres).
8. **Un plugin qui plante** : « réponse recommandée » — dans le processus, nommé, réouverture avec ce plugin
   contourné ; l'isolement à l'incubation.
