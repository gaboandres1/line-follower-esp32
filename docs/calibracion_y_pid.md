# Calibración y control PID

Esta guía cubre el testbench y el seguidor PID básico. Todo lo que dice del PID aplica también a `firmware/seguidor_web`, que usa el mismo control.

| Carpeta | Para qué sirve |
|---|---|
| `firmware/testbench/testbench.ino` | Calibrar el robot: descubre cómo está armado y te imprime las **constantes de hardware** listas para copiar. |
| `firmware/seguidor_pid/seguidor_pid.ino` | El seguidor de línea con control **PID**, configurable con constantes. |

El orden de trabajo es: **1)** subir el testbench y calibrar → **2)** copiar las constantes al seguidor → **3)** subir el seguidor y ajustar el PID en la pista.

> Arduino IDE exige que cada `.ino` esté dentro de una carpeta con su mismo nombre. Abre cada uno con **Archivo → Abrir**.

---

## 1. Conexiones

### Sensor BFD-1000 → shield ESP32 (jumper JUMP en **3.3V**)

| BFD-1000 | Pin ESP32 | Nota |
|---|---|---|
| VCC | V (3.3 V) | nunca 5 V: las salidas irían a 5 V |
| GND | G | |
| S1 (U1) | D21 | |
| S2 (U2) | D19 | |
| S3 (U4) | D18 | **sensor central** |
| S4 (U5) | D17 | |
| S5 (U6) | D16 | |
| CLP | D22 | interruptor de choque |
| Near | D23 | obstáculo IR |

### L298N → ESP32

| L298N | Pin ESP32 | Función |
|---|---|---|
| ENA | D25 | velocidad canal A (PWM) |
| IN1 | D26 | dirección canal A |
| IN2 | D27 | dirección canal A |
| ENB | D14 | velocidad canal B (PWM) |
| IN3 | D33 | dirección canal B |
| IN4 | D13 | dirección canal B |
| 5V | 5V del bloque de alimentación de la shield | alimenta el ESP32 |
| GND | GND de la shield **y** − de la batería | tierra común obligatoria |
| 12V | + de la batería, pasando por el interruptor | |

- **Quita los jumpers de ENA y ENB**; si los dejas, no hay control de velocidad.
- Deja puesto el jumper de 5V del L298N (la batería da 7.4 V).
- Canal A = OUT1/OUT2, canal B = OUT3/OUT4. **No importa qué motor pusiste en cada canal**: eso lo resuelve la calibración.
- Desconecta el cable de 5V del L298N cuando programes por USB.

---

## 2. Constantes

El sketch del seguidor separa las constantes en dos grupos.

### 2.1 Constantes de hardware

Dependen de cómo está armado **tu** robot. No cambian al cambiar de pista. El testbench las calcula por ti.

| Constante | Valores | Qué significa |
|---|---|---|
| `SENSOR_PINS` | `{21,19,18,17,16}` | pines de S1..S5 |
| `BLACK_VALUE` | `0` o `1` | lo que lee un sensor **sobre negro**. Blanco es el complemento. Por defecto `1`. |
| `S1_IS_LEFT` | `true` / `false` | si S1 queda a la **izquierda** del robot mirando hacia adelante. Si montaste el sensor al revés, es `false`. |
| `LEFT_CHANNEL` | `CH_A` / `CH_B` | qué canal del L298N mueve la rueda **izquierda**. La derecha es automáticamente el otro. |
| `LEFT_INVERT` | `true` / `false` | `true` si la rueda izquierda gira al revés (hacia atrás cuando debería ir adelante). |
| `RIGHT_INVERT` | `true` / `false` | lo mismo para la rueda derecha. |
| `CLP_ACTIVE_VALUE` | `0` / `1` | lectura de CLP al presionarlo. |
| `NEAR_ACTIVE_VALUE` | `0` / `1` | lectura de NEAR cuando hay un obstáculo. |

"Adelante" significa **hacia el lado donde está el sensor BFD-1000**.

### 2.2 Constantes lógicas

Definen el comportamiento. Son las que vas a ajustar en la pista.

