# `testbench` — calibración

Asistente por el Monitor Serie que descubre cómo está armado tu robot y te imprime las **constantes de hardware** listas para pegar en cualquiera de los seguidores (`seguidor_pid`, `seguidor_web`, `seguidor_wifi`).

## Uso

1. Sube `testbench.ino` y abre el Monitor Serie a **115200** con **"Nueva línea"**.
2. **Levanta el robot** para que las ruedas giren en el aire y enciende la batería.
3. Escribe `1` + Enter y sigue los 4 pasos: color de línea, orden de sensores, motores y CLP/NEAR.
4. Copia el bloque que imprime al final (o en cualquier momento con `p`).

| Comando | Acción |
|---|---|
| `1` | asistente completo |
| `s` | sensores en vivo |
| `c` / `o` / `m` / `e` | repetir el paso 1 / 2 / 3 / 4 |
| `a`, `a-`, `b`, `b-` | girar un canal en sentido + / − |
| `t` | probar la configuración calibrada |
| `v 200` | velocidad de prueba |
| `p` | imprimir las constantes |
| `x` | parar motores |

## Qué calcula

`BLACK_VALUE`, `S1_IS_LEFT`, `LEFT_CHANNEL`, `LEFT_INVERT`, `RIGHT_INVERT`, `CLP_ACTIVE_VALUE`, `NEAR_ACTIVE_VALUE`.

Guía completa: [docs/calibracion_y_pid.md](../../docs/calibracion_y_pid.md#3-calibración-con-el-testbench).
