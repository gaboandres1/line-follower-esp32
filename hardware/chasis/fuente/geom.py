"""Geometría del chasis v2: UNA sola tabla (mm).
Ejes: X = derecha, Y = adelante, Z = arriba. Origen: centro del borde trasero, piso z=0.
Motores, rueda loca y sensor van DEBAJO de la tabla; batería, L298N y ESP32 ENCIMA.
"""
T = 3.0
WHEEL_D, WHEEL_W = 65.0, 26.0
MOTOR_L, MOTOR_W, MOTOR_H = 70.0, 19.0, 22.5
SHAFT_FROM_END = 11.5
AXLE_Z = WHEEL_D / 2
PLATE_ZB = AXLE_Z + MOTOR_H / 2            # 43.75
PLATE_ZT = PLATE_ZB + T

PW, PL = 150.0, 190.0
R_REAR, R_FRONT = 8.0, 15.0
SHAFT_Y = 70.0
MOTOR_Y1 = SHAFT_Y + SHAFT_FROM_END        # 81.5
MOTOR_Y0 = MOTOR_Y1 - MOTOR_L              # 11.5
MOTOR_X_OUT = 55.0
MOTOR_X_IN = MOTOR_X_OUT - MOTOR_W         # 36
WHEEL_X_IN = MOTOR_X_OUT + 2.0
WHEEL_XC = WHEEL_X_IN + WHEEL_W / 2        # 70
NOTCH_X0, NOTCH_Y0, NOTCH_Y1 = 55.0, 35.0, 105.0

SPACER = 3.0
TOP = {
    "l298":   dict(x=-21.5, y=13.0,   w=43.0, l=43.0, h=27.0, name="L298N", z=PLATE_ZT + SPACER),
    "bat":    dict(x=-38.0, y=58.0,  w=76.0, l=41.1, h=18.3, name="Portapilas 2x18650", z=PLATE_ZT),
    "shield": dict(x=-34.3, y=102.0, w=68.6, l=53.4, h=22.0, name="ESP32 + shield", z=PLATE_ZT + SPACER),
}

CASTER = dict(x=0.0, y=26.0, plate=44.0, h=34.0, wheel=25.0, sweep_r=24.0)
CASTER_HOLE_PITCH = 34.0
N_CASTER_SPACERS = 3

SENSOR = dict(bar_w=129.0, bar_l=22.0, stem_w=40.0, total_l=56.0, h=11.0, y0=160.0)
SENSOR_ZB = PLATE_ZB - SENSOR["h"]          # 32.75
WINDOW = (-58.0, 163.5, 116.0, 15.0)

ZT_L, ZT_S = 4.5, 2.0
def slot_x(cx, cy): return (cx, cy, ZT_L, ZT_S)
def slot_y(cx, cy): return (cx, cy, ZT_S, ZT_L)

PLATE_SLOTS = []
for yy in (65.0, 92.0):
    PLATE_SLOTS += [slot_y(-42.5, yy), slot_y(42.5, yy)]
for s in (-1, 1):
    mx = s * (MOTOR_X_IN + MOTOR_W / 2)
    PLATE_SLOTS += [slot_x(mx, MOTOR_Y0 - 3.5), slot_x(mx, MOTOR_Y1 + 3.5)]
for xx in (-62.0, -37.5, 37.5, 62.0):
    PLATE_SLOTS += [slot_x(xx, SENSOR["y0"] - 3.0), slot_x(xx, SENSOR["y0"] + SENSOR["bar_l"] + 3.0)]

_h = CASTER_HOLE_PITCH / 2
CASTER_HOLES = [(CASTER["x"] + dx, CASTER["y"] + dy, 4.5) for dx in (-_h, _h) for dy in (-_h, _h)]
PLATE_HOLES = [(-30.0, 10.0, 7.0), (30.0, 10.0, 7.0), (-50.0, 150.0, 10.0), (50.0, 150.0, 10.0)] + CASTER_HOLES
N_PCB_SPACERS, PCB_SPACER = 8, 8.0

def outline(n_arc=10):
    """Contorno de la tabla como lista de puntos (sentido antihorario), con recortes para ruedas."""
    import math
    def arc(cx, cy, r, a0, a1):
        return [(cx + r*math.cos(math.radians(a0 + (a1-a0)*i/n_arc)), cy + r*math.sin(math.radians(a0 + (a1-a0)*i/n_arc))) for i in range(n_arc+1)]
    X, L = PW/2, PL
    p = []
    p += arc(X - R_REAR, R_REAR, R_REAR, 270, 360)                # esquina trasera derecha
    p += [(X, NOTCH_Y0), (NOTCH_X0, NOTCH_Y0), (NOTCH_X0, NOTCH_Y1), (X, NOTCH_Y1)]
    p += arc(X - R_FRONT, L - R_FRONT, R_FRONT, 0, 90)
    p += arc(-X + R_FRONT, L - R_FRONT, R_FRONT, 90, 180)
    p += [(-X, NOTCH_Y1), (-NOTCH_X0, NOTCH_Y1), (-NOTCH_X0, NOTCH_Y0), (-X, NOTCH_Y0)]
    p += arc(-X + R_REAR, R_REAR, R_REAR, 180, 270)
    return p
