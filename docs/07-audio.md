# 07 — Audio (M5)

*Brique 1 : le périphérique, les voix, le son positionné (section 1). Brique 2 : les sources sonores deviennent des données de scène (section 2). Brique 3 : l'occlusion (section 3). Brique 4 : les matériaux de surface et les pas (section 4). Brique 5 : la couche de tension (section 5).*

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

**Ce qui vient après (brique 2)** : les sources sonores comme composants de scène, sérialisées au même endroit que les colliders et les lumières.

---

# 2. Brique 2 — les sources sonores deviennent des données

## 2.1 Le problème

Le crépitement de la brique 1 était posé en dur dans le code du jeu : *charger ce fichier, trouver l'entité nommée « braise », jouer à cette position*. Trois défauts, dans l'ordre de gravité :

- ajouter un second son demandait de **recompiler** ;
- le jeu devait connaître le nom d'une entité de la scène, ce qui est exactement l'inverse du principe posé en M3 ;
- et surtout, **la position était figée au chargement**.

Ce dernier point a produit un vrai bug, et il est instructif. La braise est **fille de la statue**, qui tourne en continu. Le foyer orbitait donc autour de la pièce pendant que son crépitement restait planté à l'endroit du démarrage. L'œil et l'oreille se contredisaient — et dans un jeu où l'on s'oriente à l'oreille, c'est un défaut de fond, pas un détail.

La correction *ponctuelle* aurait été de replacer la voix chaque frame pour cette entité-là. La correction *structurelle* est de faire des sources des composants, et de synchroniser **toutes** les sources chaque frame. Le bug devient alors impossible à écrire.

## 2.2 Les mêmes deux composants qu'en physique

Le découpage est identique à celui des colliders, et ce n'est pas un hasard : c'est le même problème.

- **`AudioSource`** décrit ce qui sonne — quel son, quel volume, en boucle ou non. C'est de la **donnée**, elle part dans le fichier.
- **`AudioVoice`** contient l'identifiant de la voix en cours. Il n'est **jamais sérialisé** : une voix n'existe qu'à l'exécution, et son identifiant change à chaque lancement.

Cette frontière entre *ce qui décrit* et *ce qui vit* traverse maintenant tout le moteur : `Collider`/`PhysicsBody`, `MeshRenderer`/poignées de ressources, `AudioSource`/`AudioVoice`.

```json
"audio": { "sound": "braises", "volume": 0.7, "looping": true }
```

Le fichier cite un **nom logique**, pas un chemin : renommer `braises.wav` sur le disque ne casse aucune scène. C'est la table de ressources de M3 qui fait le lien, étendue aux sons — elle connaissait les maillages et les textures, elle connaît désormais aussi ce troisième type.

## 2.3 Le flux va dans l'autre sens que celui de la physique

Une différence mérite d'être soulignée, parce qu'elle décide de qui détient la vérité.

```
physique :  simulation ──> Transform      (le corps fait autorité, la scène recopie)
audio    :  Transform  ──> voix           (la scène fait autorité, l'audio suit)
```

Un corps physique **décide** où se trouve l'objet : lui écrire un `Transform` à la main produirait un objet tremblant, tiraillé entre deux vérités. Une source audio ne décide de rien — elle ne peut pas déplacer ce qu'elle sonorise. Il n'y a donc aucune ambiguïté à lever ici, et la synchronisation est une simple recopie, à sens unique.

C'est aussi ce qui la rend sûre à appeler chaque frame : elle ne peut rien casser.

## 2.4 Pas de son de remplacement

Les maillages et les textures ont un repli sur `missing` : un nom inconnu **se voit**, sous la forme d'un damier rose. Un objet silencieusement absent se diagnostique bien plus difficilement.

Pour les sons, ce choix est **inversé** : un nom inconnu laisse la source muette, avec un avertissement dans la console. Jouer un son de remplacement à la place du bon serait pire que le silence — on entendrait quelque chose de faux sans savoir que c'est faux, et dans un jeu où le son porte l'information, un faux signal trompe le joueur. Le silence, lui, ne ment pas.

