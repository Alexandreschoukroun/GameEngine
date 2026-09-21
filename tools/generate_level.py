"""Genere un niveau : sa geometrie en glTF, et la scene qui la place.

Pourquoi un generateur plutot qu'un modeleur
--------------------------------------------
Un niveau d'interieur d'horreur est fait de pieces rectangulaires, de couloirs et
d'embrasures. C'est une description, pas une sculpture - et une description se REGLE :
changer la largeur d'un couloir est ici un nombre ; dans un fichier .blend, c'est une
demi-heure. Le jour ou le niveau demandera des moulures sculptees et des fissures, Blender
reprendra la main ; les deux entrent par le meme chargeur glTF.

Ce qui rend un interieur credible, et ce qui le rend cubique
------------------------------------------------------------
La premiere version de ce script produisait un plan juste et un rendu de carton : des
pieces qui se lisaient comme des boites. Le defaut ne venait pas de la taille, mais de
l'absence de RELIEF D'ARCHITECTURE. Quatre choses le donnent, et ce script les produit
toutes :

1. **Les murs ont une epaisseur.** Quatorze centimetres, la meme partout - voir le
   commentaire de WALL_THICKNESS pour savoir pourquoi cette uniformite n'est pas un
   raccourci. Ce qu'on en voit est l'EMBRASURE : en franchissant une porte, on longe le
   tableau du mur. Un mur d'epaisseur nulle prive le regard de cette profondeur, et c'est
   ce qui trahit le decor de theatre plus surement que tout le reste.
2. **Les murs ont des profils.** Plinthe en bas, cimaise a mi-hauteur, corniche en haut,
   chambranle autour des portes. Ce sont de petites boites en saillie de deux a six
   centimetres. Elles coutent peu de triangles et rattrapent l'angle mort du regard : une
   jonction mur/sol nette au millimetre n'existe dans aucun batiment reel.
3. **Les murs ont plusieurs matieres sur leur hauteur.** Un soubassement carrele ou
   lambrisse jusqu'a 1,15 m, du platre peint ou du papier au-dessus. Une surface d'un seul
   tenant sur trois metres de haut ne se rencontre que dans un entrepot.
4. **Les plafonds ne sont pas tous a la meme hauteur.** 2,80 m dans un couloir, 4,20 m
   dans un atelier. C'est le contraste qui fait lire une architecture ; une hauteur unique
   fait lire une grille.

Le plan
-------
Il est pose sur une GRILLE de cellules d'un metre. Chaque cellule appartient a une piece et
produit son sol et son plafond. Un mur separe deux cellules des qu'elles n'appartiennent
PAS a la meme piece - qu'il y ait une autre piece de l'autre cote, ou le vide. Cette regle
garantit deux choses qu'un assemblage piece par piece rend penibles : aucune face en double
la ou deux pieces se touchent, et aucun trou dans l'enveloppe.

Les portes ne sont pas declarees a la main. Le script releve toutes les frontieres entre
deux pieces, les decoupe en troncons continus, et perce chacun en son milieu. Une porte est
donc la CONSEQUENCE du plan : deplacer une cloison deplace sa porte, et il devient
impossible d'oublier une piece. Un couple de pieces peut etre declare SEALED pour forcer un
detour, et le script verifie ensuite, par parcours, que toutes les pieces restent
atteignables depuis le hall.

Usage :  python tools/generate_level.py
"""

import collections
import json
import pathlib
import struct

REPO = pathlib.Path(__file__).resolve().parent.parent
MODELS = REPO / "assets" / "models" / "niveau"
SCENE = REPO / "assets" / "scenes" / "niveau.json"

# --- cotes ---------------------------------------------------------------------------------
#
# Toutes en metres. Ce sont les seuls nombres a toucher pour changer l'allure du batiment.

FLOOR_Y = 0.0

# Une SEULE epaisseur, et c'est un choix corrige apres essai.
#
# Donner 30 cm a une facade et 14 a une cloison est une cote juste. Mais la ou les deux se
# rejoignent sur le meme plan de mur, leurs faces se retrouvent decalees de 8 cm. Laisser
# le decalage ouvert troue le mur ; le fermer par un retour donne un decrochement en plein
# milieu d'une surface plate, ce qui est pire : le defaut devient visible au lieu de
# disparaitre.
#
# La racine du probleme est qu'on n'emet JAMAIS la face exterieure d'une facade - personne
# ne voit le batiment de dehors, et aucune ouverture ne la perce. L'epaisseur
# supplementaire n'etait donc representee par aucune geometrie : elle ne faisait que
# deplacer la face interieure. Elle ne coutait que son defaut.
WALL_THICKNESS = 0.14

