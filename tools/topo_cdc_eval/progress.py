#!/usr/bin/env python3
"""Real-time progress tracker for TopoCDC offline eval."""

from __future__ import annotations

import json
import time
from pathlib import Path
from typing import Any, Dict, Optional

DEFAULT_STATUS = Path(__file__).parent / "reports" / "eval_progress.json"


class ProgressTracker:
    def __init__(self, path: Path = DEFAULT_STATUS) -> None:
        self.path = path
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._start = time.time()
        self._write("init", "starting", 0.0, status="running")

    def _write(
        self,
        stage: str,
        detail: str,
        pct: float,
        status: str = "running",
        extra: Optional[Dict[str, Any]] = None,
    ) -> None:
        payload: Dict[str, Any] = {
            "updated_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
            "elapsed_s": round(time.time() - self._start, 1),
            "stage": stage,
            "detail": detail,
            "pct": round(max(0.0, min(100.0, pct)), 1),
            "status": status,
        }
        if extra:
            payload.update(extra)
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        tmp.replace(self.path)

    def update(
        self,
        stage: str,
        detail: str,
        pct: float,
        extra: Optional[Dict[str, Any]] = None,
    ) -> None:
        self._write(stage, detail, pct, status="running", extra=extra)

    def done(self, exit_code: int, summary: Optional[Dict[str, Any]] = None) -> None:
        self._write(
            "done",
            f"exit_code={exit_code}",
            100.0,
            status="done" if exit_code == 0 else "failed",
            extra={"exit_code": exit_code, "summary": summary or {}},
        )

    def fail(self, error: str) -> None:
        self._write("error", error, 100.0, status="failed", extra={"error": error})


def read_progress(path: Path = DEFAULT_STATUS) -> Optional[Dict[str, Any]]:
    if not path.is_file():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
