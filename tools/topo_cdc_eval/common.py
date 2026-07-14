#!/usr/bin/env python3
"""TopoCDC shared utilities: gear table, mask, chunk profile."""

from __future__ import annotations

from dataclasses import dataclass
from typing import List, Tuple

GEAR_POLY = 0xD0000001


def splitmix64(x: int) -> int:
    x = (x + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
    x = ((x ^ (x >> 30)) * 0xBF58476D1CE4E5B9) & 0xFFFFFFFFFFFFFFFF
    x = ((x ^ (x >> 27)) * 0x94D049BB133111EB) & 0xFFFFFFFFFFFFFFFF
    return (x ^ (x >> 31)) & 0xFFFFFFFFFFFFFFFF


def rotl32(v: int, bits: int) -> int:
    bits &= 31
    if bits == 0:
        return v & 0xFFFFFFFF
    v &= 0xFFFFFFFF
    return ((v << bits) | (v >> (32 - bits))) & 0xFFFFFFFF


def init_gear_table() -> List[int]:
    table = [0] * 256
    for i in range(256):
        h = i
        for _ in range(16):
            h = (h >> 1) ^ ((-(h & 1)) & GEAR_POLY)
        table[i] = h & 0xFFFFFFFF
    return table


def init_keyed_gear_table(seed: int) -> List[int]:
    gear = init_gear_table()
    if seed == 0:
        return gear
    state = seed & 0xFFFFFFFFFFFFFFFF
    for i in range(256):
        state = splitmix64(state)
        gear[i] ^= state & 0xFFFFFFFF
        gear[i] = rotl32(gear[i], (seed + i) & 15)
    return gear


def build_mask(avg_size: int) -> int:
    bits = 0
    v = max(avg_size, 1)
    while v > 1:
        v >>= 1
        bits += 1
    if bits == 0:
        bits = 1
    if bits > 31:
        bits = 31
    return (1 << bits) - 1


def build_topo_mask(avg_size: int, calib_permille: int = 0, topo_shift: int = 1) -> int:
    if calib_permille > 0:
        p_topo = max(calib_permille, 1) / 1000.0
        effective = max(int(avg_size * p_topo), 1)
        return build_mask(effective)
    shifted = max(avg_size >> topo_shift, 1)
    return build_mask(shifted)


@dataclass
class ChunkProfile:
    min_size: int = 64 * 1024
    avg_size: int = 256 * 1024
    max_size: int = 1024 * 1024
    window_w: int = 64


def profile_for_file_size(file_size: int) -> ChunkProfile:
    if file_size <= 256 * 1024:
        return ChunkProfile(min_size=32 * 1024, avg_size=128 * 1024, max_size=512 * 1024)
    if file_size >= 64 * 1024 * 1024:
        return ChunkProfile(min_size=128 * 1024, avg_size=512 * 1024, max_size=2 * 1024 * 1024)
    return ChunkProfile()


def init_window_hash(data: bytes, end: int, w: int, gear: List[int]) -> int:
    h = 0
    start = end - w
    for i in range(start, end):
        h = ((h << 1) + gear[data[i]]) & 0xFFFFFFFF
    return h


def chunk_lengths_from_cuts(cuts: List[int], total: int) -> List[int]:
    if not cuts:
        return [total] if total else []
    lengths: List[int] = []
    prev = 0
    for c in cuts:
        lengths.append(c - prev)
        prev = c
    if prev < total:
        lengths.append(total - prev)
    return lengths


def mean_chunk(lengths: List[int]) -> float:
    if not lengths:
        return 0.0
    return sum(lengths) / len(lengths)


def simulate_one_byte_reuse(
    cuts_a: List[int], cuts_b: List[int], edit_offset: int
) -> Tuple[int, int, float]:
    if not cuts_a:
        return 0, 0, 0.0
    boundaries_a = [0] + list(cuts_a)
    boundaries_b = [0] + list(cuts_b)
    total = max(boundaries_a[-1], boundaries_b[-1])

    def to_ranges(bounds: List[int]) -> List[Tuple[int, int]]:
        out = []
        for i in range(len(bounds) - 1):
            out.append((bounds[i], bounds[i + 1]))
        if bounds[-1] < total:
            out.append((bounds[-1], total))
        return out

    ra = to_ranges(boundaries_a)
    rb = to_ranges(boundaries_b)
    unchanged = 0
    for (a0, a1), (b0, b1) in zip(ra, rb):
        if a0 == b0 and a1 == b1:
            if a1 <= edit_offset or a0 > edit_offset:
                unchanged += 1
    n = max(len(ra), len(rb))
    return unchanged, n, (100.0 * unchanged / n) if n else 0.0
