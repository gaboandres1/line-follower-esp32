"""Archivo de corte láser v2 (una sola tabla + separadores pequeños)."""
import math
from geom import *

CUT, ENG = "CORTE", "GRABADO"
prims = []
def line(a, b, layer=CUT): prims.append(("line", layer, (a, b)))
def circle(c, r, layer=CUT): prims.append(("circle", layer, (c, r)))
def text(p, s, h, layer=ENG): prims.append(("text", layer, (p, s, h)))
def poly(pts, layer=CUT, closed=True):
    n = len(pts)
    for i in range(n if closed else n - 1):
        if pts[i] != pts[(i + 1) % n]: line(pts[i], pts[(i + 1) % n], layer)
def rect(x0, y0, w, h, layer=CUT): poly([(x0, y0), (x0 + w, y0), (x0 + w, y0 + h), (x0, y0 + h)], layer)
def rrect(x0, y0, w, h, r, layer=CUT, n=6):
    pts = []
    for (cx, cy, a0) in ((x0+w-r, y0+r, 270), (x0+w-r, y0+h-r, 0), (x0+r, y0+h-r, 90), (x0+r, y0+r, 180)):
        pts += [(cx + r*math.cos(math.radians(a0 + 90*i/n)), cy + r*math.sin(math.radians(a0 + 90*i/n))) for i in range(n+1)]
    poly(pts, layer)
def cslot(cx, cy, w, l, ox, oy): rect(ox + cx - w/2, oy + cy - l/2, w, l)

def base(ox, oy):
    poly([(ox + x, oy + y) for x, y in outline(12)])
    for s in PLATE_SLOTS: cslot(*s, ox, oy)
    for cx, cy, d in PLATE_HOLES: circle((ox + cx, oy + cy), d / 2)
    x0, y0, w, l = WINDOW; rrect(ox + x0, oy + y0, w, l, 3)
    for c in TOP.values(): rect(ox + c["x"], oy + c["y"], c["w"], c["l"], ENG)
    p = CASTER["plate"]; rect(ox + CASTER["x"] - p/2, oy + CASTER["y"] - p/2, p, p, ENG)
    text((ox - 8, oy + 31), "L298N", 4)
    text((ox - 12, oy + 77), "BATERIA", 4)
    text((ox - 9, oy + 128), "ESP32", 4)
    text((ox - 30, oy + 186), "SENSOR DEBAJO - FRENTE", 3)
    text((ox - 62, oy + 22), "RUEDA LOCA", 2.5); text((ox - 62, oy + 18.5), "DEBAJO", 2.5)
    ax = ox + 45; line((ax, oy + 112), (ax, oy + 135), ENG); line((ax, oy + 135), (ax - 3, oy + 130), ENG); line((ax, oy + 135), (ax + 3, oy + 130), ENG)
    text((ax - 7, oy + 107), "FRENTE", 3)

M = 6.0
BASE_O = (M + PW / 2, M)
base(*BASE_O)
SX = M + PW + 6
p = CASTER["plate"]
for i in range(N_CASTER_SPACERS):
    y0 = M + i * (p + 6)
    rect(SX, y0, p, p)
    for cx, cy, d in CASTER_HOLES:
        circle((SX + p/2 + (cx - CASTER["x"]), y0 + p/2 + (cy - CASTER["y"])), d / 2)
text((SX, M + 3*(p+6) - 2), "SEP. RUEDA LOCA x3", 2.5)
for i in range(N_PCB_SPACERS):
    rect(SX + (i % 4) * (PCB_SPACER + 3), M + 3*(p+6) + 4 + (i // 4) * (PCB_SPACER + 3), PCB_SPACER, PCB_SPACER)
text((SX, M + 3*(p+6) + 4 + 2*(PCB_SPACER+3) + 2), "SEP. PCB x8", 2.5)
SHEET_W, SHEET_H = SX + p + M, PL + 2 * M

def fy(y): return SHEET_H - y
COL = {CUT: "#FF0000", ENG: "#0000FF"}
def svg():
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{SHEET_W}mm" height="{SHEET_H}mm" viewBox="0 0 {SHEET_W} {SHEET_H}">',
         '<title>Chasis seguidor de linea - una tabla - MDF 3 mm</title>', '<!-- ROJO = cortar, AZUL = grabar. Unidades: mm -->']
    for layer in (ENG, CUT):
        o.append(f'<g id="{layer}" fill="none" stroke="{COL[layer]}" stroke-width="0.1">')
        for t, l, d in prims:
            if l != layer: continue
            if t == "line":
                (x1, y1), (x2, y2) = d; o.append(f'<line x1="{x1:.3f}" y1="{fy(y1):.3f}" x2="{x2:.3f}" y2="{fy(y2):.3f}"/>')
            elif t == "circle":
                (cx, cy), r = d; o.append(f'<circle cx="{cx:.3f}" cy="{fy(cy):.3f}" r="{r:.3f}"/>')
            elif t == "text":
                (x, y), s, h = d; o.append(f'<text x="{x:.2f}" y="{fy(y):.2f}" font-family="Arial" font-size="{h}" fill="{COL[ENG]}" stroke="none">{s}</text>')
        o.append('</g>')
    o.append('</svg>'); return "\n".join(o)
def dxf():
    o = ["0","SECTION","2","HEADER","9","$INSUNITS","70","4","9","$ACADVER","1","AC1009","0","ENDSEC",
         "0","SECTION","2","TABLES","0","TABLE","2","LAYER","70","2",
         "0","LAYER","2",CUT,"70","0","62","1","6","CONTINUOUS","0","LAYER","2",ENG,"70","0","62","5","6","CONTINUOUS",
         "0","ENDTAB","0","ENDSEC","0","SECTION","2","ENTITIES"]
    for t, l, d in prims:
        if t == "line":
            (x1, y1), (x2, y2) = d; o += ["0","LINE","8",l,"10",f"{x1:.4f}","20",f"{y1:.4f}","11",f"{x2:.4f}","21",f"{y2:.4f}"]
        elif t == "circle":
            (cx, cy), r = d; o += ["0","CIRCLE","8",l,"10",f"{cx:.4f}","20",f"{cy:.4f}","40",f"{r:.4f}"]
        elif t == "text":
            (x, y), s, h = d; o += ["0","TEXT","8",l,"10",f"{x:.4f}","20",f"{y:.4f}","40",f"{h}","1",s]
    o += ["0","ENDSEC","0","EOF"]; return "\n".join(o) + "\n"
if __name__ == "__main__":
    import os; os.makedirs("out", exist_ok=True)
    import os, glob
    for f in glob.glob("out/*"): os.remove(f)
    open("out/chasis_corte_laser.svg", "w").write(svg())
    open("out/chasis_corte_laser.dxf", "w").write(dxf())
    print(SHEET_W, SHEET_H, len(prims))
