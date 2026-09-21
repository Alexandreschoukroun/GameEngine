"""Genere un niveau : sa geometrie en glTF, et la scene qui la place.

Pourquoi un generateur plutot qu'un modeleur
--------------------------------------------
Un niveau d'interieur d'horreur est fait de pieces rectangulaires, de couloirs et
d'embrasures. C'est une description, pas une sculpture - et une description se REGLE :
changer la largeur d'un couloir est ici un nombre ; dans un fichier .blend, c'est une
demi-heure. Le jour ou le niveau demandera des moulures et des fissures, Blender reprendra
la main ; les deux entrent par le meme chargeur glTF.

Le plan est pose sur une GRILLE de cellules d'un metre. Chaque cellule occupee produit son
sol et son plafond ; un mur n'est emis que la ou une cellule borde le vide. Cette regle,
qui tient en trois lignes, garantit deux choses qu'un assemblage piece par piece rend
penibles : aucune face en double la ou deux pieces se touchent, et aucun trou.

Usage :  python tools/generate_level.py
"""

import collections
import json
import pathlib
import struct

REPO = pathlib.Path(__file__).resolve().parent.parent
MODELS = REPO / "assets" / "models" / "niveau"
SCENE = REPO / "assets" / "scenes" / "niveau.json"

FLOOR_Y = 0.0
CEILING_Y = 3.0
DOOR_HEIGHT = 2.1
DOOR_HALF_WIDTH = 0.6

# Taille reelle de chaque matiere, en metres : elle decide de la repetition des textures.
# Une matiere de 2 m doit se repeter tous les deux metres, sinon elle parait geante et
# floue - c'est le piege le plus courant avec des textures libres.
MATERIAL_SIZE = {
    "beton": 2.0,
    "platre": 2.0,
    "carrelage": 2.0,
    "plancher": 1.5,
}

WALL_MATERIAL = "platre"
CEILING_MATERIAL = "beton"

# --- le plan ------------------------------------------------------------------------------

Room = collections.namedtuple("Room", "name x0 x1 z0 z1 floor")

ROOMS = [
    Room("hall", -4, 4, 0, 6, "carrelage"),
    Room("couloir", -1, 1, 6, 16, "carrelage"),
    Room("chambre", -8, -1, 8, 14, "plancher"),
    Room("atelier", 1, 8, 9, 15, "plancher"),
    Room("reserve", -3, 3, 16, 21, "beton"),
]

# (cellule, direction) : l'embrasure perce le mur entre cette cellule et sa voisine.
DOORWAYS = [
    ((0, 5), (0, 1)),
    ((-1, 10), (-1, 0)),
    ((0, 11), (1, 0)),
    ((0, 15), (0, 1)),
]

# Lumieres du niveau : position et couleur. Volontairement rares et chaudes - dans ce
# genre, l'obscurite est le sujet, pas un defaut a corriger.
LIGHTS = [
    ("lampe_hall", [0.0, 2.6, 3.0], [1.0, 0.82, 0.6], 14.0),
    ("lampe_couloir", [0.0, 2.6, 11.0], [0.9, 0.85, 0.7], 9.0),
    ("lampe_atelier", [4.5, 2.6, 12.0], [1.0, 0.75, 0.5], 12.0),
    ("lampe_reserve", [0.0, 2.6, 18.5], [0.7, 0.8, 1.0], 8.0),
]


def build_cells():
    """Cellules occupees, avec la matiere de leur sol."""
    cells = {}
    for room in ROOMS:
        for x in range(room.x0, room.x1):
            for z in range(room.z0, room.z1):
                # Une cellule deja prise garde sa matiere : le couloir traverse le hall
                # sans y changer le carrelage.
                cells.setdefault((x, z), room.floor)
    return cells


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


def emit_floor_and_ceiling(cells, meshes):
    for (x, z), material in cells.items():
        floor = meshes[material]
        # Vu d'au-dessus, dans le sens antihoraire : la normale pointe vers le haut, donc
        # vers le joueur qui marche dessus.
        floor.quad(
            [(x, FLOOR_Y, z), (x, FLOOR_Y, z + 1), (x + 1, FLOOR_Y, z + 1), (x + 1, FLOOR_Y, z)],
            (0.0, 1.0, 0.0),
            [(x, z), (x, z + 1), (x + 1, z + 1), (x + 1, z)],
        )
        ceiling = meshes[CEILING_MATERIAL]
        # Ordre inverse : le plafond se regarde d'en dessous.
        ceiling.quad(
            [(x, CEILING_Y, z), (x + 1, CEILING_Y, z), (x + 1, CEILING_Y, z + 1), (x, CEILING_Y, z + 1)],
            (0.0, -1.0, 0.0),
            [(x, z), (x + 1, z), (x + 1, z + 1), (x, z + 1)],
        )