DOOR_WIDTH = 1.00
DOOR_HEIGHT = 2.10
ARCH_WIDTH = 2.60   # passage large, sans porte
ARCH_HEIGHT = 2.60
DOOR_MARGIN = 0.70  # jeu minimal de chaque cote : une porte ne se colle pas a un angle

PLINTH_HEIGHT = 0.14   # plinthe
PLINTH_DEPTH = 0.025
RAIL_Y = 1.15          # hauteur du soubassement
RAIL_HEIGHT = 0.06     # cimaise
RAIL_DEPTH = 0.035
CORNICE_HEIGHT = 0.18  # corniche
CORNICE_DEPTH = 0.055
CASING_WIDTH = 0.11    # chambranle
CASING_DEPTH = 0.028
BEAM_WIDTH = 0.26      # poutre
BEAM_DROP = 0.34
BEAM_SPACING = 3.0

TRIM = "bois"          # plinthes, cimaises, chambranles, poutres
CEILING = "platre"     # plafonds, corniches et tableaux d'embrasure

# Taille reelle de chaque matiere, en metres : elle decide de la repetition des textures.
# Une matiere de 2 m doit se repeter tous les deux metres, sinon elle parait geante et
# floue - c'est le piege le plus courant avec des textures libres.
MATERIAL_SIZE = {
    "beton": 2.0,
    "platre": 2.0,
    "platre_peint": 2.0,
    "papier_peint": 1.6,
    "brique": 2.0,
    "carrelage": 2.0,
    "carrelage_mural": 1.0,
    "plancher": 1.5,
    "bois": 1.2,
}

# Le bruit des pas depend de la matiere foulee. Seules les matieres de SOL comptent
# vraiment ; les autres recoivent une valeur par defaut qu'on n'entendra jamais.
FOOTSTEP = collections.defaultdict(lambda: "pas_pierre", {
    "plancher": "pas_bois",
    "bois": "pas_bois",
    "papier_peint": "pas_bois",
})

# --- le plan -------------------------------------------------------------------------------

Room = collections.namedtuple("Room", "name x0 x1 z0 z1 floor wall height wainscot beams")


def room(name, x0, x1, z0, z1, floor, wall, height, wainscot=None, beams=False):
    return Room(name, x0, x1, z0, z1, floor, wall, height, wainscot, beams)


ROOMS = [
    # -- corps sud : l'entree ---------------------------------------------------------------
    room("hall",       -7,   7,  0,  9, "carrelage", "platre_peint", 3.60, "carrelage_mural"),
    room("bureau",    -13,  -7,  0,  7, "plancher",  "papier_peint", 3.00, "bois"),
    room("vestiaire",   7,  13,  0,  6, "carrelage", "platre_peint", 3.00, "carrelage_mural"),
    # -- l'epine dorsale ---------------------------------------------------------------------
    room("couloir",    -2,   2,  9, 27, "carrelage", "platre_peint", 2.80, "carrelage_mural"),
    # -- aile est ----------------------------------------------------------------------------
    room("atelier",     2,  14,  9, 19, "beton",     "brique",       4.20, None, True),
    room("laverie",     2,  10, 19, 25, "carrelage", "platre_peint", 2.90, "carrelage_mural"),
    room("chaufferie", 10,  16, 19, 27, "beton",     "brique",       4.00),
    # -- aile ouest --------------------------------------------------------------------------
    room("chambre_1", -10,  -2,  9, 15, "plancher",  "papier_peint", 3.00, "bois"),
    room("chambre_2", -10,  -2, 15, 21, "plancher",  "papier_peint", 3.00, "bois"),
    room("salle_eau", -16, -10,  9, 15, "carrelage", "platre_peint", 2.90, "carrelage_mural"),
    room("reserve",   -10,  -2, 21, 27, "beton",     "platre_peint", 3.20),
    # -- corps nord --------------------------------------------------------------------------
    room("refectoire", -2,  10, 27, 35, "plancher",  "papier_peint", 3.60, "bois", True),
]

# Couples de pieces qui se touchent mais ne communiquent PAS. Un seul suffit a rendre la
# circulation moins evidente : sans lui, on passerait de l'atelier a la laverie en ligne
# droite, et ce couloir ne servirait a rien.
SEALED = {frozenset(("atelier", "laverie"))}

# Couples relies par un passage LARGE et sans porte. Le hall s'ouvre sur son couloir : une
# porte de 1 m y ferait un sas, pas une entree.
ARCHES = {frozenset(("hall", "couloir"))}

