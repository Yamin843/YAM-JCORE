#!/usr/bin/env python3
"""
split_lib.py — split libyamjs.a into chunks small enough for GitHub's
per-file push limits.

Defaults:
  INPUT     app/src/main/jni/all_libs/LIBYAMJS/libyamjs.a
  OUTPUT    app/src/main/jni/all_libs/LIBYAMJS/libyamjs.a.parts/
  PART_SIZE 45 * 1024 * 1024 (45 MiB)

The script writes:
  part_00, part_01, …, part_NN   — raw chunks of the input file
  MANIFEST.txt                    — original size + sha256 + chunk count

reassemble_lib.sh reads MANIFEST.txt and joins the parts back into
the original file, verifying the hash on completion.

There is no cap on the input size. A 4 GB library produces 92 parts
of 45 MiB each, all of which git can handle individually.
"""
import argparse
import hashlib
import sys
from pathlib import Path

DEFAULT_INPUT  = "app/src/main/jni/all_libs/LIBYAMJS/libyamjs.a"
DEFAULT_PARTS  = "app/src/main/jni/all_libs/LIBYAMJS/libyamjs.a.parts"
DEFAULT_SIZE   = 45 * 1024 * 1024    # 45 MiB
DEFAULT_PREFIX = "part_"


def sha256_of_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def split(in_path: Path, out_dir: Path, part_size: int, prefix: str) -> int:
    if not in_path.exists():
        print(f"ERROR: input missing: {in_path}", file=sys.stderr)
        return 1

    total = in_path.stat().st_size
    if total == 0:
        print(f"ERROR: input is empty: {in_path}", file=sys.stderr)
        return 1

    # Clean output dir.
    if out_dir.exists():
        for f in out_dir.glob(prefix + "*"):
            f.unlink()
    out_dir.mkdir(parents=True, exist_ok=True)

    # Split.
    part_count = (total + part_size - 1) // part_size
    written = []
    with in_path.open("rb") as fin:
        for i in range(part_count):
            name = f"{prefix}{i:02d}"
            target = out_dir / name
            remaining = part_size
            with target.open("wb") as fout:
                while remaining > 0:
                    chunk = fin.read(min(1024 * 1024, remaining))
                    if not chunk:
                        break
                    fout.write(chunk)
                    remaining -= len(chunk)
            written.append((name, target.stat().st_size))
            print(f"  wrote {name}: {target.stat().st_size:>10} bytes")

    # Manifest.
    digest = sha256_of_file(in_path)
    manifest = out_dir / "MANIFEST.txt"
    manifest.write_text(
        f"original_name:   {in_path.name}\n"
        f"original_size:   {total}\n"
        f"sha256:          {digest}\n"
        f"part_size:       {part_size}\n"
        f"part_count:      {part_count}\n"
        f"part_prefix:     {prefix}\n",
        encoding="utf-8",
    )

    print()
    print(f"✓ split {in_path} ({total} bytes) into {part_count} parts")
    print(f"  manifest: {manifest}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Split a large library into parts.")
    ap.add_argument("--input",    default=DEFAULT_INPUT)
    ap.add_argument("--parts",    default=DEFAULT_PARTS)
    ap.add_argument("--size",     type=int, default=DEFAULT_SIZE,
                    help="part size in bytes (default 45 MiB)")
    ap.add_argument("--prefix",   default=DEFAULT_PREFIX)
    args = ap.parse_args()

    in_path = Path(args.input)
    out_dir = Path(args.parts)
    return split(in_path, out_dir, args.size, args.prefix)


if __name__ == "__main__":
    sys.exit(main())
