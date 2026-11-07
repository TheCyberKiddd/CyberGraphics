#!/usr/bin/env python3
"""Regenerates the Phase 24 asset fixtures.

The binary fixtures are committed so the tests never depend on Pillow, zlib, or
a particular encoder version at build time. This script documents exactly how
they were produced and makes them reproducible.

Usage (from the repository root):

    py -3 tests/fixtures/generate_fixtures.py

Requires Pillow only for the two JPEG fixtures; every other file is written
directly by this script.
"""

from __future__ import annotations

import pathlib
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parent


def write_png(path: pathlib.Path, width: int, height: int, color_type: int, depth: int,
              raw_scanlines: bytes, palette: bytes | None = None,
              transparency: bytes | None = None, interlaced: bool = False) -> None:
    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", width, height, depth, color_type, 0, 0,
                         1 if interlaced else 0)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
    if palette is not None:
        data += chunk(b"PLTE", palette)
    if transparency is not None:
        data += chunk(b"tRNS", transparency)
    data += chunk(b"IDAT", zlib.compress(raw_scanlines, 9))
    data += chunk(b"IEND", b"")
    path.write_bytes(data)


def write_rgba_png(path: pathlib.Path, pixels: list[list[tuple[int, int, int, int]]]) -> None:
    height = len(pixels)
    width = len(pixels[0])
    rows = b""
    for row in pixels:
        rows += b"\x00" + b"".join(bytes(pixel) for pixel in row)
    write_png(path, width, height, 6, 8, rows)


def write_interlaced_rgba_png(path: pathlib.Path,
                              pixels: list[list[tuple[int, int, int, int]]]) -> None:
    """Writes an Adam7-interlaced 8-bit RGBA PNG with filter type 0 per row."""
    height = len(pixels)
    width = len(pixels[0])
    x_start = [0, 4, 0, 2, 0, 1, 0]
    y_start = [0, 0, 4, 0, 2, 0, 1]
    x_step = [8, 8, 4, 4, 2, 2, 1]
    y_step = [8, 8, 8, 4, 4, 2, 2]

    scans = b""
    for pass_index in range(7):
        pass_width = (width - x_start[pass_index] + x_step[pass_index] - 1) // x_step[pass_index]
        pass_height = (height - y_start[pass_index] + y_step[pass_index] - 1) // y_step[pass_index]
        if pass_width <= 0 or pass_height <= 0:
            continue
        for row in range(pass_height):
            y = y_start[pass_index] + row * y_step[pass_index]
            scans += b"\x00"
            for column in range(pass_width):
                x = x_start[pass_index] + column * x_step[pass_index]
                scans += bytes(pixels[y][x])
    write_png(path, width, height, 6, 8, scans, interlaced=True)


def write_palette_png(path: pathlib.Path) -> None:
    # 4x4 indices 0..3 with a 4-entry palette; index 3 is transparent.
    indices = [0, 1, 2, 3] * 4
    rows = b""
    for row in range(4):
        rows += b"\x00" + bytes(indices[row * 4:(row + 1) * 4])
    palette = bytes([255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255])
    write_png(path, 4, 4, 3, 8, rows, palette=palette, transparency=bytes([255, 255, 255, 0]))


def write_gray16_png(path: pathlib.Path) -> None:
    # 4x4 16-bit grayscale; samples are 0x0000, 0x1111, ... 0xffff.
    rows = b""
    for row in range(4):
        rows += b"\x00"
        for column in range(4):
            value = (row * 4 + column) * 0x1111
            rows += struct.pack(">H", value)
    write_png(path, 4, 4, 0, 16, rows)


def write_dds(path: pathlib.Path) -> None:
    # 4x4 BC1 (DXT1) with two levels: 4x4 and 2x2. Both levels are one block.
    # Block 0: colour0 = red, colour1 = blue, 2-bit indices 00..11.
    def bc1_block(colour0: int, colour1: int, indices: int) -> bytes:
        return struct.pack("<HHI", colour0, colour1, indices)

    level0 = bc1_block(0xF800, 0x001F, 0xE4)
    level1 = bc1_block(0x07E0, 0x0000, 0x00)
    payload = level0 + level1

    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000
    header = struct.pack("<7I", 124, flags, 4, 4, 4 * 8, 0, 2)
    header += b"\x00" * 44  # reserved1
    header += struct.pack("<8I", 32, 0x4, 0x31545844, 0, 0, 0, 0, 0)
    header += struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    path.write_bytes(struct.pack("<I", 0x20534444) + header + payload)