# Lumieres : une par piece, rares et chaudes. Dans ce genre, l'obscurite est le sujet, pas
# un defaut a corriger. La hauteur suit celle du plafond de la piece.
LIGHTS = [
    ("lampe_hall",        "hall",       [1.00, 0.82, 0.60], 16.0),
    ("lampe_bureau",      "bureau",     [1.00, 0.78, 0.52], 10.0),
    ("lampe_vestiaire",   "vestiaire",  [0.92, 0.88, 0.74], 8.0),
    ("lampe_couloir",     "couloir",    [0.90, 0.85, 0.70], 9.0),
    ("lampe_atelier",     "atelier",    [1.00, 0.75, 0.50], 14.0),
    ("lampe_laverie",     "laverie",    [0.78, 0.86, 1.00], 9.0),
    ("lampe_chaufferie",  "chaufferie", [1.00, 0.58, 0.30], 11.0),
    ("lampe_chambre_1",   "chambre_1",  [1.00, 0.80, 0.56], 9.0),
    ("lampe_chambre_2",   "chambre_2",  [1.00, 0.80, 0.56], 9.0),
    ("lampe_salle_eau",   "salle_eau",  [0.72, 0.84, 1.00], 8.0),
    ("lampe_reserve",     "reserve",    [0.70, 0.80, 1.00], 8.0),
    ("lampe_refectoire",  "refectoire", [1.00, 0.84, 0.62], 13.0),
]

DIRECTIONS = ((1, 0), (-1, 0), (0, 1), (0, -1))


# --- la grille -----------------------------------------------------------------------------

def build_cells():
    """Cellule -> indice de la piece a laquelle elle appartient.

    Une cellule deja prise garde sa piece : les rectangles peuvent donc se chevaucher dans
    le tableau sans qu'on ait a les decouper a la main.
    """
    cells = {}
    for index, place in enumerate(ROOMS):
        for x in range(place.x0, place.x1):
            for z in range(place.z0, place.z1):
                cells.setdefault((x, z), index)
    return cells


def wall_key(cell, direction):
    """Decrit le plan de mur que cette cellule ferme de ce cote.

    Renvoie (axe, ligne, sens) : l'axe perpendiculaire au mur, la coordonnee entiere de la
    frontiere, et le sens dans lequel la face regarde - c'est-a-dire vers l'interieur de la
    cellule.
    """
    x, z = cell
    dx, dz = direction
    if dx == 1:
        return 0, x + 1, -1
    if dx == -1:
        return 0, x, 1
    if dz == 1:
        return 2, z + 1, -1
    return 2, z, 1


def along_axis(axis):
    """L'axe le long duquel court un mur perpendiculaire a `axis`."""
    return 2 if axis == 0 else 0


def cell_span(cell, axis):
    """La coordonnee de la cellule le long du mur."""
    return cell[0] if axis == 2 else cell[1]


# --- frontieres et embrasures ---------------------------------------------------------------

def merge_runs(values):
    """Regroupe des entiers consecutifs en troncons [debut, fin]."""
    runs = []
    for value in sorted(values):
        if runs and runs[-1][1] == value:
            runs[-1][1] = value + 1
        else:
            runs.append([value, value + 1])
    return [tuple(run) for run in runs]


def collect_boundaries(cells):
    """Toutes les frontieres du plan, regroupees par plan de mur et par voisinage.

    Renvoie un dictionnaire (axe, ligne, sens, piece, voisin) -> troncons. `voisin` vaut
    None quand c'est le vide qui borde : c'est un mur de facade.
    """
    segments = collections.defaultdict(set)
    for cell, index in cells.items():
        for direction in DIRECTIONS:
            neighbour = (cell[0] + direction[0], cell[1] + direction[1])
            other = cells.get(neighbour)
            if other == index:
                continue  # meme piece des deux cotes : pas de mur
            axis, line, sign = wall_key(cell, direction)
            segments[(axis, line, sign, index, other)].add(cell_span(cell, axis))
    return {key: merge_runs(values) for key, values in segments.items()}


def ordered(boundaries):
    """Les frontieres dans un ordre stable.

    On ne peut pas trier les cles telles quelles : le voisin vaut None pour un mur de
    facade, et None ne se compare pas a un entier. Un ordre FIXE est indispensable -
    c'est lui qui rend le fichier produit identique d'une execution a l'autre, donc
    lisible dans un diff.
    """
    return sorted(boundaries.items(),
                  key=lambda item: (item[0][:4], -1 if item[0][4] is None else item[0][4]))


