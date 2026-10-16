# Panel web — `firmware/seguidor_web`

![Panel y métricas](img/panel_y_metricas.png)

Es el mismo seguidor PID de `firmware/seguidor_pid`, pero ahora el ESP32 crea su propia red WiFi y sirve un panel web para controlarlo desde el celular.

```
firmware/seguidor_web/
├── seguidor_web.ino   ← el programa (configuración arriba)
└── web_ui.h           ← la interfaz web ya empaquetada (se abre como pestaña)
web/                   ← fuentes editables de la interfaz + build.py
docs/ui_preview.html   ← render de la interfaz con un robot simulado
```

Solo usa librerías incluidas en el soporte ESP32 de Arduino IDE (`WiFi`, `WebServer`, `DNSServer`, `Preferences`). **No hay que instalar nada.** Selecciona la placa **ESP32 Dev Module** y sube el sketch como siempre.

---

## 1. Primer uso

1. Copia tus constantes de hardware (las que imprimió `firmware/testbench`) en la sección **1) CONSTANTES DE HARDWARE** de `seguidor_web.ino`.
2. Sube el sketch. En el Monitor Serie (115200) verás:
   ```
   WiFi "MiRobot" listo. Abre http://mirobot.com  o  http://192.168.4.1
   ```
3. En el celular, conéctate a la red **MiRobot** (clave `robot1234`).
   Si el teléfono avisa que *"la red no tiene internet"*, elige **mantener la conexión**.
4. Abre el navegador y escribe **`http://mirobot.com`**, o directamente `192.168.4.1`.

---

## 2. Red: punto de acceso, DHCP y DNS

Todo se configura en la sección **0) RED**:

| Constante | Valor | Qué hace |
|---|---|---|
| `AP_SSID` / `AP_PASS` | `MiRobot` / `robot1234` | nombre y clave de la red (la clave necesita al menos 8 caracteres) |
| `AP_CHANNEL` | `6` | canal WiFi; cámbialo si hay interferencia |
| `AP_MAX_CLIENTS` | `1` | solo un dispositivo conectado a la vez |
| `AP_IP` | `192.168.4.1` | IP del robot; también es la puerta de enlace |
| `AP_MASK` | `255.255.255.128` | red **/25**: direcciones `.1` a `.126` |
| `DNS_NAME` | `mirobot.com` | nombre que se escribe en el navegador |
| `DNS_CATCH_ALL` | `false` | con `true`, **cualquier** nombre lleva al robot (como un portal cautivo) |
| `ROBOT_NAME` | `mirobot` | nombre que muestra el panel |

**DHCP:** el ESP32 asigna automáticamente una IP de la red /25 al dispositivo que se conecta.

**DNS:** el robot tiene su propio servidor DNS que responde `mirobot.com → 192.168.4.1`. Solo funciona para los dispositivos conectados a la red del robot.

> **Si `mirobot.com` no abre el panel:**
> - Escribe la dirección con `http://` delante. Algunos navegadores intentan `https://` por defecto, y el robot no tiene certificado.
> - **Apaga los datos móviles.** Cuando la red WiFi no tiene internet, muchos teléfonos siguen resolviendo nombres por los datos móviles y llegan al `mirobot.com` real de internet.
> - Desactiva el **DNS privado** del teléfono (Android: Ajustes → Red → DNS privado → Desactivado).
> - Si nada funciona, `http://192.168.4.1` siempre funciona.
>
> `mirobot.com` es un dominio real de internet. Si quieres evitar confusiones, usa un nombre que no exista, como `mirobot.lan`.

---

## 3. El panel (`/`)

| Elemento | Qué hace |
|---|---|
| Punto de conexión (arriba a la derecha) | verde = conectado al robot, rojo = se perdió la conexión |
| **Sensores** | los 5 sensores de izquierda a derecha; oscuro = ve la línea. La raya verde marca el error; se pone roja si perdió la línea. Debajo: error y PWM de cada rueda. |
| **Arrancar / Detener** | botón principal. El botón **BOOT** del ESP32 sigue funcionando igual. |
| **Velocidad** | `Base` (en recta) y `Máxima` (saturación de cada motor) |
| **Control PID** | `Kp`, `Ki`, `Kd`. Los botones − / + mueven un paso, para ajustes finos con el dedo. |
| **Pista** | línea negra o blanca (la compuerta NOT de la lógica) |
| Pie | estado del guardado, **Restaurar valores** (pide tocar dos veces) y enlace a **Métricas** |

Los cambios se aplican **al instante** al robot, incluso mientras corre.

### Alertas

| Alerta | Cuándo | Qué pasa |
|---|---|---|
| **Emergencia: choque** (roja) | se presiona el CLP mientras corre | se detiene. **No se puede volver a arrancar** hasta tocar *Entendido*. El celular vibra. |
| **Emergencia: obstáculo** (roja) | NEAR detecta algo mientras corre | igual que la anterior |
| **Línea perdida** (ámbar) | más de `LOST_TIMEOUT_MS` sin ver la línea | se detiene; se puede volver a arrancar directamente |

Las emergencias se activan con `USE_BUMPER` y `USE_NEAR` (ambas en `true`). Si el NEAR se dispara solo, ajusta su potenciómetro o pon `USE_NEAR = false`.

---

## 4. Memoria y protección de la flash

