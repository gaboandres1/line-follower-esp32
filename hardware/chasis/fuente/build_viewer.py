import os; os.makedirs("out", exist_ok=True)
import json
from geom import *
from scene import scene

GROUPS = [
    dict(g="base", name="Tabla MDF", dims=f"{PW:g}×{PL:g}×3", color="#d9b382"),
    dict(g="bat", name="Portapilas 2×18650", dims="76×41×18", color="#2f6db5"),
    dict(g="l298", name="L298N", dims="43×43×27", color="#c8322b"),
    dict(g="esp", name="ESP32 + shield", dims="69×53×22", color="#1c1c1c"),
    dict(g="motor", name="Motores TT (debajo)", dims="70×19×22.5", color="#f2c418"),
    dict(g="wheel", name="Ruedas", dims="65×26", color="#2b2b2b"),
    dict(g="sensor", name="BFD-1000 (debajo)", dims="129×56×11", color="#1f4fbf"),
    dict(g="caster", name="Rueda loca", dims="Ø25, alto 34", color="#b9bdc2"),
    dict(g="spacer", name="Separadores", dims="3 mm c/u", color="#e6c9a0"),
]

t = open("viewer_template.html").read()
rep = {
    "__SCENE__": json.dumps(scene(), ensure_ascii=False),
    "__GROUPS__": json.dumps(GROUPS, ensure_ascii=False),
    "__PLATE_ZB__": f"{PLATE_ZB:g}", "__AXLE__": f"{AXLE_Z:g}",
    "__SENSOR_ZB__": f"{SENSOR_ZB:g}", "__REACH__": f"{SENSOR['y0'] + SENSOR['bar_l']/2 - SHAFT_Y:g}",
}
for k, v in rep.items():
    t = t.replace(k, v)
open("out/chasis_3d.html", "w").write(t)
print("ok", len(t))
