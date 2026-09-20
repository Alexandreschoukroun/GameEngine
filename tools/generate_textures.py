"""Genere les textures de la scene de demonstration : couleur de base et relief.

Comme les sons, ces textures sont produites plutot que telechargees : provenance certaine,
regenerable, et aucune licence a surveiller.

Une carte de normales encode, pour chaque pixel, l'orientation de la surface a cet endroit.
Elle est calculee ici a partir d'un RELIEF (une hauteur par pixel) : la normale est la
perpendiculaire a la pente locale, obtenue par differences finies entre pixels voisins.

Le codage est celui de tout le monde : une composante de -1 a 1 devient un octet de 0 a
255. Une surface parfaitement plate vaut donc (128, 128, 255), le bleu lavande
caracteristique de ces textures.

Les deux sortent du MEME relief, ce qui est indispensable : si la couleur dessinait des
joints a un endroit et le relief a un autre, la surface se contredirait elle-meme et
l'oeil le verrait immediatement.

Attention au codage : une couleur de base est destinee a l'oeil, elle se charge en sRGB.
Une carte de normales contient des directions, elle se charge en lineaire. Intervertir les
deux donne soit des couleurs delavees, soit un relief penche de travers.

Usage :  python tools/generate_textures.py
"""

import math
import pathlib
import random
import struct
import zlib

SIZE = 512
OUTPUT = pathlib.Path(__file__).resolve().parent.parent / "assets" / "textures"


def write_png(path, pixels, width, height):
    """Ecrit un PNG RGB 8 bits, sans dependance externe."""

    def chunk(tag, payload):
        data = tag + payload
        return struct.pack(">I", len(payload)) + data + struct.pack(">I", zlib.crc32(data))

    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filtre "None" pour cette ligne
        row = pixels[y * width * 3:(y + 1) * width * 3]
        raw.extend(row)

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)  # 8 bits, type 2 = RGB
    blob = (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))
    path.write_bytes(blob)


def heights_to_normals(heights, width, height, strength):
    """Convertit un relief en carte de normales.

    La pente en un point se mesure par la difference de hauteur entre ses voisins. Les
    indices bouclent (modulo) pour que la texture reste raccordable : sans cela, une
    couture apparaitrait sur chaque bord.
    """
    pixels = bytearray(width * height * 3)
    for y in range(height):
        for x in range(width):
            left = heights[y * width + (x - 1) % width]
            right = heights[y * width + (x + 1) % width]
            up = heights[((y - 1) % height) * width + x]
            down = heights[((y + 1) % height) * width + x]

            # Le signe : une hauteur qui croit vers la droite incline la normale vers la
            # gauche, d'ou le moins.
            nx = (left - right) * strength
            ny = (up - down) * strength
            nz = 1.0
            length = math.sqrt(nx * nx + ny * ny + nz * nz)

            index = (y * width + x) * 3
            pixels[index + 0] = int((nx / length * 0.5 + 0.5) * 255.0)
            pixels[index + 1] = int((ny / length * 0.5 + 0.5) * 255.0)
            pixels[index + 2] = int((nz / length * 0.5 + 0.5) * 255.0)
    return pixels


def heights_to_albedo(heights, shades, width, height):
    """Colore un relief : les creux sont plus sombres, les bosses plus claires.

    C'est l'occlusion ambiante du pauvre - dans la vraie vie, un joint recoit moins de
    lumiere qu'une surface exposee. La calculer depuis le relief garantit que les deux
    textures parlent du meme objet.
    """
    pixels = bytearray(width * height * 3)
    low, high = shades
    for i, value in enumerate(heights):
        t = max(0.0, min(1.0, value))
        for channel in range(3):
            mixed = low[channel] + (high[channel] - low[channel]) * t
            pixels[i * 3 + channel] = int(max(0.0, min(255.0, mixed)))
    return pixels


def smooth_edge(distance, width):
    """Transition douce sur `width` pixels : 0 au fond du joint, 1 sur la dalle."""
    if width <= 0.0:
        return 1.0
    t = max(0.0, min(1.0, distance / width))
    return t * t * (3.0 - 2.0 * t)