def wall_corners(x, z, direction, low, high, t0, t1):
    """Les quatre coins d'un pan de mur, dans l'ordre qui le fait regarder l'interieur.

    `direction` est celle du VIDE : le mur ferme ce cote de la cellule. `t0`/`t1` bornent
    le pan le long du mur, `low`/`high` sa hauteur - c'est ce qui permet de contourner une
    embrasure.
    """
    dx, dz = direction
    if dx == 1:
        wall_x = x + 1
        return [(wall_x, low, t1), (wall_x, low, t0), (wall_x, high, t0), (wall_x, high, t1)], (-1.0, 0.0, 0.0)
    if dx == -1:
        wall_x = x
        return [(wall_x, low, t0), (wall_x, low, t1), (wall_x, high, t1), (wall_x, high, t0)], (1.0, 0.0, 0.0)
    if dz == 1:
        wall_z = z + 1
        return [(t0, low, wall_z), (t1, low, wall_z), (t1, high, wall_z), (t0, high, wall_z)], (0.0, 0.0, -1.0)
    wall_z = z
    return [(t1, low, wall_z), (t0, low, wall_z), (t0, high, wall_z), (t1, high, wall_z)], (0.0, 0.0, 1.0)


def emit_wall(mesh, x, z, direction, doorway):
    """Un pan de mur, perce d'une embrasure si la cellule en declare une de ce cote."""
    dx, dz = direction
    # Coordonnee le long du mur : X pour un mur oriente en Z, Z sinon.
    along0, along1 = (x, x + 1) if dz != 0 else (z, z + 1)

    if not doorway:
        segments = [(along0, along1, FLOOR_Y, CEILING_Y)]
    else:
        middle = (along0 + along1) * 0.5
        left, right = middle - DOOR_HALF_WIDTH, middle + DOOR_HALF_WIDTH
        segments = [
            (along0, left, FLOOR_Y, CEILING_Y),      # jambage
            (right, along1, FLOOR_Y, CEILING_Y),     # jambage
            (left, right, DOOR_HEIGHT, CEILING_Y),   # linteau
        ]

    for t0, t1, low, high in segments:
        if t1 - t0 < 1e-4 or high - low < 1e-4:
            continue
        corners, normal = wall_corners(x, z, direction, low, high, t0, t1)
        # On INVERSE l'ordre des coins. wall_corners les donne dans le sens qui produit
        # une normale geometrique opposee a celle qu'on declare - et c'est l'ENROULEMENT
        # qui compte, pas la declaration : l'elimination des faces arriere ne regarde que
        # lui. Des murs enroules a l'envers seraient invisibles depuis l'interieur.
        #
        # En revanche ils resteraient SOLIDES : un personnage de Jolt heurte aussi les
        # faces arriere. Marcher dans le niveau ne prouve donc rien sur son enroulement -
        # c'est verifie dans tests/scene/test_niveau.cpp, par la normale que rapporte un
        # rayon.
        corners = corners[::-1]
        # Les coordonnees de texture suivent le MUR : sa longueur et sa hauteur. Les
        # prendre dans le plan du sol etirerait la matiere sur les surfaces verticales.
        uvs = [(t1, low), (t0, low), (t0, high), (t1, high)]
        if (dx, dz) in ((-1, 0), (0, 1)):
            uvs = [(t0, low), (t1, low), (t1, high), (t0, high)]
        # Les coordonnees suivent les coins : elles s'inversent avec eux.
        mesh.quad(corners, normal, uvs[::-1])


def emit_walls(cells, meshes):
    doorways = set(DOORWAYS)
    mesh = meshes[WALL_MATERIAL]
    for (x, z) in cells:
        for direction in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            neighbour = (x + direction[0], z + direction[1])
            if neighbour in cells:
                continue  # deux cellules voisines : pas de mur entre elles
            emit_wall(mesh, x, z, direction, ((x, z), direction) in doorways)


# --- ecriture glTF -------------------------------------------------------------------------

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


# --- la scene ---------------------------------------------------------------------------

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
            ("surface", collections.OrderedDict([
                ("footstep", "pas_bois" if material == "plancher" else "pas_pierre"),
            ])),
        ])))

    for offset, (name, position, color, intensity) in enumerate(LIGHTS):
        node = entity(100 + offset, name, collections.OrderedDict([
            ("light", collections.OrderedDict([
                ("color", list(color)),
                ("intensity", intensity),
                ("type", "point"),
                ("range", 12.0),
                ("castsShadow", False),
            ])),
        ]))
        node["transform"]["position"] = list(position)
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
    meshes = {name: Mesh(size) for name, size in MATERIAL_SIZE.items()}

    emit_floor_and_ceiling(cells, meshes)
    emit_walls(cells, meshes)

    groups = []
    for material, mesh in meshes.items():
        if mesh.empty():
            continue
        name = f"niveau_{material}"
        triangles = write_gltf(name, mesh, material)
        groups.append((name, material))
        print(f"{name:<20} {triangles:6d} triangles  matiere {material}")

    write_scene(groups)
    area = len(cells)
    print(f"\n{len(ROOMS)} pieces, {area} m2 au sol, {len(DOORWAYS)} embrasures")
    print(f"scene ecrite : {SCENE.relative_to(REPO)}")


if __name__ == "__main__":
    main()
