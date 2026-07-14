#!/usr/bin/env python3
"""TopoCDC Tri research-track offline chunker (simplified Delaunay flip proxy)."""

from __future__ import annotations

import math
from typing import List, Tuple

from common import ChunkProfile, build_mask, init_keyed_gear_table, init_window_hash


def _cross(ax: float, ay: float, bx: float, by: float) -> float:
    return ax * by - ay * bx


def _delaunay_flip_count(points: List[Tuple[float, float]]) -> int:
    """Count non-Delaunay edges in a fan triangulation (research proxy, O(K))."""
    k = len(points)
    if k < 4:
        return 0
    flips = 0
    for i in range(1, k - 1):
        ax, ay = points[0]
        bx, by = points[i]
        cx, cy = points[i + 1]
        if _cross(bx - ax, by - ay, cx - ax, cy - ay) <= 0:
            flips += 1
    for i in range(k - 2):
        ax, ay = points[i]
        bx, by = points[i + 1]
        cx, cy = points[i + 2]
        if _cross(bx - ax, by - ay, cx - ax, cy - ay) <= 0:
            flips += 1
    return flips


def tri_chunk_cuts(
    data: bytes,
    profile: ChunkProfile,
    seed: int = 0,
    topo_shift: int = 1,
    k_points: int = 16,
    q_mod: int = 65521,
) -> Tuple[List[int], int]:
    n = len(data)
    if n == 0:
        return [], 0
    if n <= profile.min_size:
        return [n], 0

    gear = init_keyed_gear_table(seed)
    w = profile.window_w
    shifted_avg = max(profile.avg_size >> topo_shift, 1)
    mask = build_mask(shifted_avg)
    cuts: List[int] = []
    probes = 0
    pos = 0
    recent_h: List[int] = []
    recent_t: List[int] = []

    while pos < n:
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
            probes += 1
            primary_ok = (h & mask) == 0
            recent_h.append(h)
            recent_t.append(p)
            if len(recent_h) > k_points:
                recent_h.pop(0)
                recent_t.pop(0)
            pts = [
                (float(t % k_points), float(h % q_mod))
                for t, h in zip(recent_t, recent_h)
            ]
            topo_ok = _delaunay_flip_count(pts) >= 1
            if primary_ok and topo_ok:
                pos = p
                found = True
                break
            h = ((h << 1) + gear[data[p]] - gear[data[p - w]]) & 0xFFFFFFFF
            p += 1

        if not found:
            pos = cut_limit
        if pos < n:
            cuts.append(pos)

    return cuts, probes