def place_openings(boundaries):
    """Perce chaque troncon de frontiere entre deux pieces, en son milieu.

    Une porte est ainsi la CONSEQUENCE du plan et non une declaration separee : deplacer
    une cloison deplace sa porte, et aucune piece ne peut etre oubliee.
    """
    openings = collections.defaultdict(list)
    links = set()
    skipped = []
    seen = set()

    for (axis, line, sign, index, other), runs in ordered(boundaries):
        if other is None:
            continue
        pair = frozenset((ROOMS[index].name, ROOMS[other].name))
        if pair in SEALED:
            continue
        wide = pair in ARCHES
        width = ARCH_WIDTH if wide else DOOR_WIDTH
        height = ARCH_HEIGHT if wide else DOOR_HEIGHT

        for start, end in runs:
            # Les deux cotes d'une meme cloison decrivent le meme troncon : on ne le perce
            # qu'une fois, sans quoi on obtiendrait deux trous decales d'une epaisseur.
            token = (axis, line, start, end, tuple(sorted((index, other))))
            if token in seen:
                continue
            seen.add(token)

            if end - start < width + 2 * DOOR_MARGIN:
                skipped.append((ROOMS[index].name, ROOMS[other].name, end - start))
                continue
            middle = (start + end) * 0.5
            openings[(axis, line)].append((middle - width / 2, middle + width / 2, height))
            links.add(pair)

    return openings, links, skipped


def check_connectivity(links, start="hall"):
    """Parcours des pieces par leurs portes. Une piece isolee est une erreur de plan."""
    graph = collections.defaultdict(set)
    for pair in links:
        a, b = tuple(pair)
        graph[a].add(b)
        graph[b].add(a)

    seen = {start}
    queue = [start]
    while queue:
        for neighbour in graph[queue.pop()]:
            if neighbour not in seen:
                seen.add(neighbour)
                queue.append(neighbour)
    return seen


# --- geometrie -------------------------------------------------------------------------------

class Mesh:
    """Accumulateur de geometrie pour une matiere."""

    def __init__(self, uv_size):
        self.positions = []
        self.normals = []
        self.uvs = []
        self.indices = []
        self.uv_size = uv_size

    def quad(self, corners, normal, uvs):
        """Ajoute un quad. L'ORDRE des coins decide du cote visible."""
        base = len(self.positions)
        for corner, uv in zip(corners, uvs):
            self.positions.append(corner)
            self.normals.append(normal)
            self.uvs.append((uv[0] / self.uv_size, uv[1] / self.uv_size))
        self.indices.extend([base, base + 1, base + 2, base, base + 2, base + 3])

    def empty(self):
        return not self.positions


def point(axis, plane, u, v):
    """Reconstruit un point 3D a partir d'un plan et des deux autres coordonnees.

    (u, v) sont les deux axes restants, pris dans l'ordre croissant : (y, z) pour un plan
    perpendiculaire a X, (x, z) pour Y, (x, y) pour Z.
    """
    if axis == 0:
        return (plane, u, v)
    if axis == 1:
        return (u, plane, v)
    return (u, v, plane)


# Les quatre coins de chaque face, dans l'ordre qui produit une normale sortante. C'est
# l'ENROULEMENT qui decide du cote visible, pas la normale qu'on declare a cote : une face
# enroulee a l'envers est eliminee par le rendu, donc absente a l'ecran.
CORNERS = {
    (0, 1): lambda a, b, c, d: [(a, d), (a, c), (b, c), (b, d)],
    (0, -1): lambda a, b, c, d: [(a, c), (a, d), (b, d), (b, c)],
    (1, 1): lambda a, b, c, d: [(a, d), (b, d), (b, c), (a, c)],
    (1, -1): lambda a, b, c, d: [(a, c), (b, c), (b, d), (a, d)],
    (2, 1): lambda a, b, c, d: [(a, c), (b, c), (b, d), (a, d)],
    (2, -1): lambda a, b, c, d: [(b, c), (a, c), (a, d), (b, d)],
}


