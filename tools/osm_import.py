"""Import an OpenStreetMap extract into a Void Warzone map.

Usage:
    python tools/osm_import.py tools/maps/<spec>.json [--fetch] [--preview out.png]

The spec names the area (`origin` = north-west corner lat/lon, `cell` = metres per cell, `width`/`height` in cells),
the OSM cache file (`osm`, Overpass JSON with `out geom`) and the output map. Everything under `map` in the spec
(name, theme, units, lanes, defense, ...) is copied into the output, so gameplay lives in the spec and geometry
comes from OSM. Map data (c) OpenStreetMap contributors, ODbL.
"""
import json
import math
import os
import sys
import urllib.parse
import urllib.request

OVERPASS = "https://overpass-api.de/api/interpreter"

ROAD_WIDTH = {"motorway": 16, "trunk": 14, "primary": 13, "secondary": 10, "tertiary": 8, "residential": 6, "unclassified": 6,
              "living_street": 5, "service": 4, "track": 3.5}
FOREST = {"wood", "forest"}


def fetch(spec, path):
    lat0, lon0 = spec["origin"]
    lat1, lon1 = corner(spec)
    bbox = f"{lat1:.6f},{lon0:.6f},{lat0:.6f},{lon1:.6f}"
    keys = ["building", "highway", "natural", "landuse", "leisure", "railway", "power", "amenity", "waterway"]
    body = "".join(f'way["{k}"]({bbox});' for k in keys) + f'relation["natural"="water"]({bbox});'
    for k, v in (("amenity", "fuel"), ("man_made", "water_tower|mast|tower|communications_tower|chimney"), ("highway", "bus_stop"),
                 ("power", "substation|transformer"), ("shop", "kiosk")):
        body += f'node["{k}"~"{v}"]({bbox});'
    query = f"[out:json][timeout:180];({body});out geom tags;"
    req = urllib.request.Request(OVERPASS, data=urllib.parse.urlencode({"data": query}).encode(), headers={"User-Agent": "VoidWarzone-mapgen"})
    with urllib.request.urlopen(req, timeout=300) as r:
        data = r.read()
    with open(path, "wb") as f:
        f.write(data)


DEM = "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png"


