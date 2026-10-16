"""Escena 3D (cajas y cilindros) a partir de geom.py; exporta JSON para el visor y render PNG."""
import json
from geom import *

def scene():
    S = []  # cajas: tipo box (x0,y0,z0,w,l,h,color,name) ; cyl: (cx,cy,cz,r,len,axis,color,name)
    box = lambda x, y, z, w, l, h, c, n: S.append(dict(t="box", x=x, y=y, z=z, w=w, l=l, h=h, c=c, n=n))
    cyl = lambda x, y, z, r, L, c, n: S.append(dict(t="cyl", x=x, y=y, z=z, r=r, L=L, c=c, n=n))
    wood, wood2 = "#d9b382", "#e6c9a0"
    # tabla única aproximada con 3 cajas (recortes de rueda a los lados)
    box(-PW / 2, 0, PLATE_ZB, PW, NOTCH_Y0, T, wood, "Base MDF")
    box(-NOTCH_X0, NOTCH_Y0, PLATE_ZB, 2 * NOTCH_X0, NOTCH_Y1 - NOTCH_Y0, T, wood, "Base MDF")
    box(-PW / 2, NOTCH_Y1, PLATE_ZB, PW, PL - NOTCH_Y1, T, wood, "Base MDF")
    for s in (-1, 1):
        x0 = MOTOR_X_IN if s > 0 else -MOTOR_X_OUT
        box(x0, MOTOR_Y0, PLATE_ZB - MOTOR_H, MOTOR_W, MOTOR_L, MOTOR_H, "#f2c418", "Motor TT")
        cyl(s * WHEEL_XC, SHAFT_Y, AXLE_Z, WHEEL_D / 2, WHEEL_W, "#2b2b2b", "Rueda 65 mm"); S[-1]["ri"] = 20
        cyl(s * WHEEL_XC, SHAFT_Y, AXLE_Z, 20, WHEEL_W + 0.6, "#f2c418", "Rin")
    cols = {"bat": "#3a3a3a", "l298": "#c8322b", "shield": "#1c1c1c"}
    for k, c in TOP.items():
        if k == "bat":
            box(c["x"], c["y"], c["z"], c["w"], c["l"], 2, cols[k], c["name"])
            box(c["x"], c["y"], c["z"] + 2, 4, c["l"], c["h"] - 2, cols[k], c["name"])
            box(c["x"] + c["w"] - 4, c["y"], c["z"] + 2, 4, c["l"], c["h"] - 2, cols[k], c["name"])
            for f in (0.27, 0.73):  # celdas 18650 (eje en X)
                cyl(0, c["y"] + f * c["l"], c["z"] + 2 + 9, 9, 66, "#2f6db5", "Celda 18650")
        elif k == "l298":
            box(c["x"], c["y"], c["z"], c["w"], c["l"], 1.6, cols[k], c["name"])
            box(c["x"] + 26, c["y"] + 6, c["z"] + 1.6, 4, 31, 25, "#222", "Disipador")
            box(c["x"] + 2, c["y"] + 1, c["z"] + 1.6, 10, 8, 10, "#2a78d0", "Bornera")
            box(c["x"] + 2, c["y"] + 34, c["z"] + 1.6, 10, 8, 10, "#2a78d0", "Bornera")
            box(c["x"] + 4, c["y"] + 16, c["z"] + 1.6, 9, 12, 10, "#2a78d0", "Bornera potencia")
        else:
            box(c["x"], c["y"], c["z"], c["w"], c["l"], 1.6, cols[k], c["name"])
            box(c["x"] + 6, c["y"] + 13, c["z"] + 1.6, 55, 28, 9, "#111", "Zócalos")
            box(c["x"] + 8, c["y"] + 15, c["z"] + 10.6, 51, 26, 1.6, "#1a1a1a", "ESP32 DevKit")
            box(c["x"] + 25, c["y"] + 16, c["z"] + 12.2, 18, 24, 3, "#b8b8b8", "Módulo WROOM-32")
            box(c["x"] + c["w"] - 8, c["y"] + 24, c["z"] + 12.2, 8, 9, 3.2, "#cfcfcf", "USB-C")
        for (dx, dy) in ((1, 1), (c["w"] - 9, 1), (1, c["l"] - 9), (c["w"] - 9, c["l"] - 9)):
            if k != "bat":
                box(c["x"] + dx, c["y"] + dy, PLATE_ZT, 8, 8, SPACER, wood2, "Separador")
    Sx = SENSOR
    pz = PLATE_ZB - 1.6
    box(-Sx["bar_w"] / 2, Sx["y0"], pz, Sx["bar_w"], Sx["bar_l"], 1.6, "#1f4fbf", "BFD-1000")
    box(-Sx["stem_w"] / 2, Sx["y0"] + Sx["bar_l"], pz, Sx["stem_w"], Sx["total_l"] - Sx["bar_l"], 1.6, "#1f4fbf", "BFD-1000")
    for i in range(5):
        box(-50 + i * 25 - 3, Sx["y0"] + 8, SENSOR_ZB, 6, 6, pz - SENSOR_ZB, "#111", f"Sensor S{i+1}")
    # rueda loca giratoria 34 mm + 3 separadores
    p = CASTER["plate"]; cx, cy = CASTER["x"], CASTER["y"]
    box(cx - p / 2, cy - p / 2, 34, p, p, PLATE_ZB - 34, wood2, "Separadores rueda loca")
    box(cx - p / 2, cy - p / 2, 32.5, p, p, 1.5, "#b9bdc2", "Rueda loca")
    box(cx - 11, cy - 6, 14, 3, 14, 18.5, "#b9bdc2", "Rueda loca")
    box(cx + 8, cy - 6, 14, 3, 14, 18.5, "#b9bdc2", "Rueda loca")
    S.append(dict(t="cyl", x=cx, y=cy - 4, z=12.5, r=12.5, L=16, c="#f0f0f0", n="Rueda loca"))
    G = [("Base", "base"), ("Plataforma", "deck"), ("Pata", "deck"), ("Motor", "motor"), ("Rueda 65", "wheel"), ("Rin", "wheel"),
         ("Portapilas", "bat"), ("Celda", "bat"), ("L298N", "l298"), ("Disipador", "l298"), ("Bornera", "l298"),
         ("ESP32", "esp"), ("Zócalos", "esp"), ("Módulo", "esp"), ("USB", "esp"), ("Separador", "spacer"),
         ("BFD", "sensor"), ("Sensor S", "sensor"), ("Rueda loca", "caster"), ("Separadores rueda", "spacer")]
    for o in S:
        o["g"] = next(g for p, g in G if o["n"].startswith(p))
    return S

if __name__ == "__main__":
    import os; os.makedirs("out", exist_ok=True)
    json.dump(scene(), open("out/scene.json", "w"))
    print(len(scene()))
