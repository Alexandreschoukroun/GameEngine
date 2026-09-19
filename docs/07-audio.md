# 07 — Audio (M5)

*Brique 1 : le périphérique, les voix, le son positionné (section 1).*

Le jalon M5 apporte ce que le SPEC appelle « le système le plus important » du moteur. Dans un jeu d'horreur en intérieur sombre, l'oreille porte plus d'information que l'œil : on sait qu'une chose marche dans la pièce d'à côté avant de la voir, et le plus souvent on ne la voit jamais.

# 1. Brique 1 — le son sort des enceintes, et il vient d'un endroit

## 1.1 Le problème

Le moteur est muet. Trois questions à régler : ouvrir la carte son, charger un fichier, et faire qu'un son **vienne d'un endroit du monde** plutôt que du centre du crâne.

## 1.2 Les options

**Un moteur commercial — FMOD ou Wwise.** C'est le choix de l'industrie : occlusion, reverb, mixage et outil pour sound designers, tout de suite. Mais la licence n'est pas libre, ce qui contredit le choix open source, et la boîte noire enterrerait l'apprentissage. Le SPEC avait déjà tranché : *« l'audio est notre différenciateur, on ne prend pas FMOD »*.

**SDL_audio seul.** Déjà dans la stack, zéro dépendance nouvelle. Mais SDL ne fournit qu'un flux d'octets : spatialisation, mixage de 64 voix, rééchantillonnage, filtres — tout serait à écrire. C'est réécrire une bibliothèque de la stack, ce que le SPEC interdit.

**miniaudio.** Un fichier d'en-tête, domaine public, qui couvre exactement la couche du dessous : périphérique, décodage, graphe de mixage, moteur 3D avec atténuation par distance.

**Choix retenu : miniaudio.** Il occupe la place de Jolt pour la physique — il fait le travail ingrat, et on garde la main sur ce qui nous distingue : l'occlusion par secteurs, la reverb par pièce, la couche de tension.

## 1.3 La contrainte qui structure tout code audio

La carte son réclame des échantillons dans un **thread temps réel** qui ne doit jamais être bloqué. Pas d'allocation, pas de verrou, pas de lecture de fichier dedans : le moindre retard produit un **trou audible** — un clic, un silence, un bégaiement. C'est une contrainte plus dure que celle du rendu, où une frame en retard ne fait que saccader.

Conséquence directe sur l'API : `loadSound()` **décode entièrement le fichier en mémoire**, au moment du chargement de la scène. Jouer un son ne touche plus jamais au disque.

## 1.4 Prototypes et voix

Deux notions séparées, exactement comme un maillage et les entités qui l'affichent :

- un **son chargé** (`SoundHandle`) est la donnée : les échantillons décodés, en un seul exemplaire ;
- une **voix** (`VoiceHandle`) est une instance en cours de lecture. Cent entités peuvent jouer le même son à cent endroits sans le décoder cent fois.

```
fichier .wav ──décodage (une fois)──> prototype ──copie──> voix ──┐
                                                  ──copie──> voix ──┼──> mixage ──> carte son
                                                  ──copie──> voix ──┘
```

## 1.5 Pourquoi un identifiant de voix porte une génération

Une voix se termine, son emplacement est recyclé par la voix suivante. Si l'identifiant n'était qu'un indice, un identifiant gardé trop longtemps désignerait **le son du voisin** : on couperait une source sans jamais comprendre pourquoi.

L'identifiant contient donc l'indice **et** un numéro de génération, incrémenté à chaque libération. Un identifiant périmé est reconnu comme tel et ne fait rien. C'est le patron classique du *slot map*, et il reviendra pour toute ressource dont la durée de vie n'est pas maîtrisée par l'appelant.

## 1.6 Le budget est refusé, pas volé

Au-delà de **64 voix** — le chiffre du SPEC —, jouer un son de plus échoue proprement. L'autre stratégie serait de voler la voix la plus ancienne ou la plus lointaine ; elle viendra peut-être, mais couper un pas ou un grincement en cours **s'entend davantage** que l'absence du son nouveau.

## 1.7 Ce que le mode silencieux permet

`create(true)` ouvre le backend « null » de miniaudio : tout le moteur fonctionne au bon rythme, aucune carte son n'est ouverte, rien ne sort. C'est ce qui rend l'audio **testable en CI**, sur une machine de build qui n'a pas de sortie audio — le même problème que le GPU, résolu de la même façon.

## 1.8 Vocabulaire

- **Périphérique** (*device*) : la carte son, et son thread temps réel.
- **Fréquence d'échantillonnage** : 48 000 valeurs par seconde et par canal.
- **Voix** : une instance de son en lecture.
- **Écouteur** (*listener*) : l'oreille virtuelle — position et orientation. Chez nous, la caméra.
- **Atténuation par distance** : la décroissance du volume avec l'éloignement, en 1/d — même logique que le 1/d² des lumières de M2, autre exposant.
- **Distance minimale** : le plancher en deçà duquel le son ne monte plus. Sans lui, l'atténuation en 1/d ferait exploser le volume quand la source touche l'oreille.

## 1.9 Un son spatialisé doit être mono

Un fichier stéréo est **déjà** réparti entre les enceintes par son auteur. Lui donner une position dans le monde n'a pas de sens : il y a deux canaux, donc deux sources implicites. La règle vaut dans tous les moteurs — les sons positionnés sont mono, la musique et les ambiances non localisées sont stéréo.

Le crépitement de braises de la scène de démonstration est **généré**, pas téléchargé : un souffle grave filtré passe-bas, une centaine d'impulsions brèves, et un fondu croisé sur la boucle — sans ce fondu, on entendrait un clic à chaque répétition. Le dépôt reste ainsi autonome, sans asset d'origine incertaine.

## 1.10 Coût

Le fichier décodé occupe ~96 Ko par seconde de son mono 48 kHz — 164 Ko pour les deux secondes de braises. Un thread supplémentaire, créé par miniaudio. Le mixage de quelques dizaines de voix reste sous 1 % de CPU : la part audio du budget de 3,5 ms sera consommée par l'occlusion et la reverb, pas par cette brique.

## 1.11 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié à l'écoute** : le crépitement vient du foyer. En tournant sur soi-même, il passe d'une oreille à l'autre ; en s'éloignant, il faiblit.

**Six tests**, exécutés en CI grâce au mode silencieux : le moteur ouvre et ferme sans fuite, un fichier absent ne fait pas tomber le jeu, le même fichier n'est décodé qu'une fois, une voix arrêtée libère sa place, un identifiant périmé ne peut pas couper son successeur, et le budget de 64 voix est refusé proprement. **51 tests, 218 assertions** au total.

**Ce qui n'existe pas encore** : les sources sonores sont posées en code, pas décrites dans la scène. Pas d'occlusion — un son traverse les murs comme s'ils n'existaient pas. Pas de reverb, pas de HRTF, pas de sons de pas, pas de couche de tension. L'audio ne démarre pas non plus le jeu : un poste sans carte son affiche le jeu en silence, et le signale.

**Ce qui vient après (brique 2)** : les sources sonores comme composants de scène, sérialisées au même endroit que les colliders et les lumières — puis l'occlusion, qui réutilisera le graphe de secteurs de M3 et le lancer de rayon de M4.