def face(mesh, axis, sign, plane, u0, u1, v0, v1):
    """Un rectangle plan, perpendiculaire a `axis`, regardant dans le sens `sign`."""
    if u1 - u0 < 1e-4 or v1 - v0 < 1e-4:
        return
    pairs = CORNERS[(axis, sign)](u0, u1, v0, v1)
    corners = [point(axis, plane, u, v) for u, v in pairs]
    normal = [0.0, 0.0, 0.0]
    normal[axis] = float(sign)
    # Les coordonnees de texture suivent la SURFACE. Sur un mur perpendiculaire a X, les
    # deux axes restants sont (y, z) : il faut les prendre dans l'ordre (le long, la
    # hauteur) et non l'inverse, faute de quoi la matiere serait couchee.
    uvs = [(v, u) for u, v in pairs] if axis == 0 else list(pairs)
    mesh.quad(corners, tuple(normal), uvs)


def box(mesh, lo, hi):
    """Un pave, six faces tournees vers l'exterieur."""
    for axis in range(3):
        u_axis, v_axis = [i for i in range(3) if i != axis]
        for sign in (1, -1):
            plane = hi[axis] if sign == 1 else lo[axis]
            face(mesh, axis, sign, plane, lo[u_axis], hi[u_axis], lo[v_axis], hi[v_axis])


def subtract(start, end, holes):
    """Les morceaux de [start, end] qui ne sont pas couverts par `holes`."""
    pieces = []
    cursor = start
    for hole_start, hole_end in sorted(holes):
        if hole_end <= cursor or hole_start >= end:
            continue
        if hole_start > cursor:
            pieces.append((cursor, hole_start))
        cursor = max(cursor, hole_end)
    if cursor < end:
        pieces.append((cursor, end))
    return pieces


# --- emission ---------------------------------------------------------------------------------

def emit_floors_and_ceilings(cells, meshes):
    for (x, z), index in cells.items():
        place = ROOMS[index]
        face(meshes[place.floor], 1, 1, FLOOR_Y, x, x + 1, z, z + 1)
        # Le plafond regarde vers le bas : c'est de dessous qu'on le voit.
        face(meshes[CEILING], 1, -1, place.height, x, x + 1, z, z + 1)


def wall_bands(place, height):
    """Les bandes horizontales d'un mur, de bas en haut.

    Chaque bande est (bas, haut, matiere). C'est cette composition - soubassement, cimaise,
    partie haute, corniche - qui distingue un mur d'une surface plate.
    """
    bands = []
    low = PLINTH_HEIGHT
    if place.wainscot:
        bands.append((low, RAIL_Y, place.wainscot))
        low = RAIL_Y + RAIL_HEIGHT
    bands.append((low, height - CORNICE_HEIGHT, place.wall))
    return bands


def split_at(bands, heights):
    """Coupe les bandes aux hauteurs donnees.

    Sans cette coupe, une bande qui chevauche le linteau d'une porte devrait etre percee
    d'une encoche - ce qui n'est plus un rectangle. Apres la coupe, chaque bande est soit
    entierement sous un linteau, soit entierement au-dessus, et une simple soustraction
    d'intervalles suffit.
    """
    pieces = []
    for low, high, material in bands:
        edges = [low] + sorted(h for h in heights if low + 1e-4 < h < high - 1e-4) + [high]
        for i in range(len(edges) - 1):
            pieces.append((edges[i], edges[i + 1], material))
    return pieces


def wall_face(mesh, axis, sign, plane, t0, t1, low, high):
    """Une portion de mur : le long du mur de t0 a t1, de la hauteur `low` a `high`."""
    if axis == 0:
        face(mesh, 0, sign, plane, low, high, t0, t1)
    else:
        face(mesh, 2, sign, plane, t0, t1, low, high)