## 2.5 Un piège déjà rencontré

`startAudioSources` collecte les entités **avant** de leur ajouter un composant : ajouter pendant qu'on parcourt une vue EnTT invaliderait le parcours. Exactement le même piège qu'à la création des corps physiques en M4 — c'est désormais un réflexe.

Elle exclut aussi les entités qui ont déjà une voix. Sans cette exclusion, chaque appel empilerait une voix de plus et le budget de 64 serait consommé en quelques secondes ; un test le vérifie.

## 2.6 Coût

Une recopie de position par source et par frame — quelques dizaines d'entités, une lecture de matrice chacune. Négligeable. Le calcul de la matrice monde, lui, était déjà fait pour le rendu.

## 2.7 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié à l'écoute** : le crépitement tourne avec la braise, puisqu'il suit maintenant son entité à travers la hiérarchie.

**Cinq tests** s'ajoutent : une source de scène démarre une voix à la position de l'entité, une source sans son reste muette sans planter, les sources ne démarrent qu'une fois, une voix suit son entité **à travers son parent** (la braise sur la statue qui tourne), et une source audio survit à une sauvegarde/rechargement sans casser le déterminisme du fichier. **56 tests, 241 assertions** au total.

**Ce qui n'existe pas encore** : aucune occlusion — un son traverse les murs comme s'ils n'existaient pas. Pas de reverb, pas de HRTF, pas de sons de pas, pas de déclenchement à l'événement (tout se joue au chargement), pas de portée réglable par source.

**Ce qui vient après (brique 3)** : l'**occlusion**.

---

# 3. Brique 3 — l'occlusion

## 3.1 Le problème

Un son traverse les murs comme s'ils n'existaient pas. Dans un jeu où l'on s'oriente à l'oreille, c'est le défaut le plus grave possible : le joueur ne peut pas distinguer une menace *dans sa pièce* d'une menace *derrière une cloison*. Toute l'information spatiale que la brique 1 a rendue possible est faussée.

Le SPEC en fait le point central du système : *« un son derrière une porte fermée doit être filtré passe-bas et atténué »*.

## 3.2 Deux effets, pas un seul

Baisser le volume ne suffit pas. Un son lointain est *plus faible* ; un son derrière un mur est **étouffé**, ce qui n'est pas la même chose. L'oreille distingue parfaitement les deux, et se fie à cette différence.

La raison est physique : un obstacle n'absorbe pas toutes les fréquences également. Les graves, dont la longueur d'onde dépasse l'épaisseur d'une cloison, la traversent en la faisant vibrer ; les aigus sont arrêtés. C'est pour ça qu'on entend la basse de la musique du voisin et pas les voix.

Le moteur applique donc :

- une **atténuation**, parce qu'un obstacle absorbe de l'énergie. Pas jusqu'à zéro : un mur laisse toujours passer quelque chose, et une source qui disparaît complètement se remarque ;
- un **filtre passe-bas**, qui efface les fréquences au-dessus d'une coupure. C'est lui qui fait *reconnaître* une porte fermée.

```
voix ──> filtre passe-bas ──> mélangeur ──> carte son
          coupure 18 kHz (dégagé) .. 350 Hz (masqué)
```

## 3.3 Pourquoi la coupure s'interpole géométriquement

Faire glisser la coupure de 18 000 Hz à 350 Hz **linéairement** serait une erreur audible. L'oreille perçoit les fréquences en **rapports**, pas en écarts : l'octave qui sépare 100 de 200 Hz s'entend comme celle qui sépare 5 000 de 10 000 Hz, alors que l'une fait 100 Hz d'écart et l'autre 5 000.

Une interpolation linéaire passerait donc l'essentiel de sa course dans les aigus — inaudible — et l'étouffement arriverait d'un coup à la toute fin. La coupure est donc interpolée géométriquement :

