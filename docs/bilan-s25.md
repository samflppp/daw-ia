# Bilan de fin de S25 — DAW IA

**Période :** semaine 25 sur 26. Travail des 7 et 8 octobre 2026 ; bilan rédigé le 8 octobre 2026.
**Dépôt :** `samflppp/daw-ia`, branche `main`. Commits S25 de `649b0bd` à `34618cf`, plus l'évaluation des
instruments IA et ce bilan.
**Où le travail a été fait :** ta machine (i5-8365U, 16 Go, sans carte graphique), Windows, compilé par
`scripts/build.cmd` ; vérifications dans des dossiers, des dispositions et des réglages de machine jetables.
**La machine n'était pas au repos** pendant les mesures : Chrome et YouTube actifs pendant une partie de la
session. Les temps sont donnés comme mesurés, sans les corriger.
**Tests :** domaine, 523 cas (+7) ; moteur, 123 (+2) ; Python, 77 (+14).
**CI :** voir §12 — un commit poussé pendant qu'une CI était *annulée* (pas rouge) ; **un commit rouge sur `main`**
(`4e532dc`, ne compilait pas sous Linux), réparé par le suivant (`66554c4`) ; le reste, chaque commit seul, après
le vert du précédent.
**Vérifications :**
- `--verify-voix` (neuve) : **202 sur 202** (Debug, transcripteur rejoué) ; **208 sur 208** (Release, vrai modèle
  et vrai micro, `--voix-transcripteur parakeet --voix-micro-reel`) ;
- `--verify` complet (Release) : **672 sur 675, deux fois** — les étapes 58 et 60 : les vu-mètres à -100 dBFS au
  premier tour de boucle, puis une attente dépassée. Cause non trouvée ; on ne sait pas si elle précède la S25 (§12) ;
- `--verify-lecture` avec le micro de l'ordinateur ouvert pendant tout le passage (`--micro-ouvert`, Release) :
  **220 sur 220, 0 lecture muette sur 75** ;
- `--verify-audio` : 35 sur 35 (deux contrôles rendus capables de tomber, le repli sur le partagé éprouvé).

**Registry :** 62 types, inchangé. **Schéma du projet :** 7, inchangé. **Enveloppe :** v3, inchangée.

## En bref

- **On dirige le DAW à la voix.** Ctrl droit tenu (ou le bouton « Parler »), on parle, on relâche : la phrase
  comprise s'affiche dans le champ du copilote, ses mots incertains soulignés. **Sûre**, elle part après 1,5 s
  qu'une touche retient ; **douteuse**, elle dit pourquoi et attend Entrée. **Aucune phrase douteuse n'a produit de
  commande** sur les 67 du jeu d'essai, le projet identique à l'octet.
- **La brique : NVIDIA Parakeet TDT 0.6B v3**, en local, dans le service Python (sherpa-onnx, sans PyTorch), poids
  CC-BY-4.0 téléchargés au premier usage (487 Mo).