def emit_wall(meshes, axis, line, sign, place, start, end, openings):
    """Un pan de mur : ses bandes de matiere, ses profils en saillie, et ses trous."""
    # La face visible est en retrait de la moitie de l'epaisseur : le mur OCCUPE la
    # frontiere, il ne s'y reduit pas. C'est de cette moitie que vient l'embrasure.
    #
    # Le retrait est le MEME pour tous les murs, et c'est ce qui garantit qu'un mur reste
    # D'APLOMB sur toute sa longueur, meme la ou il change de role - cloison d'un cote
    # d'une piece, facade de l'autre.
    plane = line + sign * WALL_THICKNESS / 2
    height = place.height
    along = along_axis(axis)

    holes = [(o0, o1) for o0, o1, _ in openings]
    heads = {h for _, _, h in openings}

    for low, high, material in split_at(wall_bands(place, height), heads):
        blocking = [(o0, o1) for o0, o1, h in openings if h > low + 1e-4]
        for t0, t1 in subtract(start, end, blocking):
            wall_face(meshes[material], axis, sign, plane, t0, t1, low, high)

    # -- les profils en saillie ---------------------------------------------------------------
    #
    # Chacun est un pave pose contre le mur. Leur relief est minuscule - deux a six
    # centimetres - et c'est pourtant lui qui fait qu'une piece cesse de se lire comme une
    # boite : aucune jonction reelle entre un mur et un sol n'est nette au millimetre.
    def moulding(low, high, depth, material, interrupted):
        for t0, t1 in subtract(start, end, holes if interrupted else []):
            lo = [0.0, low, 0.0]
            hi = [0.0, high, 0.0]
            lo[along], hi[along] = t0, t1
            # La saillie va vers l'interieur de la piece, donc dans le sens de la face.
            near, far = plane, plane + sign * depth
            lo[axis], hi[axis] = min(near, far), max(near, far)
            box(meshes[material], tuple(lo), tuple(hi))

    moulding(FLOOR_Y, PLINTH_HEIGHT, PLINTH_DEPTH, TRIM, True)
    if place.wainscot:
        moulding(RAIL_Y, RAIL_Y + RAIL_HEIGHT, RAIL_DEPTH, TRIM, True)
    moulding(height - CORNICE_HEIGHT, height, CORNICE_DEPTH, CEILING, False)

    # -- le chambranle ------------------------------------------------------------------------
    near, far = plane, plane + sign * CASING_DEPTH
    lo_axis, hi_axis = min(near, far), max(near, far)
    for o0, o1, head in openings:
        for a, b, low, high in ((o0 - CASING_WIDTH, o0, FLOOR_Y, head + CASING_WIDTH),
                                (o1, o1 + CASING_WIDTH, FLOOR_Y, head + CASING_WIDTH),
                                (o0, o1, head, head + CASING_WIDTH)):
            lo = [0.0, low, 0.0]
            hi = [0.0, high, 0.0]
            lo[along], hi[along] = a, b
            lo[axis], hi[axis] = lo_axis, hi_axis
            box(meshes[TRIM], tuple(lo), tuple(hi))


def emit_reveals(meshes, axis, line, thickness, openings):
    """Les tableaux d'une embrasure : ses deux jambages et son linteau.

    C'est la seule partie de la geometrie qui rende l'epaisseur du mur VISIBLE, et c'est
    pour elle que les murs en ont une.
    """
    along = along_axis(axis)
    near, far = line - thickness / 2, line + thickness / 2
    for o0, o1, head in openings:
        # Les deux jambages se font face, de part et d'autre du passage.
        for edge, sign in ((o0, 1), (o1, -1)):
            if along == 0:
                face(meshes[CEILING], 0, sign, edge, FLOOR_Y, head, near, far)
            else:
                face(meshes[CEILING], 2, sign, edge, near, far, FLOOR_Y, head)
        # Le linteau se regarde par en dessous.
        if along == 0:
            face(meshes[CEILING], 1, -1, head, o0, o1, near, far)
        else:
            face(meshes[CEILING], 1, -1, head, near, far, o0, o1)


