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


def make_footstep(seed, resonances, brightness, decay, seconds=0.28):
    """Un pas : une frappe breve, plus un peu de resonance de la matiere.

    Un pas reel est un choc - donc une impulsion qui decroit vite - colore par ce que la
    surface renvoie. La pierre renvoie des frequences hautes et s'eteint aussitot ; le
    bois creux resonne plus bas et plus longtemps. C'est cette difference que l'oreille
    utilise pour reconnaitre le sol sous ses pieds.
    """
    random.seed(seed)
    count = int(RATE * seconds)
    samples = [0.0] * count

    # Le choc : du bruit qui s'eteint exponentiellement.
    low = 0.0
    for i in range(count):
        white = random.uniform(-1.0, 1.0)
        low += (white - low) * brightness
        samples[i] = low * math.exp(-i / (RATE * decay))

    # Les resonances de la matiere, elles aussi amorties.
    for frequency, amplitude, length in resonances:
        phase = 0.0
        for i in range(count):
            phase += 2.0 * math.pi * frequency / RATE
            samples[i] += math.sin(phase) * amplitude * math.exp(-i / (RATE * length))

    # Une attaque tres courte evite le clic d'un signal qui demarre a pleine amplitude.
    attack = int(RATE * 0.002)
    for k in range(attack):
        samples[k] *= k / attack

    return normalize(samples, 0.9)


def make_tension_layer(seed, partials, noise_amount, pulse_hz, seconds=4.0):
    """Une couche de la musique de tension.

    Le systeme ne joue pas des morceaux : il superpose en permanence trois couches et
    fait varier leurs volumes. Chacune doit donc etre BOUCLABLE et sans evenement
    marquant - une couche dont on reconnait le debut trahirait la boucle des la deuxieme
    repetition.

    partials     : (frequence, amplitude) des composantes harmoniques
    noise_amount : part de souffle filtre, qui donne la matiere
    pulse_hz     : battement lent du volume, 0 pour une couche immobile
    """
    random.seed(seed)
    count = int(RATE * seconds)
    samples = [0.0] * count

    phases = [0.0] * len(partials)
    low = 0.0
    for i in range(count):
        value = 0.0
        for index, (frequency, amplitude) in enumerate(partials):
            phases[index] += 2.0 * math.pi * frequency / RATE
            value += math.sin(phases[index]) * amplitude

        low += (random.uniform(-1.0, 1.0) - low) * 0.05
        value += low * noise_amount

        if pulse_hz > 0.0:
            # Le battement est cale sur la duree de la boucle pour qu'il se raccorde.
            value *= 0.55 + 0.45 * math.sin(2.0 * math.pi * pulse_hz * i / RATE)

        samples[i] = value * 0.5

    return normalize(crossfade_loop(samples, RATE), 0.8)


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    sounds = (
        ("braises.wav", make_embers()),
        ("souffle.wav", make_breath()),
        # Pierre : clair, sec, sans resonance basse. Il s'eteint presque aussitot.
        ("pas_pierre.wav", make_footstep(21, [(900.0, 0.25, 0.012), (1700.0, 0.12, 0.008)],
                                         brightness=0.35, decay=0.030)),
        # Bois creux : plus sourd, et il resonne - c'est le plancher qui vibre sous le pied.
        ("pas_bois.wav", make_footstep(22, [(180.0, 0.55, 0.070), (320.0, 0.30, 0.045)],
                                       brightness=0.08, decay=0.055)),
        # --- les trois couches de la musique de tension ---------------------------------
        # Calme : une seule fondamentale grave et son octave. Presente en permanence,
        # c'est le lit sur lequel les autres se posent.
        ("tension_calme.wav", make_tension_layer(31, [(55.0, 0.6), (110.0, 0.2)],
                                                 noise_amount=0.5, pulse_hz=0.0)),
        # Pouls : un battement lent, a la frequence d'un coeur au repos (1 Hz), sur une
        # quinte. C'est la couche qui dit "quelque chose se prepare".
        ("tension_pouls.wav", make_tension_layer(32, [(82.5, 0.5), (123.5, 0.3)],
                                                 noise_amount=0.3, pulse_hz=1.0)),
        # Aigu : une SECONDE MINEURE - deux notes separees d'un demi-ton, l'intervalle le
        # plus dissonant de la gamme. C'est physiologique : leurs ondes se heurtent et
        # produisent un battement rapide que l'oreille percoit comme une agression.
        ("tension_aigu.wav", make_tension_layer(33, [(440.0, 0.35), (466.2, 0.35),
                                                     (880.0, 0.12)],
                                                noise_amount=0.15, pulse_hz=0.0)),
    )
    for name, samples in sounds:
        path = OUTPUT / name
        write_wav(path, samples)
        print(f"{path.relative_to(OUTPUT.parent.parent)} : {path.stat().st_size // 1024} Ko")


if __name__ == "__main__":
    main()
