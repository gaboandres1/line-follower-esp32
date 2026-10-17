# `seguidor_web` — PID + panel web (punto de acceso)

El mismo seguidor PID, pero el ESP32 **crea su propia red WiFi** y sirve un panel web para controlarlo desde el celular.

## Uso

1. Pega las constantes de hardware del [`testbench`](../testbench) en la sección **1) CONSTANTES DE HARDWARE**.
2. Sube el sketch. `web_ui.h` va en la misma carpeta y se abre como pestaña. No hay librerías que instalar.
3. Conéctate a la red **MiRobot** (clave `robot1234`) y abre **`http://mirobot.com`** o `http://192.168.4.1`.

## Qué incluye

- Punto de acceso con DHCP en una red /25, un solo cliente a la vez y DNS propio (`mirobot.com`).
- Panel en `/`: arrancar/detener, sliders de velocidad y PID, color de línea y alertas.
- Métricas en `/metrics` y JSON en `/api/metrics`.
- Control PID en una tarea de FreeRTOS fijada al núcleo 1, con periodo exacto de 10 ms.
- **Línea perdida:** pasado `LOST_TIMEOUT_MS` gira en el lugar buscándola y retoma el PID al encontrarla. El panel muestra *Buscando la línea* (`SEARCH_ON_LOST`, `SEARCH_SPEED`, `SEARCH_TIMEOUT_MS`).
- Alertas de **choque (CLP)** y **obstáculo (NEAR)** con filtro integrador (`INPUT_FILTER_N`). Detienen el robot y bloquean el arranque hasta confirmarlas con *Entendido* o con **BOOT**.
- Parámetros guardados en la flash (NVS): 3 s después del último cambio, como mucho cada 10 s y **solo con el robot detenido**.
- La API rechaza valores que no sean números (`nan`, `inf`, texto) y, al encender, corrige lo guardado si quedó inválido.

Configuración de red, memoria, API y problemas comunes: [docs/panel_web.md](../../docs/panel_web.md).

> La interfaz se edita en [`web/`](../../web) y se regenera con `python3 web/build.py`. No edites `web_ui.h` a mano.