| Constante | Valor inicial | Qué hace |
|---|---|---|
| `LINE_COLOR` | `LINE_BLACK` | `LINE_BLACK` sigue línea negra sobre fondo blanco. `LINE_WHITE` sigue línea blanca sobre fondo negro (aplica una compuerta NOT a la medición). |
| `BASE_SPEED` | `140` | velocidad en recta (0–255). |
| `MAX_SPEED` | `210` | **saturación**: ningún motor supera este valor, aunque el PID lo pida. |
| `ALLOW_REVERSE` | `true` | si una rueda puede ir hacia atrás en curvas cerradas. Con `false`, la mínima es 0. |
| `KP`, `KI`, `KD` | `45`, `0`, `120` | ganancias del PID (ver sección 4). |
| `INTEGRAL_LIMIT` | `50` | tope del término integral (anti-windup). |
| `LOOP_MS` | `10` | periodo fijo del control, en ms. |
| `LOST_ERROR` | `3.0` | error que se asume al perder la línea. |
| `LOST_TIMEOUT_MS` | `1000` | si pierde la línea más de este tiempo, se detiene. |
| `USE_BUMPER` / `USE_NEAR` | `false` | detenerse al chocar o al detectar un obstáculo. |
| `DEBUG_SERIAL` | `true` | imprime telemetría en el Monitor Serie. |

### 2.3 Cómo se combinan `BLACK_VALUE` y `LINE_COLOR`

Para cada sensor el programa calcula:

```
esNegro  = (lectura == BLACK_VALUE)                       // hardware
veLinea  = (LINE_COLOR == LINE_BLACK) ? esNegro : !esNegro // lógica (NOT)
```

Así, cambiar de pista (línea negra o blanca) **no** obliga a recalibrar el sensor, y cambiar de sensor **no** obliga a tocar la lógica.

---

## 3. Calibración con el testbench

### Preparación

1. Sube `testbench.ino`.
2. Abre el Monitor Serie: **115200 baudios** y, abajo a la derecha, **"Nueva línea"**.
3. Ten a mano una hoja blanca con una tira de cinta aislante negra.
4. Enciende la batería (los motores la necesitan) y **levanta el robot** para que las ruedas giren en el aire.

### Asistente guiado (`1` + Enter)

| Paso | Qué haces | Qué deduce |
|---|---|---|
| 1. Color | Pones los sensores sobre blanco → Enter, luego sobre negro → Enter. | `BLACK_VALUE`. Avisa si algún sensor no distingue los colores. |
| 2. Orden | Pones la cinta **solo bajo el sensor del extremo izquierdo** (mirando el robot desde atrás). | `S1_IS_LEFT`. |
| 3. Motores | Gira el canal A durante 1.5 s y te pregunta: ¿qué rueda giró? (`i`/`d`) y ¿giró hacia adelante? (`s`/`n`). Luego lo mismo con el canal B. | `LEFT_CHANNEL`, `LEFT_INVERT`, `RIGHT_INVERT`. Al final ofrece una prueba: izquierda, derecha, ambas adelante, ambas atrás. |
| 4. Extras | Presionas el CLP y acercas la mano al frente cuando te lo pide (tienes 6 s para cada uno). | `CLP_ACTIVE_VALUE`, `NEAR_ACTIVE_VALUE`. |

Al terminar imprime un bloque como este:

```cpp
// ===== Copia esto en seguidor_pid.ino (sección HARDWARE) =====
#define BLACK_VALUE        1
#define S1_IS_LEFT         true
#define LEFT_CHANNEL       CH_B
#define LEFT_INVERT        true
#define RIGHT_INVERT       false
#define CLP_ACTIVE_VALUE   0
#define NEAR_ACTIVE_VALUE  0
```

Reemplaza esas líneas en `seguidor_pid.ino`.

### Comandos sueltos

| Comando | Acción |
|---|---|
| `?` | menú |
| `s` | ver los sensores en vivo (`#` = lee 1). Cualquier tecla para salir. |
| `c`, `o`, `m`, `e` | repetir solo el paso 1, 2, 3 o 4 |
| `a` / `a-` | girar el canal A en sentido + / − |
| `b` / `b-` | girar el canal B en sentido + / − |
| `t` | probar la configuración calibrada |
| `v 200` | cambiar la velocidad de prueba (si los motores zumban pero no giran, súbela) |
| `p` | imprimir el bloque de constantes |
| `x` | parar motores |

El sentido "+" de un canal significa INx1 = HIGH, INx2 = LOW. Es la misma convención que usa el seguidor, por eso los resultados del testbench se pueden copiar tal cual.

---

## 4. El seguidor PID

### Uso

