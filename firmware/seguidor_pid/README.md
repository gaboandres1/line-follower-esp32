# `seguidor_pid` — seguidor PID base

Seguidor de línea con control PID, sin WiFi. Todo se configura con constantes al inicio del sketch.

## Uso

1. Pega las constantes de hardware que imprimió el [`testbench`](../testbench) en la sección **1) CONSTANTES DE HARDWARE**.
2. Ajusta la sección **2) CONSTANTES LÓGICAS** si hace falta (velocidad, PID, color de línea).
3. Sube el sketch, pon el robot sobre la línea y presiona **BOOT** para arrancar (LED azul encendido). Otra pulsación lo detiene.

## Comportamiento

- Control PID con periodo fijo de `LOOP_MS` = 10 ms, saturación `MAX_SPEED` y anti-windup.
- **Línea perdida** más de `LOST_TIMEOUT_MS` (1 s): **gira en el lugar buscándola** hacia donde la vio por última vez, y retoma el PID al encontrarla. Se configura con `SEARCH_ON_LOST`, `SEARCH_SPEED` y `SEARCH_TIMEOUT_MS` (`0` = sin límite).
- **Choque / obstáculo** (con `USE_BUMPER` / `USE_NEAR` en `true`): se detiene. CLP y NEAR pasan por un filtro integrador (`INPUT_FILTER_N` = 3 lecturas = 30 ms), así que un rebote o un pico de ruido no lo detienen.
- Cuando se detiene solo (choque, obstáculo, o búsqueda desactivada o agotada) **queda detenido hasta presionar BOOT**.
- Con `DEBUG_SERIAL` imprime la telemetría en el Monitor Serie (115200): `[..#..] e=0.0 u=0 L=140 R=140`.

Guía completa (constantes, cálculo del error y ajuste del PID): [docs/calibracion_y_pid.md](../../docs/calibracion_y_pid.md#4-el-seguidor-pid).
