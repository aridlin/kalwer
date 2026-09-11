#!/usr/bin/env python3
"""Build a native Kar sprite pack from a JSON manifest and RGBA PNG artwork.

Only the supplied artwork is included. No game executable or emulator is used.
Manifest: [{"bank": 1, "frame": 0, "file": "car.png", "x": -36, "y": -64}]
Coordinates are offsets from the sprite anchor. Artwork keeps its own license.
"""
import argparse
import json
from pathlib import Path
import struct
from PIL import Image


def build(manifest: Path, destination: Path):
    entries = json.loads(manifest.read_text(encoding="utf-8"))
    if not 0 < len(entries) <= 4096:
        raise ValueError("Expected 1..4096 sprite entries")
    result = bytearray(b"KARPX001" + struct.pack("<I", len(entries)))
    seen = set()
    total = 0
    for entry in entries:
        bank, frame = int(entry["bank"]), int(entry["frame"])
        if not 0 <= bank <= 65535 or not 0 <= frame <= 65535:
            raise ValueError("Bank and frame must fit unsigned 16 bits")
        key = bank << 16 | frame
        if key in seen:
            raise ValueError("Duplicate sprite")
        seen.add(key)
        x, y = int(entry.get("x", 0)), int(entry.get("y", 0))
        if not -4096 <= x <= 4096 or not -4096 <= y <= 4096:
            raise ValueError("Sprite anchor out of bounds")
        with Image.open(manifest.parent / entry["file"]) as source:
            if not 0 < source.width <= 2048 or not 0 < source.height <= 2048:
                raise ValueError("Sprite dimensions out of bounds")
            total += source.width * source.height
            if total > 16 * 1024 * 1024:
                raise ValueError("Sprite pack exceeds 16 million pixels")
            result.extend(struct.pack("<IiiII", key, x, y, source.width, source.height))
            result.extend(source.convert("RGBA").tobytes("raw", "BGRA"))
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    temporary.write_bytes(result)
    temporary.replace(destination)


def build_scene(manifest: Path, destination: Path):
    entries = json.loads(manifest.read_text(encoding="utf-8"))
    if len(entries) > 65536:
        raise ValueError("Scene exceeds 65536 primitives")
    output = bytearray(b"KARSC001" + struct.pack("<I", len(entries)))
    for entry in entries:
        kind, color = int(entry["kind"]), int(entry.get("color", 0))
        bank, frame = int(entry.get("bank", 0)), int(entry.get("frame", 0))
        vertices = [int(value) for value in entry["vertices"]]
        if not 0 <= kind <= 3 or not 0 <= color <= 0xFFFFFF:
            raise ValueError("Invalid primitive kind or color")
        if not 0 <= bank <= 65535 or not 0 <= frame <= 65535:
            raise ValueError("Bank and frame must fit unsigned 16 bits")
        if len(vertices) != 9 or any(abs(value) > 1000000 for value in vertices):
            raise ValueError("Expected nine bounded vertex coordinates")
        output.extend(struct.pack("<4I9i", kind, color, bank, frame, *vertices))
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    temporary.write_bytes(output)
    temporary.replace(destination)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--scene", type=Path, help="Optional native scene manifest")
    args = parser.parse_args()
    build(args.manifest, args.output)
    if args.scene:
        build_scene(args.scene, args.output.with_suffix(".kars"))
