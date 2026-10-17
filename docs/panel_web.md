# Panel web — `firmware/seguidor_web` y `firmware/seguidor_wifi`

![Panel y métricas](img/panel_y_metricas.png)

Es el mismo seguidor PID de `firmware/seguidor_pid`, pero ahora el ESP32 crea su propia red WiFi y sirve un panel web para controlarlo desde el celular.

> Si prefieres que el robot se conecte a una red WiFi existente (DHCP o IP fija, sin DNS), usa `firmware/seguidor_wifi`: todo lo de las secciones 3 a 7 aplica igual, y la red se explica en la [sección 9](#9-modo-cliente-wifi-firmwareseguidor_wifi).

```
firmware/seguidor_web/
├── seguidor_web.ino   ← el programa (configuración arriba)
└── web_ui.h           ← la interfaz web ya empaquetada (se abre como pestaña)
firmware/seguidor_wifi/  ← misma estructura, versión cliente WiFi (sección 9)
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
| **Arrancar / Detener** | botón principal. El botón **BOOT** del ESP32 también arranca y detiene (y confirma las emergencias, ver abajo). |
| **Velocidad** | `Base` (en recta) y `Máxima` (saturación de cada motor) |
| **Control PID** | `Kp`, `Ki`, `Kd`. Los botones − / + mueven un paso, para ajustes finos con el dedo. |
| **Pista** | línea negra o blanca (la compuerta NOT de la lógica) |
| Pie | estado del guardado, **Restaurar valores** (pide tocar dos veces) y enlace a **Métricas** |

Los cambios se aplican **al instante** al robot, incluso mientras corre.

### Alertas

| Alerta | Cuándo | Qué pasa |
|---|---|---|
| **Emergencia: choque** (roja) | se presiona el CLP mientras corre | se detiene. **No se puede volver a arrancar** hasta confirmarla: tocar *Entendido* en el panel o presionar **BOOT** una vez. El celular vibra. |
| **Emergencia: obstáculo** (roja) | NEAR detecta algo mientras corre | igual que la anterior |
| **Buscando la línea** (ámbar) | más de `LOST_TIMEOUT_MS` sin ver la línea | gira en el lugar hacia donde la vio por última vez y retoma el PID al encontrarla. Es solo un aviso: desaparece solo y no hay que confirmarlo. |
| **Línea perdida** (ámbar) | la búsqueda está desactivada (`SEARCH_ON_LOST = false`) o se acabó `SEARCH_TIMEOUT_MS` | se detiene; se puede volver a arrancar directamente |

La búsqueda se configura con `SEARCH_ON_LOST`, `SEARCH_SPEED` y `SEARCH_TIMEOUT_MS` (por defecto busca sin límite de tiempo a PWM 120). Detalles en [calibracion_y_pid.md](calibracion_y_pid.md#si-pierde-la-línea).

Las emergencias se activan con `USE_BUMPER` y `USE_NEAR` (ambas en `true`). Si el NEAR se dispara solo, ajusta su potenciómetro, sube `INPUT_FILTER_N` o pon `USE_NEAR = false`.

**Con BOOT, después de una emergencia:** la primera pulsación la confirma (igual que *Entendido*) y la segunda arranca. Así el robot se puede recuperar sin celular, por ejemplo con `seguidor_wifi` si se cayó la red.

**Filtro:** CLP y NEAR pasan por un filtro integrador antes de disparar la alarma. Un acumulador sube con cada lectura activa y baja con cada inactiva, y la alarma salta al llegar a `INPUT_FILTER_N` (3 → 30 ms). Así un rebote o un pico de ruido no detienen el robot. Más detalles en [calibracion_y_pid.md](calibracion_y_pid.md#filtro-de-clp-y-near).

---

## 4. Memoria y protección de la flash

Los valores de `Kp`, `Ki`, `Kd`, `Base`, `Máxima` y el color de línea **se conservan al apagar el robot**. Se guardan en la memoria NVS del ESP32 con la librería `Preferences`.

Un slider puede mandar decenas de cambios por segundo, así que el guardado tiene cuatro protecciones:

1. **En la web:** el slider envía como máximo un valor cada 150 ms mientras lo arrastras, más el valor final al soltarlo.
2. **Espera tras el último cambio:** el robot usa el valor nuevo de inmediato (en RAM), pero solo lo escribe en flash cuando pasan **`SAVE_DELAY_MS` = 3 s** sin cambios nuevos.
3. **Intervalo mínimo:** nunca guarda dos veces en menos de **`SAVE_MIN_GAP_MS` = 10 s**.
4. **Solo con el robot detenido:** escribir la flash pausa los dos núcleos del ESP32 unos milisegundos, también la tarea del PID. Por eso, mientras el robot corre, los cambios se aplican pero **no se guardan**. El pie del panel dice *"Se guardará al detener el robot"* y el guardado se hace en cuanto se detiene.

Además, **solo escribe las claves cuyo valor cambió**, y no escribe nada si volviste al valor que ya estaba guardado.

En una simulación, 40 movimientos del slider en 2 segundos produjeron **un solo guardado**. El pie del panel muestra *"Guardando en 3 s…"* y luego *"✓ Valores guardados"*. En **Métricas** puedes ver cuántas veces se ha guardado en total.

Otros detalles:
- **Restaurar valores** vuelve a los `DEF_…` del código y borra lo guardado. Si el robot está corriendo, los valores cambian al instante y se guardan al detenerlo.
- **Valores inválidos:** el robot rechaza cualquier valor que no sea un número (vacío, texto, `nan`, `inf`). Al encender también revisa lo guardado: si algo está fuera de rango o no es un número, lo corrige y avisa por el Monitor Serie.
- **Subir el sketch otra vez no borra lo guardado.** Si cambias el significado de algún valor guardado, sube `CONFIG_VERSION` en 1: al arrancar, el robot borra los datos viejos y usa los valores por defecto.
- Si apagas el robot justo después de mover un slider (antes de que diga *Guardado*), ese último cambio se pierde.

---

## 5. Métricas (`/metrics`)

Página técnica que se actualiza cada 200 ms:

- **Gráfico** de los últimos 30 s: el error (verde, de −3 a +3) y la acción de control `u` (gris, dividida entre 100).
- **Control:** estado, error, los tres términos P, I y D por separado, `u`, PWM de cada rueda, constantes actuales, frecuencia real del lazo (debería ser ~100 Hz) y tiempo de cálculo por ciclo.
- **Sensores:** lectura cruda S1…S5, lo que interpreta como línea (de izquierda a derecha, ya con `S1_IS_LEFT`, `BLACK_VALUE` y `LINE_COLOR` aplicados), cuántos ven la línea, y CLP/NEAR en crudo.
- **Eventos:** alarma actual, alarmas totales, veces que perdió la línea, búsquedas de línea y tiempo corriendo. El estado dice *buscando la línea* mientras gira para encontrarla.
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

El robot vuelve a limitar todos los valores que recibe (`KP_LIMIT`, etc.), aunque alguien los mande por fuera del panel. Si `v` no es un número válido (vacío, texto, `nan`, `inf`), responde **400** y no cambia nada.

---

## 7. Cómo está organizado el programa

- **El control PID corre en una tarea propia** de FreeRTOS, fijada al núcleo 1 con prioridad alta y periodo exacto de 10 ms (`vTaskDelayUntil`). Así el WiFi y las páginas web no alteran el ritmo del control.
- **El servidor web, el DNS y el guardado** corren en `loop()`. El guardado espera a que el robot esté detenido, porque escribir la flash pausa ambos núcleos.
- Las métricas pasan de una tarea a otra en una copia protegida (`portENTER_CRITICAL`), para que nunca se lean a medio escribir.
- El panel no usa internet: no carga fuentes ni librerías externas, porque la red del robot no tiene salida.

### Modificar la interfaz

Edita los archivos de `web/` (`common.css`, `panel.html`, `metrics.html`) y ejecuta, desde la raíz del repo:

```bash
python3 web/build.py
```

Esto regenera `web_ui.h` en `firmware/seguidor_web` y `firmware/seguidor_wifi`, y el render `docs/ui_preview.html`. No edites `web_ui.h` a mano.

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

---

## 9. Modo cliente WiFi (`firmware/seguidor_wifi`)

Es el mismo programa (control PID, panel, métricas, memoria y API idénticos), pero en vez de crear su propia red **el ESP32 se conecta a una red WiFi existente**, por ejemplo la de tu casa o un router del laboratorio. No hay DNS: el panel se abre por la IP del robot.

### Configuración (sección **0) RED**)

| Constante | Valor | Qué hace |
|---|---|---|
| `WIFI_SSID` / `WIFI_PASS` | `MiRedWiFi` / `miclave123` | red a la que se conecta. Debe ser de **2.4 GHz** (el ESP32 no ve redes de 5 GHz). |
| `USE_STATIC_IP` | `false` | `false` = **DHCP**: el router asigna la IP. `true` = **IP fija**. |
| `STATIC_IP` | `192.168.1.50` | IP del robot (solo con IP fija). Que no la use otro equipo. |
| `STATIC_GW` | `192.168.1.1` | puerta de enlace: la IP del router |
| `STATIC_MASK` | `255.255.255.0` | máscara de la red |
| `WIFI_CONNECT_WAIT_MS` | `15000` | cuánto espera al encender antes de seguir sin red |
| `WIFI_RETRY_MS` | `30000` | si pasa este tiempo sin red, fuerza un reintento de conexión (respaldo de la reconexión automática) |
| `STOP_ON_WIFI_LOSS` | `true` | si se cae el WiFi con el robot corriendo, lo detiene |
| `ROBOT_NAME` | `mirobot` | nombre del panel y del equipo en la lista de clientes del router |

### Uso

1. Pon tu red en `WIFI_SSID` / `WIFI_PASS` y sube el sketch.
2. Abre el Monitor Serie (115200). Al conectarse imprime:
   ```
   Conectando a "MiRedWiFi"........
   Conectado a "MiRedWiFi" (-58 dBm). IP por DHCP: 192.168.1.37  ->  abre http://192.168.1.37
   ```
3. Desde cualquier equipo **conectado a la misma red**, abre `http://192.168.1.37`.

- **Enter en el Monitor Serie** vuelve a imprimir la IP en cualquier momento.
- Si se cae la red, el ESP32 **reconecta solo** y vuelve a imprimir la IP al recuperarla. Si a los `WIFI_RETRY_MS` (30 s) sigue sin red, fuerza un reintento.
- Sin WiFi el robot sigue funcionando con el botón **BOOT**, incluso para confirmar una emergencia.

### DHCP o IP fija

Con **DHCP** la IP puede cambiar cuando el robot se reconecta o el router se reinicia. Para tener siempre la misma:

- **IP fija en el robot:** `USE_STATIC_IP = true` y completa `STATIC_IP`, `STATIC_GW` y `STATIC_MASK` con los datos de tu red. Elige una IP **fuera del rango DHCP del router** para que no se la asigne a otro equipo.
- **Reserva DHCP en el router:** deja `USE_STATIC_IP = false` y reserva una IP para la MAC del robot. El router lo muestra como `mirobot`.

### Diferencias con `seguidor_web`

| | `seguidor_web` | `seguidor_wifi` |
|---|---|---|
| Red | la crea el robot | la del router |
| Dirección | `http://mirobot.com` o `192.168.4.1` | `http://<IP>` que imprime el Monitor Serie |
| DNS | sí | no |
| Clientes | 1 | cualquiera en la misma red |
| Métricas → Red | clientes conectados | intensidad de señal (dBm) y modo de IP (DHCP / fija) |

> **Ojo:** cualquiera conectado a esa red puede abrir el panel y mover el robot. Úsalo en una red de confianza.

### Problemas comunes

| Síntoma | Qué hacer |
|---|---|
| Se queda en `Conectando...` | revisa SSID y clave (distinguen mayúsculas), que la red sea de 2.4 GHz y que el robot esté en alcance |
| Con IP fija no conecta o no abre el panel | la IP, la puerta de enlace y la máscara deben ser de la red del router; prueba primero con DHCP para ver qué datos asigna |
| La IP cambió | es normal con DHCP: usa IP fija o una reserva en el router |
| El panel no abre desde otro equipo | debe estar en la **misma red**; algunas redes de invitados o universitarias aíslan a los clientes entre sí |

