Projet DAW IA — semaine 25 sur 26. Dépôt : samflppp/daw-ia, branche main, push direct.
Lis d'abord : CLAUDE.md, docs/plan-s21-s26.md (section S25 : ce brief la précise), docs/bilan-s24.md, docs/bilan-s23.md (le clavier de l'ordinateur, Raw Input, Ctrl+T), docs/bilan-s22.md (§1 la licence des poids, §2 le service de séparation : même forme de problème), docs/ bilan-s21.md (§2 la carte son, le casque Bluetooth), IDEES.md (« Couche décision », « Quatre idées d'IA » : push-to-talk, instruments IA, la règle sur l'audio généré), services/src/daw_services/ (copilot, stems, ia_provider), core/app/src/CopilotBridge.cpp, core/ui/src/panels/ CopilotPanel.cpp, core/engine/src/AudioOutputKeeper.cpp et AudioSettings.cpp (l'entrée y est vidée exprès).
ÉTAT ACQUIS (S1–S24, ne rien rouvrir) :

* Tout CLAUDE.md §2. Registry de 62 types, schéma 7, enveloppe v3.
* Le copilote : une phrase entre, ses commandes sont mises en attente, essayées sur une copie, puis partent en un seul groupe d'annulation. La phrase est gardée dans le journal (`group_label`).
* Le clavier de l'ordinateur joue des notes (Ctrl+T), lu par scan code en Raw Input ; avec Ctrl, Alt ou Windows, aucune touche ne joue.
* La carte son : AudioOutputKeeper suit Windows ; la fenêtre « Audio » (F12) règle pilote, sortie et tampon, avec un conseil mesuré.
* Les réglages de la machine ne sont jamais réécrits par une vérification (dossier reglages-machine jetable).
* Le flux audio (F3), le kit, les bus intelligents, les effets depuis la tranche (S24).
* Les décisions du 3 octobre (CLAUDE.md §4) : on construit puis on essaie par phases ; la S26 livre un prototype ; un excellent logiciel, même si ça prend plus de temps et de moyens. Le fine-tune du routeur se fera pendant l'incubation.
* Windows seule cible. Job CI Linux en compilation, ne pas toucher.

CAP : on dirige le DAW à la voix. On tient une touche, on parle, on relâche : la phrase part au copilote. C'est la dernière fonctionnalité du prototype ; la S26 emballe. Elle doit donner confiance : on voit ce que le logiciel a compris AVANT qu'il agisse, et une phrase mal comprise ne déclenche jamais rien.
MON OPTIQUE, À APPLIQUER ICI : la place libérée par le fine-tune va à la qualité du push-to-talk, pas à un chantier de plus. Pour la brique de reconnaissance vocale, recommande la meilleure voie pour le produit et dis ce qu'elle coûte en temps, en taille et en moyens. Si la semaine déborde, dis ce qui tombe et où il va.
RÈGLES DE SESSION (elles tiennent) :

* Une seule session dans ce dossier. Du travail non commité ou une branche inconnue : tu t'arrêtes et tu me le dis.
* Compilation par scripts/build.cmd seulement.
* Vérifications avec --project, --layout et les réglages de la machine dans un dossier jetable.
* Espace libre sur C: regardé avant une compilation complète et avant de télécharger des poids de modèle.
* Machine au repos pour mesurer ; sinon dis-le avant.
* Aucun commit poussé avec la CI rouge : chaque commit seul, après le vert du précédent (tenu en S24).
* Mes décisions de modèle recopiées dans le bilan, avec leur date.
* Une licence se vérifie à la source, pour le code ET pour les poids, avec le lien et la date de lecture. La S22 a montré qu'elles diffèrent.
* Un commit, un sujet. Un message de commit ne dit que ce qui est prouvé.
* Debug et Release recompilés dans mon dossier de build à la fin.
* Aucune étape n'attend un essai de ma part.

CHANTIER 0 — LES RESTES DE LA S24. Une journée au plus, dans cet ordre. Tu codes, tu expliques après.

1. LA FIN DE LA S24 N'EST PAS SUR GITHUB. Le 7 octobre à 18 h, origin/main s'arrête à 6a96861 (l'index des samples), CI verte. Le bilan cite des commits jusqu'à 237ef6a (la décimation de YIN, la page « Kit », les bus intelligents, la page « Bus ») et docs/bilan-s24.md : ils n'y sont pas. Pousse-les, un par un, chacun après le vert du précédent, et dis- moi ce qui les retenait.
2. Deux décisions du bilan S24, PRISES LE 7 OCTOBRE 2026 en suivant tes recommandations :
   * Le kit sur une petite bibliothèque (§6.4) : quand un rôle compte moins de dix samples, la contrainte de couleur se relâche et l'élément retenu est dit « hors couleur », avec son écart. Un kit entier dont l'écart est dit vaut mieux qu'un kit incomplet.
   * La réverbération par envoi (§7.3) : un test au label `audio`, hors CI, avec un VST3 donné par DAW_TEST_VST3. Pas de réverbération interne cette semaine : c'est une décision de modèle, elle se discutera à part.
3. La fenêtre que la vérification ne voit pas affichée (bilan S24 §10) : « F3 rouvre la fenêtre » deux fois sur dix, et 7 `isShowing()` faux pendant une minute, barre de titre comprise. Une demi-journée. Si c'est la fenêtre principale qui passe derrière une autre ou se réduit pendant un passage, dis-le : ce serait la vérification, pas le logiciel. Cause non trouvée : dis ce que tu as écarté et passe.
4. scripts/verify-quit.ps1 utilise encore mes réglages : il passe par le même dossier jetable que les autres.
5. Les deux contrôles de --verify-audio qui ne peuvent pas tomber seuls (« le conseil a tenu », la présence des réglages copiés) : rends-les capables de tomber, ou retire-les en le disant.
6. Le repli de la carte sur le partagé n'a jamais été éprouvé : une vérification qui perd une carte ouverte en exclusif.

PAS CETTE SEMAINE, et reste une dette : la piste compagne (branche s21-piste-compagne), les raccourcis des pages « Kit » et « Bus », la page de la chaîne de plugins en double de la tranche, `MeterTests` intermittent.
CHANTIER 1 — LE MODÈLE. EXPOSE AVANT DE CODER. Attends ma validation.

7. LA BRIQUE DE RECONNAISSANCE VOCALE.
   * Les candidats, et celui que tu recommandes pour la qualité. Pour chacun : licence du code ET des poids (à la source, lien et date), taille, langues, qualité publiée en français.
   * Locale ou distante. Mon produit prévoit les deux (local en achat, API en crédits) ; l'argument du produit est « le cœur tourne en local, sans attente ni crédits ». Recommande la voie du prototype et dis ce que l'autre demanderait. Si c'est distant, la voix quitte la machine : ça se dit à l'écran.
   * Jev a été écarté (IDEES.md) et Laya ne transcrit pas, c'est un classeur de texte. Si tu veux reprendre l'un des deux, dis-le comme un écart, avec sa raison.
   * Où elle tourne : dans le service Python (PyTorch y est déjà en option depuis les stems) ou en C++ natif. Dis ce que chaque voie pèse dans l'installeur de la S26, à côté des poids du séparateur.
   * Le temps MESURÉ sur ma machine (i5-8365U, sans carte graphique) pour une phrase de 3 à 6 secondes, modèle chargé et modèle à froid. Le modèle reste-t-il chargé entre deux phrases, et combien de mémoire garde-t-il ?
   * Le vocabulaire : je parle français avec des mots de musique et des mots anglais mêlés (« mets un compresseur sur la 808 », « passe en fa dièse mineur à 140 BPM », « duplique le pattern trois », le nom de mes pistes et de mes plugins). Dis comment tu aides le modèle (les noms du projet donnés en contexte, une liste de termes) et ce que ça change, mesuré.
8. LE MICRO.
   * Il ne passe pas par le moteur, et aucune inférence ne touche le fil audio. Montre le chemin, fil par fil. Aujourd'hui l'entrée de la carte est vidée exprès (AudioOutputKeeper, AudioSettings) : dis comment le micro s'ouvre sans défaire ça.
   * LE PIÈGE DU CASQUE BLUETOOTH, je le nomme : ouvrir le micro d'un casque Bluetooth (mes AirPods) fait basculer Windows en profil mains-libres, et le son du morceau devient mauvais. Dis ce que tu recommandes (le micro de l'ordinateur par défaut, jamais celui du casque sans le dire) et prouve que la sortie ne change pas de qualité quand le micro s'ouvre.
   * Quel micro, et où on le choisit (la fenêtre « Audio », F12). Un réglage de la machine, pas du projet.
   * Parler pendant que le morceau joue : le micro entend les haut-parleurs. Baisser le morceau le temps de la phrase, le mettre en pause, ne rien faire ? Recommande, et mesure ce que la transcription y perd.
   * Aucun micro, ou accès refusé par Windows : le push-to-talk le dit et ne casse rien.
   * Le son de la voix n'est gardé nulle part après la transcription.
9. LA TOUCHE.
   * Laquelle. Elle se tient, donc elle ne peut pas être une touche qui joue une note, ni Espace (le transport), ni une lettre. Elle doit marcher clavier-piano allumé (Ctrl+T) comme éteint, et se lire par sa place comme le reste (Raw Input). Montre la table des touches à jour.
   * Dans un champ de texte : dis ce qu'elle fait.
   * Un bouton à l'écran qui se tient à la souris fait la même chose.
   * Relâcher trop tôt (moins d'une demi-seconde), tenir trop longtemps (une limite, dite), perdre le focus de la fenêtre pendant qu'on parle : chaque cas finit proprement, sans phrase envoyée.
10. CE QU'ON VOIT, ET CE QUI PART. Le cœur de la semaine.
   * Pendant qu'on parle : on voit que le logiciel écoute, et le niveau du micro. Si le modèle le permet à un coût raisonnable, la phrase qui se forme ; sinon dis-le.
   * Au relâchement : la phrase comprise s'affiche dans le champ du copilote, corrigeable au clavier. Dis ce que tu recommandes entre « elle part toute seule si elle est sûre » et « Entrée toujours ». Je penche pour : sûre, elle part après un court délai visible qu'une touche annule ; douteuse, elle attend.
   * CE QUI REND UNE TRANSCRIPTION DOUTEUSE : propose les critères (la confiance du modèle, un silence, une phrase trop courte, une langue inattendue, un mot hors du vocabulaire du projet là où un nom de piste est attendu) et règle les seuils sur le jeu d'essai, pas au jugé. Une phrase douteuse ne déclenche AUCUNE commande : elle s'affiche, les mots incertains marqués, et attend ma correction.
   * La garde existante reste : le copilote essaie à blanc et écrit en un groupe ; un Ctrl+Z défait ce qu'une phrase a fait.
   * Une phrase qui retire ou écrase beaucoup (retirer une piste, vider un pattern) : dis si la voix demande une confirmation de plus que le clavier. Recommande.
   * La provenance : le journal sait qu'une phrase a été DITE et pas tapée. C'est le jeu de données du fine-tune de l'incubation (IDEES.md : ne jamais purger ces lignes). Dis où tu le ranges sans changer la version de l'enveloppe, ou dis pourquoi il le faut.
   * Sans le modèle installé : le bouton propose de l'installer (comme les stems), et le copilote au clavier marche comme avant.
11. LE JEU D'ESSAI. Tout se prouve sans moi. Dis d'où viennent les phrases enregistrées : des voix de synthèse (celles de Windows en français) donnent un taux d'erreur optimiste, dis-le ; un corpus de parole libre de droits en français, s'il en existe un utilisable (licence vérifiée) ; et la place prévue pour mes propres enregistrements, que je ferai dans la phase d'essais. Au moins 60 phrases : des commandes du copilote, des noms de notes et de tonalités, des nombres (BPM, dB), des mots anglais, des noms de pistes, et des phrases qui ne sont pas des commandes. Une partie avec un morceau qui joue derrière.
12. LA VÉRIFICATION SANS LE MODÈLE. La CI ne télécharge aucun poids et n'ouvre aucun micro : un transcripteur simulé derrière la même interface, et des fichiers audio injectés à la place du micro. Dis ce que --verify-voix prouve en local avec le vrai modèle.
13. LES INSTRUMENTS IA : UNE ÉVALUATION ÉCRITE, PAS DE CODE. docs/evaluation-instruments-ia.md. Ce que c'est : un instrument dont le son est rendu par un modèle à partir des notes écrites ; les notes restent dans le projet (la règle « matière, jamais morceau fini »). Je veux, pour chaque modèle ou service sérieux : ce qu'il fait, licence du code et des poids ou conditions du service (à la source, lien et date), latence, coût, ce qu'on en entend dans ses démos, et comment il entrerait dans DAW IA (un rendu hors ligne par pattern mis en cache, invalidé quand les notes changent ; ce que devient le projet sans le modèle). Une recommandation à la fin, et ce que ça demanderait en temps et en moyens pendant l'incubation. Une demi-journée, en fin de semaine.

CHANTIER 2 — CONSTRUIS-LE, dans cet ordre, une livraison à chaque étape : 14. Le service de transcription, sans écran : un fichier audio donne sa phrase, sa confiance et ses mots incertains. Le taux d'erreur sur le jeu d'essai, en nombres, avec et sans l'aide du vocabulaire, avec et sans morceau derrière. 15. Le micro : ouverture, choix, niveau, le casque Bluetooth, le morceau qui joue. 16. La touche et le bouton, avec le clavier-piano allumé et éteint, et les cas de bord du point 9. 17. Ce qu'on voit et ce qui part : la phrase affichée, la douteuse qui attend, la sûre qui part, l'annulation, la provenance dans le journal. 18. --verify-voix. 19. L'évaluation des instruments IA.
PRIORITÉ SI LE TEMPS MANQUE : 14, 15, 16, 17 (la phrase douteuse qui n'agit pas avant la sûre qui part seule), 18, puis 19. Dis-moi en cours de semaine ce qui tombe, et où ça va.
HORS PÉRIMÈTRE : le fine-tune du routeur (incubation), tout code d'instrument IA, la dictée de texte libre, un mot d'éveil (le logiciel n'écoute JAMAIS sans la touche), les réponses parlées du copilote, l'enregistrement audio dans le projet, la piste compagne, l'installeur (S26).
CRITÈRES DE RÉUSSITE, tous prouvables par la machine :

* Le taux d'erreur sur le jeu d'essai, donné par catégorie de phrases. Donne les nombres même s'ils déçoivent, et dis ce qu'ils valent pour une vraie voix.
* Du relâchement de la touche à la phrase affichée : le temps, mesuré sur ma machine, modèle chargé et à froid.
* Aucune phrase marquée douteuse ne produit de commande : prouvé sur toutes les douteuses du jeu d'essai, projet identique à l'octet.
* Une phrase sûre produit le même projet que la même phrase tapée.
* Un Ctrl+Z défait ce qu'une phrase dite a fait, à l'octet.
* Le micro ouvert ne change ni la sortie ni sa qualité ; --verify-lecture reste à 0 sur 75 avec le micro ouvert.
* Sans la touche tenue, aucun échantillon du micro n'est lu.
* Le journal distingue une phrase dite d'une phrase tapée.
* --verify-voix passe en CI sans poids et sans micro ; --verify complet à 674 sur 674 ou plus, zéro échec.

MÉTHODE :

* Chantier 0 : tu codes, tu expliques après. Chantier 1 : exposé et validé avant toute ligne de code.
* Un test qui interroge l'état ne prouve pas l'effet : une phrase comprise se prouve par le projet qu'elle produit, pas par un champ rempli.
* Un test neuf qui passe du premier coup est cassé une fois.
* Une cause non trouvée se dit « non trouvée ».
* La CI n'appelle aucune API, ne télécharge aucun poids, n'ouvre aucun micro.
* scripts/check-all.sh avant chaque commit. Zéro avertissement, -Werror. Commits atomiques, chacun après le vert du précédent.
* La table de descriptions du copilote suit le registry (test croisé).
* Si un choix contredit l'acquis, dis-le au lieu de l'appliquer.
* IDEES.md se remplit, ne se met pas en œuvre.
* Concis.

LIVRABLE DE FIN : docs/bilan-s25.md au format habituel : mes décisions avec leur date, la brique retenue et pourquoi (licences vérifiées, liens), le taux d'erreur par catégorie, les temps mesurés, le chemin du micro fil par fil, la table des touches, ce qui rend une phrase douteuse et les seuils retenus, ce que la brique pèsera dans l'installeur de la S26 (avec le séparateur de stems : la liste de tout ce qui se télécharge au premier usage), ce qui est tombé et où ça va, les écarts à l'acquis. Sa section « À essayer, dans l'ordre » va dans docs/essais-apres-s26.md. docs/evaluation-instruments-ia.md. Mets à jour CLAUDE.md (cap, décisions acquises, dettes) et docs/plan-s21-s26.md si la semaine déplace quelque chose.
Commence par le chantier 0, puis expose le modèle.