```
coupure = 18000 × (350 / 18000) ^ occlusion
```

Le même raisonnement vaudra pour tout réglage perçu en rapports : le volume en décibels, la luminosité d'une lumière.

## 3.4 Trois rayons plutôt qu'un

Un rayon unique donne une réponse binaire : bloqué ou non. Le résultat bascule brutalement quand le joueur fait un pas, et surtout, **une porte entrouverte se comporterait comme une porte fermée** jusqu'à ce que le rayon central passe enfin.

Le moteur lance donc trois rayons : un direct, deux décalés latéralement de 45 cm. L'occlusion est la **fraction** de rayons bloqués — 0, ⅓, ⅔ ou 1. C'est grossier, et c'est suffisant : ce qu'on veut n'est pas une simulation acoustique, c'est que le joueur *sente* la porte s'ouvrir.

Le décalage est horizontal, parce que dans un intérieur les obstacles sont des murs et des portes : ils se contournent latéralement, pas par le haut.

Une marge de 15 cm est retirée à la longueur du rayon, sinon il toucherait le collider de l'objet qui sonne lui-même — et toute source posée sur une caisse se croirait murée.

## 3.5 Le lissage, et son exception

Un rayon qui clignote entre deux frames — le joueur se balance légèrement, l'obstacle passe d'un côté à l'autre — produirait un cliquetis. L'occlusion appliquée rejoint donc sa consigne par **lissage exponentiel**, le même qu'utilise l'inertie de la lampe torche depuis M2, et pour la même raison : le résultat ne dépend pas de la fréquence d'images.

Une exception : la **première** application prend la consigne telle quelle. Sans elle, toute source déjà masquée au chargement d'un niveau s'entendrait « s'ouvrir » pendant une demi-seconde, comme si une porte venait de bouger.

## 3.6 Ce que la porte de M4 apporte enfin

L'occlusion donne rétrospectivement tout son sens au choix fait en M4 : la porte est une **contrainte physique**, pas une animation. Son angle est une donnée continue, simulée, que le joueur contrôle à la main.

Conséquence directe : entrouvrir la porte de dix centimètres laisse passer *un peu* de son. Une porte animée entre deux états n'aurait jamais pu produire ça — elle aurait été ouverte ou fermée, et le son avec elle.

La scène de démonstration contient donc un **souffle grave placé derrière la porte**. C'est là qu'on entend le système fonctionner : on tire sur le battant, et ce qui respire derrière se dégage progressivement.

## 3.7 Ce que ça ne fait pas

- **Pas de propagation par les portails.** Un son bloqué est atténué là où il est, alors qu'en réalité il contourne l'obstacle et arrive *par la porte ouverte d'à côté*, donc d'une autre direction. Le graphe de secteurs de M3 servira à ça, et le SPEC le prévoit — c'est aussi ce dont l'ouïe de l'antagoniste aura besoin en M7.
- **Pas de réverbération**, donc aucune sensation de volume de pièce.
- **Aucune notion de matériau** : une porte en bois et un mur de pierre occluent identiquement.

## 3.8 Coût

Trois lancers de rayon par voix et par frame. Pour les deux sources de la démo, c'est invisible ; pour 64 voix, ce serait 192 rayons par frame, soit de l'ordre du demi-milliseconde — déjà un septième du budget CPU du SPEC.

La parade est connue et viendra quand le besoin sera réel : ne recalculer l'occlusion que toutes les N frames, en répartissant les voix sur plusieurs frames. Le lissage temporel rend d'ailleurs ce découpage inaudible — il est déjà en place.

## 3.9 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié à l'écoute** : le souffle derrière la porte est étouffé ; l'entrouvrir le dégage progressivement.