Los valores de `Kp`, `Ki`, `Kd`, `Base`, `Máxima` y el color de línea **se conservan al apagar el robot**. Se guardan en la memoria NVS del ESP32 con la librería `Preferences`.

Un slider puede mandar decenas de cambios por segundo, así que el guardado tiene tres protecciones:

1. **En la web:** el slider envía como máximo un valor cada 150 ms mientras lo arrastras, más el valor final al soltarlo.
2. **Espera tras el último cambio:** el robot usa el valor nuevo de inmediato (en RAM), pero solo lo escribe en flash cuando pasan **`SAVE_DELAY_MS` = 3 s** sin cambios nuevos.
3. **Intervalo mínimo:** nunca guarda dos veces en menos de **`SAVE_MIN_GAP_MS` = 10 s**.

Además, **solo escribe las claves cuyo valor cambió**, y no escribe nada si volviste al valor que ya estaba guardado.

En una simulación, 40 movimientos del slider en 2 segundos produjeron **un solo guardado**. El pie del panel muestra *"Guardando en 3 s…"* y luego *"✓ Valores guardados"*. En **Métricas** puedes ver cuántas veces se ha guardado en total.

Otros detalles:
- **Restaurar valores** vuelve a los `DEF_…` del código y borra lo guardado.
- **Subir el sketch otra vez no borra lo guardado.** Si cambias el significado de algún valor guardado, sube `CONFIG_VERSION` en 1: al arrancar, el robot borra los datos viejos y usa los valores por defecto.
- Si apagas el robot justo después de mover un slider (antes de que diga *Guardado*), ese último cambio se pierde.

---

## 5. Métricas (`/metrics`)

Página técnica que se actualiza cada 200 ms:

- **Gráfico** de los últimos 30 s: el error (verde, de −3 a +3) y la acción de control `u` (gris, dividida entre 100).
- **Control:** estado, error, los tres términos P, I y D por separado, `u`, PWM de cada rueda, constantes actuales, frecuencia real del lazo (debería ser ~100 Hz) y tiempo de cálculo por ciclo.
- **Sensores:** lectura cruda S1…S5, lo que interpreta como línea (de izquierda a derecha, ya con `S1_IS_LEFT`, `BLACK_VALUE` y `LINE_COLOR` aplicados), cuántos ven la línea, y CLP/NEAR en crudo.
- **Eventos:** alarma actual, alarmas totales, veces que perdió la línea y tiempo corriendo.
- **Sistema:** tiempo encendido, clientes conectados, memoria libre, guardados en flash, último guardado y versión del firmware.

Los mismos datos están en JSON crudo en `/api/metrics`, por si quieres leerlos desde un script.

---

## 6. API

Todas las rutas responden JSON con el estado actualizado.

| Método | Ruta | Uso |
|---|---|---|
| GET | `/api/state` | estado para el panel (el panel la consulta cada 300 ms) |
| GET | `/api/metrics` | métricas completas |
| POST | `/api/set?k=kp&v=50` | cambiar un parámetro: `kp`, `ki`, `kd`, `base`, `max`, `line` |
| POST | `/api/run?on=1` | arrancar (`on=0` para detener) |
| POST | `/api/ack` | confirmar la alerta |
| POST | `/api/reset` | restaurar los valores por defecto |

El robot vuelve a limitar todos los valores que recibe (`KP_LIMIT`, etc.), aunque alguien los mande por fuera del panel.

---

## 7. Cómo está organizado el programa

- **El control PID corre en una tarea propia** de FreeRTOS, fijada al núcleo 1 con prioridad alta y periodo exacto de 10 ms (`vTaskDelayUntil`). Así el WiFi y las páginas web no alteran el ritmo del control.
- **El servidor web, el DNS y el guardado** corren en `loop()`.
- Las métricas pasan de una tarea a otra en una copia protegida (`portENTER_CRITICAL`), para que nunca se lean a medio escribir.
- El panel no usa internet: no carga fuentes ni librerías externas, porque la red del robot no tiene salida.

### Modificar la interfaz

Edita los archivos de `web/` (`common.css`, `panel.html`, `metrics.html`) y ejecuta, desde la raíz del repo:

```bash
python3 web/build.py
```

Esto regenera `firmware/seguidor_web/web_ui.h` y el render `docs/ui_preview.html`. No edites `web_ui.h` a mano.

---

## 8. Problemas comunes

| Síntoma | Qué hacer |
|---|---|
| No aparece la red MiRobot | revisa el Monitor Serie; la clave debe tener al menos 8 caracteres |
| `mirobot.com` no abre | ver la nota de la sección 2; usa `http://192.168.4.1` |
| El panel dice "Sin conexión" | el teléfono se desconectó de MiRobot o se alejó demasiado |
| No puedo conectar un segundo dispositivo | es intencional: `AP_MAX_CLIENTS = 1`. Súbelo si lo necesitas. |
| Se activa "obstáculo" sin nada al frente | ajusta el potenciómetro del NEAR o pon `USE_NEAR = false` |
| El ESP32 se reinicia al arrancar los motores | el WiFi consume más energía: revisa la batería, la tierra común y agrega un condensador de 470 µF entre 5V y GND |
| Error de compilación por `ledcAttach` | el código ya soporta el core 2.x y 3.x; actualiza el soporte ESP32 si persiste |
