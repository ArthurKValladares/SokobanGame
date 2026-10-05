"""Generate original, deterministic PCM sound effects; requires NumPy.

Run from any directory. Loop oscillators and filtered noise are periodic, so
the minecart and elevator files can wrap without a fade or a silent gap.
"""

from pathlib import Path
import json
import wave

import numpy as np


RATE = 44100
OUTPUT = Path(__file__).resolve().parents[1] / "assets/custom/audio"
RNG = np.random.default_rng(20261005)


def noise(count, low, high):
    spectrum = np.fft.rfft(RNG.standard_normal(count))
    frequencies = np.fft.rfftfreq(count, 1 / RATE)
    spectrum *= np.exp(-((frequencies / high) ** 4)) * (
        1 - np.exp(-((frequencies / max(low, 1)) ** 4))
    )
    result = np.fft.irfft(spectrum, n=count)
    return result / max(np.std(result), 1e-9)


def envelope(t, duration, attack=0.015, release=0.06):
    return np.minimum(t / attack, 1) * np.minimum((duration - t) / release, 1)


def write(name, signal, peak=0.65, loop=False):
    signal = signal - np.mean(signal)
    signal *= peak / max(np.max(np.abs(signal)), 1e-9)
    pcm = np.rint(signal * 32767).astype("<i2")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    with wave.open(str(OUTPUT / name), "wb") as stream:
        stream.setnchannels(1)
        stream.setsampwidth(2)
        stream.setframerate(RATE)
        stream.writeframes(pcm.tobytes())
    report = {"file": name, "seconds": len(pcm) / RATE, "peak": peak}
    if loop:
        # The wrap must be as smooth as ordinary adjacent samples.
        report["wrap_step"] = abs(int(pcm[0]) - int(pcm[-1])) / 32767
        report["rms"] = float(np.sqrt(np.mean(signal**2)))
    print(json.dumps(report))


def click(name, duration, frequency, release=False):
    t = np.arange(round(duration * RATE)) / RATE
    phase = 2 * np.pi * (frequency * t - frequency * 0.8 * t**2 / duration)
    body = np.sin(phase) * np.exp(-t * (26 if release else 42))
    tick = noise(len(t), 1200, 7000) * np.exp(-t * 140)
    signal = (0.75 * body + 0.15 * tick) * envelope(t, duration, 0.001, 0.025)
    write(name, signal, 0.5)


def main():
    duration = 0.65
    t = np.arange(round(duration * RATE)) / RATE
    sweep = np.sin(2 * np.pi * (230 * t + 650 * t**2 / duration))
    sparkle = np.sin(2 * np.pi * (1400 * t - 550 * t**2 / duration))
    signal = (0.32 * sweep + 0.12 * sparkle + 0.17 * noise(len(t), 400, 4500))
    signal *= np.sin(np.pi * t / duration) ** 1.8
    write("portalTravel.wav", signal)

    duration = 2.0
    t = np.arange(round(duration * RATE)) / RATE
    # Sixteen wheel joints per loop, a low chassis rumble and metallic overtones.
    wheel_phase = np.mod(t, 0.125)
    joints = np.sin(2 * np.pi * 176 * wheel_phase) * np.exp(-wheel_phase * 80)
    joints *= np.minimum(wheel_phase / 0.002, 1)
    rumble = 0.14 * np.sin(2 * np.pi * 52 * t) + 0.05 * np.sin(2 * np.pi * 104 * t)
    signal = rumble + 0.23 * joints + 0.09 * noise(len(t), 50, 1500)
    signal += 0.025 * np.sin(2 * np.pi * 726 * t) * (0.6 + 0.4 * np.cos(2 * np.pi * 8 * t))
    write("minecartTravelLoop.wav", signal, 0.48, loop=True)

    for name, duration, frequency, sweep_rate in (
        ("minecartGateOpen.wav", 0.42, 180, 400),
        ("rotatorTurn.wav", 0.38, 130, -55),
    ):
        t = np.arange(round(duration * RATE)) / RATE
        motor = np.sin(2 * np.pi * (frequency * t + sweep_rate * t**2))
        gears = noise(len(t), 500, 3500) * (0.45 + 0.25 * np.sin(2 * np.pi * 35 * t))
        signal = (0.35 * motor + 0.14 * gears) * envelope(t, duration, 0.012, 0.09)
        write(name, signal, 0.55)

    click("pressurePlatePress.wav", 0.14, 150)
    click("pressurePlateRelease.wav", 0.18, 230, release=True)
    click("buttonPress.wav", 0.09, 680)

    duration = 2.0
    t = np.arange(round(duration * RATE)) / RATE
    hum = 0.24 * np.sin(2 * np.pi * 90 * t) + 0.08 * np.sin(2 * np.pi * 180 * t)
    hum += 0.04 * np.sin(2 * np.pi * 360 * t)
    signal = hum * (0.85 + 0.15 * np.cos(2 * np.pi * 3 * t))
    signal += 0.04 * noise(len(t), 200, 2800)
    write("elevatorMovingLoop.wav", signal, 0.42, loop=True)

    for closing in (False, True):
        duration = 0.30 if closing else 0.36
        t = np.arange(round(duration * RATE)) / RATE
        start, end = (560, 150) if closing else (150, 560)
        phase = 2 * np.pi * (start * t + (end - start) * t**2 / (2 * duration))
        motor = np.sin(phase) + 0.16 * np.sin(phase * 2)
        slide = noise(len(t), 350, 3800)
        signal = (0.32 * motor + 0.09 * slide) * envelope(t, duration, 0.012, 0.07)
        latch_time = duration - 0.065 if closing else 0.005
        latch_t = np.maximum(t - latch_time, 0)
        latch = (0.35 * np.sin(2 * np.pi * 105 * latch_t) + 0.08 * slide)
        latch *= np.exp(-latch_t * 65) * np.minimum(latch_t / 0.002, 1)
        signal += latch * (t >= latch_time) * envelope(t, duration)
        write("gateClose.wav" if closing else "gateOpen.wav", signal, 0.55)


if __name__ == "__main__":
    main()