def write_ktx2(path: pathlib.Path) -> None:
    # 8x8 BC1 with a 4x4 mip. One face, one layer, uncompressed supercompression.
    identifier = bytes([0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A])
    level0 = struct.pack("<HHI", 0xF800, 0x001F, 0xE4) * 4
    level1 = struct.pack("<HHI", 0x07E0, 0x0000, 0x00)

    header_size = 80
    index_size = 2 * 24
    level0_offset = header_size + index_size
    level1_offset = level0_offset + len(level0)

    header = identifier
    header += struct.pack("<9I", 131, 1, 8, 8, 0, 1, 1, 2, 0)
    header += struct.pack("<4I", header_size + index_size, 1, 0, 0)
    header += struct.pack("<QQ", 0, 0)
    header += struct.pack("<QQQ", level0_offset, len(level0), len(level0))
    header += struct.pack("<QQQ", level1_offset, len(level1), len(level1))
    path.write_bytes(header + level0 + level1)


def write_obj(path: pathlib.Path) -> None:
    path.write_text(
        "# CyberGraphics Phase 24 OBJ fixture\n"
        "mtllib quad.mtl\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 1 1 0\n"
        "v 0 1 0\n"
        "v 0 0 1\n"
        "v 1 0 1\n"
        "v 1 1 1\n"
        "v 0 1 1\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 1 1\n"
        "vt 0 1\n"
        "vn 0 0 1\n"
        "g front\n"
        "usemtl checker\n"
        "f 1/1/1 2/2/1 3/3/1 4/4/1\n"
        "g back\n"
        "usemtl flat\n"
        "f -4/1/1 -3/2/1 -2/3/1 -1/4/1\n",
        encoding="utf-8",
    )
    (ROOT / "quad.mtl").write_text(
        "newmtl checker\n"
        "Kd 1.0 1.0 1.0\n"
        "map_Kd png_rgba.png\n"
        "newmtl flat\n"
        "Kd 0.8 0.2 0.2\n",
        encoding="utf-8",
    )


