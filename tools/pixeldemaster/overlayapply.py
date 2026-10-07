#!/usr/bin/env python3
"""Apply Pixel Demaster / ChronoMod .ctp files to an Android Chrono Trigger
resources.bin.

Usage:
  python3 apply_pixeldemaster.py <resources.bin> <out.bin> <layer1.ctp> [layer2.ctp ...] [--with-text]

Layers are applied in order; later .ctp files override earlier ones on
name conflicts.

Both existing files and NEW files are supported. New files are added to
resources.bin rather than being discarded.

  --with-text  also apply Localize/*/msg/*.txt files.

Example:
  python3 apply_pixeldemaster.py resources.bin out.bin master.ctp
"""

import zipfile
import sys
import os

import ctarc
import ctrepack


PNG = bytes.fromhex("89504e47")


def main():
    args = [x for x in sys.argv[1:] if not x.startswith("--")]
    with_text = "--with-text" in sys.argv

    if len(args) < 3:
        print(__doc__)
        sys.exit(1)

    src, out, ctps = args[0], args[1], args[2:]

    _, toc, _ = ctarc.open_arc(src)
    _, ents = ctarc.parse_toc(toc)

    existing = {n for n, _, _, _ in ents}

    ov = {}

    for ctp in ctps:
        with zipfile.ZipFile(ctp) as z:
            applied = 0
            replacements = 0
            new_files = 0

            for n in z.namelist():
                if n.endswith("/"):
                    continue

                d = z.read(n)

                if d[:4] == PNG or with_text:
                    ov[n] = d
                    applied += 1

                    if n in existing:
                        replacements += 1
                    else:
                        new_files += 1

            print(
                "  %-40s -> %d files (%d replacements, %d new)"
                % (
                    os.path.basename(ctp),
                    applied,
                    replacements,
                    new_files,
                )
            )

    print(
        "total unique files: %d (with_text=%s)"
        % (len(ov), with_text)
    )

    ctrepack.repack(src, out, ov)

    print("done ->", out)


if __name__ == "__main__":
    main()