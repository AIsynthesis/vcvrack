#!/usr/bin/env python3
"""Build a VCV Rack panel SVG + C++ layout header straight from a module's panel Gerbers.

    python3 tools/gerber_to_panel.py AI250
    python3 tools/gerber_to_panel.py AI018
    python3 tools/gerber_to_panel.py all

For module XXX it reads   res-src/XXX-gerbers/
                 writes   res/XXX-black.svg   (black mask, white silkscreen, gold exposed copper)
                          src/XXXLayout.hpp   (exact hole centre for every knob / jack / switch / LED)
Requires: pip install gerbonara

Works with KiCad (Edge_Cuts holes written as arcs) and EAGLE (profile holes written as
polylines) exports. Every named control is snapped to its real hole, and the script
refuses to finish if a control has no hole or a hole has no control — so the VCV
widget positions always match the hardware.

To add a module: add an entry to MODULES below, drop its Gerbers in res-src/<NAME>-gerbers/.
"""
import math
import os
import re
import sys
import warnings

from gerbonara import GerberFile

warnings.filterwarnings("ignore")  # EAGLE silkscreen has harmless zero-width draws

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PANEL_H = 128.5
MASK_COLOUR = "#111111"   # black soldermask
SILK_COLOUR = "#ffffff"
GOLD_COLOUR = "#c8a24b"   # exposed ENIG copper


def ai018_controls():
    c = {}
    in_rows = [15.8, 26.5, 37.1, 47.8, 58.3, 69.0, 79.6, 90.3]   # L1 R1 L2 R2 ... (left column)
    for i, y in enumerate(in_rows):
        c[f"IN_{'L' if i % 2 == 0 else 'R'}{i // 2 + 1}"] = (6.8, y)
    cols = {"A": 23.8, "B": 42.7, "C": 61.5, "D": 80.3}
    for r, y in enumerate([21.6, 42.6, 63.6, 84.6], start=1):
        for col, x in cols.items():
            c[f"KNOB_{r}{col}"] = (x, y)               # input row r -> output col
    for col, x in cols.items():
        c[f"OUT_L{col}"] = (x, 102.5)
        c[f"OUT_R{col}"] = (x, 113.3)
    return c


MODULES = {
    "AI250": dict(
        hp=14, files=dict(outline="AI250-Edge_Cuts.gbr", silk="AI250-F_Silkscreen.gbr", mask="AI250-F_Mask.gbr"),
        controls={
            "KNOB_ARG": (17.9, 22.8), "KNOB_FUNC": (53.3, 22.8),
            "KNOB_IN1VOL": (10.6, 46.1), "KNOB_WAVE": (35.6, 46.1), "KNOB_DIST": (60.5, 46.1),
            "SWITCH_IN2OSC": (23.6, 57.8), "SWITCH_SPEED": (47.6, 57.8),
            "KNOB_CENTER": (10.6, 69.4), "KNOB_MIX": (60.5, 69.4),
            "IN_VOCT": (23.6, 78.7), "IN_WAVE_CV": (35.6, 78.7), "IN_MIX_CV": (47.6, 78.7),
            "LIGHT_GATE1": (49.1, 88.9), "LIGHT_GATE2": (61.1, 88.9),
            "IN_ARG_CV": (11.6, 95.7), "IN_FUNC_CV": (23.6, 95.7), "IN_DIST_CV": (35.6, 95.7),
            "OUT_GATE1": (47.6, 95.7), "OUT_GATE2": (59.6, 95.7),
            "IN_IN1": (11.6, 112.7), "SWITCH_SWAP": (23.6, 112.7), "IN_IN2": (35.6, 112.7),
            "OUT_OSC": (47.6, 112.7), "OUT_OUT": (59.6, 112.7),
        }),
    "AI018": dict(
        hp=18, files=dict(outline="profile.gbr", silk="silkscreen_top.gbr", mask="soldermask_top.gbr"),
        controls=ai018_controls()),
}


def gerber_path(name, fname):
    return os.path.join(ROOT, "res-src", f"{name}-gerbers", fname)


def layer_paths(path, colour):
    svg = str(GerberFile.open(path).to_svg(fg="white", bg="black"))
    body = re.sub(r"^.*?<g[^>]*>", "", svg, flags=re.S)
    body = body[: body.rfind("</g>")]
    return body.replace('"white"', f'"{colour}"')


def read_outline(path):
    """Return (units_per_mm, list of polylines/arcs) from the outline Gerber."""
    txt = open(path).read()
    fmt = re.search(r"%FSLAX(\d)(\d)Y", txt)
    scale = 10 ** int(fmt.group(2))
    return txt, scale