def emit_beams(meshes):
    """Des poutres sous le plafond des grandes pieces.

    Un plafond nu de cent metres carres n'existe pas ; et un plafond qui porte quelque
    chose donne au regard une echelle, ce qu'une surface unie lui refuse.
    """
    for place in ROOMS:
        if not place.beams:
            continue
        # Les poutres traversent la piece dans sa plus PETITE dimension : c'est le sens
        # dans lequel une poutre porte.
        width, depth = place.x1 - place.x0, place.z1 - place.z0
        longest = max(width, depth)
        top = place.height
        count = max(1, int(longest // BEAM_SPACING) - 1)
        for i in range(count):
            offset = (i + 1) * (longest / (count + 1))
            if width >= depth:
                x = place.x0 + offset
                box(meshes[TRIM], (x - BEAM_WIDTH / 2, top - BEAM_DROP, place.z0),
                    (x + BEAM_WIDTH / 2, top, place.z1))
            else:
                z = place.z0 + offset
                box(meshes[TRIM], (place.x0, top - BEAM_DROP, z - BEAM_WIDTH / 2),
                    (place.x1, top, z + BEAM_WIDTH / 2))


def wall_faces(boundaries):
    """Ou se trouve reellement chaque face de mur.

    Renvoie (axe, ligne, sens) -> liste de (debut, fin, plan, piece), triee le long du mur.
    Cette liste sert deux fois : a emettre les murs, et a reperer les RESSAUTS - les
    endroits ou deux troncons voisins du meme plan de mur ne presentent pas leur face au
    meme endroit.
    """
    faces = collections.defaultdict(list)
    for (axis, line, sign, index, other), runs in ordered(boundaries):
        plane = line + sign * WALL_THICKNESS / 2
        for start, end in runs:
            faces[(axis, line, sign)].append((start, end, plane, index))
    for runs in faces.values():
        runs.sort()
    return faces


def find_steps(faces):
    """Les ressauts : deux troncons colles dont les faces ne sont pas au meme endroit.

    C'est le defaut qui a produit des trous visibles dans les murs du premier batiment.
    Il naissait de deux epaisseurs de mur differentes sur un meme plan, et il etait
    invisible a la generation : la piece est fermee, les faces sont a l'endroit, rien ne
    manque. Ce qui manquait, c'etait le raccord entre les deux plans.

    Avec une epaisseur unique, cette liste doit rester VIDE. La fonction n'est donc plus
    un correctif mais un GARDE-FOU : elle ne repare rien, elle interdit de reintroduire
    la cause.
    """
    steps = []
    for (axis, line, sign), runs in sorted(faces.items()):
        for before, after in zip(runs, runs[1:]):
            if abs(before[1] - after[0]) > 1e-6 or abs(before[2] - after[2]) < 1e-6:
                continue
            steps.append((axis, line, before[1], before[2], after[2]))
    return steps


def emit_walls(boundaries, openings, meshes):
    for (axis, line, sign, index, other), runs in ordered(boundaries):
        place = ROOMS[index]
        for start, end in runs:
            local = [o for o in openings.get((axis, line), []) if o[1] > start and o[0] < end]
            emit_wall(meshes, axis, line, sign, place, start, end, local)

    for (axis, line), holes in sorted(openings.items()):
        emit_reveals(meshes, axis, line, WALL_THICKNESS, holes)


# --- ecriture glTF ---------------------------------------------------------------------------

def write_gltf(name, mesh, material_name):
    """Ecrit un .gltf et son .bin. Le format est du JSON plus un tampon binaire."""
    MODELS.mkdir(parents=True, exist_ok=True)

    positions = b"".join(struct.pack("<3f", *p) for p in mesh.positions)
    normals = b"".join(struct.pack("<3f", *n) for n in mesh.normals)
    uvs = b"".join(struct.pack("<2f", *uv) for uv in mesh.uvs)
    indices = b"".join(struct.pack("<I", i) for i in mesh.indices)

    def pad(data):
        # Chaque vue doit commencer sur un multiple de quatre octets : c'est une exigence
        # du format, et l'ignorer donne un fichier que certains lecteurs refusent.
        return data + b"\x00" * ((4 - len(data) % 4) % 4)

    blob = pad(positions) + pad(normals) + pad(uvs) + pad(indices)
    offsets = [0, len(pad(positions)), len(pad(positions)) + len(pad(normals))]
    offsets.append(offsets[2] + len(pad(uvs)))

    def bounds(values, count):
        lo = [min(v[i] for v in values) for i in range(count)]
        hi = [max(v[i] for v in values) for i in range(count)]
        return lo, hi

    position_min, position_max = bounds(mesh.positions, 3)
    binary_name = f"{name}.bin"
    (MODELS / binary_name).write_bytes(blob)

    gltf = {
        "asset": {"version": "2.0", "generator": "GameEngine tools/generate_level.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{
            "name": name,
            "primitives": [{
                "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                "indices": 3,
                "material": 0,
            }],
        }],
        # Le materiau ne porte que des facteurs neutres : les textures sont declarees dans
        # la scene, par leur nom logique. Le niveau reste ainsi habillable sans regenerer
        # sa geometrie.
        "materials": [{"name": material_name,
                       "pbrMetallicRoughness": {"baseColorFactor": [1.0, 1.0, 1.0, 1.0],
                                                "metallicFactor": 0.0,
                                                "roughnessFactor": 1.0}}],
        "buffers": [{"uri": binary_name, "byteLength": len(blob)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": offsets[0], "byteLength": len(positions), "target": 34962},
            {"buffer": 0, "byteOffset": offsets[1], "byteLength": len(normals), "target": 34962},
            {"buffer": 0, "byteOffset": offsets[2], "byteLength": len(uvs), "target": 34962},
            {"buffer": 0, "byteOffset": offsets[3], "byteLength": len(indices), "target": 34963},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(mesh.positions), "type": "VEC3",
             "min": position_min, "max": position_max},
            {"bufferView": 1, "componentType": 5126, "count": len(mesh.normals), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": len(mesh.uvs), "type": "VEC2"},
            {"bufferView": 3, "componentType": 5125, "count": len(mesh.indices), "type": "SCALAR"},
        ],
    }
    (MODELS / f"{name}.gltf").write_text(json.dumps(gltf, indent=1), encoding="utf-8")
    return len(mesh.indices) // 3


# --- la scene ---------------------------------------------------------------------------------

def entity(index, name, extra):
    node = collections.OrderedDict([
        ("id", f"0x{0x200000 + index:016x}"),
        ("name", name),
        ("transform", collections.OrderedDict([
            ("position", [0.0, 0.0, 0.0]),
            ("rotation", [0.0, 0.0, 0.0, 1.0]),
            ("scale", [1.0, 1.0, 1.0]),
        ])),
    ])
    node.update(extra)
    return node


def write_scene(groups):
    entities = []
    for index, (name, material) in enumerate(groups):
        entities.append(entity(index, name, collections.OrderedDict([
            ("mesh", collections.OrderedDict([("mesh", name), ("material", material)])),
            # La collision suit exactement la geometrie : c'est tout l'interet du collider
            # de maillage, et la seule facon de rendre un niveau importe praticable.
            ("collider", collections.OrderedDict([
                ("shape", "mesh"),
                ("halfExtents", [0.5, 0.5, 0.5]),
                ("static", True),
                ("collisionMesh", name),
            ])),
            ("surface", collections.OrderedDict([("footstep", FOOTSTEP[material])])),
        ])))

    places = {place.name: place for place in ROOMS}
    for offset, (name, where, color, intensity) in enumerate(LIGHTS):
        place = places[where]
        node = entity(100 + offset, name, collections.OrderedDict([
            ("light", collections.OrderedDict([
                ("color", list(color)),
                ("intensity", intensity),
                ("type", "point"),
                ("range", 14.0),
                ("castsShadow", False),
            ])),
        ]))
        node["transform"]["position"] = [
            round((place.x0 + place.x1) / 2, 3),
            round(place.height - 0.55, 3),
            round((place.z0 + place.z1) / 2, 3),
        ]
        entities.append(node)

    scene = collections.OrderedDict([
        ("version", 1),
        ("environment", collections.OrderedDict([
            ("skyColor", [0.05, 0.055, 0.07]),
            ("groundColor", [0.018, 0.016, 0.014]),
            ("intensity", 1.0),
        ])),
        ("entities", sorted(entities, key=lambda e: e["id"])),
    ])
    SCENE.write_text(json.dumps(scene, indent=2) + "\n", encoding="utf-8")


def main():
    cells = build_cells()
    boundaries = collect_boundaries(cells)
    openings, links, skipped = place_openings(boundaries)

    meshes = {name: Mesh(size) for name, size in MATERIAL_SIZE.items()}
    emit_floors_and_ceilings(cells, meshes)
    emit_walls(boundaries, openings, meshes)
    emit_beams(meshes)

    groups = []
    total = 0
    for material in sorted(meshes):
        mesh = meshes[material]
        if mesh.empty():
            continue
        name = f"niveau_{material}"
        triangles = write_gltf(name, mesh, material)
        total += triangles
        groups.append((name, material))
        print(f"{name:<24} {triangles:6d} triangles  pas : {FOOTSTEP[material]}")

    write_scene(groups)

    doors = sum(len(holes) for holes in openings.values())
    steps = find_steps(wall_faces(boundaries))
    reached = check_connectivity(links)
    print(f"\n{len(ROOMS)} pieces, {len(cells)} m2 au sol, {doors} embrasures, "
          f"{total} triangles")
    for name_a, name_b, length in skipped:
        print(f"  frontiere trop courte pour une porte : {name_a}/{name_b} ({length} m)")
    if steps:
        # On refuse d'ecrire un batiment troue. Ce defaut ne se voit ni au chargement ni a
        # la marche : il faut donc l'arreter ici, au seul endroit ou il soit detectable.
        for axis, line, junction, before, after in steps:
            print(f"  RESSAUT sur le plan {'XYZ'[axis]}={line} en {junction} : "
                  f"{before:.2f} != {after:.2f}")
        raise SystemExit("murs non alignes : le batiment serait troue")
    print("  aucun ressaut : tous les murs sont d'aplomb")
    missing = sorted({place.name for place in ROOMS} - reached)
    if missing:
        print(f"  INATTEIGNABLES depuis le hall : {', '.join(missing)}")
    else:
        print("  toutes les pieces sont atteignables depuis le hall")
    print(f"scene ecrite : {SCENE.relative_to(REPO)}")


if __name__ == "__main__":
    main()
