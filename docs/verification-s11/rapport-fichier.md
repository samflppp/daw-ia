# Vérification S11 — 26 Sep 2026 8:08:56am


## 1. Ouvrir refuse un dossier qui n'est pas un projet
- OK : refusé
- OK : le message : « Ouvrir un projet : pas-un-projet n'est pas un projet DAW IA : choisis un dossier .dawproj. »
- capture : `1-Ouvrir-refuse-un-dossier-qui-n'est-pas-un-projet.png`

## 2. Nouveau et Enregistrer sous refusent un projet qui existe
- OK : Nouveau refuse : le nom est pris
- OK : le message : « Nouveau projet : Existant.dawproj existe déjà : « Ouvrir » pour le reprendre. »
- OK : Enregistrer sous refuse : rien n'est écrasé
- OK : le dossier existant n'a pas été touché
- capture : `2-Nouveau-et-Enregistrer-sous-refusent-un-projet-qui-existe.png`

## 3. glisser la barre déplace la fenêtre
- OK : de 60 px à droite et 40 px vers le bas, comme la souris
- OK : et revient à sa place
- capture : `3-glisser-la-barre-déplace-la-fenêtre.png`

## 4. agrandie, la fenêtre ne se glisse pas
- OK : agrandie
- OK : elle reste à sa place, plein écran
- OK : le double-clic lui rend sa taille
- capture : `4-agrandie-la-fenêtre-ne-se-glisse-pas.png`

## 5. Enregistrer sous, la suite
- à la fin de ce rapport : copie vers `Copie.dawproj`, puis ce processus se ferme et un autre s'ouvre sur la copie. Ce qu'elle contient se vérifie par --verify-reopen sur elle.
- capture : `5-Enregistrer-sous-la-suite.png`

## Résultat
- 11 vérifications passées, 0 en échec