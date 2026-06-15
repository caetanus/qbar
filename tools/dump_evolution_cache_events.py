#!/usr/bin/env python3

from __future__ import annotations

import argparse
import configparser
import datetime as dt
import glob
import os
import re
import sqlite3
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional


@dataclass
class EventRow:
    month_key: str
    source_label: str
    source_uid: str
    title: str
    location: str
    start: dt.datetime
    end: dt.datetime
    all_day: bool


def month_start(value: dt.date) -> dt.date:
    return dt.date(value.year, value.month, 1)


def add_months(value: dt.date, delta: int) -> dt.date:
    year = value.year + (value.month - 1 + delta) // 12
    month = (value.month - 1 + delta) % 12 + 1
    return dt.date(year, month, 1)


def load_display_name(source_uid: str) -> str:
    path = Path.home() / ".config/evolution/sources" / f"{source_uid}.source"
    cp = configparser.ConfigParser(interpolation=None)
    if path.exists():
        cp.read(path)
        if cp.has_section("Data Source"):
            name = cp.get("Data Source", "DisplayName", fallback="").strip()
            if name:
                return name
        if cp.has_section("GNOME Online Accounts"):
            name = cp.get("GNOME Online Accounts", "Name", fallback="").strip()
            if name:
                return name
    return source_uid


def parse_eds_timestamp(value: str) -> Optional[dt.datetime]:
    value = value.strip()
    if not value:
        return None
    try:
        if len(value) == 8 and value.isdigit():
            return dt.datetime.strptime(value, "%Y%m%d")
        if value.endswith("Z"):
            parsed = dt.datetime.strptime(value, "%Y%m%dT%H%M%SZ")
            return parsed.replace(tzinfo=dt.timezone.utc).astimezone()
        if "T" in value and len(value) >= 15:
            return dt.datetime.strptime(value[:15], "%Y%m%dT%H%M%S")
    except ValueError:
        return None
    return None


def parse_ical_from_raw(raw: str) -> tuple[Optional[dt.datetime], Optional[dt.datetime], bool]:
    dtstart = None
    dtend = None
    all_day = False

    m = re.search(r"DTSTART(?:;[^:\r\n]*)?:(\d{8}(?:T\d{6}Z?)?)", raw, re.IGNORECASE)
    if m:
        token = m.group(1)
        all_day = "T" not in token
        dtstart = parse_eds_timestamp(token)

    m = re.search(r"DTEND(?:;[^:\r\n]*)?:(\d{8}(?:T\d{6}Z?)?)", raw, re.IGNORECASE)
    if m:
        dtend = parse_eds_timestamp(m.group(1))

    if dtstart is not None and dtend is None:
        dtend = dtstart

    return dtstart, dtend, all_day


def to_local_label(value: dt.datetime) -> str:
    if value.tzinfo is None:
        return value.strftime("%Y-%m-%d %H:%M")
    return value.astimezone().strftime("%Y-%m-%d %H:%M")


def scan_cache_db(db_path: Path, source_uid: str) -> Iterable[EventRow]:
    source_label = load_display_name(source_uid)
    con = sqlite3.connect(str(db_path))
    con.row_factory = sqlite3.Row
    try:
        cur = con.cursor()
        cur.execute(
            """
            SELECT ECacheUID, summary, location, occur_start, occur_end, ECacheOBJ
            FROM ECacheObjects
            WHERE ECacheState = 0
            """
        )
        for row in cur.fetchall():
            summary = (row["summary"] or "").strip()
            location = (row["location"] or "").strip()
            raw = row["ECacheOBJ"] or ""
            start = None
            end = None
            all_day = False

            if row["occur_start"]:
                start = parse_eds_timestamp(str(row["occur_start"]))
            if row["occur_end"]:
                end = parse_eds_timestamp(str(row["occur_end"]))
            if start is None:
                raw_start, raw_end, raw_all_day = parse_ical_from_raw(raw)
                start = raw_start
                if end is None:
                    end = raw_end
                all_day = raw_all_day
            if start is None:
                continue
            if end is None:
                end = start
            if end < start:
                end = start

            yield EventRow(
                month_key=f"{start.year:04d}-{start.month:02d}",
                source_label=source_label,
                source_uid=source_uid,
                title=summary or "(untitled)",
                location=location,
                start=start,
                end=end,
                all_day=all_day,
            )
    finally:
        con.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="Print cached Evolution calendar events for last/current/next month.")
    args = parser.parse_args()

    cache_root = Path.home() / ".cache/evolution/calendar"
    db_paths = sorted(cache_root.glob("*/cache.db"))
    if not db_paths:
        print("No Evolution calendar caches found.", file=os.sys.stderr)
        return 1

    today = dt.date.today()
    first_month = month_start(add_months(today, -1))
    month_keys = [f"{first_month:%Y-%m}", f"{add_months(first_month, 1):%Y-%m}", f"{add_months(first_month, 2):%Y-%m}"]

    start = time.perf_counter()
    events: list[EventRow] = []
    for db_path in db_paths:
        source_uid = db_path.parent.name
        try:
            events.extend(scan_cache_db(db_path, source_uid))
        except sqlite3.Error as exc:
            print(f"[cache error] {db_path}: {exc}", file=os.sys.stderr)

    elapsed_ms = (time.perf_counter() - start) * 1000.0
    print(f"Scanned {len(db_paths)} cache dbs in {elapsed_ms:.1f} ms")

    grouped: dict[str, list[EventRow]] = {key: [] for key in month_keys}
    for row in events:
        grouped.setdefault(row.month_key, []).append(row)

    for month_key in month_keys:
        rows = sorted(grouped.get(month_key, []), key=lambda r: (r.start, r.source_label, r.title))
        print(f"\n== {month_key} ({len(rows)} events) ==")
        if not rows:
            print("  (no events)")
            continue
        for row in rows:
            range_text = "All day" if row.all_day else f"{to_local_label(row.start)} -> {row.end.astimezone().strftime('%H:%M') if row.end.tzinfo else row.end.strftime('%H:%M')}"
            location = f" @ {row.location}" if row.location else ""
            print(f"  {to_local_label(row.start)} | {range_text} | {row.source_label} | {row.title}{location}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
