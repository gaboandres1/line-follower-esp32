"""Planos con cotas (PDF A4 apaisado, escala 1:1 al imprimir al 100 %)."""
import math
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.patches import FancyBboxPatch, Rectangle, Circle, Polygon, Arc
from geom import *

PW_MM, PH_MM = 297, 210
INK, DIMC, ENGC, GREY = "#111", "#c0392b", "#2c5aa0", "#888"

def page(title, sub):
    fig = plt.figure(figsize=(PW_MM / 25.4, PH_MM / 25.4))
    ax = fig.add_axes([0, 0, 1, 1]); ax.set_xlim(0, PW_MM); ax.set_ylim(0, PH_MM); ax.axis("off")
    ax.set_aspect("equal")
    ax.add_patch(Rectangle((5, 5), PW_MM - 10, PH_MM - 10, fill=False, lw=0.6, ec=INK))
    ax.add_patch(Rectangle((PW_MM - 125, 5), 120, 16, fill=False, lw=0.6, ec=INK))
    ax.text(PW_MM - 122, 15, title, fontsize=9, weight="bold", va="center")
    ax.text(PW_MM - 122, 9, sub, fontsize=6, va="center", color="#444")
    ax.text(10, 9, "Medidas en mm  ·  MDF 3 mm  ·  Escala 1:1 al imprimir al 100 % (sin 'ajustar a página')",
            fontsize=6, color="#444")
    # regla de comprobación 50 mm
    ax.plot([10, 60], [14, 14], color=INK, lw=0.8)
    for x in (10, 60): ax.plot([x, x], [12.5, 15.5], color=INK, lw=0.8)
    ax.text(35, 15.5, "50 mm (verificar escala)", fontsize=5, ha="center")
    return fig, ax

def dim(ax, p1, p2, off, txt=None, fs=6, color=DIMC):
    (x1, y1), (x2, y2) = p1, p2
    dx, dy = x2 - x1, y2 - y1
    L = math.hypot(dx, dy)
    nx, ny = -dy / L, dx / L
    a = (x1 + nx * off, y1 + ny * off); b = (x2 + nx * off, y2 + ny * off)
    s = 1 if off >= 0 else -1
    for p, q in ((p1, a), (p2, b)):
        ax.plot([p[0] + nx * s * 1, q[0] + nx * s * 1.5], [p[1] + ny * s * 1, q[1] + ny * s * 1.5], color=color, lw=0.3)
    ax.annotate("", xy=b, xytext=a, arrowprops=dict(arrowstyle="<|-|>", color=color, lw=0.4,
                                                     mutation_scale=4, shrinkA=0, shrinkB=0))
    ang = math.degrees(math.atan2(dy, dx))
    if ang > 90 or ang <= -90: ang += 180
    t = txt if txt is not None else f"{L:g}"
    ax.text((a[0] + b[0]) / 2 + nx * 1.6, (a[1] + b[1]) / 2 + ny * 1.6, t, fontsize=fs, color=color,
            ha="center", va="center", rotation=ang,
            bbox=dict(fc="white", ec="none", pad=0.15))

def rrect(ax, x, y, w, h, r, **kw):
    ax.add_patch(FancyBboxPatch((x + r, y + r), w - 2 * r, h - 2 * r, boxstyle=f"round,pad={r}", **kw))

def slot(ax, ox, oy, s, **kw):
    cx, cy, w, l = s
    ax.add_patch(Rectangle((ox + cx - w / 2, oy + cy - l / 2), w, l, fill=False, ec=INK, lw=0.4, **kw))


def P1(ox, oy):
    """robot (x,y) -> página, con el frente hacia la derecha"""
    return lambda x, y: (ox + y, oy - x)

