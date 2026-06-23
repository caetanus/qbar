#!/usr/bin/env python3
"""Decode a qmlprofiler .qtd trace (XML) into readable aggregates.

Qt Creator renders these traces graphically; this reproduces its two most useful
views headlessly:

  * Statistics  - timed ranges (JavaScript / Binding / HandlingSignal / Creating /
                  Compiling) aggregated by source location: total time, calls, max,
                  mean. The biggest totals/maxes are what stalls the GUI thread.
  * Animation   - per-frame framerate over time. Dips below the display's refresh
                  rate are dropped frames ("travadinhas").

Usage: qtd-decode.py <trace.qtd> [--top N]
"""
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict

TIMED = {"JavaScript", "Javascript", "Binding", "HandlingSignal",
         "Creating", "Compiling", "Painting", "Event"}


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    top = 25
    if "--top" in sys.argv:
        top = int(sys.argv[sys.argv.index("--top") + 1])
    if not args:
        sys.exit("usage: qtd-decode.py <trace.qtd> [--top N]")
    path = args[0]

    root = ET.parse(path).getroot()
    trace_start = int(root.get("traceStart", 0))
    trace_end = int(root.get("traceEnd", 0))
    span_ms = (trace_end - trace_start) / 1e6 if trace_end else 0

    # event index -> metadata
    events = {}
    for ev in root.iter("event"):
        idx = int(ev.get("index"))
        etype = (ev.findtext("type") or "?").strip()
        fn = (ev.findtext("filename") or "").strip()
        line = (ev.findtext("line") or "").strip()
        details = (ev.findtext("details") or "").strip()
        disp = (ev.findtext("displayname") or "").strip()
        loc = f"{fn.split('/')[-1]}:{line}" if fn else disp
        events[idx] = (etype, loc, details)

    # aggregate timed ranges by (type, location, details)
    agg = defaultdict(lambda: [0, 0, 0])  # total_ns, count, max_ns
    longest = []   # (duration_ns, type, loc, details, startMs)
    frames = []    # (start_ns, framerate, animationcount, thread)
    timed = []     # (start_ns, end_ns, dur_ns, type, loc, details) — for correlation

    for r in root.iter("range"):
        idx = int(r.get("eventIndex"))
        etype, loc, details = events.get(idx, ("?", "?", ""))
        start = int(r.get("startTime", 0))
        if r.get("framerate") is not None:
            frames.append((start, int(r.get("framerate")),
                           int(r.get("animationcount", 0)),
                           r.get("thread", "?")))
            continue
        dur = r.get("duration")
        if dur is None:
            continue  # memory/amount range
        dur = int(dur)
        key = (etype, loc, details)
        a = agg[key]
        a[0] += dur
        a[1] += 1
        a[2] = max(a[2], dur)
        longest.append((dur, etype, loc, details, (start - trace_start) / 1e6))
        # JS-level ranges are what block the GUI thread; ignore Compiling/Creating noise.
        if etype in ("JavaScript", "Javascript", "Binding", "HandlingSignal", "Painting"):
            timed.append((start, start + dur, dur, etype, loc, details))
    timed.sort()

    print(f"trace span: {span_ms:.1f} ms\n")

    print("=== STATISTICS — timed ranges by source (total time desc) ===")
    print(f"{'total ms':>10} {'calls':>7} {'max ms':>9} {'mean ms':>9}  type / location / details")
    rows = sorted(agg.items(), key=lambda kv: kv[1][0], reverse=True)
    for (etype, loc, details), (tot, cnt, mx) in rows[:top]:
        d = (details[:48] + "…") if len(details) > 49 else details
        print(f"{tot/1e6:10.3f} {cnt:7d} {mx/1e6:9.3f} {tot/cnt/1e6:9.3f}  "
              f"{etype:14s} {loc:22s} {d}")

    print("\n=== SINGLE LONGEST RANGES (worst individual stalls) ===")
    print(f"{'dur ms':>9} {'@ ms':>10}  type / location / details")
    for dur, etype, loc, details, st in sorted(longest, reverse=True)[:top]:
        d = (details[:48] + "…") if len(details) > 49 else details
        print(f"{dur/1e6:9.3f} {st:10.1f}  {etype:14s} {loc:22s} {d}")

    if frames:
        rates = [f[1] for f in frames]
        print(f"\n=== ANIMATION FRAMES: {len(frames)} frames, "
              f"min {min(rates)} / median {sorted(rates)[len(rates)//2]} / "
              f"max {max(rates)} fps ===")
        drops = [f for f in frames if f[1] < 50]
        print(f"frames under 50fps (dropped/stutter): {len(drops)}")

        # Correlate each dropped frame with the JS that ran in the gap before it.
        # A frame at T with rate F means the previous frame was ~1000/F ms earlier;
        # the GUI thread was busy somewhere in [T-gap, T]. Blame the largest JS-level
        # range overlapping that window.
        print("\n=== STUTTER CULPRITS (dropped frame ← JS blocking the GUI thread in its gap) ===")
        print(f"{'@ ms':>9} {'fps':>4} {'gap ms':>8} {'blocked ms':>11}  culprit (type / location / details)")
        for start, fr, ac, th in drops[:top]:
            gap_ns = 1e9 / fr if fr else 0
            w0, w1 = start - gap_ns, start
            overlap = [t for t in timed if t[1] > w0 and t[0] < w1]
            t_ms = (start - trace_start) / 1e6
            if overlap:
                # attribute by time actually spent inside the gap window
                best = max(overlap, key=lambda t: min(t[1], w1) - max(t[0], w0))
                blk = (min(best[1], w1) - max(best[0], w0)) / 1e6
                d = (best[5][:40] + "…") if len(best[5]) > 41 else best[5]
                print(f"{t_ms:9.1f} {fr:4d} {gap_ns/1e6:8.1f} {blk:11.2f}  "
                      f"{best[3]:14s} {best[4]:22s} {d}")
            else:
                print(f"{t_ms:9.1f} {fr:4d} {gap_ns/1e6:8.1f} {'—':>11}  (no JS in window; render/GPU-bound or idle)")


if __name__ == "__main__":
    main()
