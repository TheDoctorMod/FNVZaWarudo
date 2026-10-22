#!/usr/bin/env python3
"""Create the final distributable The World NV FOMOD ZIP."""

from __future__ import annotations

import argparse
import shutil
import tempfile
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
MOD_SOURCE = PROJECT_ROOT / "mod"
FOMOD_TEMPLATE = PROJECT_ROOT / "installer"
FOMOD_INFO = FOMOD_TEMPLATE / "fomod" / "info.xml"
NATIVE_SOURCE = PROJECT_ROOT / "native" / "TheWorldNV" / "main.cpp"
RELEASE_DLL = (
    PROJECT_ROOT / "native" / "TheWorldNV" / "bin" / "Release" / "TheWorldNV.dll"
)

# These files are supplied by mutually exclusive installer options, are source
# backups, or are documentation for authors.  They must not be copied into the
# shared runtime payload.
#
# The mod intentionally has no ESP.  New Vegas only auto-loads a BSA that is
# associated with an enabled plugin (or is added to the user's archive INI), so
# creating a standalone BSA here would make its contents unavailable at runtime.
# Keep the engine/UI assets loose instead of adding an inert archive or changing
# the user's load order/INI.  The voice WAVs are also deliberately loose:
# Start.txt uses JohnnyGuitar's PlaySoundFromPath, which reads their filesystem
# paths directly.
CORE_EXCLUSIONS = {
    Path("Config/TheWorldNVGameplay.ini"),
    Path("Config/TheWorldNVVoice.ini"),
    Path("Sound/fx/TheWorldNV/dio_activation.mp3"),
    Path("Sound/fx/TheWorldNV/jotaro_activation.mp3"),
    Path("Sound/fx/TheWorldNV/dio_activation.wav"),
    Path("Sound/fx/TheWorldNV/jotaro_activation.wav"),
    Path("Sound/fx/TheWorldNV/active.pre-44100-mono.wav"),
    Path("Sound/fx/TheWorldNV/resume.pre-44100-mono.wav"),
    Path("Sound/fx/TheWorldNV/README.txt"),
}

VOICE_ASSETS = {
    "DIO Brando": "dio_activation.wav",
    "Jotaro Kujo": "jotaro_activation.wav",
}


def release_filename() -> str:
    """Build the distributable filename from the FOMOD's canonical metadata."""
    require_file(FOMOD_INFO)
    metadata = ET.parse(FOMOD_INFO).getroot()
    name = metadata.findtext("Name", default="TheWorldNV").strip()
    version = metadata.findtext("Version", default="0.0.0").strip()
    return f"{name} v{version}.zip"


def parse_args() -> argparse.Namespace:
    default_output = PROJECT_ROOT / "dist" / release_filename()
    parser = argparse.ArgumentParser(
        description="Package the current The World NV sources as a final FOMOD ZIP."
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=default_output,
        help=f"ZIP destination (default: {default_output})",
    )
    return parser.parse_args()


def require_file(path: Path) -> None:
    if not path.is_file():
        raise FileNotFoundError(f"Required file is missing: {path}")


def require_directory(path: Path) -> None:
    if not path.is_dir():
        raise FileNotFoundError(f"Required directory is missing: {path}")


def copy_installer_template(staging: Path) -> None:
    """Copy installer metadata and option payloads, excluding the stale core."""
    for source in sorted(FOMOD_TEMPLATE.iterdir(), key=lambda item: item.name.lower()):
        if source.name == "00 Core":
            continue
        destination = staging / source.name
        if source.is_dir():
            # Runtime audio is normalized to WAV. Never carry obsolete source
            # MP3 files from an older template into a new release archive.
            shutil.copytree(source, destination, ignore=shutil.ignore_patterns("*.mp3"))
        else:
            shutil.copy2(source, destination)


def copy_shared_core(staging: Path) -> None:
    core = staging / "00 Core"
    for source in sorted(MOD_SOURCE.rglob("*"), key=lambda item: item.as_posix().lower()):
        if not source.is_file():
            continue
        relative = source.relative_to(MOD_SOURCE)
        if relative in CORE_EXCLUSIONS:
            continue
        destination = core / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def refresh_native_plugin(staging: Path) -> None:
    """Use the current Release DLL and reject an output older than its source."""
    require_file(NATIVE_SOURCE)
    require_file(RELEASE_DLL)

    if RELEASE_DLL.stat().st_mtime_ns < NATIVE_SOURCE.stat().st_mtime_ns:
        raise RuntimeError(
            "The Release TheWorldNV.dll is older than native/TheWorldNV/main.cpp. "
            "Compile Release/Win32 manually before packaging; this script does not "
            "compile the native project."
        )

    destination = staging / "00 Core" / "NVSE" / "Plugins" / "TheWorldNV.dll"
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(RELEASE_DLL, destination)


def refresh_voice_payloads(staging: Path) -> None:
    for option_name, filename in VOICE_ASSETS.items():
        source = MOD_SOURCE / "Sound" / "fx" / "TheWorldNV" / filename
        require_file(source)
        destination = (
            staging
            / "10 Voice"
            / option_name
            / "Sound"
            / "fx"
            / "TheWorldNV"
            / filename
        )
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def next_available_output(output: Path) -> Path:
    """Return a non-conflicting sibling for a ZIP locked by another process."""
    candidate = output.with_name(f"{output.stem}-new{output.suffix}")
    index = 2
    while candidate.exists():
        candidate = output.with_name(f"{output.stem}-new-{index}{output.suffix}")
        index += 1
    return candidate


def write_zip(staging: Path, output: Path) -> Path:
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_output = output.with_name(f".{output.name}.tmp")

    if temporary_output.exists():
        temporary_output.unlink()

    try:
        with zipfile.ZipFile(
            temporary_output,
            mode="w",
            compression=zipfile.ZIP_DEFLATED,
            compresslevel=9,
        ) as archive:
            for source in sorted(
                staging.rglob("*"), key=lambda item: item.as_posix().lower()
            ):
                if source.is_file():
                    archive.write(source, source.relative_to(staging).as_posix())

        # Replacement occurs only after the new archive has been written fully.
        # Explorer, MO2, or an antivirus scan can temporarily lock the old ZIP.
        # Keep a complete package in that case instead of discarding the work.
        try:
            temporary_output.replace(output)
            return output
        except PermissionError:
            fallback_output = next_available_output(output)
            temporary_output.replace(fallback_output)
            print(
                f"Warning: {output.name} is locked; "
                f"created {fallback_output.name} instead."
            )
            return fallback_output
    finally:
        if temporary_output.exists():
            temporary_output.unlink()


def main() -> int:
    args = parse_args()
    require_directory(MOD_SOURCE)
    require_directory(FOMOD_TEMPLATE)
    require_file(FOMOD_INFO)
    require_file(FOMOD_TEMPLATE / "fomod" / "ModuleConfig.xml")

    with tempfile.TemporaryDirectory(prefix="theworldnv-fomod-") as temp_directory:
        staging = Path(temp_directory)
        copy_installer_template(staging)
        copy_shared_core(staging)
        refresh_native_plugin(staging)
        refresh_voice_payloads(staging)
        output = write_zip(staging, args.output)

    print(f"Created FOMOD ZIP: {output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