- **Du relâchement à la phrase affichée**, Release, vrai modèle chargé : **0,68 s en médiane, 0,99 s au pire** sur
  68 phrases ; à froid (le modèle se charge pendant qu'on parle) : **3,3 s** pour la première.
- **Taux d'erreur par mot** sur des voix de synthèse : **10,2 %** sans le morceau (8,1 % avec les noms du projet),
  **16,0 %** avec un morceau aussi fort que la voix, **9,9 %** avec le morceau baissé de 20 dB. Sur de vraies voix
  lisant du texte (FLEURS) : **8,1 %**. Ta voix, tes mots anglais, ta pièce : à mesurer en phase d'essais.
- **Le piège du casque Bluetooth évité et prouvé** : morceau sur tes AirPods, micro de l'ordinateur ouvert, la
  sortie garde son format (44,1 kHz, 2 canaux).
- **Le journal sait qu'une phrase a été dite** : texte entendu, texte envoyé, confiances, raisons, dans la
  provenance du groupe, jamais le son, sans changer l'enveloppe.
- **Chantier 0 fermé** : les cinq commits S24 absents de GitHub poussés ; le kit sur une petite bibliothèque et la
  réverbération par envoi tranchés et faits ; la fenêtre « non vue » expliquée (c'était la vérification, pas le
  logiciel) ; `verify-quit.ps1` sur des réglages jetables ; `--verify-audio` durci ; le repli de la carte éprouvé.
- **Les instruments IA** : une évaluation écrite (`docs/evaluation-instruments-ia.md`) ; recommandation : rien
  avant le comité, un essai de Magenta RealTime 2 pendant l'incubation.

## 1. Tes décisions, avec leur date

| Date | Décision |
|---|---|
| 7 octobre 2026 (brief) | Le kit sur une petite bibliothèque : sous dix samples dans un rôle, l'élément le plus proche est pris « hors couleur », son écart dit. |
| 7 octobre 2026 (brief) | La réverbération par envoi : un test au label `audio`, avec un VST3 donné par `DAW_TEST_VST3`, hors CI. Pas de réverbération interne cette semaine. |
| 7 octobre 2026, après l'exposé | **« Je valide tout. »** Parakeet v3 en local dans le service Python ; l'aide au vocabulaire par les noms du projet ; le micro de l'ordinateur par défaut, jamais celui d'un casque Bluetooth sans le dire ; le micro ouvert seulement pendant l'appui ; le morceau baissé de 20 dB pendant l'appui ; Ctrl droit (« mon portable a un ctrl droit ») ; une phrase sûre part après 1,5 s (« le délai me va ») ; une phrase douteuse attend Entrée ; une phrase dite qui retire est confirmée ; la provenance dans `Provenance.context` ; pas de phrase qui se forme cette semaine. |

## 2. Le chantier 0

### 2.1 La fin de la S24 n'était pas sur GitHub

`6013db6`, `71c46d4`, `69bde92`, `237ef6a` et `f9fabbd` étaient commités et jamais poussés : la session de la S24
s'est arrêtée après le vert de `6a96861` (17 h 31), sans pousser la suite. Le bilan S24 disait « chaque commit
poussé seul » : c'était faux pour ces cinq-là. Poussés un par un, chacun après le vert du précédent, le 7 octobre.

### 2.2 Le kit sur une petite bibliothèque (`649b0bd`)

Quand aucun sample d'un rôle de moins de dix n'est à 0,75 du kit sur chaque axe, le plus proche en couleur est pris
et dit « hors couleur : écart de 1,00 sur un axe, au-delà de 0,75 ; ta bibliothèque n'en compte que 4 ». Il ne
déplace pas la couleur que gardent les suivants. Le kick garde la contrainte du grave. Un rôle de dix samples ou plus
garde 0,75 et dit ce qui manque.

### 2.3 La réverbération par envoi, au rendu (`8383936`)

Test au label `audio`, ValhallaSupermassive donné par `DAW_TEST_VST3`. Six pistes jouent une note, chacune avec la
même réverbération ; la proposition passe sur un bus par envois. **Queue : −34,30 dBFS avant, −34,27 dBFS après**,
rien sans réverbération. Deux trouvailles :
- **le catalogue range Valhalla en « Fx »**, pas en « reverb » : la page « Bus » ne le proposerait jamais (§13) ;
- **pendant les notes, 7,2 dB de plus après** : le son sec que la réverbération laisse passer (son mix n'est pas à
  100 %) est ajouté une seconde fois par chaque envoi. La page « Bus » le mesure et le dit déjà (S24) ; ici il est
  au rendu.
Les notes sont MIDI : les plugins de la personne ne traitent pas les clips audio (la piste compagne).

### 2.4 La fenêtre que la vérification ne voyait pas (`d47ada1`)

Pour une fenêtre du bureau, `Component::isShowing()` n'est faux que si la fenêtre est réduite (ou n'a pas de
fenêtre native). La vérification dit maintenant, à chaque changement, les fenêtres que Windows n'affiche pas et
l'application au premier plan ; le résultat dit « passage non probant à l'écran ». Un `--verify-flux` du 7 octobre
l'a montré : **la fenêtre principale réduite pendant que Chrome était au premier plan**. C'est la vérification
(quelqu'un se sert de la machine pendant le passage), pas le logiciel. Ce passage a donné huit échecs : le coût des
prises au-dessus d'un pour cent (machine chargée), puis sept qui suivent la fenêtre réduite. **Le message du commit
`d47ada1` dit à tort que les huit découlent de la fenêtre réduite** ; le premier vient de la charge. Le corriger
demandait de réécrire l'historique, refusé par la session.

### 2.5 `verify-quit.ps1` sur des réglages jetables (`d2527a1`)

`--reglages "<dossier>"` donne au moteur une copie des réglages de la machine sans faire du processus une
vérification. Le script la passe et vérifie, à chaque fermeture, `plugins.xml` copié à l'octet et tes réglages
inchangés. Le premier contrôle tombe sur un binaire qui ne copie rien (4 sur 4) ; le second n'a pas pu être cassé :
une fermeture ne réécrit pas `Settings.xml`, même sans copie.

### 2.6 Les deux contrôles de `--verify-audio` (`d544da4`)

« Le conseil a tenu » se lit sur les nombres de l'essai, plus par `held()` ; la copie se prouve par `plugins.xml` à
l'octet au départ et par `Settings.xml` écrit dans le dossier de la vérification pendant le passage. Chacun est
tombé sous une mutation (pour l'une, tes réglages sauvegardés et remis à l'octet ; un `carte-audio.json` écrit chez
toi par le mutant a été déplacé dans le dossier de travail de la session).

### 2.7 Le repli de la carte sur le partagé (`a782ac5`)

Simulé : la carte en mode exclusif, le pilote exclusif refusé au gardien, la carte fermée. Le gardien ouvre la sortie
par défaut de Windows en partagé (tes AirPods ce jour-là), 96 blocs joués ; le pilote rendu, il revient à
l'exclusif. Aucune vérification ne débranche une vraie carte.

## 3. La brique de reconnaissance vocale

### 3.1 Les candidats, mesurés sur ta machine

| Candidat | Code | Poids | Taille | Erreur, voix de synthèse | Erreur, FLEURS (vraies voix) | Par phrase | Mémoire |
|---|---|---|---|---|---|---|---|
| **Parakeet TDT 0.6B v3** par sherpa-onnx | Apache-2.0 | **CC-BY-4.0** | 641 Mo (487 à télécharger) | 16,9 % → **10,2 %** après les règles (§5) | **8,1 %** | **0,47 s** (service seul), 0,68 s (dans le DAW) | 850 Mo |
| Whisper small int8 (faster-whisper) | MIT | MIT | 464 Mo | 26,1 % ; 16,2 % avec les termes | 12,6 % | 3,0 à 3,6 s | 440 Mo |
| Whisper large-v3-turbo int8 | MIT | MIT | 1,6 Go | 12,7 % ; 9,9 % avec les termes | non mesuré | **13 s** | 1 Go |
| Canary 1B v2 | — | CC-BY-4.0 | ≈1 B | publié 4,83 % (FLEURS+CoVoST) | — | non mesuré, trop lourd | — |
| Kyutai stt-1b-en_fr | — | CC-BY-4.0 | ≈1 B | — | — | non mesuré (prévu pour GPU) | — |

Licences lues à la source le 7 octobre 2026 : [Parakeet v3](https://huggingface.co/nvidia/parakeet-tdt-0.6b-v3)
(« governed by the CC-BY-4.0 license »), [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) (Apache-2.0),
[Whisper turbo](https://huggingface.co/openai/whisper-large-v3-turbo) (MIT),
[faster-whisper](https://github.com/SYSTRAN/faster-whisper) (MIT), [Canary 1B v2](https://huggingface.co/nvidia/canary-1b-v2)
(CC-BY-4.0), [Kyutai](https://huggingface.co/kyutai/stt-1b-en_fr) (CC-BY 4.0),
[FLEURS](https://huggingface.co/datasets/google/fleurs) (CC-BY-4.0, sans compte). Chiffres publiés en français :
[Open ASR Leaderboard](https://arxiv.org/html/2510.06961v4) (Parakeet v3 5,42 %, Whisper large-v3 6,36 %,
Canary 1B v2 4,83 %). Le code (sherpa-onnx) et les poids (NVIDIA) ont des licences différentes ; la CC-BY-4.0
oblige à citer NVIDIA (le docstring de `voice/parakeet.py` le fait ; l'écran « À propos » de la S26 devra le faire).

**Retenue : Parakeet v3.** Le seul assez rapide sur un i5 sans carte graphique, le meilleur sur de vraies voix
françaises mesurées, et il donne une probabilité par morceau de mot, sur laquelle repose le doute. Jev et Laya ne
sont pas repris (Jev écarté en S9, Laya ne transcrit pas). La voie distante (Voxtral Mini Transcribe, ~0,003 $/min
selon [OpenRouter](https://openrouter.ai/mistralai/voxtral-mini-transcribe/pricing), à vérifier chez Mistral)
passerait derrière la même interface, la voix quittant la machine dite à l'écran (`IDEES.md`).

### 3.2 Où elle tourne

Dans le service Python, un processus à lui (`daw-services voix`), gardé entre deux phrases, arrêté après un quart
d'heure sans phrase (le modèle tient 850 Mo). Il joint le DAW par une socket locale, comme le copilote. Installé à la
première demande par `uv sync --inexact --extra voix` (sherpa-onnx 28 Mo, **sans PyTorch**), puis
`daw-services voix --install` (les poids, chaque fichier vérifié par son empreinte).

### 3.3 Ce que l'installeur de la S26 doit savoir : tout ce qui se télécharge au premier usage

| Quoi | Quand | Taille | Licence |
|---|---|---|---|
| Extra `voix` : sherpa-onnx + son cœur natif | premier appui sur « Installer la voix » | ~28 Mo installés | Apache-2.0 |
| Poids Parakeet v3 int8 | idem | 487 Mo téléchargés, 641 Mo sur le disque, `%LOCALAPPDATA%\DAW IA\models` | CC-BY-4.0 (citer NVIDIA) |
| Extra `stems` : PyTorch CPU, demucs, soundfile | première séparation | ~480 Mo (PyTorch) | BSD / MIT |
| Poids HTDemucs (rapide, meilleur) | première séparation | ~400 Mo, `%LOCALAPPDATA%\DAW IA\models\huggingface` | « for research purpose » (bilan S22) |

Les deux extras s'installent maintenant sans se retirer l'un l'autre (`--inexact`, `f03c285`).

## 4. Le micro, fil par fil

1. **La touche** : Ctrl droit lu par Raw Input sur le fil du clavier (S23), qui le passe aussi au push-to-talk ; le
   fil des messages le traite (`VoiceInput`). Une touche pressée dans une autre application est ignorée.
2. **Le micro** (`Microphone`) : un périphérique de capture WASAPI **à lui**, jamais celui du moteur, dont l'entrée
   reste vide (S21, `AudioOutputKeeper` et `AudioSettings` inchangés). Ouvert à l'appui, fermé au relâchement :
   sans la touche, il n'est même pas ouvert (prouvé : 0 échantillon lu en deux secondes).
3. **Le fil du périphérique** (WASAPI, de JUCE) copie chaque bloc, ramené en mono, dans un tampon de 15 s fait à la
   construction : ni verrou ni allocation. Il garde le crête du bloc pour l'écran.
4. **Le fil des messages** lit le niveau 30 fois par seconde ; au relâchement, ramène le son à 16 kHz (sinc fenêtré)
   et l'envoie au service en PCM 16 bits par la socket. Le tampon est vidé.
5. **Le processus du service** transcrit (4 fils de sherpa-onnx) et renvoie la phrase. **Aucune inférence ne touche
   le fil audio du moteur** : elle est dans un autre processus.
6. **Le son de la voix n'est gardé nulle part** : ni fichier, ni journal (le contexte gardé fait 450 octets de
   texte).

**Quel micro.** Par défaut, le micro par défaut de Windows s'il n'est pas d'un casque Bluetooth, sinon le premier qui
ne l'est pas. Bluetooth se lit dans la topologie de Windows (le pilote auquel l'extrémité est branchée), pas dans le
nom. Le choix est dans la fenêtre « Audio » (F12), réglage de la machine (`micro.json` à côté de `Settings.xml`) ;
choisir le micro d'un casque Bluetooth y est dit : « Windows fait passer le son de ce casque en qualité téléphone ».

**Le piège du casque, prouvé** (`--voix-micro-reel`, Release) : sortie Realtek, micro de l'ordinateur ouvert : la
même sortie, 48 kHz, 2 canaux. **Sortie sur tes AirPods (« Find My Stereo »), micro de l'ordinateur ouvert : la même
sortie, 44,1 kHz, 2 canaux.** Le micro mains-libres des AirPods est reconnu Bluetooth et jamais pris par défaut. Le
cas inverse (ouvrir le micro des AirPods et voir la sortie changer) n'a pas été provoqué.

**Le morceau qui joue** : baissé de 20 dB tant que la touche est tenue (le volume maître de Tracktion, hors du
projet, hors des rendus), rendu ensuite ; prouvé au rendu (test moteur) et dans `--verify-voix`. Ce que la
transcription y perd, mesuré sur un morceau construit mélangé à la voix : §6.

**Aucun micro, ou refusé par Windows** : le micro ne s'ouvre pas, le panneau dit pourquoi (« l'accès au micro
est-il permis dans les réglages de Windows ? »), rien d'autre ne bouge. **Non éprouvé** par une vérification :
aucune ne retire le micro de la machine.

## 5. La touche

| Touche | Clavier-piano éteint | Clavier-piano allumé (Ctrl+T) | Dans un champ de texte |
|---|---|---|---|
| **Ctrl droit tenu** (0x1D + E0) | parle | parle (avec Ctrl, aucune touche ne joue) | parle ; la phrase arrive dans le champ du copilote |
| Ctrl gauche, AltGr (envoyé comme Ctrl gauche) | inchangé | inchangé | inchangé |
| Une autre touche pendant l'appui | la phrase est abandonnée ; la touche fait ce qu'elle fait (Ctrl+Z reste Ctrl+Z) | idem | idem |
| Une touche pendant qu'une phrase sûre attend son départ | la retient (elle attend Entrée) | idem | idem |
| Échap sur une phrase affichée | l'oublie | idem | idem |
| Entrée sur une phrase affichée | l'envoie, corrigée ou non | idem | idem |
| Le reste (lettres, Espace, F1–F12, Ctrl+…) | inchangé (S23) | inchangé | inchangé |

Le bouton « Parler » du panneau, tenu à la souris, fait comme Ctrl droit ; sans le modèle, il devient « Installer la
voix ». Relâchée avant 0,5 s, tenue plus de 15 s, une autre touche, la fenêtre qui perd la main : rien ne part, et
c'est dit (chaque cas prouvé, le projet à l'octet).

## 6. Le taux d'erreur, par catégorie

Le jeu d'essai (`services/tests/voix/`) : **67 phrases** — 12 commandes, 10 tonalités, 10 nombres, 10 mots anglais,
10 noms de pistes et de plugins, 8 phrases qui ne sont pas des commandes, 7 construites pour être douteuses (silence,
souffle, « Euh. », deux phrases en anglais, deux noms absents du projet). Dites par les trois voix françaises de
Windows (Paul, Julie, Hortense) et la voix anglaise ; **optimiste** : des voix propres, sans accent, qui disent les
mots anglais à la française. Chaque phrase aussi avec un morceau construit derrière (grosse caisse, caisse claire,
charleys, basse, accords à 120 BPM), la voix à −20 dBFS. Taux d'erreur par mot, chiffres écrits en toutes lettres
ou non comptés pareil :

| Catégorie | Propre | Morceau aussi fort que la voix (0 dB) | Morceau à −10 dB | Morceau baissé de 20 dB (la baisse retenue) |
|---|---|---|---|---|
| Commandes | 1,4 % (0,0 % avec les noms) | 12,9 % (11,4) | 1,4 % (0,0) | 1,4 % (0,0) |
| Tonalités | 6,0 % | 10,4 % | 11,9 % | 7,5 % |
| Nombres | 1,4 % | 5,8 % | 0,0 % | 0,0 % |
| Mots anglais | 20,6 % (19,1) | 26,5 % (23,5) | 19,1 % (17,6) | 19,1 % (17,6) |
| Noms de pistes, de plugins | 31,1 % (21,3) | 36,1 % (24,6) | 34,4 % (16,4) | 31,1 % (23,0) |
| Pas des commandes | 0,0 % | 2,1 % | 2,1 % | 0,0 % |
| **Toutes** | **10,2 % (8,1)** | **16,0 % (13,4)** | **11,5 % (8,1)** | **9,9 % (8,1)** |

« Avec les noms » : chaque nom proposé mis à la place de ce qui a été entendu, ce qu'on obtient en acceptant la
proposition. **Ce que ça vaut pour ta voix** : rien de garanti. Les erreurs restantes sont des noms et des mots
anglais dits à la française par une voix de synthèse (« Velray la supermassive » pour « Valhalla Supermassive »,
« ainsi de chaîne » pour « sidechain ») ; une vraie voix les dit autrement, en mieux ou en pire. Sur de vraies voix
qui lisent du texte (FLEURS, 40 phrases), 8,1 %. **La place pour tes enregistrements** est prévue
(`services/tests/voix/` : un fichier par phrase, le même `phrases.tsv`) : à enregistrer en phase d'essais.

**L'aide au vocabulaire, mesurée** : favoriser les mots du projet dans la recherche de Parakeet (« hotwords » de
sherpa-onnx, à 1,5 et à 3) n'a **rien** changé (16,9 % les deux fois). Ce qui aide est après coup : rapprocher les
mots là où un nom est attendu des noms du projet (−2,1 points sur l'ensemble, −10 sur les noms), et une règle :
**« Mais » en tête de phrase devant un article devient « Mets »** (la même prononciation ; 6 phrases sur 36 du jeu
d'essai). Le mot garde ce qui a été entendu, et le journal aussi.

## 7. Ce qui rend une phrase douteuse, et les seuils retenus

Une phrase est douteuse — elle s'affiche, attend Entrée, **ne déclenche aucune commande** — quand :

| Règle | Seuil | Réglé comment |
|---|---|---|
| Rien entendu | pas de texte | — |
| Trop peu de parole | moins de 0,4 s entre le premier et le dernier mot (+0,3 s) | le silence, le souffle et « Euh. » du jeu d'essai ; la mesure par le niveau du son tombait avec un morceau derrière, remplacée par les temps des mots |
| Un seul mot | moins de 2 mots | « Euh. » |
| Une autre langue | plus de mots d'une liste anglaise que d'une liste française | les deux phrases en anglais (Parakeet ne dit pas la langue) |
| Un mot incertain | un morceau de mot sous **0,6** de probabilité | balayé de 0,3 à 0,7 sur le jeu d'essai : à 0,5, 7 phrases sûres fausses et 2 justes douteuses ; à **0,6, 4 sûres fausses et 9 justes douteuses** ; à 0,7, 3 et 21. Retenu 0,6 : une phrase juste qui attend coûte Entrée, une fausse qui part coûte un Ctrl+Z |
| Un nom inconnu | après « piste », « pattern », « bus », « plugin », « le volume de », ou un mot que le modèle a mis en majuscule : ni un nom du projet, ni une note, ni un rang | les deux noms absents du jeu d'essai ; un nom proche (à 0,5) est proposé, jamais mis à sa place |

**Sur le jeu d'essai, propre** : 36 phrases sûres, dont **4 fausses** (« la base » pour « la basse », « ainsi de
chaîne » pour « sidechain », « fadout », « mut ») : des mots entendus avec assurance, qu'aucune règle sur la
confiance ne peut prendre. Elles partent, et le copilote essaie à blanc, puis un Ctrl+Z défait. 9 phrases justes sur
41 sont douteuses. Les **7 construites** sont douteuses dans les quatre conditions.

**Prouvé dans `--verify-voix`** : 32 phrases douteuses sur 67 (les 7 construites comprises), **aucune n'a fait de
commande, le copilote n'a rien reçu, le projet identique à l'octet** ; le même résultat avec le vrai modèle (Release),
qui rend exactement ce qu'il avait rendu à l'enregistrement de la table.

**Une phrase dite qui retire** (une piste, un pattern, une ligne, un clip audio, un plugin, une ligne d'automation,
ou quatre notes ou poses et plus) demande une confirmation de plus que le clavier : les commandes du copilote sont
essayées, puis **« La phrase dite retire une piste : confirmer ? »**. Refusée : rien n'est écrit (prouvé à
l'octet).

## 8. Ce qu'on voit, et ce qui part

- **Pendant qu'on parle** : « J'écoute — 2,1 s », le niveau du micro sous la ligne d'état, le point rouge. Pas de
  phrase qui se forme (décidé : `IDEES.md`).
- **Au relâchement** : « Je transcris… », puis la phrase dans le champ du copilote, les mots incertains soulignés en
  rouge. **Sûre** : « Part dans 1,2 s — une touche la retient, Échap l'oublie », une barre qui se vide. **Douteuse** :
  « Douteuse : des mots incertains ; « charlies » : est-ce « Charleys » ? Corrige, puis Entrée. »
- **Ce qui part** passe par le même chemin qu'une phrase tapée : le copilote, l'essai à blanc, un seul groupe
  d'annulation. **Une phrase sûre fait le projet de la même phrase tapée, à l'octet ; un Ctrl+Z la défait, à
  l'octet** (prouvé, le copilote répondant par une table : aucune vérification n'appelle un modèle).
- **La provenance** : le groupe d'une phrase dite porte, dans `Provenance.context` (une empreinte dans le magasin
  du projet, comme le mixage), `{"source": "voix", "entendu", "envoye", "corrigee", "douteuse", "raisons", "mots"
  (et leurs confiances), "transcripteur"}`. La colonne `context_digest` du journal la garde : une phrase dite se
  distingue d'une tapée (sans contexte) sans changer la version de l'enveloppe. **C'est le jeu de données du
  fine-tune de l'incubation** : ces lignes ne se purgent pas. Jamais le son.
- **Sans le modèle** : le bouton propose de l'installer ; le copilote au clavier marche comme avant.

## 9. Les temps, mesurés

| Mesure | Valeur | Conditions |
|---|---|---|
| Du relâchement à la phrase affichée, modèle chargé | **0,68 s** en médiane, **0,99 s** au pire, 68 phrases | Release, vrai modèle, machine pas au repos |
| La même chose, la première phrase (modèle chargé pendant qu'on parle) | **3,3 s** | Release ; le chargement (lancement du processus, import, modèle) a pris 5,3 s, commencé à l'appui |
| La même chose, Debug | 1,32 s en médiane, 8,2 s à froid (chargement 9,0 s) | Debug ; l'écart Debug/Release vient du DAW (la transcription est dans le même Python) |
| La transcription seule, dans le service | 0,47 s en médiane, 0,62 s au 95e centile, pour 2,7 s de parole | outil `voix_essai.py`, hors du DAW |
| Mémoire du modèle chargé | 850 Mo | processus du service |

## 10. Les tests cassés une fois

Chaque test neuf est tombé au rouge sous une mutation du code qu'il regarde : les 13 tests Python de la voix (la
règle de confiance ne tombait pas d'abord — le mot du test était aussi un nom inconnu ; test corrigé, puis tombé) ;
le geste et les retraits (4 mutations du domaine) ; le kit hors couleur ; la réverbération par envoi (la commande qui
pose la réverbération retirée) ; la baisse du morceau (la réconciliation qui remet le volume) ; le copilote par
table ; `--verify-audio` (trois mutations) ; `verify-quit.ps1` (un binaire qui ne copie rien) ; `--verify-voix` :
ses premiers passages sont tombés sur de vrais défauts (§11), et le contrôle des douteuses est tombé sous une mutation qui envoie les phrases douteuses au copilote (30 échecs : « le copilote n'a rien reçu » sur chacune).

## 11. Ce que la vérification a trouvé en route

- **Le panneau retenait lui-même la phrase sûre** : la notification asynchrone de son propre texte passait pour une
  correction. Corrigé avant tout commit.
- **Une phrase envoyée vide** : `sendPhrase` lisait une référence à la phrase qu'il venait d'oublier. Corrigé.
- **Une touche frappée dans une autre application retenait la phrase**, et Ctrl droit dans une autre application
  aurait ouvert le micro (Raw Input écoute partout, `RIDEV_INPUTSINK`) : une touche pressée hors de nos fenêtres est
  ignorée.
- **`uv sync` exact** aurait fait s'exclure l'extra de la voix et celui du séparateur (`f03c285`).
- **sherpa-onnx déclare son cœur natif dynamiquement** : le verrou d'uv ne le voyait pas (DLL introuvable) ; nommé
  dans l'extra.
- **La mesure de parole par le niveau du son** classait tout en douteux avec un morceau aussi fort que la voix ;
  remplacée par les temps des mots.
- **Le catalogue range ValhallaSupermassive en « Fx »** : la page « Bus » ne le proposerait pas (§13).

## 12. Ce qui s'est mal passé, dit

- **La CI de `d47ada1` a été annulée** (le job Checks bloqué dix minutes sur `apt-get update`, sans rapport avec le
  code) et **j'ai poussé `d2527a1` avant de le voir**. Relancée, verte ; celle de `d2527a1`, annulée par la relance,
  relancée, verte. Puis `a782ac5` : même blocage, relancé, vert. Le job Checks s'est bloqué trois fois sur
  `apt-get update` dans la soirée (§14).
- **`4e532dc` est rouge sur `main`** : `juce::File::windowsLocalAppData` n'existe pas hors de Windows, et le job
  `linux-clang` n'a pas compilé `VoiceService.cpp`. Le compilateur de Windows ne pouvait pas le voir, ni le contrôle
  clang local (il lit les définitions de Windows). Réparé seul par `66554c4` (le même dossier que `parakeet.py`), vert,
  puis les deux commits suivants rejoués dessus (`0af8c88`, `34618cf`, contenu inchangé) et poussés chacun après le
  vert du précédent. Le script qui enchaînait les poussées s'était arrêté sur un résultat vide, sans rien pousser de
  plus.
- **Le message de `d47ada1`** surestime ce que la fenêtre réduite explique (§2.4).
- **Un `carte-audio.json` écrit dans tes réglages** par un binaire mutant de la vérification ; déplacé dans le
  dossier de travail de la session, tes `Settings.xml` et `plugins.xml` vérifiés identiques par empreinte.
- **Des scripts de modification avec des barres obliques inverses** ont cassé deux fichiers en cours de route (des
  `\b` devenus des caractères de contrôle) ; vus et réparés avant tout commit.
- **`--verify` complet n'est pas à 675 sur 675** : 672, deux passages de suite, les mêmes trois contrôles (les
  vu-mètres muets au premier tour de boucle, étapes 58 et 60). La bissection a commencé (le Release de `a782ac5`,
  dernier commit du chantier 0, avant toute ligne de la voix) et s'est arrêtée là : ce Release n'a pas été recompilé.
  On ne sait donc pas si la voix en est la cause. `--verify-lecture`, qui lit le même son, est à 0 sur 75, micro
  ouvert compris. Notée dans les dettes.
- **La machine n'était pas au repos** pour les mesures (Chrome, YouTube).
- **`--verify-voix` n'est pas lancé par la CI** : aucune vérification ne l'est (elles demandent une fenêtre et une
  carte son). Il tourne sans poids ni micro, comme la CI le ferait ; l'ajouter à la CI Windows est une décision
  (§14).

## 13. Les écarts à l'acquis, dits

- **Un processus Python de plus**, `daw-services voix`, à côté du copilote, avec sa socket : la transcription isolée
  du copilote et du DAW, et sa mémoire libérée en arrêtant le processus.
- **Un deuxième périphérique audio** ouvert par l'application (le micro), hors du moteur.
- **Le volume maître de Tracktion n'est plus toujours à l'unité** : la baisse pendant l'appui. Hors du projet, hors
  des rendus.
- **`RawKeyboard` passe aussi chaque touche à un écouteur** (le push-to-talk), avec « notre fenêtre devant ou non ».
- **`PanelServices` a un service de plus**, `VoiceHost`.
- **Le copilote peut répondre par une table** (`--table`), pour les vérifications seulement.
- **`--reglages`**, **`--voix-transcripteur`**, **`--voix-micro-reel`**, **`--micro-ouvert`** : des options de
  vérification.
- **Une règle d'homophone** (« Mais » → « Mets ») dans le service : la seule correction écrite à la place de ce qui
  a été entendu, dite et gardée.
- **`Microphone` et `VoiceInput` : des pas d'horloge nommés** en constantes du code (10, 33, 250 ms), comme ceux des
  autres sessions de l'application ; pas des jetons d'interface.
- **Le seuil de confiance à 0,6 et les listes de mots** (français, anglais, musique) sont des choix, réglés sur un jeu
  d'essai de voix de synthèse.

## 14. À trancher par toi

1. **La catégorie des plugins pour la page « Bus »** : Valhalla est rangé « Fx ». Reconnaître une réverbération par
   son nom (« verb », « room », « plate »…) est une décision de modèle.
2. **La CI** : `apt-get update` bloque le job Checks par moments (trois fois le 7 octobre). Installer clang-format
   sans `apt-get update`, ou un délai court avec une seconde tentative. Et lancer `--verify-voix` sur le job Windows
   (sans carte son sur le runner, à essayer).
3. **Les instruments IA** : l'essai de Magenta RealTime 2 pendant l'incubation, ou rien avant le comité
   (`docs/evaluation-instruments-ia.md`).

### Tes réponses, le 8 octobre 2026

1. **La catégorie des plugins** : « tu peux reconnaître les VST […] par le nom, avec des suggestions à accepter pour
   des cas durs à discerner ». Le détail est exposé avant d'être écrit.
2. **La CI** : « oui ». Fait : clang-format depuis PyPI (`eaf2a71`), `--verify-voix` sur le job Windows (`2535573`),
   198 sur 198 sur le runner en 9 min, chacun essayé d'abord sur une branche puis poussé après le vert.
3. **Magenta RealTime 2** : « doit être intégré […] un outil fou pour mon projet […] on l'intégrera en incubation ».
   Précision : sa carte le donne en temps réel sur Apple Silicon, pas seulement sur Mac ; sur un GPU NVIDIA hors
   temps réel ; le CPU n'y est pas mentionné.

## 15. À essayer, dans l'ordre

Sur `main`, Release recompilé par `scripts/build.cmd`. Les poids sont déjà sur ta machine.

1. **Tiens Ctrl droit et dis « Baisse la caisse claire de trois décibels »** sur un projet qui a une caisse claire.
   La phrase arrive-t-elle juste ? Le délai de 1,5 s se lit-il, et laisse-t-il le temps de lire ?
2. **Tes phrases de tous les jours**, avec tes mots anglais et tes noms de pistes (« mets un compresseur sur la
   808 », « passe en fa dièse mineur à 140 », « duplique le pattern trois »). Combien partent justes ? Combien
   attendent alors qu'elles étaient justes ? Si trop attendent, le seuil de 0,6 descend.
3. **Enregistre le jeu d'essai avec ta voix** (`services/tests/voix/phrases.tsv`, un fichier WAV 16 kHz mono par
   phrase, même nom) : `voix_essai.py record` puis `report` donnent ton vrai taux d'erreur.
4. **Parle pendant que le morceau joue**, sur les haut-parleurs puis au casque. La baisse de 20 dB gêne-t-elle ?
   Suffit-elle sur les haut-parleurs ?
5. **Avec tes AirPods sur les oreilles**, parle : le son des AirPods doit rester le même pendant que le micro de
   l'ordinateur écoute. Choisis ensuite le micro des AirPods dans F12 pour entendre ce que l'avertissement annonce.
6. **« Supprime la piste guitare » dite** : la confirmation est-elle de trop, ou bienvenue ?
7. **Une phrase douteuse** : la raison affichée aide-t-elle à corriger ? Le mot proposé (« est-ce Charleys ? ») est-il
   le bon ?
8. **Les instruments IA** : écoute les démos dans l'ordre de `docs/evaluation-instruments-ia.md` §2.

## 16. Reste à faire

- **La S26 emballe** : l'écran « À propos » cite NVIDIA (CC-BY-4.0) ; l'installeur prévoit les téléchargements du
  §3.3.
- **Pas cette semaine, dettes inchangées** : la piste compagne (branche `s21-piste-compagne`), les raccourcis des pages
  « Kit » et « Bus », la page de la chaîne de plugins en double de la tranche, `MeterTests` intermittent.
- **`--verify` complet à 672 sur 675** : finir la bissection (Release de `a782ac5` contre `main`), au début de la
  S26.
- **Non éprouvé** : un micro refusé par Windows ou absent ; le micro d'un casque Bluetooth ouvert (le piège lui-même).
- **La phrase qui se forme**, la voie distante : `IDEES.md`.