def p1(pdf):
    fig, ax = page("1/3  TABLA ÚNICA - CORTE", "Vista superior · frente a la derecha · MDF 3 mm")
    P = P1(28, 112)
    ax.add_patch(Polygon([P(x, y) for x, y in outline(12)], closed=True, fill=False, ec=INK, lw=0.8))
    for (cx, cy, w, l) in PLATE_SLOTS:
        a = P(cx + w/2, cy - l/2); ax.add_patch(Rectangle(a, l, w, fill=False, ec=INK, lw=0.4))
    for cx, cy, d in PLATE_HOLES: ax.add_patch(Circle(P(cx, cy), d/2, fill=False, ec=INK, lw=0.4))
    x0, y0, w, l = WINDOW
    a = P(x0 + w, y0); ax.add_patch(FancyBboxPatch((a[0]+3, a[1]+3), l-6, w-6, boxstyle="round,pad=3", fill=False, ec=INK, lw=0.5))
    for c in TOP.values():
        a = P(c["x"] + c["w"], c["y"]); ax.add_patch(Rectangle(a, c["l"], c["w"], fill=False, ec=ENGC, lw=0.4, ls="--"))
        ax.text(*P(c["x"] + c["w"]/2, c["y"] + c["l"]/2), c["name"], color=ENGC, fontsize=5.5, ha="center", va="center", rotation=90)
    pc = CASTER["plate"]; a = P(CASTER["x"] + pc/2, CASTER["y"] - pc/2)
    ax.add_patch(Rectangle(a, pc, pc, fill=False, ec=GREY, lw=0.4, ls=":"))
    for s in (-1, 1):
        x0m = MOTOR_X_IN if s > 0 else -MOTOR_X_OUT
        a = P(x0m + MOTOR_W, MOTOR_Y0); ax.add_patch(Rectangle(a, MOTOR_L, MOTOR_W, fill=False, ec=GREY, lw=0.3, ls=":"))
    S = SENSOR; a = P(S["bar_w"]/2, S["y0"]); ax.add_patch(Rectangle(a, S["bar_l"], S["bar_w"], fill=False, ec=GREY, lw=0.3, ls=":"))
    ax.text(*P(0, S["y0"] + 4), "BFD-1000 (debajo)", color=GREY, fontsize=4.5, ha="center", va="center", rotation=90)
    ax.text(*P(-45.5, 46), "motor (debajo)", color=GREY, fontsize=4.5, ha="center", va="center")
    ax.text(*P(45.5, 46), "motor (debajo)", color=GREY, fontsize=4.5, ha="center", va="center")
    ax.text(*P(0, CASTER["y"]), "rueda loca\n(debajo)", color=GREY, fontsize=4.5, ha="center", va="center")
    # cotas
    dim(ax, P(-PW/2, 0), P(-PW/2, PL), 7, f"{PL:g}")
    dim(ax, P(PW/2, 0), P(-PW/2, 0), 7, f"{PW:g}")
    dim(ax, P(-PW/2, NOTCH_Y0), P(-PW/2, NOTCH_Y1), -4, f"{NOTCH_Y1-NOTCH_Y0:g}", fs=5)
    dim(ax, P(-PW/2, 0), P(-PW/2, NOTCH_Y0), -4, f"{NOTCH_Y0:g}", fs=5)
    dim(ax, P(PW/2, NOTCH_Y1 + 2), P(NOTCH_X0, NOTCH_Y1 + 2), -3, f"{PW/2-NOTCH_X0:g}", fs=5)
    ax.text(*P(-PW/2 + 4, PL - 8), f"R{R_FRONT:g}", fontsize=5, color=DIMC)
    ax.text(*P(-PW/2 + 3, 5), f"R{R_REAR:g}", fontsize=5, color=DIMC)
    ax.annotate("", xy=(P(0, PL)[0] + 14, 112), xytext=(P(0, PL)[0] + 2, 112), arrowprops=dict(arrowstyle="-|>", color=INK, lw=0.6))
    ax.text(P(0, PL)[0] + 3, 115, "FRENTE", fontsize=6)
    ax.text(235, 190, "Cómo leer este plano", fontsize=7, weight="bold")
    y = 184
    for n in ["Negro: se corta.", "Azul discontinuo: se graba", "  (guía para pegar cada placa).", "Gris punteado: va DEBAJO", "  (motores, rueda loca, sensor).", "", "Coordenadas de cada ranura", "y agujero: hoja 2."]:
        ax.text(235, y, n, fontsize=5.2); y -= 4.6
    pdf.savefig(fig); plt.close(fig)