def find_cutouts(txt, scale):
    """Every closed cut-out as (cx, cy, w, h) in Gerber mm (y up)."""
    cut = []
    # KiCad: a circle is two 180° arcs; keep one per circle.
    arc = r"X(-?\d+)Y(-?\d+)D02\*\s*G75\*\s*G0[23]\*\s*X(-?\d+)Y(-?\d+)I(-?\d+)J(-?\d+)D01"
    halves = {}
    for x0, y0, _x1, _y1, i, j in re.findall(arc, txt):
        r = math.hypot(int(i), int(j)) / scale
        centre = (round(int(x0) / scale + int(i) / scale, 3), round(int(y0) / scale + int(j) / scale, 3))
        halves.setdefault(centre, []).append(r)
    for (cx, cy), rs in halves.items():
        if len(rs) >= 2 and rs[0] > 0.5:   # two half-arcs on one centre = a round hole (slot caps are single)
            cut.append((cx, cy, 2 * rs[0], 2 * rs[0]))
    # EAGLE (and KiCad slots): polylines that start with D02 and close on themselves.
    cur = []
    for x, y, d in re.findall(r"X(-?\d+)Y(-?\d+)D0([12])\*", txt) + [("0", "0", "2")]:
        p = (int(x) / scale, int(y) / scale)
        if d == "2":
            if len(cur) >= 8 and math.dist(cur[0], cur[-1]) < 0.05:
                xs, ys = [q[0] for q in cur], [q[1] for q in cur]
                cut.append(((max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2, max(xs) - min(xs), max(ys) - min(ys)))
            cur = [p]
        else:
            cur.append(p)
    uniq = {}
    for c in cut:
        uniq[(round(c[0], 2), round(c[1], 2))] = c
    return list(uniq.values())


def build(name):
    cfg = MODULES[name]
    panel_w = cfg["hp"] * 5.08
    outline = gerber_path(name, cfg["files"]["outline"])
    (xmin, ymin), (xmax, ymax) = GerberFile.open(outline).bounding_box()
    line_w = 0.254 if "profile" in outline else 0.05
    left, top = xmin + line_w / 2, ymax - line_w / 2
    board_w = (xmax - xmin) - line_w
    x_off = left - (panel_w - board_w) / 2   # centre the board in its HP slot

    transform = f"matrix(1 0 0 -1 {-x_off:.4f} {top:.4f})"
    gold = layer_paths(gerber_path(name, cfg["files"]["mask"]), GOLD_COLOUR)
    silk = layer_paths(gerber_path(name, cfg["files"]["silk"]), SILK_COLOUR)
    svg = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{panel_w:.2f}mm" height="{PANEL_H}mm" '
        f'viewBox="0 0 {panel_w:.2f} {PANEL_H}">\n'
        f'<rect width="{panel_w:.2f}" height="{PANEL_H}" fill="{MASK_COLOUR}"/>\n'
        f'<g transform="{transform}">\n{gold}\n{silk}\n</g>\n</svg>\n'
    )
    with open(os.path.join(ROOT, "res", f"{name}-black.svg"), "w") as f:
        f.write(svg)

    txt, scale = read_outline(outline)
    cutouts = [(round(cx - x_off, 3), round(top - cy, 3), w, h) for cx, cy, w, h in find_cutouts(txt, scale)]
    board_edge = [c for c in cutouts if c[2] > board_w * 0.8]
    cutouts = [c for c in cutouts if c not in board_edge]
    slots = [c for c in cutouts if abs(c[2] - c[3]) > 0.4]          # mounting slots (oval)
    holes = [c for c in cutouts if abs(c[2] - c[3]) <= 0.4]         # round control holes

    ns = f"{name.lower()}layout"
    lines = ["#pragma once",
             f"// GENERATED by tools/gerber_to_panel.py from res-src/{name}-gerbers — do not edit by hand.",
             f"// Hole centres in millimetres, matching res/{name}-black.svg.",
             f"namespace {ns} {{", "struct Pos { float x, y; };"]
    used = set()
    for cname, (ax, ay) in cfg["controls"].items():
        best = min(holes, key=lambda h: math.hypot(h[0] - ax, h[1] - ay))
        dist = math.hypot(best[0] - ax, best[1] - ay)
        if dist > 1.5 or best in used:
            raise SystemExit(f"{name} {cname}: no unique hole near ({ax}, {ay}); nearest {best[:2]} at {dist:.2f} mm")
        used.add(best)
        lines.append(f"constexpr Pos {cname} = {{{best[0]:.3f}f, {best[1]:.3f}f}};  // hole Ø{best[2]:.2f} mm")
    unused = [h[:2] for h in holes if h not in used]
    if unused:
        raise SystemExit(f"{name}: holes with no control assigned: {unused}")

    if not slots:  # KiCad: slots are two short straight edges joined by arcs
        for xa, ya, xb, yb in re.findall(r"X(-?\d+)Y(-?\d+)D02\*\s*X(-?\d+)Y(-?\d+)D01", txt):
            xa, ya, xb, yb = (int(v) / scale for v in (xa, ya, xb, yb))
            if ya == yb and 0.5 < abs(xb - xa) < 3:
                slots.append(((xa + xb) / 2 - x_off, top - ya, abs(xb - xa), 0))
    slot_xs = sorted({round(s[0], 2) for s in slots}) or [7.5, panel_w - 7.5]
    lx, rx = slot_xs[0], slot_xs[-1]
    for tag, x, y in (("TL", lx, 3.0), ("TR", rx, 3.0), ("BL", lx, 125.5), ("BR", rx, 125.5)):
        lines.append(f"constexpr Pos SCREW_{tag} = {{{x:.2f}f, {y:.1f}f}};")
    lines += [f"constexpr float PANEL_WIDTH = {panel_w:.2f}f;", f"}}  // namespace {ns}"]
    with open(os.path.join(ROOT, "src", f"{name}Layout.hpp"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{name}: wrote res/{name}-black.svg ({len(svg)//1024} KB) + src/{name}Layout.hpp — "
          f"{len(used)} controls snapped to Gerber holes, {len(slots)} mounting slots")


if __name__ == "__main__":
    targets = MODULES if len(sys.argv) < 2 or sys.argv[1] == "all" else sys.argv[1:]
    for t in targets:
        build(t)
