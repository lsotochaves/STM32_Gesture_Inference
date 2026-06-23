#!/usr/bin/env python3
"""
Train/Validation Split
Usage: python split.py

Reads ../Training_Data/training_features.csv and writes:
  ../Training_Data/train.csv
  ../Training_Data/val.csv

Stratified 70/30 split: each class keeps its proportion in both sets.
"""

import os
import csv
import random

SEED        = 42
TRAIN_RATIO = 0.70

SCRIPT_DIR  = os.path.dirname(os.path.abspath(__file__))
DATA_DIR    = os.path.join(os.path.dirname(SCRIPT_DIR), "Training_Data")
INPUT_FILE  = os.path.join(DATA_DIR, "training_features.csv")
TRAIN_FILE  = os.path.join(DATA_DIR, "train.csv")
VAL_FILE    = os.path.join(DATA_DIR, "val.csv")


def main():
    if not os.path.isfile(INPUT_FILE):
        print(f"[ERROR] Not found: {INPUT_FILE}")
        print("        Run feature_extractor.py first.")
        raise SystemExit(1)

    with open(INPUT_FILE, newline="") as f:
        reader = csv.DictReader(f)
        fieldnames = reader.fieldnames
        rows = list(reader)

    # Group rows by class label
    by_class: dict[str, list] = {}
    for row in rows:
        by_class.setdefault(row["label"], []).append(row)

    rng = random.Random(SEED)

    train_rows, val_rows = [], []

    for label, samples in sorted(by_class.items()):
        rng.shuffle(samples)
        n_train = round(len(samples) * TRAIN_RATIO)
        train_rows.extend(samples[:n_train])
        val_rows.extend(samples[n_train:])
        print(f"  {label}: {len(samples)} total -> {n_train} train, {len(samples) - n_train} val")

    # Shuffle so classes aren't grouped sequentially
    rng.shuffle(train_rows)
    rng.shuffle(val_rows)

    def write_csv(path, data):
        with open(path, "w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(data)

    write_csv(TRAIN_FILE, train_rows)
    write_csv(VAL_FILE, val_rows)

    print(f"\n[SAVED]  {TRAIN_FILE}  ({len(train_rows)} rows)")
    print(f"[SAVED]  {VAL_FILE}  ({len(val_rows)} rows)")


if __name__ == "__main__":
    main()