def elevation(spec, base, step=2, zoom=14):
    """Samples AWS Terrain Tiles (terrarium, public, SRTM-based) on a grid every `step` cells; returns metres."""
    from PIL import Image
    lat0, lon0 = spec["origin"]
    kx, ky = metres_per_deg(lat0)
    cache = os.path.join(base, "dem")
    os.makedirs(cache, exist_ok=True)
    tiles = {}
    n = 2 ** zoom

    def pixel(lat, lon):
        x = (lon + 180.0) / 360.0 * n * 256
        y = (1.0 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2.0 * n * 256
        return x, y

    def height(px, py):
        tx, ty = int(px // 256), int(py // 256)
        if (tx, ty) not in tiles:
            path = os.path.join(cache, f"{zoom}_{tx}_{ty}.png")
            if not os.path.exists(path):
                req = urllib.request.Request(DEM.format(z=zoom, x=tx, y=ty), headers={"User-Agent": "VoidWarzone-mapgen"})
                with urllib.request.urlopen(req, timeout=60) as r, open(path, "wb") as f:
                    f.write(r.read())
            tiles[(tx, ty)] = Image.open(path).convert("RGB").load()
        r, g, b = tiles[(tx, ty)][int(px) % 256, int(py) % 256]
        return r * 256 + g + b / 256 - 32768

    def bilinear(px, py):
        x0, y0 = math.floor(px - 0.5), math.floor(py - 0.5)
        fx, fy = px - 0.5 - x0, py - 0.5 - y0
        h00, h10 = height(x0, y0), height(x0 + 1, y0)
        h01, h11 = height(x0, y0 + 1), height(x0 + 1, y0 + 1)
        return (h00 * (1 - fx) + h10 * fx) * (1 - fy) + (h01 * (1 - fx) + h11 * fx) * fy

    w = spec["width"] // step + 1
    h = spec["height"] // step + 1
    grid = []
    for j in range(h):
        for i in range(w):
            lon = lon0 + i * step * spec["cell"] / kx
            lat = lat0 - j * step * spec["cell"] / ky
            grid.append(bilinear(*pixel(lat, lon)))
    lo = min(grid)
    return {"step": step, "columns": w, "rows": h, "base": round(lo, 1), "data": [round(v - lo, 1) for v in grid]}


def metres_per_deg(lat):
    return 111320.0 * math.cos(math.radians(lat)), 110574.0


def corner(spec):
    lat0, lon0 = spec["origin"]
    kx, ky = metres_per_deg(lat0)
    return lat0 - spec["height"] * spec["cell"] / ky, lon0 + spec["width"] * spec["cell"] / kx


def project(spec):
    lat0, lon0 = spec["origin"]
    kx, ky = metres_per_deg(lat0)
    s = spec["cell"]
    return lambda p: ((p["lon"] - lon0) * kx / s, (lat0 - p["lat"]) * ky / s)


def area(poly):
    return 0.5 * sum(poly[i][0] * poly[i - 1][1] - poly[i - 1][0] * poly[i][1] for i in range(len(poly)))


def simplify(points, tol):
    if len(points) < 3:
        return points
    a, b = points[0], points[-1]
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(dx, dy)
    best, index = 0.0, 0
    for i in range(1, len(points) - 1):
        p = points[i]
        d = abs(dy * p[0] - dx * p[1] + b[0] * a[1] - b[1] * a[0]) / length if length > 1e-9 else math.hypot(p[0] - a[0], p[1] - a[1])
        if d > best:
            best, index = d, i
    if best <= tol:
        return [a, b]
    return simplify(points[:index + 1], tol)[:-1] + simplify(points[index:], tol)


def ring(points, tol):
    pts = points[:-1] if len(points) > 1 and points[0] == points[-1] else points
    if len(pts) < 3:
        return []
    far = max(range(len(pts)), key=lambda i: (pts[i][0] - pts[0][0]) ** 2 + (pts[i][1] - pts[0][1]) ** 2)
    out = simplify(pts[:far + 1], tol)[:-1] + simplify(pts[far:] + [pts[0]], tol)[:-1]
    if len(out) < 3:
        return []
    if area(out) < 0:
        out.reverse()
    return out


def inside(poly, spec, margin=0.0):
    return all(margin <= x <= spec["width"] - margin and margin <= y <= spec["height"] - margin for x, y in poly)


def clip_line(points, spec):
    out, cur = [], []
    for x, y in points:
        if -4 <= x <= spec["width"] + 4 and -4 <= y <= spec["height"] + 4:
            cur.append((x, y))
        elif cur:
            out.append(cur)
            cur = []
    if cur:
        out.append(cur)
    return [c for c in out if len(c) >= 2]


def point_in(p, poly):
    x, y = p
    res = False
    j = len(poly) - 1
    for i in range(len(poly)):
        (xi, yi), (xj, yj) = poly[i], poly[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            res = not res
        j = i
    return res


def levels_of(tags, poly, cell, residential):
    footprint = abs(area(poly)) * cell * cell
    for key in ("building:levels", "levels"):
        try:
            return max(1, min(30, int(float(tags[key].split(";")[0]))))
        except (KeyError, ValueError):
            pass
    kind = tags.get("building", "yes")
    if kind in ("house", "detached", "hut", "shed", "cabin", "bungalow"):
        return 1
    if kind in ("garages", "garage", "service", "kiosk", "roof"):
        return 1
    if kind in ("industrial", "warehouse", "hangar", "manufacture"):
        return 2
    if kind in ("school", "kindergarten", "commercial", "retail", "public", "office", "hospital", "civic"):
        return 3 if kind != "kindergarten" else 2
    if kind in ("apartments", "residential", "dormitory") or (residential and footprint > 350):
        return 9 if footprint > 700 else 5
    return 2 if footprint > 150 else 1


def kind_of(tags, levels):
    b = tags.get("building", "yes")
    if b in ("house", "detached", "hut", "shed", "cabin", "bungalow"):
        return "house"
    if b in ("garages", "garage"):
        return "garage"
    if b in ("industrial", "warehouse", "hangar", "manufacture", "service"):
        return "industrial"
    if b in ("church", "chapel", "cathedral"):
        return "church"
    if b in ("school", "kindergarten", "commercial", "retail", "public", "office", "hospital", "civic"):
        return "public"
    if levels >= 4:
        return "panel"
    return "block" if levels > 1 else "shed"


def garages(spec, existing):
    """Rows of garage boxes ("garazhnyi kooperativ") at spec `garages: [{pos, rows, length, angle}]`."""
    out = []
    taken = [b["poly"] for b in existing]
    for g in spec.get("garages", []):
        cx, cy = g["pos"]
        a = math.radians(g.get("angle", 0.0))
        ux, uy = math.cos(a), math.sin(a)
        vx, vy = -uy, ux
        rows, length, depth, lane = g.get("rows", 4), g.get("length", 16.0), 1.1, 3.2
        for r in range(rows):
            for side in (0, 1):
                off = (r - (rows - 1) / 2) * (2 * depth + lane) + (side - 0.5) * (depth + 0.05)
                pts = []
                for du, dv in ((-length / 2, -depth / 2), (length / 2, -depth / 2), (length / 2, depth / 2), (-length / 2, depth / 2)):
                    pts.append((cx + ux * du + vx * (off + dv), cy + uy * du + vy * (off + dv)))
                if any(point_in(p, t) for p in pts for t in taken) or not inside(pts, spec, 1.0):
                    continue
                out.append({"poly": [r2(p) for p in pts], "levels": 1, "kind": "garage"})
    return out


def ruin_chance(spec, c, kind):
    if kind in ("house", "shed", "garage"):
        return 0.0
    chance = spec.get("ruins", 0.15)
    for z in spec.get("ruinZones", []):
        if (c[0] - z["pos"][0]) ** 2 + (c[1] - z["pos"][1]) ** 2 < z["radius"] ** 2:
            chance = max(chance, z.get("chance", 0.7))
    return chance


def r2(p):
    return [round(p[0], 2), round(p[1], 2)]


def convert(spec, osm):
    proj = project(spec)
    tol = spec.get("simplify", 0.2)
    buildings, areas, paths, props = [], [], [], []
    residential = []
    for e in osm["elements"]:
        t = e.get("tags", {})
        if e.get("type") == "way" and t.get("landuse") == "residential" and "geometry" in e:
            residential.append(ring([proj(p) for p in e["geometry"]], 1.0))

    def polys(e):
        if e["type"] == "way" and "geometry" in e:
            yield [proj(p) for p in e["geometry"]]
        elif e["type"] == "relation":
            for m in e.get("members", []):
                if m.get("role") == "outer" and "geometry" in m:
                    yield [proj(p) for p in m["geometry"]]

    for e in osm["elements"]:
        t = e.get("tags", {})
        prop = prop_of(t)
        if prop:
            if e["type"] == "node":
                x, y = proj(e)
            elif "geometry" in e:
                pts = [proj(p) for p in e["geometry"]]
                x, y = sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts)
            else:
                continue
            if 2 <= x <= spec["width"] - 2 and 2 <= y <= spec["height"] - 2:
                props.append({"model": prop, "pos": r2((x, y)), "yaw": float(hash((round(x), round(y))) % 360), "radius": SOLID.get(prop, 0.0)})
            if "building" not in t:
                continue
        if "building" in t:
            if t["building"] in ("ruins", "no"):
                continue
            unfinished = t["building"] == "construction"
            for raw in polys(e):
                poly = ring(raw, tol)
                if not poly or not inside(poly, spec, 1.0) or abs(area(poly)) < 0.6:
                    continue
                cx = sum(p[0] for p in poly) / len(poly)
                cy = sum(p[1] for p in poly) / len(poly)
                res = any(r and point_in((cx, cy), r) for r in residential)
                lv = levels_of(t, poly, spec["cell"], res)
                b = {"poly": [r2(p) for p in poly], "levels": lv, "kind": kind_of(t, lv)}
                if unfinished:
                    b["levels"] = max(lv, 9)
                    b["kind"] = "panel"
                    b["unfinished"] = True
                ruin = ruin_chance(spec, (cx, cy), b["kind"])
                roll = (hash((round(cx, 1), round(cy, 1))) % 1000) / 1000.0
                if ruin > 0 and roll < ruin and not unfinished:
                    b["ruin"] = True
                elif b["kind"] == "panel" and not unfinished and roll > 1.0 - spec.get("unfinished", 0.06):
                    b["unfinished"] = True
                buildings.append(b)
            continue
        kind = None
        if t.get("natural") in FOREST or t.get("landuse") == "forest":
            kind = "forest"
        elif t.get("natural") == "scrub":
            kind = "scrub"
        elif t.get("natural") == "water" or t.get("landuse") in ("reservoir", "basin"):
            kind = "water"
        elif t.get("landuse") in ("industrial", "construction", "railway", "garages", "brownfield"):
            kind = "industrial"
        elif t.get("leisure") in ("park", "garden") or t.get("landuse") in ("grass", "meadow", "recreation_ground", "village_green"):
            kind = "park"
        elif t.get("landuse") in ("cemetery",) or t.get("amenity") == "grave_yard":
            kind = "cemetery"
        elif t.get("leisure") in ("pitch", "playground"):
            kind = "yard"
        if kind:
            for raw in polys(e):
                poly = ring(raw, 0.5)
                if len(poly) >= 3 and abs(area(poly)) > 2.0:
                    poly = [(min(max(x, -2), spec["width"] + 2), min(max(y, -2), spec["height"] + 2)) for x, y in poly]
                    areas.append({"kind": kind, "poly": [r2(p) for p in poly]})
            continue
        line_kind, width = None, 0.0
        hw = t.get("highway")
        if hw in ROAD_WIDTH:
            line_kind, width = ("track" if hw == "track" else "road"), ROAD_WIDTH[hw]
        elif hw in ("footway", "path", "pedestrian", "steps", "cycleway"):
            line_kind, width = "path", 2.0
        elif t.get("railway") in ("rail", "tram", "light_rail", "abandoned", "disused"):
            line_kind, width = "rail", 3.0
        elif t.get("power") == "line":
            line_kind, width = "power", 0.0
        if line_kind and e["type"] == "way" and "geometry" in e:
            pts = [proj(p) for p in e["geometry"]]
            for part in clip_line(simplify(pts, 0.3), spec):
                paths.append({"kind": line_kind, "width": round(width / spec["cell"], 2), "points": [r2(p) for p in part]})
    return buildings, areas, paths, props


SOLID = {"prop_gas_station": 3.0, "prop_substation": 2.0, "prop_water_tower": 1.0, "prop_cell_tower": 1.0}


def prop_of(t):
    if t.get("amenity") == "fuel":
        return "prop_gas_station"
    mm = t.get("man_made")
    if mm == "water_tower":
        return "prop_water_tower"
    if mm in ("mast", "tower", "communications_tower"):
        return "prop_cell_tower"
    if t.get("power") in ("substation", "transformer"):
        return "prop_substation"
    if t.get("highway") == "bus_stop":
        return "prop_bus_stop"
    if t.get("shop") == "kiosk":
        return "prop_kiosk"
    return None


def raster(m):
    w, h = m["width"], m["height"]
    blocked = [[0] * w for _ in range(h)]
    road = [[0.0] * w for _ in range(h)]

    def fill(poly, grid, value):
        xs = [p[0] for p in poly]
        ys = [p[1] for p in poly]
        for y in range(max(0, int(min(ys))), min(h, int(max(ys)) + 1)):
            for x in range(max(0, int(min(xs))), min(w, int(max(xs)) + 1)):
                if point_in((x + 0.5, y + 0.5), poly):
                    grid[y][x] = value

    for a in m["areas"]:
        if a["kind"] == "forest":
            fill(a["poly"], blocked, 1)
    for b in m["buildings"]:
        fill(b["poly"], blocked, 1)
    for p in m["paths"]:
        if p["kind"] not in ("road", "track"):
            continue
        quality = 1.0 if p["kind"] == "road" else 0.6
        pts = p["points"]
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            n = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
            for i in range(n + 1):
                x = int(x0 + (x1 - x0) * i / n)
                y = int(y0 + (y1 - y0) * i / n)
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        if 0 <= x + dx < w and 0 <= y + dy < h:
                            road[y + dy][x + dx] = max(road[y + dy][x + dx], quality)
    return blocked, road


def route(blocked, road, a, b):
    import heapq
    h, w = len(blocked), len(blocked[0])
    clear = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if blocked[y][x]:
                for dy in range(-2, 3):
                    for dx in range(-2, 3):
                        if 0 <= x + dx < w and 0 <= y + dy < h:
                            clear[y + dy][x + dx] = 1
    start, goal = (int(a[0]), int(a[1])), (int(b[0]), int(b[1]))
    dist = {start: 0.0}
    prev = {}
    heap = [(0.0, 0.0, start)]
    while heap:
        _, d, c = heapq.heappop(heap)
        if c == goal:
            break
        if d > dist[c]:
            continue
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)):
            n = (c[0] + dx, c[1] + dy)
            if not (0 <= n[0] < w and 0 <= n[1] < h) or (blocked[n[1]][n[0]] and n != goal):
                continue
            step = (1.414 if dx and dy else 1.0) * (1.0 + 2.0 * (1.0 - road[n[1]][n[0]]) + 6.0 * clear[n[1]][n[0]])
            nd = d + step
            if nd < dist.get(n, 1e18):
                dist[n] = nd
                prev[n] = c
                heapq.heappush(heap, (nd + math.hypot(goal[0] - n[0], goal[1] - n[1]), nd, n))
    path = [goal]
    while path[-1] != start:
        path.append(prev[path[-1]])
    path.reverse()
    return [(x + 0.5, y + 0.5) for x, y in path]


