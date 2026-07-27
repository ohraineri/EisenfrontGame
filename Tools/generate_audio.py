#!/usr/bin/env python3
"""
Offline generator for Outpost's placeholder audio (Assets/audio/*.wav).

Every sound here is synthesized from scratch - filtered noise and sine
oscillators, no recordings, no external assets - matching the vertical
slice's "no external/downloaded art or audio" decision. This is a
one-time, reproducible step (not something the engine runs at load
time) because Audio's real API (sound_load()) only takes a file path;
see Game/src/main.c's file header comment on why this is the one
necessarily file-based exception to "generate at load time."

Run: python3 Tools/generate_audio.py
Requires: numpy (only at generation time - the engine itself never
imports Python or numpy; this script's only job is to produce the
three .wav files checked into Assets/audio/).
"""
import os
import wave

import numpy as np

SAMPLE_RATE = 44100
OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "..", "Assets", "audio")


def write_wav(path: str, samples: np.ndarray) -> None:
    clipped = np.clip(samples, -1.0, 1.0)
    pcm = (clipped * 32767.0).astype(np.int16)
    with wave.open(path, "w") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(SAMPLE_RATE)
        handle.writeframes(pcm.tobytes())
    print(f"wrote {path} ({len(samples) / SAMPLE_RATE:.2f}s)")


def one_pole_lowpass(signal: np.ndarray, cutoff_alpha: float) -> np.ndarray:
    """Simple one-pole IIR low-pass - no scipy dependency needed for a
    placeholder wind/noise character."""
    out = np.empty_like(signal)
    state = 0.0
    for i, sample in enumerate(signal):
        state += cutoff_alpha * (sample - state)
        out[i] = state
    return out


def crossfade_loop(signal: np.ndarray, fade_samples: int) -> np.ndarray:
    """Blends the tail into the head so the clip loops without an
    audible seam when Audio plays it with SoundPlayDesc.loop = true."""
    result = signal.copy()
    fade = np.linspace(0.0, 1.0, fade_samples)
    head = result[:fade_samples]
    tail = result[-fade_samples:]
    blended = tail * (1.0 - fade) + head * fade
    result[:fade_samples] = blended
    return result


def generate_wind_loop() -> np.ndarray:
    duration = 6.0
    count = int(duration * SAMPLE_RATE)
    rng = np.random.default_rng(seed=1)
    noise = rng.uniform(-1.0, 1.0, count)
    wind = one_pole_lowpass(noise, cutoff_alpha=0.02)
    wind = one_pole_lowpass(wind, cutoff_alpha=0.05)  # a second pass narrows it further

    # Slow amplitude drift so a looping gust doesn't sound perfectly
    # mechanical - two slow, detuned sines modulating volume.
    t = np.arange(count) / SAMPLE_RATE
    swell = 0.6 + 0.25 * np.sin(2 * np.pi * 0.07 * t) + 0.15 * np.sin(2 * np.pi * 0.13 * t + 1.0)
    wind *= swell

    wind /= np.max(np.abs(wind)) + 1e-9
    wind *= 0.18  # quiet background layer, not the loudest thing in the mix
    return crossfade_loop(wind, fade_samples=int(0.5 * SAMPLE_RATE))


def generate_footstep() -> np.ndarray:
    duration = 0.14
    count = int(duration * SAMPLE_RATE)
    rng = np.random.default_rng(seed=2)
    t = np.arange(count) / SAMPLE_RATE

    noise = rng.uniform(-1.0, 1.0, count)
    noise = one_pole_lowpass(noise, cutoff_alpha=0.35)
    noise_envelope = np.exp(-t * 45.0)

    thump = np.sin(2 * np.pi * 85.0 * t)
    thump_envelope = np.exp(-t * 30.0)

    footstep = noise * noise_envelope * 0.7 + thump * thump_envelope * 0.6
    footstep /= np.max(np.abs(footstep)) + 1e-9
    footstep *= 0.9
    return footstep


def generate_ambience() -> np.ndarray:
    duration = 24.0
    count = int(duration * SAMPLE_RATE)
    rng = np.random.default_rng(seed=3)
    t = np.arange(count) / SAMPLE_RATE

    drone = 0.5 * np.sin(2 * np.pi * 55.0 * t) + 0.5 * np.sin(2 * np.pi * 57.5 * t)
    drone_swell = 0.5 + 0.5 * np.sin(2 * np.pi * 0.03 * t)
    drone *= drone_swell

    noise = rng.uniform(-1.0, 1.0, count)
    air = one_pole_lowpass(noise, cutoff_alpha=0.015)
    air_swell = 0.5 + 0.5 * np.sin(2 * np.pi * 0.045 * t + 2.0)
    air *= air_swell

    ambience = drone * 0.05 + air * 0.12
    ambience /= np.max(np.abs(ambience)) + 1e-9
    ambience *= 0.15
    return crossfade_loop(ambience, fade_samples=int(1.0 * SAMPLE_RATE))


def main() -> None:
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    write_wav(os.path.join(OUTPUT_DIR, "wind_loop.wav"), generate_wind_loop())
    write_wav(os.path.join(OUTPUT_DIR, "footstep.wav"), generate_footstep())
    write_wav(os.path.join(OUTPUT_DIR, "ambience.wav"), generate_ambience())


if __name__ == "__main__":
    main()
