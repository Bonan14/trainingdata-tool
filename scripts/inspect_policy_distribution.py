#!/usr/bin/env python3
"""
Inspect policy probability distributions in V6 training data chunks.

Supports:
  - Individual .gz chunk files
  - .tar archives containing .gz chunk files
  - Directories containing .gz or .tar files

Reports:
  - Record index / move ply
  - Total legal moves
  - Non-zero legal moves
  - Illegal moves (probabilities < 0)
  - Sum of legal probabilities (verifying sum == 1.0)
  - Shannon entropy in nats (-sum(p * ln(p)))
  - Top-5 move probabilities
  - Aggregate dataset summary statistics
"""

import argparse
import gzip
import math
import os
import struct
import sys
import tarfile

STRUCT_SIZE = 8356
PROB_OFFSET = 8
PROB_COUNT = 1858


def analyze_record_data(data, max_records=None, verbose=True):
    n_records = len(data) // STRUCT_SIZE
    if max_records is not None:
        n_records = min(n_records, max_records)

    records = []
    for i in range(n_records):
        offset = i * STRUCT_SIZE + PROB_OFFSET
        probs = struct.unpack(f"<{PROB_COUNT}f", data[offset:offset + PROB_COUNT * 4])

        legal = [p for p in probs if p >= 0.0]
        illegal = [p for p in probs if p < 0.0]
        nonzero = [p for p in legal if p > 0.0]
        sum_legal = sum(legal)

        sorted_legal = sorted(legal, reverse=True)
        top5 = [round(x, 4) for x in sorted_legal[:5]]

        entropy = -sum(p * math.log(p) for p in nonzero) if nonzero else 0.0

        rec_info = {
            "index": i,
            "legal_count": len(legal),
            "nonzero_count": len(nonzero),
            "illegal_count": len(illegal),
            "sum": sum_legal,
            "entropy": entropy,
            "top5": top5,
        }
        records.append(rec_info)

        if verbose:
            print(
                f"  rec {i:3d}: legal={len(legal):2d}, "
                f"nonzero={len(nonzero):2d}, "
                f"illegal={len(illegal):4d}, "
                f"sum={sum_legal:.4f}, "
                f"entropy={entropy:.4f} nats, "
                f"top5={top5}"
            )

    return records


def inspect_file_gz(gz_path, max_records=None, verbose=True):
    with gzip.open(gz_path, "rb") as f:
        data = f.read()
    if verbose:
        print(f"\n--- Chunk: {gz_path} ({len(data) // STRUCT_SIZE} records) ---")
    return analyze_record_data(data, max_records=max_records, verbose=verbose)


def inspect_tar(tar_path, max_members=1, max_records_per_member=5, verbose=True):
    print(f"\n=== Archive: {tar_path} ===")
    all_records = []
    with tarfile.open(tar_path, "r") as tar:
        gz_members = [m for m in tar.getmembers() if m.name.endswith(".gz")]
        print(f"Total .gz chunk members: {len(gz_members)}")
        for idx, member in enumerate(gz_members[:max_members]):
            f = tar.extractfile(member)
            if f is None:
                continue
            data = gzip.decompress(f.read())
            if verbose:
                print(f"\nMember [{idx+1}/{min(len(gz_members), max_members)}]: {member.name} ({len(data) // STRUCT_SIZE} records)")
            recs = analyze_record_data(data, max_records=max_records_per_member, verbose=verbose)
            all_records.extend(recs)
    return all_records


def print_summary(records, label="Dataset"):
    if not records:
        print(f"No records processed for {label}.")
        return

    n = len(records)
    avg_entropy = sum(r["entropy"] for r in records) / n
    avg_legal = sum(r["legal_count"] for r in records) / n
    avg_nonzero = sum(r["nonzero_count"] for r in records) / n
    sum_errors = [r for r in records if abs(r["sum"] - 1.0) > 1e-4]

    print("\n" + "=" * 60)
    print(f"Summary for {label}:")
    print(f"  Total records analyzed: {n}")
    print(f"  Average Shannon entropy: {avg_entropy:.4f} nats")
    print(f"  Average legal moves:     {avg_legal:.2f}")
    print(f"  Average non-zero moves:  {avg_nonzero:.2f}")
    print(f"  Probability sum errors:  {len(sum_errors)}")
    if sum_errors:
        print(f"  WARNING: {len(sum_errors)} records had sum(probabilities) != 1.0000!")
    print("=" * 60 + "\n")


def main():
    parser = argparse.ArgumentParser(
        description="Inspect policy distributions in V6 training data chunks."
    )
    parser.add_argument("path", help="Path to .tar archive, .gz chunk file, or directory")
    parser.add_argument(
        "--max-records",
        type=int,
        default=10,
        help="Max records to display per chunk (default: 10, set 0 for all)",
    )
    parser.add_argument(
        "--max-members",
        type=int,
        default=3,
        help="Max .gz members to inspect if path is a .tar (default: 3)",
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Suppress per-record printouts and show summary only",
    )

    args = parser.parse_args()
    max_recs = None if args.max_records == 0 else args.max_records
    verbose = not args.quiet

    if not os.path.exists(args.path):
        print(f"Error: Path not found: {args.path}", file=sys.stderr)
        sys.exit(1)

    all_records = []
    if os.path.isdir(args.path):
        entries = sorted(os.listdir(args.path))
        tars = [os.path.join(args.path, e) for e in entries if e.endswith(".tar")]
        gzs = [os.path.join(args.path, e) for e in entries if e.endswith(".gz")]

        if tars:
            for t in tars[:args.max_members]:
                recs = inspect_tar(t, max_members=args.max_members, max_records_per_member=max_recs, verbose=verbose)
                all_records.extend(recs)
        elif gzs:
            for g in gzs[:args.max_members]:
                recs = inspect_file_gz(g, max_records=max_recs, verbose=verbose)
                all_records.extend(recs)
        else:
            # Recursive search for .gz
            for root, _, files in os.walk(args.path):
                for f in sorted(files):
                    if f.endswith(".gz"):
                        recs = inspect_file_gz(os.path.join(root, f), max_records=max_recs, verbose=verbose)
                        all_records.extend(recs)
                        if len(all_records) >= (max_recs or 50):
                            break
                if len(all_records) >= (max_recs or 50):
                    break
    elif args.path.endswith(".tar"):
        all_records = inspect_tar(
            args.path,
            max_members=args.max_members,
            max_records_per_member=max_recs,
            verbose=verbose,
        )
    elif args.path.endswith(".gz"):
        all_records = inspect_file_gz(args.path, max_records=max_recs, verbose=verbose)
    else:
        print(f"Error: Unsupported file format: {args.path}", file=sys.stderr)
        sys.exit(1)

    print_summary(all_records, label=os.path.basename(args.path))


if __name__ == "__main__":
    main()
