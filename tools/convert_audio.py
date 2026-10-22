#!/usr/bin/env python3
"""Convert The World NV cues to engine-friendly PCM WAV files."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
import wave
from array import array
from pathlib import Path


TARGET_RATE = 44_100
TARGET_CHANNELS = 1
TARGET_SAMPLE_WIDTH = 2
DEFAULT_SOURCE_DIR = (
    Path(__file__).resolve().parent.parent
    / "mod"
    / "Sound"
    / "fx"
    / "TheWorldNV"
)
DEFAULT_INSTALL_DIR = Path(
    r"C:\Modlists\VNV\mods\The World NV\Sound\fx\TheWorldNV"
)
WAV_FILE_NAMES = ("active.wav", "resume.wav")
MP3_TO_WAV = {
    "dio_activation.mp3": "dio_activation.wav",
    "jotaro_activation.mp3": "jotaro_activation.wav",
}


def read_pcm16(path: Path) -> tuple[int, int, array]:
    with wave.open(str(path), "rb") as source:
        if source.getcomptype() != "NONE":
            raise ValueError(f"{path.name}: compressed WAV is not supported")
        if source.getsampwidth() != TARGET_SAMPLE_WIDTH:
            raise ValueError(f"{path.name}: expected 16-bit PCM input")

        channels = source.getnchannels()
        sample_rate = source.getframerate()
        samples = array("h")
        samples.frombytes(source.readframes(source.getnframes()))

    if sys.byteorder != "little":
        samples.byteswap()
    return channels, sample_rate, samples


def downmix_to_mono(samples: array, channels: int) -> array:
    if channels == TARGET_CHANNELS:
        return samples
    if channels < 1:
        raise ValueError("WAV has no audio channels")

    mono = array("h")
    for offset in range(0, len(samples), channels):
        frame = samples[offset : offset + channels]
        mono.append(round(sum(frame) / len(frame)))
    return mono


def resample_linear(samples: array, source_rate: int) -> array:
    if source_rate == TARGET_RATE or len(samples) < 2:
        return samples

    output_count = max(1, round(len(samples) * TARGET_RATE / source_rate))
    output = array("h")
    source_step = source_rate / TARGET_RATE
    last_index = len(samples) - 1

    for output_index in range(output_count):
        position = min(output_index * source_step, last_index)
        left_index = int(position)
        right_index = min(left_index + 1, last_index)
        fraction = position - left_index
        value = samples[left_index] + (
            samples[right_index] - samples[left_index]
        ) * fraction
        output.append(max(-32_768, min(32_767, round(value))))

    return output


def write_pcm16(path: Path, samples: array) -> None:
    output_samples = array("h", samples)
    if sys.byteorder != "little":
        output_samples.byteswap()

    with wave.open(str(path), "wb") as destination:
        destination.setnchannels(TARGET_CHANNELS)
        destination.setsampwidth(TARGET_SAMPLE_WIDTH)
        destination.setframerate(TARGET_RATE)
        destination.writeframes(output_samples.tobytes())


def convert_in_place(path: Path) -> None:
    if not path.is_file():
        raise FileNotFoundError(path)

    backup = path.with_name(f"{path.stem}.pre-44100-mono.wav")
    if not backup.exists():
        shutil.copy2(path, backup)

    channels, sample_rate, samples = read_pcm16(path)
    samples = downmix_to_mono(samples, channels)
    samples = resample_linear(samples, sample_rate)

    temporary_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            prefix=f"{path.stem}.", suffix=".wav", dir=path.parent, delete=False
        ) as temporary:
            temporary_name = temporary.name
        write_pcm16(Path(temporary_name), samples)
        Path(temporary_name).replace(path)
    finally:
        if temporary_name:
            Path(temporary_name).unlink(missing_ok=True)

    print(
        f"Converted {path.name}: {channels} channel(s) at {sample_rate} Hz -> "
        f"mono PCM16 at {TARGET_RATE} Hz"
    )


def convert_mp3(source: Path, destination: Path, ffmpeg: str) -> None:
    if not source.is_file():
        raise FileNotFoundError(source)

    temporary = destination.with_name(f".{destination.name}.tmp.wav")
    temporary.unlink(missing_ok=True)
    try:
        subprocess.run(
            [
                ffmpeg,
                "-hide_banner",
                "-loglevel",
                "error",
                "-y",
                "-i",
                str(source),
                "-vn",
                "-ac",
                str(TARGET_CHANNELS),
                "-ar",
                str(TARGET_RATE),
                "-c:a",
                "pcm_s16le",
                str(temporary),
            ],
            check=True,
        )
        temporary.replace(destination)
    finally:
        temporary.unlink(missing_ok=True)

    print(f"Converted {source.name} -> {destination.name}: mono PCM16 at {TARGET_RATE} Hz")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, default=DEFAULT_SOURCE_DIR)
    parser.add_argument("--install-dir", type=Path, default=DEFAULT_INSTALL_DIR)
    parser.add_argument(
        "--no-install",
        action="store_true",
        help="Convert project files without copying them into the MO2 mod.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    ffmpeg = shutil.which("ffmpeg")
    if MP3_TO_WAV and ffmpeg is None:
        raise RuntimeError(
            "ffmpeg was not found in PATH; it is required to decode the current "
            "DIO and Jotaro MP3 activation cues"
        )

    for file_name in WAV_FILE_NAMES:
        convert_in_place(args.source_dir / file_name)

    for source_name, destination_name in MP3_TO_WAV.items():
        convert_mp3(
            args.source_dir / source_name,
            args.source_dir / destination_name,
            ffmpeg,
        )

    if not args.no_install:
        args.install_dir.mkdir(parents=True, exist_ok=True)
        install_names = (*WAV_FILE_NAMES, *MP3_TO_WAV.values())
        for file_name in install_names:
            source = args.source_dir / file_name
            destination = args.install_dir / file_name
            shutil.copy2(source, destination)
            print(f"Installed {destination}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
