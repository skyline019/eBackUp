#!/usr/bin/env python3
"""Sliding window union-find (adjacent same-key) for TopoCDC Hom-0 v1.1."""

from __future__ import annotations

from typing import List


class SlotUfWindow:
    """Ring buffer of window_w keys with adjacent same-key union."""

    def __init__(self, window_w: int) -> None:
        self.w = window_w
        self.key = [0] * window_w
        self.parent = list(range(window_w))
        self.rank = [0] * window_w
        self.head = 0
        self.filled = 0
        self.components = 0

    def _find(self, x: int) -> int:
        while self.parent[x] != x:
            self.parent[x] = self.parent[self.parent[x]]
            x = self.parent[x]
        return x

    def _union(self, a: int, b: int) -> bool:
        ra = self._find(a)
        rb = self._find(b)
        if ra == rb:
            return False
        if self.rank[ra] < self.rank[rb]:
            ra, rb = rb, ra
        self.parent[rb] = ra
        if self.rank[ra] == self.rank[rb]:
            self.rank[ra] += 1
        self.components -= 1
        return True

    def _rebuild_components(self) -> None:
        self.parent = list(range(self.w))
        self.rank = [0] * self.w
        if self.filled == 0:
            self.components = 0
            return
        self.components = self.filled
        limit = self.filled - 1
        for i in range(limit):
            s0 = (self.head + i) % self.w
            s1 = (self.head + i + 1) % self.w
            if self.key[s0] == self.key[s1]:
                self._union(s0, s1)

    def load_window(self, keys: List[int]) -> None:
        assert len(keys) <= self.w
        self.filled = len(keys)
        self.head = 0
        for i, k in enumerate(keys):
            self.key[i] = k & 0xFF
        self._rebuild_components()

    def component_count(self) -> int:
        return self.components

    def slide(self, key_out_unused: int, key_in: int) -> int:
        """Slide window left; return delta C (C_after - C_before)."""
        if self.filled < self.w:
            slot = self.filled
            self.key[slot] = key_in & 0xFF
            self.filled += 1
            self._rebuild_components()
            return self.components

        c_before = self.components
        tail = (self.head + self.w - 1) % self.w
        left_neighbor = (self.head + self.w - 2) % self.w if self.w > 1 else tail

        self.head = (self.head + 1) % self.w
        self.key[tail] = key_in & 0xFF
        self._rebuild_components()
        return self.components - c_before

    def clone(self) -> "SlotUfWindow":
        out = SlotUfWindow(self.w)
        out.key = list(self.key)
        out.parent = list(self.parent)
        out.rank = list(self.rank)
        out.head = self.head
        out.filled = self.filled
        out.components = self.components
        return out