def p2(pdf):
    fig, ax = page("2/3  CORTES INTERNOS Y SEPARADORES", "Coordenadas al centro, desde la esquina trasera izquierda")
    rows = []
    names = {0: "Amarre batería", 4: "Amarre motor", 8: "Amarre sensor"}
    for i, (cx, cy, w, l) in enumerate(PLATE_SLOTS):
        n = "Amarre batería" if i < 4 else ("Amarre motor" if i < 8 else "Amarre sensor")
        rows.append((n, cx + PW/2, cy, f"ranura {w:g} x {l:g}"))
    for i, (cx, cy, d) in enumerate(PLATE_HOLES):
        n = "Cables motor" if i < 2 else ("Cables sensor" if i < 4 else "Rueda loca")
        rows.append((n, cx + PW/2, cy, f"Ø {d:g}"))
    x0, y0, w, l = WINDOW
    rows.append(("Ventana sensor", x0 + w/2 + PW/2, y0 + l/2, f"{w:g} x {l:g}, R3"))
    tx, ty = 15, 195
    ax.text(tx, ty, f"{'#':<4}{'Elemento':<17}{'X':>7}{'Y':>8}   Tamaño (X x Y)", fontsize=6, family="monospace", weight="bold")
    y = ty
    for i, (n, x, yy, sz) in enumerate(rows, 1):
        y -= 5
        ax.text(tx, y, f"{i:<4}{n:<17}{x:>7.1f}{yy:>8.1f}   {sz}", fontsize=6, family="monospace")
    # separador rueda loca
    sx, sy = 175, 120
    pc = CASTER["plate"]
    ax.add_patch(Rectangle((sx, sy), pc, pc, fill=False, ec=INK, lw=0.8))
    for cx, cy, d in CASTER_HOLES:
        ax.add_patch(Circle((sx + pc/2 + cx, sy + pc/2 + cy - CASTER["y"]), d/2, fill=False, ec=INK, lw=0.5))
    dim(ax, (sx, sy), (sx + pc, sy), -6, f"{pc:g}")
    dim(ax, (sx + pc, sy), (sx + pc, sy + pc), -6, f"{pc:g}")
    dim(ax, (sx + pc/2 - CASTER_HOLE_PITCH/2, sy + pc), (sx + pc/2 + CASTER_HOLE_PITCH/2, sy + pc), 4, f"{CASTER_HOLE_PITCH:g} (agujeros Ø4.5)", fs=5)
    ax.text(sx, sy + pc + 12, f"SEPARADOR RUEDA LOCA (x{N_CASTER_SPACERS})", fontsize=7, weight="bold")
    ax.text(sx, sy - 16, "Rueda loca de 34 mm + 3 separadores de 3 mm = 43 mm.\nLa tabla queda a 43.75 mm en las ruedas: queda nivelada.\nRevisa que los agujeros coincidan con los de tu rueda;\nsi no, márcalos sobre el separador y ajusta con una broca.", fontsize=5)
    qx, qy = 175, 45
    ax.add_patch(Rectangle((qx, qy), PCB_SPACER, PCB_SPACER, fill=False, ec=INK, lw=0.8))
    dim(ax, (qx, qy), (qx + PCB_SPACER, qy), -5, f"{PCB_SPACER:g}", fs=5)
    ax.text(qx, qy + 14, f"SEPARADOR PCB (x{N_PCB_SPACERS})", fontsize=7, weight="bold")
    ax.text(qx + 14, qy + 4, "Uno en cada esquina del L298N y\nde la shield, para que los pines\nsoldados no toquen la madera.", fontsize=5, va="center")
    pdf.savefig(fig); plt.close(fig)