1. Sube `seguidor_pid.ino` con las constantes de hardware ya copiadas.
2. Pon el robot sobre la línea, con el sensor central encima.
3. Presiona **BOOT** en el ESP32 para arrancar. El LED azul se enciende. Presiona BOOT otra vez para detener.

Mientras está detenido, el Monitor Serie muestra lo que ven los sensores: sirve para confirmar que el centro marca `[..#..]` sobre la línea.

### Cómo calcula el error

Cada sensor tiene un peso según su posición, de izquierda a derecha:

```
sensor:   izq  ·   ·  centro ·   ·  der
peso:     -2      -1     0      +1    +2
```

El error es el **promedio de los pesos de los sensores que ven la línea**:

| Lo que ven los sensores | Error | Qué hace el robot |
|---|---|---|
| `..#..` | 0 | sigue recto |
| `...##` | +1.5 | la línea está a la derecha: gira a la derecha |
| `#....` | −2 | gira fuerte a la izquierda |
| `.....` | ±3 | perdió la línea: sigue girando hacia donde la vio por última vez |
| `#####` | 0 | cruce o marca transversal: sigue recto |

### El control

En cada ciclo (cada `LOOP_MS`):

```
P = error
I = suma de errores (limitada a ±INTEGRAL_LIMIT)
D = error − error_anterior
u = KP·P + KI·I + KD·D

rueda izquierda = BASE_SPEED + u    (saturada a ±MAX_SPEED)
rueda derecha   = BASE_SPEED − u
```

Si el error es positivo (línea a la derecha), la rueda izquierda acelera y la derecha frena, así el robot gira hacia la línea.

Como el periodo es fijo, `I` y `D` usan unidades "por ciclo". Si cambias `LOOP_MS`, tendrás que reajustar `KI` y `KD`.

### Cómo ajustar el PID

Ajusta una cosa a la vez y prueba en la pista después de cada cambio.

1. **Empieza lento**: `BASE_SPEED = 110`, `KI = 0`, `KD = 0`.
2. **Sube `KP`** hasta que el robot siga la línea en las curvas, aunque oscile (zigzaguee). Si se sale en las curvas, súbelo; si zigzaguea mucho en las rectas, ya te pasaste.
3. **Sube `KD`** para quitar el zigzag. Un buen punto de partida es KD ≈ 2–4 × KP. Si el robot se vuelve "nervioso" o tiembla, bájalo.
4. **`KI`** casi nunca hace falta en un seguidor. Úsalo solo si el robot queda siempre corrido hacia un lado en las rectas: empieza en 0.5 y sube despacio.
5. **Sube `BASE_SPEED`** de a 10 y repite los pasos 2 y 3 cuando haga falta. Deja `MAX_SPEED` entre 40 y 70 por encima de `BASE_SPEED` para que el PID tenga margen.

Lectura de la telemetría (con el robot conectado por USB y levantado):

```
[..#..] e=0.0 u=0 L=140 R=140
[...#.] e=1.0 u=165 L=210 R=-25
```

---

## 5. Problemas comunes

| Síntoma | Causa probable | Qué hacer |
|---|---|---|
| Un sensor no cambia nunca | potenciómetro o altura | gira su potenciómetro azul con `s` abierto hasta que cambie entre blanco y negro |
| El robot gira en círculos | `LEFT_INVERT`/`RIGHT_INVERT` mal | repite el paso 3 del testbench |
| Se va hacia el lado contrario de la línea | `S1_IS_LEFT` mal, o motores izquierdo/derecho cruzados | repite los pasos 2 y 3 |
| Zigzaguea en las rectas | `KP` alto o `KD` bajo | baja `KP` o sube `KD` |
| Se sale en las curvas | `BASE_SPEED` alta o `KP` bajo | baja la velocidad o sube `KP` |
| El ESP32 se reinicia al arrancar los motores | caída de voltaje o falta de tierra común | carga las baterías, revisa el GND común, agrega un condensador de 470 µF entre 5V y GND |
| Los motores zumban pero no giran | PWM demasiado bajo | sube `BASE_SPEED` (los motores TT necesitan ~90 para arrancar) |
| No sube el código (`Wrong boot mode`) | modo de descarga | mantén BOOT presionado al aparecer `Connecting...` |
| Error en `ledcAttach` | core ESP32 antiguo | el código ya es compatible con 2.x y 3.x; si persiste, actualiza el core en el Gestor de tarjetas |
