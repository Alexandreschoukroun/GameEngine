"""Genere les sons de la scene de demonstration.

Les fichiers audio du depot ne sont pas telecharges : ils sont produits ici, a partir de
bruit filtre et d'oscillateurs. C'est la seule facon d'avoir des sons dont la provenance
est certaine, et de pouvoir les regenerer plutot que de les trainer dans l'historique Git.

Deux regles tenues par ce script :

  - MONO. Un son spatialise doit l'etre : un fichier stereo est deja reparti entre les
    enceintes par son auteur, lui donner une position dans le monde n'aurait pas de sens.
  - BOUCLABLE. Un fondu croise relie la fin au debut, sans quoi on entendrait un clic a
    chaque repetition.

Usage :  python tools/generate_audio.py
"""

import math
import pathlib
import random
import struct
import wave

RATE = 48000
OUTPUT = pathlib.Path(__file__).resolve().parent.parent / "assets" / "audio"


def write_wav(path, samples):
    """Ecrit un WAV mono 16 bits."""
    with wave.open(str(path), "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(RATE)
        handle.writeframes(
            b"".join(
                struct.pack("<h", max(-32767, min(32767, int(value * 32767))))
                for value in samples
            )
        )


def crossfade_loop(samples, fade_frames):
    """Relie la fin au debut pour que la boucle soit inaudible."""
    total = len(samples)
    for k in range(fade_frames):
        weight = k / fade_frames
        samples[k] = samples[k] * weight + samples[total - fade_frames + k] * (1.0 - weight)
    del samples[total - fade_frames:]
    return samples


def normalize(samples, target_peak):
    peak = max(abs(value) for value in samples)
    if peak <= 0.0:
        return samples
    scale = target_peak / peak
    return [value * scale for value in samples]


def make_embers(seconds=2.0):
    """Crepitement de braises : un souffle grave, ponctue d'impulsions breves."""
    random.seed(7)  # graine fixe : le meme fichier a chaque generation
    count = int(RATE * seconds)
    samples = [0.0] * count

    low = 0.0
    for i in range(count):
        # Filtre passe-bas a un pole : le souffle du foyer.
        low += (random.uniform(-1.0, 1.0) - low) * 0.02
        samples[i] = low * 0.55

    for _ in range(90):  # les crepitements
        start = random.randrange(0, count - 2000)
        amplitude = random.uniform(0.15, 0.6)
        decay = random.uniform(300.0, 1200.0)
        for k in range(1500):
            samples[start + k] += amplitude * math.exp(-k / decay) * random.uniform(-1.0, 1.0)

    return normalize(crossfade_loop(samples, RATE // 4), 0.99)


def make_breath(seconds=3.0):
    """Souffle grave : quelque chose respire dans la piece d'a cote."""
    random.seed(11)
    count = int(RATE * seconds)
    samples = [0.0] * count

    phase_a = phase_b = 0.0
    low = 0.0
    for i in range(count):
        seconds_elapsed = i / RATE
        # Deux fondamentales proches : leur battement lent cree une pulsation inquiete
        # sans qu'aucune note ne soit jouee.
        phase_a += 2.0 * math.pi * 58.0 / RATE
        phase_b += 2.0 * math.pi * 61.5 / RATE
        tone = math.sin(phase_a) * 0.5 + math.sin(phase_b) * 0.4

        low += (random.uniform(-1.0, 1.0) - low) * 0.008
        breath = low * 2.5 * (0.6 + 0.4 * math.sin(2.0 * math.pi * 0.23 * seconds_elapsed))
        samples[i] = (tone + breath) * 0.45

    return normalize(crossfade_loop(samples, RATE // 2), 0.85)


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name, samples in (("braises.wav", make_embers()), ("souffle.wav", make_breath())):
        path = OUTPUT / name
        write_wav(path, samples)
        print(f"{path.relative_to(OUTPUT.parent.parent)} : {path.stat().st_size // 1024} Ko")


if __name__ == "__main__":
    main()