def p3(pdf):
    fig, ax = page("3/3  VISTA LATERAL Y ALTURAS", "Lado derecho · frente hacia la derecha")
    ox, oz = 35, 95
    ax.plot([ox - 15, ox + 240], [oz, oz], color=INK, lw=0.8)
    for i in range(0, 255, 6): ax.plot([ox - 15 + i, ox - 12 + i], [oz, oz - 3], color=GREY, lw=0.3)
    ax.add_patch(Rectangle((ox, oz + PLATE_ZB), PL, T, fc="#d7b98e", ec=INK, lw=0.6))
    ax.add_patch(Rectangle((ox + MOTOR_Y0, oz + PLATE_ZB - MOTOR_H), MOTOR_L, MOTOR_H, fc="#f4d03f", ec=INK, lw=0.5, alpha=0.7))
    ax.text(ox + MOTOR_Y0 + 58, oz + PLATE_ZB - 5, "motor TT", fontsize=5, ha="center", va="center")
    ax.add_patch(Circle((ox + SHAFT_Y, oz + AXLE_Z), WHEEL_D/2, fill=False, ec=INK, lw=0.9))
    ax.add_patch(Circle((ox + SHAFT_Y, oz + AXLE_Z), 2, color=INK))
    ax.text(ox + SHAFT_Y, oz + 5, "rueda 65×26\n(por fuera)", fontsize=4.5, ha="center")
    cy = CASTER["y"]
    ax.add_patch(Rectangle((ox + cy - 22, oz + PLATE_ZB - 9), 44, 9, fc="#e8d2ad", ec=INK, lw=0.4))
    ax.text(ox + cy, oz + PLATE_ZB - 4.5, "3 separadores", fontsize=4, ha="center", va="center")
    ax.add_patch(Rectangle((ox + cy - 22, oz + 34 - 1.5), 44, 1.5, fc="#bbb", ec=INK, lw=0.4))
    ax.add_patch(Polygon([(ox + cy - 8, oz + 32.5), (ox + cy + 8, oz + 32.5), (ox + cy + 4, oz + 12.5), (ox + cy - 10, oz + 12.5)], fc="#ccc", ec=INK, lw=0.4))
    ax.add_patch(Circle((ox + cy - 3, oz + 12.5), 12.5, fc="white", ec=INK, lw=0.6))
    ax.text(ox + cy - 3, oz + 12.5, "Ø25", fontsize=4, ha="center", va="center")
    for c in TOP.values():
        ax.add_patch(Rectangle((ox + c["y"], oz + c["z"]), c["l"], c["h"], fill=False, ec=ENGC, lw=0.5))
        ax.text(ox + c["y"] + c["l"]/2, oz + c["z"] + c["h"]/2, c["name"].replace(" + ", "\n+").replace(" 2x", "\n2x"), fontsize=4.5, ha="center", va="center", color=ENGC)
    S = SENSOR
    ax.add_patch(Rectangle((ox + S["y0"], oz + SENSOR_ZB), S["total_l"], S["h"], fc="#5dade2", ec=INK, lw=0.5, alpha=0.8))
    ax.text(ox + S["y0"] + 28, oz + SENSOR_ZB + 5.5, "BFD-1000", fontsize=4.5, ha="center", va="center")
    dim(ax, (ox - 8, oz), (ox - 8, oz + PLATE_ZB), 0, f"{PLATE_ZB:g}")
    dim(ax, (ox + cy - 30, oz), (ox + cy - 30, oz + 34), 0, "34", fs=5)
    dim(ax, (ox + 232, oz), (ox + 232, oz + SENSOR_ZB), 0, f"{SENSOR_ZB:g}", fs=5)
    ax.plot([ox + S["y0"] + S["total_l"], ox + 234], [oz + SENSOR_ZB]*2, color=DIMC, lw=0.2, ls=":")
    dim(ax, (ox, oz + 85), (ox + PL, oz + 85), 0, f"tabla {PL:g}")
    dim(ax, (ox + SHAFT_Y, oz + 95), (ox + S["y0"] + S["bar_l"]/2, oz + 95), 0, f"eje ruedas → sensores {S['y0'] + S['bar_l']/2 - SHAFT_Y:g}")
    notes = [
        f"• Con ruedas de 65 mm la tabla queda a {PLATE_ZB:g} mm del piso; los sensores, a ~{SENSOR_ZB:g} mm.",
        "• El BFD-1000 detecta hasta unos 30-40 mm según el fabricante: calibra sus potenciómetros",
        "  sobre tu pista. Si no distingue bien la línea, pega bloques de 10-15 mm entre la tabla y",
        "  los extremos del sensor para bajarlo a unos 15 mm.",
        "• Fija todo con amarres + silicona caliente (o pegamento epóxico para la rueda loca).",
    ]
    y = 60
    for n in notes: ax.text(20, y, n, fontsize=5.5); y -= 5.5
    pdf.savefig(fig); plt.close(fig)

if __name__ == "__main__":
    import os; os.makedirs("out", exist_ok=True)
    with PdfPages("out/chasis_planos.pdf") as pdf:
        p1(pdf); p2(pdf); p3(pdf)
    print("ok")
