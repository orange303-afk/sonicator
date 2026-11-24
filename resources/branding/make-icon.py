#!/usr/bin/env python3
"""Derives the icon files that are generated for the shell, from the preserved original.

The glossy original is a 1254 px canvas whose artwork occupies about 75 % of it. Apple's grid wants
that inset canvas, so ``icon-glossy-chevron-1024.png`` keeps it. A Windows shell icon is drawn at
16-48 px next to icons that fill their box, and the same artwork then reads as a smaller icon than
its neighbours; the Windows files are therefore trimmed to the artwork and re-centred with a 3 %
margin, so the visible mark fills about 96 % of the square (about 94 % across, because the original
mark is slightly taller than wide).

Run from the repository root:

    python resources/branding/make-icon.py

Originals are never modified. Outputs are 8-bit RGBA PNG; the main-memory trim uses the same alpha
threshold (16) as the other derivatives in this folder.
"""
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ORIGINAL = HERE / "originals" / "icon-glossy-chevron-original.png"
MARGIN = 0.02           # share of the final square left transparent on each side
ALPHA_THRESHOLD = 16    # matches the other derivatives: faint glow is not part of the mark


def trimmed_square(image: Image.Image) -> Image.Image:
    alpha = image.getchannel("A")
    mask = alpha.point(lambda a: 255 if a >= ALPHA_THRESHOLD else 0)
    box = mask.getbbox()
    if box is None:
        raise SystemExit(f"{image.filename}: nothing visible above alpha {ALPHA_THRESHOLD}")
    mark = image.crop(box)
    side = max(mark.size)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.paste(mark, ((side - mark.width) // 2, (side - mark.height) // 2))
    return square


def centred(mark: Image.Image, size: int) -> Image.Image:
    """The mark scaled into a transparent square with the standard margin around it."""
    content = round(size * (1.0 - 2.0 * MARGIN))
    scaled = mark.resize((content, content), Image.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    offset = (size - content) // 2
    canvas.paste(scaled, (offset, offset), scaled)
    return canvas


def main() -> None:
    source = Image.open(ORIGINAL).convert("RGBA")
    mark = trimmed_square(source)
    outputs = {
        "icon-windows-1024.png": 1024,  # juceaide ICON_BIG on Windows: Explorer, Start menu, MSI
        "icon-window-256.png": 256,     # appWindowIcon(): the window and its taskbar button
    }
    for name, size in outputs.items():
        target = HERE / name
        centred(mark, size).save(target, "PNG", optimize=True)
        print(f"{target.relative_to(HERE.parent.parent)}: {size}x{size}, "
              f"mark fills {round(100 * (1.0 - 2.0 * MARGIN))} %")


if __name__ == "__main__":
    main()
