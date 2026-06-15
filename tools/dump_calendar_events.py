#!/usr/bin/env python3

from __future__ import annotations

import argparse
import datetime as dt
import sys
from dataclasses import dataclass
from typing import List, Optional

import gi

gi.require_version("EDataServer", "1.2")
gi.require_version("ECal", "2.0")
gi.require_version("ICalGLib", "4.0")

from gi.repository import ECal, EDataServer, ICalGLib  # type: ignore


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


def to_epoch_seconds(value: dt.datetime) -> int:
    return int(value.astimezone().timestamp())


def parse_text(value) -> str:
    if value is None:
        return ""
    if hasattr(value, "get_value"):
        try:
            inner = value.get_value()
            if inner:
                return str(inner)
        except Exception:
            pass
    text = str(value)
    return "" if text in {"None", "<None>"} else text


def time_to_datetime(value) -> Optional[dt.datetime]:
    if value is None:
        return None
    if hasattr(value, "is_valid_time") and not value.is_valid_time():
        return None
    if hasattr(value, "as_timet_with_zone"):
        zone = ECal.util_get_system_timezone()
        ts = value.as_timet_with_zone(zone)
    elif hasattr(value, "as_timet"):
        ts = value.as_timet()
    else:
        return None
    return dt.datetime.fromtimestamp(int(ts), tz=dt.timezone.utc).astimezone()


def format_range(start: dt.datetime, end: dt.datetime, all_day: bool) -> str:
    if all_day:
        return "All day"
    return f"{start:%Y-%m-%d %H:%M} -> {end:%H:%M}"


def collect_instances(client, source_label: str, source_uid: str, start_ts: int, end_ts: int) -> List[EventRow]:
    rows: List[EventRow] = []

    def callback(component, instance_start, instance_end, user_data, cancellable):
        _ = user_data, cancellable
        title = parse_text(component.get_summary()) or "(untitled)"
        location = parse_text(component.get_location())
        start = time_to_datetime(instance_start)
        if start is None:
            return True
        end = time_to_datetime(instance_end) or start
        if end < start:
            end = start
        all_day = bool(instance_start.is_date()) if hasattr(instance_start, "is_date") else False
        rows.append(
            EventRow(
                month_key=f"{start.year:04d}-{start.month:02d}",
                source_label=source_label,
                source_uid=source_uid,
                title=title,
                location=location,
                start=start,
                end=end,
                all_day=all_day,
            )
        )
        return True

    client.generate_instances_sync(start_ts, end_ts, None, callback, None)
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description="Print EDS calendar events for last/current/next month.")
    parser.add_argument("--refresh", action="store_true", help="Ask EDS to refresh each calendar backend first.")
    args = parser.parse_args()

    registry = EDataServer.SourceRegistry.new_sync(None)
    sources = list(registry.list_enabled(EDataServer.SOURCE_EXTENSION_CALENDAR))
    if not sources:
        print("No enabled calendar sources found.", file=sys.stderr)
        return 1

    today = dt.date.today()
    first_month = month_start(add_months(today, -1))
    last_month = add_months(first_month, 1)
    next_month = add_months(first_month, 2)
    window_end = add_months(first_month, 3)

    start_dt = dt.datetime.combine(first_month, dt.time.min).astimezone()
    end_dt = dt.datetime.combine(window_end, dt.time.min).astimezone()

    all_rows: List[EventRow] = []

    for source in sources:
        source_label = source.dup_display_name() or "Calendar"
        source_uid = source.dup_uid() or ""

        if args.refresh:
            try:
                registry.refresh_backend_sync(source_uid, None)
            except Exception as exc:
                print(f"[refresh failed] {source_label}: {exc}", file=sys.stderr)

        try:
            client = ECal.Client.connect_sync(
                source,
                ECal.ClientSourceType.EVENTS,
                30,
                None,
            )
        except Exception as exc:
            print(f"[connect failed] {source_label}: {exc}", file=sys.stderr)
            continue

        if client is None:
            print(f"[connect failed] {source_label}: client is None", file=sys.stderr)
            continue

        try:
            rows = collect_instances(
                client,
                source_label,
                source_uid,
                to_epoch_seconds(start_dt),
                to_epoch_seconds(end_dt),
            )
            all_rows.extend(rows)
        except Exception as exc:
            print(f"[query failed] {source_label}: {exc}", file=sys.stderr)

    grouped = {
        f"{first_month:%Y-%m}": [],
        f"{last_month:%Y-%m}": [],
        f"{next_month:%Y-%m}": [],
    }
    for row in all_rows:
        grouped.setdefault(row.month_key, []).append(row)

    for month_key in [f"{first_month:%Y-%m}", f"{last_month:%Y-%m}", f"{next_month:%Y-%m}"]:
        rows = sorted(grouped.get(month_key, []), key=lambda r: (r.start, r.source_label, r.title))
        print(f"\n== {month_key} ({len(rows)} events) ==")
        if not rows:
            print("  (no events)")
            continue
        for row in rows:
            range_text = format_range(row.start, row.end, row.all_day)
            location = f" @ {row.location}" if row.location else ""
            print(f"  {row.start:%Y-%m-%d %H:%M} | {range_text} | {row.source_label} | {row.title}{location}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