def make_stone(tiles=4, joint=10.0, bevel=14.0):
    """Relief de dalles de pierre : joints creuses, bords adoucis, surface irreguliere."""
    random.seed(101)
    cell = SIZE / tiles
    heights = [0.0] * (SIZE * SIZE)

    # Un bruit doux, commun a toute la surface : la pierre n'est jamais parfaitement lisse.
    grain = [random.uniform(-1.0, 1.0) for _ in range(SIZE * SIZE)]
    for _ in range(6):  # quelques passes de moyennage valent un vrai filtre passe-bas
        smoothed = grain[:]
        for y in range(SIZE):
            for x in range(SIZE):
                total = (grain[y * SIZE + x]
                         + grain[y * SIZE + (x + 1) % SIZE]
                         + grain[y * SIZE + (x - 1) % SIZE]
                         + grain[((y + 1) % SIZE) * SIZE + x]
                         + grain[((y - 1) % SIZE) * SIZE + x])
                smoothed[y * SIZE + x] = total / 5.0
        grain = smoothed

    for y in range(SIZE):
        for x in range(SIZE):
            # Distance au joint le plus proche, dans les deux axes.
            dx = min(x % cell, cell - (x % cell))
            dy = min(y % cell, cell - (y % cell))
            inside = min(smooth_edge(dx - joint * 0.5, bevel),
                         smooth_edge(dy - joint * 0.5, bevel))
            heights[y * SIZE + x] = inside + grain[y * SIZE + x] * 0.05 * inside

    return heights


def make_planks(planks=6, groove=7.0, bevel=5.0):
    """Relief de planches : rainures entre lames, et fil du bois oriente."""
    random.seed(202)
    lane = SIZE / planks
    heights = [0.0] * (SIZE * SIZE)

    # Chaque lame a sa propre hauteur et son propre fil : des planches identiques se
    # verraient immediatement comme un motif repete.
    lane_offset = [random.uniform(-0.05, 0.05) for _ in range(planks)]
    lane_phase = [random.uniform(0.0, 6.28) for _ in range(planks)]

    for y in range(SIZE):
        lane_index = int(y / lane) % planks
        dy = min(y % lane, lane - (y % lane))
        between = smooth_edge(dy - groove * 0.5, bevel)
        for x in range(SIZE):
            # Le fil du bois : des ondulations etirees le long de la planche.
            fibre = math.sin(x * 0.055 + lane_phase[lane_index]) * 0.06
            fibre += math.sin(x * 0.21 + lane_phase[lane_index] * 2.0) * 0.025
            heights[y * SIZE + x] = (between + lane_offset[lane_index] + fibre) * between

    return heights


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)

    stone = make_stone()
    planks = make_planks()

    outputs = (
        # La force du relief se CALCULE, elle ne se tatonne pas : la hauteur de la pierre
        # varie de 1 sur un chanfrein de 14 pixels, soit environ 0,14 entre deux voisins.
        # Pour que la pente la plus raide atteigne 45 degres, il faut que nx y vaille 1 -
        # donc une force d'environ 7.
        ("pierre_normal.png", heights_to_normals(stone, SIZE, SIZE, strength=7.0)),
        # Les rainures du bois sont plus etroites mais bien moins profondes : une force
        # plus faible suffit.
        ("bois_normal.png", heights_to_normals(planks, SIZE, SIZE, strength=5.0)),
        # Pierre : un gris froid, nettement plus sombre dans les joints.
        ("pierre_couleur.png", heights_to_albedo(stone, ((58, 58, 62), (148, 146, 140)),
                                                 SIZE, SIZE)),
        # Bois : un brun chaud, presque noir au fond des rainures.
        ("bois_couleur.png", heights_to_albedo(planks, ((36, 24, 16), (146, 102, 62)),
                                               SIZE, SIZE)),
    )

    for name, pixels in outputs:
        path = OUTPUT / name
        write_png(path, pixels, SIZE, SIZE)
        print(f"{path.relative_to(OUTPUT.parent.parent)} : {path.stat().st_size // 1024} Ko")


if __name__ == "__main__":
    main()
