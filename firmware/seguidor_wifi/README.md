# `seguidor_wifi` — PID + panel web (cliente WiFi)

Igual que [`seguidor_web`](../seguidor_web) (mismo PID, panel, métricas, memoria y API), pero el ESP32 **se conecta a una red WiFi existente**. No tiene DNS: el panel se abre por la IP del robot.

## Uso

1. Pega las constantes de hardware del [`testbench`](../testbench) en la sección **1) CONSTANTES DE HARDWARE**.
2. En la sección **0) RED** pon `WIFI_SSID` y `WIFI_PASS` (red de **2.4 GHz**) y elige la IP:
   - `USE_STATIC_IP = false` → **DHCP**: el router asigna la IP.
   - `USE_STATIC_IP = true` → **IP fija**: completa `STATIC_IP`, `STATIC_GW` y `STATIC_MASK`.
3. Sube el sketch y abre el Monitor Serie (115200):
   ```
   Conectado a "MiRedWiFi" (-58 dBm). IP por DHCP: 192.168.1.37  ->  abre http://192.168.1.37
   ```
4. Desde un equipo **en la misma red**, abre esa dirección.

## Red

- **Enter** en el Monitor Serie vuelve a imprimir la IP.
- Si se cae la red, reconecta solo. Si sigue sin red después de `WIFI_RETRY_MS` (30 s), fuerza un reintento.
- `STOP_ON_WIFI_LOSS = true`: si se pierde el WiFi con el robot corriendo, lo detiene.
- Sin WiFi el robot sigue funcionando con **BOOT**, que también confirma las emergencias.
- En el router aparece como `mirobot`, por si prefieres reservarle la IP ahí.

> Cualquiera conectado a esa red puede abrir el panel y mover el robot. Úsalo en una red de confianza.

Guía completa: [docs/panel_web.md](../../docs/panel_web.md#9-modo-cliente-wifi-firmwareseguidor_wifi).
