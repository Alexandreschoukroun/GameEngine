"""Telecharge une matiere libre d'ambientCG et la prepare pour le moteur.

Le probleme qu'il resout
------------------------
Une matiere PBR libre arrive en cinq ou six fichiers aux noms et aux formats du site qui
la publie : `Color`, `NormalGL`, `Roughness`, `AmbientOcclusion`, `Displacement`. Le
moteur, lui, en attend exactement trois, aux noms fixes :

    assets/textures/<nom>/couleur.jpg   la couleur de base, en sRGB
    assets/textures/<nom>/normal.png    la carte de normales, variante OpenGL
    assets/textures/<nom>/matiere.png   R = occlusion, G = rugosite, B = metallicite

La conversion tient en quatre gestes, mais les quatre se trompent facilement, et deux
d'entre eux ne se voient qu'a l'usage : prendre la carte de normales DirectX au lieu de
la variante OpenGL donne un relief en creux la ou il faut une bosse, et oublier la
metallicite d'un metal le rend mat comme du plastique.

Cet outil fige ces gestes. Il fait AUSSI le telechargement, pour que la provenance d'une
matiere soit une ligne de commande consultable plutot qu'un souvenir.

L'empaquetage du canal matiere est delegue a tools/pack_material, ecrit en C++ : il
reutilise le chargeur d'images du moteur, donc il lit exactement ce que le moteur sait
lire. Il faut l'avoir compile.

Usage :
    python tools/fetch_material.py <nom> <identifiant ambientCG> [--metal]

Exemple :
    python tools/fetch_material.py brique Bricks075A
    python tools/fetch_material.py acier Metal046A --metal
"""

import argparse
import io
import pathlib
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

REPO = pathlib.Path(__file__).resolve().parent.parent
TEXTURES = REPO / "assets" / "textures"

# ambientCG sert ses archives derriere une seule URL parametree. La variante 1K-JPG est
# celle qui convient ici : 1024 pixels suffisent largement pour une surface qu'on voit a
# un metre, et le JPEG divise le poids par dix face au PNG sans defaut visible sur une
# photo de matiere.
DOWNLOAD = "https://ambientcg.com/get?file={asset}_1K-JPG.zip"
AGENT = "GameEngine-fetch-material/1.0 (outil d'apprentissage, usage personnel)"

# Les suffixes du site, et ce qu'on en fait. L'ordre compte pour les messages d'erreur :
# les deux premiers sont indispensables, les deux autres ameliorent le resultat.
COLOR = "_Color."
NORMAL = "_NormalGL."          # SURTOUT PAS _NormalDX : son canal vert est inverse
ROUGHNESS = "_Roughness."
OCCLUSION = "_AmbientOcclusion."


def find_packer():
    """Retrouve pack_material dans les emplacements de compilation habituels."""
    candidates = [
        REPO / "build/windows-msvc/tools/pack_material/Release/pack_material.exe",
        REPO / "build/windows-msvc/tools/pack_material/Debug/pack_material.exe",
        REPO / "build/tools/pack_material/pack_material",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def download(asset):
    url = DOWNLOAD.format(asset=asset)
    print(f"telechargement  {url}")
    # Le site refuse l'agent par defaut d'urllib par un 403. On se presente donc, ce qui
    # est de toute facon la politesse minimale envers un serveur qui distribue gratuitement
    # plusieurs gigaoctets.
    request = urllib.request.Request(url, headers={"User-Agent": AGENT})
    with urllib.request.urlopen(request, timeout=120) as response:
        payload = response.read()
    print(f"                {len(payload) / 1_000_000:.1f} Mo")
    return zipfile.ZipFile(io.BytesIO(payload))


def extract(archive, directory):
    """Range les fichiers de l'archive par role, d'apres leur suffixe."""
    found = {}
    for name in archive.namelist():
        for role, suffix in (("couleur", COLOR), ("normal", NORMAL),
                             ("rugosite", ROUGHNESS), ("occlusion", OCCLUSION)):
            if suffix in name:
                target = directory / pathlib.Path(name).name
                target.write_bytes(archive.read(name))
                found[role] = target
    return found


def to_png(source, destination):
    """Reecrit une image en PNG.

    La carte de normales et la carte matiere contiennent des DIRECTIONS et des MESURES,
    pas des couleurs. Le JPEG, qui compresse en jetant ce que l'oeil ne verrait pas sur
    une photo, y laisse des artefacts qui se lisent comme du relief. On les garde donc
    sans perte, contrairement a la couleur de base.
    """
    from PIL import Image

    with Image.open(source) as image:
        image.convert("RGB").save(destination, format="PNG", optimize=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("name", help="nom logique dans le moteur, ex. brique")
    parser.add_argument("asset", help="identifiant ambientCG, ex. Bricks075A")
    parser.add_argument("--metal", action="store_true",
                        help="matiere metallique : metallicite a 1 au lieu de 0")
    args = parser.parse_args()

    packer = find_packer()
    if packer is None:
        print("pack_material introuvable : compilez le projet d'abord.", file=sys.stderr)
        return 1

    destination = TEXTURES / args.name
    destination.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as temporary:
        directory = pathlib.Path(temporary)
        files = extract(download(args.asset), directory)

        for role in ("couleur", "normal"):
            if role not in files:
                print(f"l'archive ne contient pas de carte {role}", file=sys.stderr)
                return 1

        # La couleur reste en JPEG : c'est une photo, destinee a l'oeil.
        shutil.copyfile(files["couleur"], destination / "couleur.jpg")
        to_png(files["normal"], destination / "normal.png")

        command = [str(packer), str(destination / "matiere.png"),
                   "--metalness", "1" if args.metal else "0"]
        # Sans carte de rugosite, une constante mediane vaut mieux qu'un echec : une
        # surface un peu trop lisse reste utilisable, une matiere absente non.
        command += ["--roughness", str(files["rugosite"]) if "rugosite" in files else "0.6"]
        if "occlusion" in files:
            command += ["--ao", str(files["occlusion"])]

        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0:
            print(result.stdout, result.stderr, file=sys.stderr)
            return 1

    total = sum(f.stat().st_size for f in destination.iterdir())
    print(f"{args.name:<18} pret dans {destination.relative_to(REPO)}  "
          f"({total / 1_000_000:.1f} Mo)  source : ambientCG {args.asset}, CC0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