**Cinq tests** s'ajoutent : une ligne dégagée ne modifie rien, un mur large occlut totalement, un obstacle étroit n'occlut que partiellement (c'est ce que les trois rayons permettent d'exprimer), l'occlusion rejoint sa consigne progressivement et non en une frame, et la scène de démonstration déclare des sons que le jeu enregistre réellement — un garde-fou sur les **données**, qui attrape un renommage dans `demo.json` que le compilateur ne verrait pas. **61 tests, 258 assertions** au total.

**Ce qui vient après (brique 4)** : les **matériaux de surface**.

---

# 4. Brique 4 — les matériaux de surface et les pas

## 4.1 Le problème

Le joueur se déplace en silence. C'est un manque double.

D'abord, **le silence casse l'immersion** : rien ne relie le corps du joueur au monde, il glisse au-dessus du sol comme une caméra. Ensuite, et c'est le point de fond du genre, **le bruit de ses propres pas est une information de gameplay**. Il renseigne le joueur sur ce qu'il foule — donc sur où il est, dans le noir — et il le renseignera bientôt sur ce que l'antagoniste peut entendre de lui.

## 4.2 Le matériau, une donnée que trois systèmes attendent

Un pas sur de la pierre ne sonne pas comme un pas sur du bois. Il faut donc que le niveau **déclare** la matière de ses surfaces. C'est un nouveau composant, `Surface`, posé à côté du `Collider` :

```json
"collider": { "shape": "box", "halfExtents": [3.0, 0.5, 6.0], "static": true },
"surface":  { "footstep": "pas_pierre" }
```

Il ne porte aujourd'hui que le son des pas, et c'est volontaire — une brique à la fois. Mais c'est la **troisième fois** que la même notion se présente, et il faut le noter :

- en M4, la **masse volumique** de la porte : du bois plein, 300 kg/m³ ;
- en brique 3, l'occlusion, qui devrait distinguer une porte en bois d'un mur de pierre ;
- ici, le son des pas.

Ce sont trois vues d'une seule donnée. Le jour où elle sera unifiée en une vraie table de matériaux, ces trois usages y puiseront — et le rendu s'y ajoutera. C'est exactement ce que prépare la brique « vrais assets » : un matériau décrit une matière, pas seulement une apparence.

## 4.3 La cadence se règle par la distance, pas par le temps

Un compteur temporel demanderait de connaître la vitesse pour ajuster l'intervalle : un réglage pour la marche, un autre pour la course, un troisième pour l'accroupi à venir.

Le moteur accumule au contraire la **distance parcourue**, et joue un pas chaque fois qu'une foulée (85 cm) est couverte. Courir rapproche donc les pas tout seul, et s'arrêter les arrête — sans une ligne de code de plus.

Un détail qui compte : c'est le déplacement **constaté** qui est mesuré, pas la vitesse demandée. Pousser contre un mur ne doit pas faire marcher sur place.

## 4.4 Varier la hauteur plutôt que multiplier les fichiers

Cinquante pas rigoureusement identiques trahissent la machine. L'oreille repère une répétition exacte bien mieux qu'une différence : c'est le défaut de mitraillette, audible dans quantité de jeux.

La parade classique est de stocker plusieurs variantes de chaque son. Le moteur fait autrement : il varie la **hauteur** (± 8 %) et le **volume** (± 15 %) à chaque pas. Un seul fichier par matière, une variation qui ne coûte rien, et le tirage vient d'un xorshift local — trois décalages de bits, aucune allocation, une suite parfaitement reproductible.

## 4.5 Remonter du corps physique à l'entité

Le rayon vers le bas renvoie un **corps physique**. Il faut retrouver l'entité qui le possède pour lire son composant `Surface` — l'opération inverse de celle que fait `createPhysicsBodies`.

`entityForBody` est une **recherche linéaire** sur les corps du niveau. C'est assumé : elle n'est appelée qu'au moment d'un pas, soit environ deux fois par seconde. Le jour où elle sera appelée par image et par corps, elle demandera une table — pas avant. Le SPEC interdit d'optimiser sans avoir mesuré.

