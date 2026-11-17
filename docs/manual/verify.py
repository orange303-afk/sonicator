#!/usr/bin/env python3
"""Structural checks for the built manual.

Usage: python3 docs/manual/verify.py docs/manual/sonicator-manual.pdf [--min-pages 70]
"""

import argparse
import re
import sys

import pymupdf


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("pdf")
    parser.add_argument("--min-pages", type=int, default=70)
    args = parser.parse_args()

    doc = pymupdf.open(args.pdf)
    problems: list[str] = []

    if len(doc) < args.min_pages:
        problems.append(f"only {len(doc)} pages, expected at least {args.min_pages}")

    toc = doc.get_toc(simple=True)
    chapters = [entry for entry in toc if re.match(r"^\s*\d+\s+\S", entry[1])]
    if len(chapters) < 25:
        problems.append(f"outline has {len(chapters)} chapters, expected 25")

    fonts = {font[3].split("+")[-1].split("-")[0] for page in doc for font in page.get_fonts()}
    if "Manrope" not in fonts:
        problems.append(f"Manrope is not embedded; fonts: {sorted(fonts)}")

    text = "".join(page.get_text() for page in doc)
    if "\ufffd" in text or "□" in text:
        problems.append("extracted text contains a replacement glyph")

    empty = [index + 1 for index, page in enumerate(doc)
             if not page.get_text().strip() and not page.get_images()]
    if empty:
        problems.append(f"pages with neither text nor images: {empty}")

    broken = []
    for index, page in enumerate(doc):
        for link in page.get_links():
            if link.get("kind") == pymupdf.LINK_GOTO:
                if not 0 <= link.get("page", -1) < len(doc):
                    broken.append((index + 1, link.get("page")))
            elif link.get("kind") == pymupdf.LINK_URI and not link.get("uri", "").startswith(
                    ("https://", "http://", "mailto:")):
                broken.append((index + 1, link.get("uri")))
    if broken:
        problems.append(f"links with no destination: {broken[:5]}")

    if problems:
        for problem in problems:
            print("FAIL:", problem)
        return 1

    print(f"OK: {len(doc)} pages, {len(chapters)} chapters, Manrope embedded, "
          f"{len(toc)} outline entries")
    return 0


if __name__ == "__main__":
    sys.exit(main())
