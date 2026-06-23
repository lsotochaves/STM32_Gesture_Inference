#!/usr/bin/env python3
"""
Gesture Feature Extractor
Usage: python feature_extractor.py

Reads all CSVs from ../CSVs/, computes a 25-feature vector per recording,
and writes ../Training_Data/training_features.csv.
"""

import os
import sys
import csv
import math

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PARENT_DIR = os.path.dirname(SCRIPT_DIR)
CSV_DIR    = os.path.join(PARENT_DIR, "CSVs")
OUTPUT_DIR = os.path.join(PARENT_DIR, "Training_Data")
OUTPUT_FILE = os.path.join(OUTPUT_DIR, "training_features.csv")

# ---------------------------------------------------------------------------
# Feature column order (must stay in sync with compute_features)
# ---------------------------------------------------------------------------
AXES = ["gx", "gy", "gz"]
AXIS_FEATURES = ["mean", "std", "min", "max", "range", "energy", "zcr"]
MAG_FEATURES   = ["mag_mean", "mag_max", "mag_std", "mag_energy"]
RATIO_FEATURES = ["gy_gx_energy_ratio"]

FEATURE_COLUMNS = (
    [f"{ax}_{feat}" for ax in AXES for feat in AXIS_FEATURES]
    + MAG_FEATURES
    + RATIO_FEATURES
)
CSV_HEADER = ["label"] + FEATURE_COLUMNS


# ---------------------------------------------------------------------------
# Math helpers (stdlib only — no numpy dependency)
# ---------------------------------------------------------------------------
def _mean(xs):
    return sum(xs) / len(xs)


def _std(xs):
    m = _mean(xs)
    return math.sqrt(sum((x - m) ** 2 for x in xs) / len(xs))


def _energy(xs):
    """Mean squared value — normalized by N so duration doesn't inflate energy."""
    return sum(x * x for x in xs) / len(xs)


def _zcr(xs):
    """Zero-crossing rate: fraction of consecutive pairs that change sign.
    Zeros are treated as +1 to avoid spurious crossings on flat segments."""
    if len(xs) < 2:
        return 0.0
    signs = [1 if x >= 0 else -1 for x in xs]
    crossings = sum(1 for a, b in zip(signs, signs[1:]) if a != b)
    return crossings / (len(xs) - 1)


def _axis_features(xs):
    lo  = min(xs)
    hi  = max(xs)
    return [
        _mean(xs),
        _std(xs),
        lo,
        hi,
        hi - lo,
        _energy(xs),
        _zcr(xs),
    ]


# ---------------------------------------------------------------------------
# Label extraction
# ---------------------------------------------------------------------------
def extract_label(stem):
    """
    'roll_001'        -> 'roll'
    'swipe_left_002'  -> 'swipe_left'
    'punch'           -> 'punch'   (no numeric suffix)
    """
    parts = stem.split("_")
    if len(parts) > 1 and parts[-1].isdigit():
        return "_".join(parts[:-1])
    return stem


# ---------------------------------------------------------------------------
# Per-file processing
# ---------------------------------------------------------------------------
def process_csv(path):
    """Returns a feature dict or None on error."""
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        # Normalise header capitalisation
        fieldnames = [c.strip().lower() for c in (reader.fieldnames or [])]
        reader.fieldnames = fieldnames

        if not {"gx", "gy", "gz"}.issubset(set(fieldnames)):
            missing = {"gx", "gy", "gz"} - set(fieldnames)
            print(f"  [WARN] {os.path.basename(path)}: missing columns {missing}, skipping.",
                  file=sys.stderr)
            return None

        rows = list(reader)

    if len(rows) < 2:
        print(f"  [WARN] {os.path.basename(path)}: fewer than 2 samples, skipping.",
              file=sys.stderr)
        return None

    try:
        gx = [float(r["gx"]) for r in rows]
        gy = [float(r["gy"]) for r in rows]
        gz = [float(r["gz"]) for r in rows]
    except ValueError as e:
        print(f"  [WARN] {os.path.basename(path)}: bad numeric data ({e}), skipping.",
              file=sys.stderr)
        return None

    mag = [math.sqrt(x**2 + y**2 + z**2) for x, y, z in zip(gx, gy, gz)]

    gx_energy = _energy(gx)
    gy_energy = _energy(gy)
    gy_gx_ratio = gy_energy / gx_energy if gx_energy != 0.0 else float("inf")

    features = (
        _axis_features(gx)
        + _axis_features(gy)
        + _axis_features(gz)
        + [_mean(mag), max(mag), _std(mag), _energy(mag)]
        + [gy_gx_ratio]
    )

    label = extract_label(os.path.splitext(os.path.basename(path))[0])
    return dict(zip(CSV_HEADER, [label] + features))


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    if not os.path.isdir(CSV_DIR):
        print(f"[ERROR] CSV directory not found: {CSV_DIR}")
        sys.exit(1)

    csv_files = sorted(
        p for p in (os.path.join(CSV_DIR, f) for f in os.listdir(CSV_DIR))
        if p.endswith(".csv")
    )

    if not csv_files:
        print(f"No CSV files found in {CSV_DIR}")
        sys.exit(0)

    print(f"Found {len(csv_files)} CSV file(s) in {CSV_DIR}\n")

    rows = []
    for path in csv_files:
        print(f"  Processing {os.path.basename(path)} ...")
        row = process_csv(path)
        if row is not None:
            rows.append(row)

    if not rows:
        print("\nNo valid recordings processed. Output not written.")
        sys.exit(1)

    os.makedirs(OUTPUT_DIR, exist_ok=True)

    with open(OUTPUT_FILE, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_HEADER)
        writer.writeheader()
        writer.writerows(rows)

    print(f"\n[SAVED]  {OUTPUT_FILE}  ({len(rows)} rows, {len(FEATURE_COLUMNS)} features)")

    # Class summary
    label_counts = {}
    for row in rows:
        label_counts[row["label"]] = label_counts.get(row["label"], 0) + 1
    print("\nSamples per class:")
    for label, count in sorted(label_counts.items()):
        print(f"  {label}: {count}")


if __name__ == "__main__":
    main()
