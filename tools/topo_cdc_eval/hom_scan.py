#!/usr/bin/env python3
"""TopoCDC Hom-0 offline chunker (UF delta-C events, v1.1)."""

from __future__ import annotations

from typing import Callable, Dict, List, Optional, Tuple

from common import (
    ChunkProfile,
    build_topo_mask,
    chunk_lengths_from_cuts,
    init_keyed_gear_table,
    init_window_hash,
    mean_chunk,
)
from uf_window import SlotUfWindow


def topo_key(gear: List[int], byte_val: int) -> int:
    return gear[byte_val] & 0xFF


def measure_probe_rates(
    data: bytes,
    profile: ChunkProfile,
    seed: int = 0,
    calib_permille: int = 0,
    topo_shift: int = 1,
) -> Dict[str, float]:
    n = len(data)
    if n <= profile.min_size:
        return {"p_primary": 0.0, "p_topo": 0.0, "p_joint": 0.0, "probes": 0.0}

    gear = init_keyed_gear_table(seed)
    w = profile.window_w
    mask = build_topo_mask(profile.avg_size, calib_permille, topo_shift)

    primary_hits = 0
    topo_hits = 0
    joint_hits = 0
    probes = 0

    pos = 0
    while pos < n:
        scan_start = pos + profile.min_size
        cut_limit = min(pos + profile.max_size, n)
        if scan_start >= cut_limit or scan_start < w:
            pos = cut_limit
            continue

        uf = SlotUfWindow(w)
        keys = [topo_key(gear, data[i]) for i in range(scan_start - w, scan_start)]
        uf.load_window(keys)
        h = init_window_hash(data, scan_start, w, gear)

        p = scan_start
        while p < cut_limit:
            probes += 1
            primary_ok = (h & mask) == 0
            delta_c = uf.slide(topo_key(gear, data[p - w]), topo_key(gear, data[p]))
            topo_ok = delta_c != 0
            if primary_ok:
                primary_hits += 1
            if topo_ok:
                topo_hits += 1
            if primary_ok and topo_ok:
                joint_hits += 1
                break
            h = ((h << 1) + gear[data[p]] - gear[data[p - w]]) & 0xFFFFFFFF
            p += 1
        pos = cut_limit if p >= cut_limit else p

    denom = float(probes) if probes else 1.0
    return {
        "p_primary": primary_hits / denom,
        "p_topo": topo_hits / denom,
        "p_joint": joint_hits / denom,
        "probes": float(probes),
    }


def mean_chunk_len_hom(
    data: bytes,
    profile: ChunkProfile,
    seed: int,
    calib_permille: int,
    topo_shift: int = 1,
) -> float:
    cuts, _, _ = hom_chunk_cuts(
        data, profile, seed=seed, calib_permille=calib_permille, topo_shift=topo_shift
    )
    lengths = chunk_lengths_from_cuts(cuts, len(data))
    return mean_chunk(lengths)


def calibrate_topo_permille(
    sample: bytes,
    profile: ChunkProfile,
    seed: int,
    window_w: Optional[int] = None,
) -> int:
    prof = ChunkProfile(
        min_size=profile.min_size,
        avg_size=profile.avg_size,
        max_size=profile.max_size,
        window_w=window_w or profile.window_w,
    )
    rates = measure_probe_rates(sample, prof, seed=seed, calib_permille=0, topo_shift=1)
    p_topo = max(rates["p_topo"], 0.05)
    permille = min(max(int(p_topo * 1000.0 + 0.5), 1), 999)

    if len(sample) < prof.avg_size * 4:
        return permille

    lo_target = prof.avg_size * 0.85
    hi_target = prof.avg_size * 1.15
    lo, hi = 1, 128
    for _ in range(16):
        mid = (lo + hi) // 2
        mean = mean_chunk_len_hom(sample, prof, seed, mid)
        if lo_target <= mean <= hi_target:
            return mid
        if mean > hi_target:
            if mid <= 1:
                break
            hi = mid - 1
        else:
            if mid >= hi:
                break
            lo = mid + 1

    best = permille
    best_err = 1e300
    for p in range(lo, hi + 1):
        mean = mean_chunk_len_hom(sample, prof, seed, p)
        if lo_target <= mean <= hi_target:
            return p
        err = abs(mean - prof.avg_size)
        if err < best_err:
            best_err = err
            best = p
    return best


def hom_chunk_cuts(
    data: bytes,
    profile: ChunkProfile,
    seed: int = 0,
    calib_permille: int = 0,
    topo_shift: int = 1,
    on_progress: Optional[Callable[[int, int, str], None]] = None,
) -> Tuple[List[int], int, Dict[str, float]]:
    n = len(data)
    stats = {"max_cut_count": 0, "hash_cut_count": 0, "p_primary": 0.0,
             "p_topo": 0.0, "p_joint": 0.0}
    if n == 0:
        return [], 0, stats
    if n <= profile.min_size:
        return [n], 0, stats

    gear = init_keyed_gear_table(seed)
    w = profile.window_w
    mask = build_topo_mask(profile.avg_size, calib_permille, topo_shift)
    cuts: List[int] = []
    probes = 0
    pos = 0

    primary_hits = 0
    topo_hits = 0
    joint_hits = 0

    while pos < n:
        scan_start = pos + profile.min_size
        cut_limit = min(pos + profile.max_size, n)
        if scan_start >= cut_limit:
            pos = cut_limit
            if pos < n:
                cuts.append(pos)
                stats["max_cut_count"] += 1
            continue
        if scan_start < w:
            pos = cut_limit
            if pos < n:
                cuts.append(pos)
                stats["max_cut_count"] += 1
            continue

        uf = SlotUfWindow(w)
        keys = [topo_key(gear, data[i]) for i in range(scan_start - w, scan_start)]
        uf.load_window(keys)
        h = init_window_hash(data, scan_start, w, gear)
        found = False
        p = scan_start
        while p < cut_limit:
            probes += 1
            primary_ok = (h & mask) == 0
            delta_c = uf.slide(topo_key(gear, data[p - w]), topo_key(gear, data[p]))
            topo_ok = delta_c != 0
            if primary_ok:
                primary_hits += 1
            if topo_ok:
                topo_hits += 1
            if primary_ok and topo_ok:
                joint_hits += 1
                cuts.append(p)
                pos = p
                found = True
                stats["hash_cut_count"] += 1
                break
            h = ((h << 1) + gear[data[p]] - gear[data[p - w]]) & 0xFFFFFFFF
            p += 1

        if not found:
            pos = cut_limit
            if pos < n:
                cuts.append(pos)
                stats["max_cut_count"] += 1

        if on_progress and (len(cuts) % 4 == 0 or pos >= n):
            on_progress(pos, n, f"chunks={len(cuts)} probes={probes}")

    denom = float(probes) if probes else 1.0
    stats["p_primary"] = primary_hits / denom
    stats["p_topo"] = topo_hits / denom
    stats["p_joint"] = joint_hits / denom
    stats["max_cut_ratio"] = stats["max_cut_count"] / max(len(cuts), 1)
    return cuts, probes, stats