def snap(blocked, p):
    x0, y0 = int(p[0]), int(p[1])
    for r in range(0, 12):
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                x, y = x0 + dx, y0 + dy
                if 0 <= y < len(blocked) and 0 <= x < len(blocked[0]) and not blocked[y][x]:
                    return (x + 0.5, y + 0.5) if (dx or dy) else tuple(p)
    return tuple(p)


def build_lanes(m, routes, tolerance=3.0):
    blocked, road = raster(m)
    lanes = []
    for r in routes:
        pts = []
        via = [snap(blocked, v) for v in r["via"]]
        for a, b in zip(via, via[1:]):
            seg = route(blocked, road, a, b)
            pts += seg if not pts else seg[1:]
        pts = simplify(pts, tolerance)
        lanes.append({"name": r["name"], "points": [r2(p) for p in [tuple(via[0])] + pts[1:-1] + [tuple(via[-1])]]})
    return lanes


def preview(m, path, scale=2):
    from PIL import Image, ImageDraw
    w, h = m["width"], m["height"]
    im = Image.new("RGB", (w * scale, h * scale), (226, 230, 236))
    dr = ImageDraw.Draw(im)
    colors = {"forest": (150, 175, 150), "scrub": (190, 205, 185), "water": (160, 190, 225), "industrial": (205, 195, 195),
              "park": (205, 220, 200), "cemetery": (190, 190, 200), "yard": (215, 215, 205)}
    sc = lambda pts: [(x * scale, y * scale) for x, y in pts]
    for a in m.get("areas", []):
        dr.polygon(sc(a["poly"]), fill=colors.get(a["kind"], (200, 200, 200)))
    for p in m.get("paths", []):
        col = {"road": (150, 140, 120), "track": (175, 160, 140), "path": (200, 190, 180), "rail": (60, 60, 60), "power": (140, 60, 160)}[p["kind"]]
        dr.line(sc(p["points"]), fill=col, width=max(1, int(p["width"] * scale)))
    for b in m.get("buildings", []):
        g = max(30, 150 - b["levels"] * 8)
        dr.polygon(sc(b["poly"]), fill=(g, g, g + 15))
    for x, y, ww, hh in m.get("walls", []):
        dr.rectangle((x * scale, y * scale, (x + ww) * scale, (y + hh) * scale), fill=(120, 110, 100))
    for lane in m.get("lanes", []):
        dr.line(sc(lane["points"]), fill=(220, 40, 40), width=2)
    for u in m.get("units", []):
        x, y = u["pos"]
        col = (40, 120, 255) if u.get("team", 0) == 0 else (255, 40, 140)
        r = 6 if u.get("role") else 3
        dr.ellipse((x * scale - r, y * scale - r, x * scale + r, y * scale + r), fill=col)
    for gx in range(0, w, 32):
        dr.line((gx * scale, 0, gx * scale, h * scale), fill=(180, 180, 230))
        dr.text((gx * scale + 2, 2), str(gx), fill=(80, 80, 160))
    for gy in range(0, h, 32):
        dr.line((0, gy * scale, w * scale, gy * scale), fill=(180, 180, 230))
        dr.text((2, gy * scale + 2), str(gy), fill=(80, 80, 160))
    im.save(path)


