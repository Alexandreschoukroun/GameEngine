"""Telecharge un modele libre de Poly Haven et le range pour le moteur.

Le probleme qu'il resout
------------------------
Un modele glTF n'est pas un fichier mais un petit arbre : le .gltf qui decrit la scene, le
.bin qui porte la geometrie, et les textures que le .gltf cite par des chemins RELATIFS.
Telecharger le premier sans les autres donne un fichier qui se charge a moitie, ou pas du
tout - et l'erreur ne se voit qu'au lancement du jeu.

Poly Haven publie la liste exacte de ces dependances dans son API. Cet outil la suit, ce
qui rend l'operation exacte par construction plutot que par attention.

La resolution par defaut est 1k, et c'est un choix : une caisse qu'on voit a deux metres
n'a pas besoin de 4096 pixels, et chaque modele pese alors deux megaoctets au lieu de
trente. Le depot en contient plusieurs dizaines.

Le moteur lit ensuite ces fichiers SANS RIEN SAVOIR de Poly Haven : c'est le chargeur glTF
de M5.5 qui s'en occupe, le meme qui lit Suzanne. Un modele importe est enregistre sous son
nom logique, avec sa geometrie de collision, et devient citable depuis une scene.

Usage :
    python tools/fetch_model.py <nom logique> <identifiant Poly Haven> [--resolution 2k]

Exemple :
    python tools/fetch_model.py caisse_bois wooden_crate_02
    python tools/fetch_model.py table WoodenTable_02 --resolution 2k
"""

import argparse
import json
import pathlib
import sys
import urllib.request

REPO = pathlib.Path(__file__).resolve().parent.parent
MODELS = REPO / "assets" / "models"

API = "https://api.polyhaven.com/files/{asset}"
AGENT = "GameEngine-fetch-model/1.0 (outil d'apprentissage, usage personnel)"


def get(url, binary=False):
    request = urllib.request.Request(url, headers={"User-Agent": AGENT})
    with urllib.request.urlopen(request, timeout=180) as response:
        payload = response.read()
    return payload if binary else json.loads(payload)


def fetch(name, asset, resolution):
    files = get(API.format(asset=asset))
    if "gltf" not in files:
        print(f"{asset} n'est pas publie en glTF", file=sys.stderr)
        return 1
    if resolution not in files["gltf"]:
        available = ", ".join(sorted(files["gltf"]))
        print(f"resolution {resolution} indisponible ; il y a : {available}", file=sys.stderr)
        return 1

    entry = files["gltf"][resolution]["gltf"]
    destination = MODELS / name
    destination.mkdir(parents=True, exist_ok=True)

    # Le .gltf garde le nom que le site lui donne : c'est lui que le jeu citera, et le
    # laisser tel quel rend la provenance lisible dans le tableau des modeles importes.
    main = pathlib.Path(entry["url"]).name
    total = 0
    print(f"{name:<20} {asset}")
    for relative, url in [(main, entry["url"])] + [
            (path, item["url"]) for path, item in entry.get("include", {}).items()]:
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        payload = get(url, binary=True)
        target.write_bytes(payload)
        total += len(payload)
        print(f"   {relative:<44} {len(payload) / 1000:8.0f} ko")

    print(f"   {'total':<44} {total / 1_000_000:8.1f} Mo   "
          f"source : Poly Haven {asset}, CC0")
    print(f"   a citer comme : models/{name}/{main}")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("name", help="nom du dossier sous assets/models/")
    parser.add_argument("asset", help="identifiant Poly Haven, ex. wooden_crate_02")
    parser.add_argument("--resolution", default="1k", help="1k par defaut")
    args = parser.parse_args()
    return fetch(args.name, args.asset, args.resolution)


if __name__ == "__main__":
    raise SystemExit(main())