## 4.6 Le silence plutôt qu'un son par défaut

Un sol sans composant `Surface` ne produit **aucun** son. C'est le même choix que pour les sources sonores en brique 2, et il mérite d'être répété : un son de pas arbitraire annoncerait au joueur une matière que le niveau n'a pas décrite. Dans un jeu où l'oreille informe, un faux signal est pire qu'une absence de signal.

## 4.7 Ce que la scène de démonstration montre

Le sol est coupé en deux, aligné sur les deux secteurs de M3 : **pierre à l'ouest, bois à l'est**. Traverser la pièce fait changer le son sous les pieds. Les deux sons sont générés, et leur différence est mesurable : la pierre est sèche et s'éteint en 85 ms, le bois résonne pendant 199 ms — ce sont les deux fréquences basses ajoutées au bois qui font entendre un plancher creux.

## 4.8 Coût

Un lancer de rayon **par pas**, soit environ deux par seconde, et une recherche linéaire par pas. Rien de mesurable. Chaque pas occupe une voix pendant sa durée, soit deux ou trois voix sur les 64 du budget.

## 4.9 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié à l'écoute** : le son des pas change en traversant la pièce, se rapproche en courant, s'arrête à l'arrêt.

**Sept tests** s'ajoutent : un pas tombe quand la foulée est couverte et pas avant, rester immobile n'en joue jamais, la matière sous les pieds décide du son, un sol sans matière reste silencieux, marcher en l'air ne joue rien et l'atterrissage repart d'une foulée neuve, les pas répétés occupent des voix distinctes, et un corps physique remonte bien à son entité. **68 tests, 407 assertions** au total.

**Ce qui n'existe pas encore** : pas de son à l'atterrissage ni au saut, pas d'impacts d'objets, pas de traînées — le SPEC les prévoit et ils réutiliseront le même composant. Le matériau ne joue encore aucun rôle dans l'occlusion. Et les pas ne font pas encore de **bruit au sens de l'IA** : en M7, l'antagoniste devra les entendre, et c'est pour ça que jouer un pas renvoie ce qui a été joué plutôt que rien.

**Ce qui vient après (brique 5)** : la **couche de tension**.

---

# 5. Brique 5 — la couche de tension

## 5.1 Le problème

Il n'y a pas de musique. Et la façon habituelle d'en ajouter — des morceaux qu'on déclenche — ne convient pas à ce genre.

Un morceau fixe doit être **attendu** : il commence, il dure, il finit. Si la situation change au milieu, on n'a que trois options, toutes mauvaises : le couper net, attendre qu'il se termine, ou fondre vers un autre morceau en perdant plusieurs secondes. Dans un jeu où la tension doit suivre la menace **seconde par seconde**, ces trois secondes de retard suffisent à détruire l'effet.

Le SPEC tranche d'avance : *« couche de tension : musique/drone paramétrique piloté par une variable `tension` 0..1, pas des morceaux fixes »*.

## 5.2 Le remixage vertical

Trois couches tournent **en permanence**, superposées, et seuls leurs **volumes** varient :

```
tension :   0 ─────────────── 0,5 ─────────────── 1
calme    ████████████████████████████████████░░░░   présent partout, s'efface à moitié
pouls    ░░░░░░░░████████████████████████████████   entre à 0,15, plein à 0,65
aigu     ░░░░░░░░░░░░░░░░░░░░░░░░░░░░████████████   n'apparaît qu'après 0,60
```

C'est ce qu'on appelle du **remixage vertical**, par opposition au remixage horizontal qui enchaîne des morceaux dans le temps. Il n'y a plus de transition à gérer : la musique *est* déjà dans l'état voulu, on ne fait que doser ce qu'on en entend.

Conséquence structurelle : les trois couches **démarrent ensemble et ne s'arrêtent jamais**. Les faire entrer et sortir les désynchroniserait, et chaque entrée s'entendrait comme un raccord.