def main():
    spec_path = sys.argv[1]
    with open(spec_path, encoding="utf-8") as f:
        spec = json.load(f)
    base = os.path.dirname(os.path.abspath(spec_path))
    osm_path = os.path.join(base, spec["osm"])
    if "--fetch" in sys.argv or not os.path.exists(osm_path):
        fetch(spec, osm_path)
    with open(osm_path, encoding="utf-8") as f:
        osm = json.load(f)
    buildings, areas, paths, props = convert(spec, osm)
    m = {"width": spec["width"], "height": spec["height"]}
    m.update(spec.get("map", {}))
    m["areas"] = areas + m.get("areas", [])
    m["buildings"] = buildings
    m["paths"] = paths + m.get("paths", [])
    m["props"] = props + m.get("props", [])
    m["attribution"] = "Map data (c) OpenStreetMap contributors, ODbL"
    m["buildings"] += garages(spec, m["buildings"])
    clear = list(spec.get("clear", []))
    for u in m.get("units", []):
        clear.append({"pos": u["pos"], "radius": 9.0 if u.get("role") else 5.0})
    if "playerStart" in m:
        clear.append({"pos": m["playerStart"]["pos"], "radius": 8.0})
    for r in m.get("resourcePoints", []):
        clear.append({"pos": r["pos"], "radius": r.get("radius", 4.0) + 3.0})
    if clear:
        def free(poly):
            for c in clear:
                if "poly" in c:
                    if any(point_in(p, c["poly"]) for p in poly):
                        return False
                elif point_in(c["pos"], poly) or any((p[0] - c["pos"][0]) ** 2 + (p[1] - c["pos"][1]) ** 2 < c["radius"] ** 2 for p in poly):
                    return False
            return True
        m["buildings"] = [b for b in m["buildings"] if free(b["poly"])]
        m["areas"] = [a for a in m["areas"] if a["kind"] != "forest" or free(a["poly"])]
        m["props"] = [p for p in m["props"] if free([p["pos"]])]
    if spec.get("elevation", True):
        m["elevation"] = elevation(spec, base)
    if "routes" in spec:
        m["lanes"] = build_lanes(m, spec["routes"], spec.get("laneSimplify", 3.0))
    out = os.path.join(os.path.dirname(base), "..", spec["output"]) if not os.path.isabs(spec["output"]) else spec["output"]
    out = os.path.normpath(out)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        json.dump(m, f, ensure_ascii=False, separators=(",", ":"))
    print(f"{out}: {len(buildings)} buildings, {len(areas)} areas, {len(paths)} paths")
    if "--preview" in sys.argv:
        preview(m, sys.argv[sys.argv.index("--preview") + 1])


if __name__ == "__main__":
    main()
