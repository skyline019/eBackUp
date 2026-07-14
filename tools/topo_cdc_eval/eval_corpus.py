#!/usr/bin/env python3
"""Run TopoCDC Phase 1/2 offline evaluation with diagnostics and calibration."""

from __future__ import annotations

import argparse
import csv
import json
import os
import random
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from common import (
    ChunkProfile,
    chunk_lengths_from_cuts,
    mean_chunk,
    init_keyed_gear_table,
    profile_for_file_size,
    simulate_one_byte_reuse,
)
from hom_scan import calibrate_topo_permille, hom_chunk_cuts, measure_probe_rates
from progress import DEFAULT_STATUS, ProgressTracker
from tri_scan import tri_chunk_cuts


def calib_sample(data: bytes, profile: ChunkProfile) -> bytes:
    need = max(profile.avg_size * 4, 1024 * 1024)
    return data[: min(len(data), need)]


def make_random_data(size: int, seed: int) -> bytes:
    rng = random.Random(seed)
    return bytes(rng.getrandbits(8) for _ in range(size))


def collect_source_corpus(root: Path, max_bytes: int) -> bytes:
    chunks: List[bytes] = []
    total = 0
    for ext in ("*.cc", "*.h", "*.py", "*.md"):
        for p in sorted(root.rglob(ext)):
            if total >= max_bytes:
                break
            try:
                blob = p.read_bytes()
            except OSError:
                continue
            take = min(len(blob), max_bytes - total)
            chunks.append(blob[:take])
            total += take
    return b"".join(chunks)


def fastcdc_chunk_cuts(data: bytes, profile: ChunkProfile) -> List[int]:
    """FastCDC Stream proxy (standard gear, single mask)."""
    from common import build_mask, init_gear_table, init_window_hash

    gear = init_gear_table()
    mask = build_mask(profile.avg_size)
    w = 64
    n = len(data)
    cuts: List[int] = []
    pos = 0
    while pos < n:
        remaining = n - pos
        if remaining <= profile.min_size:
            break
        scan_start = pos + profile.min_size
        cut_limit = min(pos + profile.max_size, n)
        if scan_start >= cut_limit or scan_start < w:
            pos = cut_limit
            if pos < n:
                cuts.append(pos)
            continue
        h = init_window_hash(data, scan_start, w, gear)
        found = False
        p = scan_start
        while p < cut_limit:
            if (h & mask) == 0:
                pos = p
                found = True
                break
            h = ((h << 1) + gear[data[p]] - gear[data[p - w]]) & 0xFFFFFFFF
            p += 1
        if not found:
            pos = cut_limit
        if pos < n:
            cuts.append(pos)
    return cuts