## 5.3 Ce que contient chaque couche

Elles sont générées, comme les autres sons du dépôt, et chacune porte une intention précise.

**Calme** — une fondamentale à 55 Hz et son octave, plus du souffle filtré. Aucune pulsation. C'est le lit sur lequel les autres se posent, et il ne disparaît jamais complètement : un silence total serait un trou, et un trou s'entend.

**Pouls** — une quinte, dont le volume bat à **1 Hz**, la fréquence d'un cœur au repos. C'est la couche qui dit *quelque chose se prépare*, et le choix de 1 Hz n'est pas décoratif : c'est un rythme que le corps reconnaît.

**Aigu** — deux notes séparées d'un **demi-ton** (440 et 466 Hz). C'est l'intervalle le plus dissonant de la gamme, et la raison est physique : leurs ondes se heurtent et produisent un battement rapide que l'oreille perçoit comme une agression. C'est le même procédé que les cordes de *Psychose*.

## 5.4 Les courbes de volume, et ce qu'un test peut en dire

La règle de mélange est une fonction pure — tension en entrée, volume en sortie — donc testable **sans aucun moteur audio**. Deux propriétés y sont vérifiées, et elles valent mieux que des valeurs figées :

- chaque couche de menace est **monotone croissante** : une couche qui monterait puis redescendrait ferait entendre un relâchement au moment où la situation empire ;
- le lit grave est **monotone décroissant**, mais jamais nul.

Les entrées de couches utilisent un `smoothstep` plutôt qu'une rampe linéaire : une rampe fait entendre son début et sa fin, la courbe en S ne s'entend pas.

## 5.5 La tension glisse, sauf au premier instant

La valeur appliquée rejoint sa consigne par lissage exponentiel, comme l'occlusion et l'inertie de la lampe — mais **beaucoup plus lentement** : environ une seconde et demie. La tension doit monter comme une inquiétude, pas comme un interrupteur.

Même exception qu'en brique 3 : la **première** application prend la consigne telle quelle, sinon chaque niveau commencerait par une montée de tension que personne n'a demandée.

## 5.6 Le pilote est provisoire, et c'est assumé

Il n'y a pas encore d'antagoniste. En attendant, la tension **monte dans le noir et retombe lampe allumée**.

Ce n'est pas une règle de jeu, c'est un banc d'essai : il rend le système audible dès maintenant, et il sera remplacé en M7 par la proximité de la créature, sa ligne de vue et l'*AI Director*. Rien de ce qui est écrit ici ne changera à ce moment-là — seule la ligne qui appelle `setTension` bougera. C'est exactement l'intérêt d'avoir réduit la musique à **une variable**.

## 5.7 Coût

Trois voix occupées en permanence sur les 64 du budget, et trois fichiers de 281 Ko décodés en mémoire. Le mélange lui-même est un réglage de volume par couche et par frame.

## 5.8 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié à l'écoute** : éteindre la lampe fait monter le pouls puis la dissonance ; la rallumer les fait refluer sans aucune coupure.

**Six tests** s'ajoutent : le lit grave n'est jamais muet et la dissonance n'apparaît que tard, les volumes sont monotones, la tension est bornée, les trois couches démarrent ensemble et s'arrêtent ensemble, la tension glisse au lieu de sauter, et une couche manquante désactive la musique au lieu d'en jouer deux tiers — un mélange amputé sonnerait faux, pas incomplet. **74 tests, 493 assertions** au total.

**Ce qui n'existe pas encore** : pas de HRTF binaural, pas de réverbération par pièce, pas de propagation du son par les portails, et le matériau ne joue aucun rôle dans l'occlusion. Ces quatre points restent ouverts dans le SPEC et reviendront quand ils auront un usage concret — la propagation, notamment, arrivera avec l'ouïe de l'antagoniste en M7, qui en a besoin pour la même raison.

**M5 est terminé.**