def write_gltf() -> None:
    positions = [
        # Quad A, centred at the origin, spanning [-1, 1] in x and y.
        (-1.0, -1.0, 0.0), (1.0, -1.0, 0.0), (1.0, 1.0, 0.0), (-1.0, 1.0, 0.0),
        # Quad B, shifted forward.
        (-1.0, -1.0, 1.0), (1.0, -1.0, 1.0), (1.0, 1.0, 1.0), (-1.0, 1.0, 1.0),
    ]
    uvs = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)] * 2
    normals = [(0.0, 0.0, 1.0)] * 4 + [(0.0, 0.0, -1.0)] * 4

    blob = bytearray()

    def append_float3(values: list[tuple[float, float, float]]) -> tuple[int, int]:
        offset = len(blob)
        for value in values:
            blob.extend(struct.pack("<3f", *value))
        return offset, len(values) * 12

    def append_float2(values: list[tuple[float, float]]) -> tuple[int, int]:
        offset = len(blob)
        for value in values:
            blob.extend(struct.pack("<2f", *value))
        return offset, len(values) * 8

    def append_indices(values: list[int]) -> tuple[int, int]:
        offset = len(blob)
        for value in values:
            blob.extend(struct.pack("<H", value))
        while len(blob) % 4:
            blob.append(0)
        return offset, len(values) * 2

    views = []
    accessors = []

    def view(offset: int, length: int, target: int) -> int:
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": length, "target": target})
        return len(views) - 1

    def accessor(view_index: int, component: int, count: int, kind: str,
                 minimum: list[float] | None = None,
                 maximum: list[float] | None = None) -> int:
        entry: dict = {"bufferView": view_index, "componentType": component,
                       "count": count, "type": kind}
        if minimum is not None:
            entry["min"] = minimum
            entry["max"] = maximum
        accessors.append(entry)
        return len(accessors) - 1

    position_a_offset, position_a_length = append_float3(positions[:4])
    normal_a_offset, normal_a_length = append_float3(normals[:4])
    uv_a_offset, uv_a_length = append_float2(uvs[:4])
    index_a_offset, index_a_length = append_indices([0, 1, 2, 0, 2, 3])
    position_b_offset, position_b_length = append_float3(positions[4:])
    normal_b_offset, normal_b_length = append_float3(normals[4:])
    uv_b_offset, uv_b_length = append_float2(uvs[4:])
    index_b_offset, index_b_length = append_indices([0, 2, 1, 0, 3, 2])

    position_a = accessor(view(position_a_offset, position_a_length, 34962), 5126, 4, "VEC3",
                          [-1.0, -1.0, 0.0], [1.0, 1.0, 0.0])
    normal_a = accessor(view(normal_a_offset, normal_a_length, 34962), 5126, 4, "VEC3")
    uv_a = accessor(view(uv_a_offset, uv_a_length, 34962), 5126, 4, "VEC2")
    index_a = accessor(view(index_a_offset, index_a_length, 34963), 5123, 6, "SCALAR")
    position_b = accessor(view(position_b_offset, position_b_length, 34962), 5126, 4, "VEC3",
                          [-1.0, -1.0, 1.0], [1.0, 1.0, 1.0])
    normal_b = accessor(view(normal_b_offset, normal_b_length, 34962), 5126, 4, "VEC3")
    uv_b = accessor(view(uv_b_offset, uv_b_length, 34962), 5126, 4, "VEC2")
    index_b = accessor(view(index_b_offset, index_b_length, 34963), 5123, 6, "SCALAR")

    document = {
        "asset": {"version": "2.0", "generator": "CyberGraphics Phase 24 fixture"},
        "scene": 0,
        "scenes": [{"name": "two-quads", "nodes": [0, 1]}],
        "nodes": [
            {"name": "left", "mesh": 0, "translation": [-1.5, 0.0, 0.0]},
            {"name": "right", "mesh": 0, "translation": [1.5, 0.0, 0.0],
             "rotation": [0.0, 0.3826834, 0.0, 0.9238795]},
        ],
        "meshes": [{
            "name": "quad",
            "primitives": [
                {"attributes": {"POSITION": position_a, "NORMAL": normal_a, "TEXCOORD_0": uv_a},
                 "indices": index_a, "material": 0},
                {"attributes": {"POSITION": position_b, "NORMAL": normal_b, "TEXCOORD_0": uv_b},
                 "indices": index_b, "material": 1},
            ],
        }],
        "materials": [
            {"name": "checker",
             "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0,
                                      "roughnessFactor": 0.8},
             "doubleSided": True},
            {"name": "flat",
             "pbrMetallicRoughness": {"baseColorFactor": [0.8, 0.2, 0.2, 1.0],
                                      "metallicFactor": 0.1, "roughnessFactor": 0.4}},
        ],
        "textures": [{"source": 0, "sampler": 0}],
        "images": [{"uri": "png_rgba.png"}],
        "samplers": [{"magFilter": 9728, "minFilter": 9728, "wrapS": 33071, "wrapT": 33071}],
        "buffers": [{"uri": "model.bin", "byteLength": len(blob)}],
        "bufferViews": views,
        "accessors": accessors,
    }

    (ROOT / "model.bin").write_bytes(bytes(blob))
    (ROOT / "two_quads.gltf").write_text(
        __import__("json").dumps(document, indent=2) + "\n", encoding="utf-8")


def write_jpeg_fixtures() -> None:
    from PIL import Image  # noqa: PLC0415

    width = 16
    height = 16
    image = Image.new("RGB", (width, height))
    for y in range(height):
        for x in range(width):
            image.putpixel((x, y), (x * 16, y * 16, (x * y * 255) // 225))
    image.save(ROOT / "jpeg_444.jpg", quality=100, subsampling=0)
    image.save(ROOT / "jpeg_420.jpg", quality=90, subsampling=2)
    image.convert("L").save(ROOT / "jpeg_gray.jpg", quality=95)


def main() -> None:
    pixels = [[((x * 32) % 256, (y * 32) % 256, ((x + y) * 16) % 256, 255 - x * 8)
               for x in range(8)] for y in range(8)]
    write_rgba_png(ROOT / "png_rgba.png", pixels)
    write_interlaced_rgba_png(ROOT / "png_interlaced.png", pixels)
    write_palette_png(ROOT / "png_palette.png")
    write_gray16_png(ROOT / "png_gray16.png")
    write_dds(ROOT / "texture_bc1.dds")
    write_ktx2(ROOT / "texture_bc1.ktx2")
    write_obj(ROOT / "quad.obj")
    write_gltf()
    write_jpeg_fixtures()


if __name__ == "__main__":
    main()