def eval_hom(
    data: bytes,
    seed: int,
    calib_permille: int,
    topo_shift: int,
    window_w: int,
    progress: Optional[ProgressTracker] = None,
    progress_label: str = "hom",
    progress_base: float = 0.0,
    progress_span: float = 100.0,
) -> Dict[str, Any]:
    profile = profile_for_file_size(len(data))
    profile.window_w = window_w

    def on_chunk(pos: int, total: int, detail: str) -> None:
        if not progress:
            return
        frac = pos / total if total else 1.0
        progress.update(
            f"{progress_label}_scan",
            f"{detail} {pos}/{total}",
            progress_base + progress_span * 0.7 * frac,
            extra={"bytes_done": pos, "bytes_total": total, "label": progress_label},
        )

    cuts, probes, stats = hom_chunk_cuts(
        data,
        profile,
        seed=seed,
        calib_permille=calib_permille,
        topo_shift=topo_shift,
        on_progress=on_chunk,
    )
    lengths = chunk_lengths_from_cuts(cuts, len(data))
    mean = mean_chunk(lengths)

    if progress:
        progress.update(
            f"{progress_label}_reuse",
            "1-byte edit reuse sim",
            progress_base + progress_span * 0.85,
        )

    edit_at = min(5 * 1024 * 1024, max(0, len(data) // 2))
    edited = bytearray(data)
    if edit_at < len(edited):
        edited.insert(edit_at, 0x42)
    else:
        edited.append(0x42)
    cuts_b, _, _ = hom_chunk_cuts(
        bytes(edited), profile, seed=seed,
        calib_permille=calib_permille, topo_shift=topo_shift,
    )
    unchanged, total_chunks, reuse_pct = simulate_one_byte_reuse(cuts, cuts_b, edit_at)

    return {
        "variant": "hom",
        "bytes": len(data),
        "chunks": len(lengths),
        "mean_chunk": mean,
        "avg_target": profile.avg_size,
        "mean_ratio": mean / profile.avg_size if profile.avg_size else 0,
        "probes": probes,
        "probes_per_byte": probes / len(data) if data else 0,
        "p_primary": stats.get("p_primary", 0.0),
        "p_topo": stats.get("p_topo", 0.0),
        "p_joint": stats.get("p_joint", 0.0),
        "max_cut_ratio": stats.get("max_cut_ratio", 0.0),
        "hash_cut_count": stats.get("hash_cut_count", 0),
        "max_cut_count": stats.get("max_cut_count", 0),
        "reuse_1byte_pct": reuse_pct,
        "reuse_1byte_unchanged": unchanged,
        "reuse_1byte_total": total_chunks,
        "calib_permille": calib_permille,
        "topo_shift": topo_shift,
        "window_w": window_w,
        "seed": seed,
    }


def eval_tri(data: bytes, seed: int, topo_shift: int, k_points: int) -> Dict[str, Any]:
    profile = profile_for_file_size(len(data))
    cuts, probes = tri_chunk_cuts(data, profile, seed=seed, topo_shift=topo_shift, k_points=k_points)
    lengths = chunk_lengths_from_cuts(cuts, len(data))
    return {
        "variant": "tri",
        "bytes": len(data),
        "chunks": len(lengths),
        "mean_chunk": mean_chunk(lengths),
        "avg_target": profile.avg_size,
        "mean_ratio": mean_chunk(lengths) / profile.avg_size if profile.avg_size else 0,
        "probes": probes,
        "probes_per_byte": probes / len(data) if data else 0,
        "topo_shift": topo_shift,
        "seed": seed,
    }


def three_way_ab(data: bytes, seed: int, calib_permille: int) -> Dict[str, Any]:
    profile = profile_for_file_size(len(data))
    hom_cuts, _, _ = hom_chunk_cuts(
        data, profile, seed=seed, calib_permille=calib_permille, topo_shift=1
    )
    stream_cuts = fastcdc_chunk_cuts(data, profile)
    edit_at = min(5 * 1024 * 1024, max(0, len(data) // 2))
    edited = bytearray(data)
    edited.insert(edit_at, 0x42) if edit_at < len(edited) else edited.append(0x42)
    edited_b = bytes(edited)

    hom_b, _, _ = hom_chunk_cuts(
        edited_b, profile, seed=seed, calib_permille=calib_permille, topo_shift=1
    )
    stream_b = fastcdc_chunk_cuts(edited_b, profile)
    _, _, hom_reuse = simulate_one_byte_reuse(hom_cuts, hom_b, edit_at)
    _, _, stream_reuse = simulate_one_byte_reuse(stream_cuts, stream_b, edit_at)
    return {
        "stream_reuse_1byte_pct": stream_reuse,
        "topo_reuse_1byte_pct": hom_reuse,
        "stream_chunks": len(chunk_lengths_from_cuts(stream_cuts, len(data))),
        "topo_chunks": len(chunk_lengths_from_cuts(hom_cuts, len(data))),
        "note": "G-TCDC v6 compare via ebbackup_topo_cdc_eval (C++)",
    }


def grid_calibrate(
    data: bytes,
    seed: int,
    windows: List[int],
    progress: Optional[ProgressTracker] = None,
    progress_base: float = 0.0,
    progress_span: float = 30.0,
) -> Tuple[int, int, Dict[str, Any]]:
    profile = profile_for_file_size(len(data))
    sample = calib_sample(data, profile)
    best_w = profile.window_w
    best_perm = calibrate_topo_permille(sample, profile, seed)
    if progress:
        progress.update("grid_calib", f"baseline w={best_w} perm={best_perm}", progress_base)
    best_eval = eval_hom(
        data, seed, best_perm, 1, best_w, progress, "grid_base", progress_base, progress_span * 0.4
    )

    step = progress_span * 0.5 / max(len(windows), 1)
    for idx, w in enumerate(windows):
        if progress:
            progress.update("grid_calib", f"window_w={w}", progress_base + progress_span * 0.4 + idx * step)
        prof = ChunkProfile(
            min_size=profile.min_size,
            avg_size=profile.avg_size,
            max_size=profile.max_size,
            window_w=w,
        )
        perm = calibrate_topo_permille(sample, prof, seed)
        ev = eval_hom(
            data,
            seed,
            perm,
            1,
            w,
            progress,
            f"grid_w{w}",
            progress_base + progress_span * 0.4 + idx * step,
            step,
        )
        if ev["max_cut_ratio"] < best_eval.get("max_cut_ratio", 1.0) or (
            0.85 <= ev["mean_ratio"] <= 1.15
            and ev["max_cut_ratio"] < 0.05
        ):
            best_w, best_perm, best_eval = w, perm, ev
    return best_w, best_perm, best_eval


def go_no_go(hom: Dict[str, Any], tri: Dict[str, Any]) -> Dict[str, Any]:
    hom_mean_ok = 0.85 <= hom["mean_ratio"] <= 1.15
    max_cut_ok = hom.get("max_cut_ratio", 1.0) < 0.05
    tri_mean_ok = 0.85 <= tri["mean_ratio"] <= 1.15
    tri_beats_hom = tri["reuse_1byte_pct"] >= hom["reuse_1byte_pct"] + 5.0 if "reuse_1byte_pct" in tri else False
    hom_go = hom_mean_ok and max_cut_ok
    tri_go = tri_mean_ok and tri_beats_hom
    return {
        "hom_go": hom_go,
        "tri_go": tri_go,
        "hom_mean_ok": hom_mean_ok,
        "max_cut_ok": max_cut_ok,
        "tri_mean_ok": tri_mean_ok,
        "tri_beats_hom_by_5pp": tri_beats_hom,
        "recommended_variant": "tri" if tri_go else "hom",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="TopoCDC offline eval v2")
    parser.add_argument("--out", default="reports/go_no_go_v2.json")
    parser.add_argument("--csv", default="reports/go_no_go_v2.csv")
    parser.add_argument("--seed", type=int, default=0x12345678)
    parser.add_argument("--topo-shift", type=int, default=1)
    parser.add_argument("--k-points", type=int, default=16)
    parser.add_argument("--synthetic-mb", type=int, default=8)
    parser.add_argument("--source-root", default="engine_cpp/src/chunk")
    parser.add_argument("--cuda-root", default=os.environ.get("TOPO_EVAL_CUDA_ROOT", ""))
    parser.add_argument("--grid", action="store_true", help="Run window_w calibration grid")
    parser.add_argument(
        "--progress-file",
        default=str(DEFAULT_STATUS),
        help="Real-time progress JSON path",
    )
    args = parser.parse_args()

    progress = ProgressTracker(Path(args.progress_file))
    progress.update("init", "loading datasets", 1.0)

    synthetic_size = args.synthetic_mb * 1024 * 1024
    datasets: Dict[str, bytes] = {"synthetic": make_random_data(synthetic_size, 93)}

    src = Path(args.source_root)
    if src.is_dir():
        progress.update("load", "collecting source corpus", 3.0)
        datasets["source"] = collect_source_corpus(src, 4 * 1024 * 1024)
    if args.cuda_root:
        cuda = Path(args.cuda_root)
        if cuda.is_dir():
            progress.update("load", "collecting cuda corpus", 5.0)
            datasets["cuda"] = collect_source_corpus(cuda, 8 * 1024 * 1024)

    report: Dict[str, Any] = {"datasets": {}, "summary": {}, "three_way_ab": {}}
    csv_rows: List[Dict[str, Any]] = []
    names = list(datasets.keys())
    span = 90.0 / max(len(names), 1)

    try:
        for idx, (name, data) in enumerate(datasets.items()):
            base = 10.0 + idx * span
            progress.update("dataset", name, base, extra={"corpus": name, "bytes": len(data)})
            if args.grid and name == "synthetic":
                best_w, best_perm, hom = grid_calibrate(
                    data, args.seed, [32, 64, 128], progress, base, span * 0.55
                )
            else:
                profile = profile_for_file_size(len(data))
                sample = calib_sample(data, profile)
                perm = calibrate_topo_permille(sample, profile, args.seed)
                hom = eval_hom(
                    data,
                    args.seed,
                    perm,
                    args.topo_shift,
                    profile.window_w,
                    progress,
                    f"{name}_hom",
                    base + span * 0.1,
                    span * 0.45,
                )

            progress.update("dataset", f"{name}: tri eval", base + span * 0.6)
            tri = eval_tri(data, args.seed, args.topo_shift, args.k_points)
            decision = go_no_go(hom, tri)
            progress.update("dataset", f"{name}: three_way_ab", base + span * 0.8)
            ab = three_way_ab(data, args.seed, hom["calib_permille"])
            report["datasets"][name] = {"hom": hom, "tri": tri, "decision": decision, "three_way": ab}
            report["three_way_ab"][name] = ab
            csv_rows.append({"corpus": name, **{k: hom[k] for k in hom if k != "variant"}})
            progress.update("dataset", f"{name}: done", base + span)

        primary = report["datasets"].get("synthetic", {}).get("hom", {})
        hom_go = report["datasets"].get("synthetic", {}).get("decision", {}).get("hom_go", False)
        report["summary"] = {
            "phase1_recommendation": "proceed_hom_calibrated" if hom_go else "recalibrate",
            "synthetic_mean_ratio": primary.get("mean_ratio", 0),
            "synthetic_max_cut_ratio": primary.get("max_cut_ratio", 0),
            "synthetic_calib_permille": primary.get("calib_permille", 0),
            "gear_table_entries": len(init_keyed_gear_table(args.seed)),
        }

        out_path = Path(args.out)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(report, indent=2), encoding="utf-8")

        csv_path = Path(args.csv)
        if csv_rows:
            with csv_path.open("w", newline="", encoding="utf-8") as f:
                writer = csv.DictWriter(f, fieldnames=csv_rows[0].keys())
                writer.writeheader()
                writer.writerows(csv_rows)

        progress.done(0, report["summary"])
        print(json.dumps(report["summary"], indent=2))
        print(f"Wrote {out_path}")
        return 0
    except Exception as exc:
        progress.fail(str(exc))
        raise


if __name__ == "__main__":
    sys.exit(main())
