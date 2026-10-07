#!/usr/bin/env python3
"""Repack Chrono Trigger Android resources.bin ('ARC1'/DetchmanResource),
optionally overriding entries with replacement raw bytes (e.g. Pixel Demaster PNGs).
Format per ct-resources-bin-format: LCG-XOR(seed=BASE+pos) + per-entry raw-deflate
([BE u32 usize][deflate]). See ctarc.py for the verified reader.
"""
import struct
import zlib
import sys
import os
import ctarc


BASE = ctarc.BASE  # 0x19000000


def enc_blob(raw, pos):
    """raw bytes -> [BE usize][GZIP stream] then XOR(seed=BASE+pos).

    The engine inflates via ZipUtils::inflateMemory -> inflateInit2_(windowBits=47),
    i.e. auto zlib/gzip header detection, which REJECTS raw/headerless deflate.

    Originals are gzip (1f 8b 08 ..). Must emit a gzip-wrapped stream,
    NOT raw deflate.
    """
    co = zlib.compressobj(
        9,
        zlib.DEFLATED,
        16 + 15,  # gzip wrapper
    )

    comp = co.compress(raw) + co.flush()
    blob = struct.pack(">I", len(raw)) + comp

    return ctarc.dec(
        blob,
        (BASE + pos) & 0xffffffff,
    )


def repack(in_path, out_path, overrides=None, verbose=True):
    overrides = overrides or {}

    f, toc, info = ctarc.open_arc(in_path)
    count, ents = ctarc.parse_toc(toc)

    # Existing archive names.
    existing_names = {nm for nm, _, _, _ in ents}

    # Keep only genuinely new override names.
    new_names = [
        nm
        for nm in overrides
        if nm not in existing_names
    ]

    # ------------------------------------------------------------------
    # 1) Gather every archive entry as (name, raw_data).
    #
    # Existing entries are either replaced by an override or preserved.
    # New entries come directly from overrides.
    # ------------------------------------------------------------------

    files = []
    used = set()

    for nm, pos, size, no in ents:
        if nm in overrides:
            raw = overrides[nm]
            used.add(nm)
        else:
            f.seek(pos)

            raw = ctarc.inflate_blob(
                ctarc.dec(
                    f.read(size),
                    (BASE + pos) & 0xffffffff,
                )
            )

        files.append((nm, raw))

    # Add genuinely new files.
    for nm in new_names:
        files.append((nm, overrides[nm]))
        used.add(nm)

    # ------------------------------------------------------------------
    # 2) CRITICAL:
    #
    # DetchmanResource::GetFileInfo() performs a binary search over the
    # TOC. Therefore the TOC MUST be sorted lexicographically by filename.
    #
    # New files cannot simply be appended to the original TOC.
    # ------------------------------------------------------------------

    files.sort(key=lambda item: item[0])

    names = [nm for nm, raw in files]
    raws = [raw for nm, raw in files]

    new_count = len(files)

    # ------------------------------------------------------------------
    # 3) Lay out all entry blobs immediately after the 16-byte archive
    # header.
    # ------------------------------------------------------------------

    HDR = 16
    cur = HDR

    blobs = []
    sizes = []

    for raw in raws:
        b = enc_blob(raw, cur)

        blobs.append(b)
        sizes.append(len(b))

        cur += len(b)

    entries_end = cur

    # ------------------------------------------------------------------
    # 4) Rebuild the TOC.
    #
    # TOC format:
    #
    #   uint32 count
    #   count * {
    #       int32 nameOff
    #       int32 pos
    #       int32 size
    #   }
    #   NUL-terminated UTF-8 name pool
    #
    # nameOff is relative to the beginning of the TOC blob.
    # ------------------------------------------------------------------

    pool_start = 4 + new_count * 12

    name_pool = bytearray()
    name_offsets = []

    for nm in names:
        name_offsets.append(
            pool_start + len(name_pool)
        )

        name_pool.extend(
            nm.encode("utf-8")
        )

        name_pool.append(0)

    newtoc = bytearray()

    newtoc += struct.pack(
        "<I",
        new_count,
    )

    cur = HDR

    for i in range(new_count):
        newtoc += struct.pack(
            "<iii",
            name_offsets[i],
            cur,
            sizes[i],
        )

        cur += sizes[i]

    newtoc += name_pool

    # ------------------------------------------------------------------
    # 5) Encrypt + compress the TOC at entries_end.
    # ------------------------------------------------------------------

    toc_off = entries_end

    toc_blob = enc_blob(
        bytes(newtoc),
        toc_off,
    )

    total = toc_off + len(toc_blob)

    # ------------------------------------------------------------------
    # 6) Header.
    # ------------------------------------------------------------------

    hdr = struct.pack(
        "<IIII",
        0x31435241,
        total,
        toc_off,
        len(toc_blob),
    )

    hdr = ctarc.dec(
        hdr,
        BASE + 0,
    )

    # ------------------------------------------------------------------
    # 7) Write archive.
    # ------------------------------------------------------------------

    with open(out_path, "wb") as o:
        o.write(hdr)

        for b in blobs:
            o.write(b)

        o.write(toc_blob)

    if verbose:
        print(
            "applied %d/%d overrides (%d new, %d unused)"
            % (
                len(used) - len(new_names),
                count,
                len(new_names),
                len(set(overrides) - used),
            )
        )

        print(
            "wrote %s  (%d bytes, tocOff=0x%x, entries=%d)"
            % (
                out_path,
                total,
                toc_off,
                new_count,
            )
        )

    return out_path


if __name__ == "__main__":
    # Round-trip self-test: repack with NO overrides, reopen, verify
    # N entries decode identically.

    src = (
        sys.argv[1]
        if len(sys.argv) > 1
        else "/Users/shant/Desktop/ct_emu/assets/resources.bin"
    )

    out = "/tmp/rt_test.bin"

    print("=== round-trip (no overrides) ===")

    repack(src, out, {})

    # Verify.
    fa, ta, ia = ctarc.open_arc(src)
    ca, ea = ctarc.parse_toc(ta)

    fb, tb, ib = ctarc.open_arc(out)
    cb, eb = ctarc.parse_toc(tb)

    assert ca == cb, (
        "count mismatch %d/%d"
        % (ca, cb)
    )

    import random

    idxs = list(
        range(
            0,
            ca,
            max(1, ca // 40),
        )
    )[:40]

    bad = 0

    for i in idxs:
        nm, pa, sa, _ = ea[i]
        _, pb, sb, _ = eb[i]

        fa.seek(pa)

        ra = ctarc.inflate_blob(
            ctarc.dec(
                fa.read(sa),
                (BASE + pa) & 0xffffffff,
            )
        )

        fb.seek(pb)

        rb = ctarc.inflate_blob(
            ctarc.dec(
                fb.read(sb),
                (BASE + pb) & 0xffffffff,
            )
        )

        if ra != rb:
            bad += 1
            print(
                "  MISMATCH %s"
                % nm
            )

    print(
        "verified %d sample entries, mismatches=%d"
        % (len(idxs), bad)
    )

    print(
        "ORIG size=%d  REPACK size=%d"
        % (
            os.path.getsize(src),
            os.path.getsize(out),
        )
    )

    print(
        "OK"
        if bad == 0
        else "FAIL"
    )