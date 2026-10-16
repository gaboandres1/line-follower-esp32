/*
 * SEGUIDOR DE LÍNEA PID + PANEL WEB (CLIENTE WIFI) — ESP32 + BFD-1000 + L298N
 * ---------------------------------------------------------------------------
 * - El ESP32 se conecta a una red WiFi existente (modo estación), sin DNS.
 * - IP por DHCP (se imprime en el Monitor Serie) o IP fija, según USE_STATIC_IP.
 * - Panel web en  http://<IP>/         : arrancar/detener, sliders PID, alertas.
 * - Métricas en   http://<IP>/metrics  : estado técnico en vivo (JSON en /api/metrics).
 * - Los parámetros se guardan en flash (NVS) con espera anti-sobreescritura.
 * - Si se cae el WiFi, reconecta solo. Enter en el Monitor Serie reimprime la IP.
 *
 * Solo usa librerías incluidas en el soporte ESP32 de Arduino IDE
 * (WiFi, WebServer, Preferences). No hay que instalar nada.
 * La interfaz está en web_ui.h (en la misma carpeta, se abre como pestaña).
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "web_ui.h"

#define FW_VERSION "wifi-1.0"

// =============================================================================
// 0) RED
// =============================================================================
const char* WIFI_SSID   = "MiRedWiFi";    // red WiFi a la que se conecta el robot (2.4 GHz)
const char* WIFI_PASS   = "miclave123";   // su clave ("" si es abierta)

// IP: false = DHCP (el router la asigna y se imprime por el Monitor Serie)
//     true  = IP fija (usa los valores de abajo; deben ser de la red del router)
const bool  USE_STATIC_IP = false;
const IPAddress STATIC_IP   (192, 168, 1, 50);    // IP del robot (que no la use otro equipo)
const IPAddress STATIC_GW   (192, 168, 1, 1);     // puerta de enlace (el router)
const IPAddress STATIC_MASK (255, 255, 255, 0);   // máscara de la red

const unsigned long WIFI_CONNECT_WAIT_MS = 15000; // cuánto espera en setup() antes de seguir sin red
const bool  STOP_ON_WIFI_LOSS = true;     // true: si se cae el WiFi con el robot corriendo, se detiene
const char* ROBOT_NAME  = "mirobot";      // nombre del panel y del equipo en la lista del router

// =============================================================================
// 1) CONSTANTES DE HARDWARE  (copia aquí lo que imprime testbench.ino con 'p')
// =============================================================================
const int SENSOR_PINS[5] = {21, 19, 18, 17, 16};  // S1..S5 (U1,U2,U4,U5,U6). S3 = centro
#define CLP_PIN   22
#define NEAR_PIN  23

#define BLACK_VALUE        1      // lectura cruda sobre NEGRO (0 o 1)
#define S1_IS_LEFT         true   // S1 a la izquierda del robot (mirando al frente)
#define CLP_ACTIVE_VALUE   0      // lectura de CLP al presionarlo
#define NEAR_ACTIVE_VALUE  0      // lectura de NEAR con obstáculo

#define CH_A 0
#define CH_B 1
const int EN_PIN[2]  = {25, 14};  // ENA, ENB (PWM)
const int INA_PIN[2] = {26, 33};  // IN1, IN3
const int INB_PIN[2] = {27, 13};  // IN2, IN4
#define LEFT_CHANNEL   CH_A
#define LEFT_INVERT    false
#define RIGHT_INVERT   false

#define START_BUTTON  0           // botón BOOT: también arranca/detiene
#define LED_PIN       2
#define PWM_FREQ      1000
#define PWM_BITS      8

// =============================================================================
// 2) CONSTANTES LÓGICAS
// =============================================================================
#define LINE_BLACK 0
#define LINE_WHITE 1

// Valores por defecto (los que se usan la primera vez o al "Restaurar valores")
const float DEF_KP = 45.0, DEF_KI = 0.0, DEF_KD = 120.0;
const int   DEF_BASE = 140, DEF_MAX = 210, DEF_LINE = LINE_BLACK;

// Límites de los sliders (el robot también los aplica, por seguridad)
const float KP_LIMIT = 150, KI_LIMIT = 5, KD_LIMIT = 400;

const bool  ALLOW_REVERSE   = true;   // una rueda puede ir hacia atrás en curvas cerradas
const float INTEGRAL_LIMIT  = 50.0;   // anti-windup
const unsigned long LOOP_MS = 10;     // periodo del control (ms)
const float LOST_ERROR      = 3.0;
const unsigned long LOST_TIMEOUT_MS = 1000;
const bool  USE_BUMPER      = true;   // CLP detiene el robot (emergencia)
const bool  USE_NEAR        = true;   // NEAR detiene el robot (emergencia)

// =============================================================================
// 3) MEMORIA (anti-sobreescritura de la flash)
// =============================================================================
// Un cambio desde la web se aplica AL INSTANTE en RAM, pero solo se escribe en
// flash cuando pasan SAVE_DELAY_MS sin cambios nuevos, y nunca más seguido que
// SAVE_MIN_GAP_MS. Además, solo se escriben las claves cuyo valor cambió.
const unsigned long SAVE_DELAY_MS   = 3000;
const unsigned long SAVE_MIN_GAP_MS = 10000;
const uint8_t CONFIG_VERSION = 1;     // súbelo si cambias el significado de algún valor guardado

// =============================================================================
//                         A PARTIR DE AQUÍ NO HACE FALTA TOCAR
// =============================================================================
#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 3
#endif

const int RIGHT_CHANNEL = 1 - LEFT_CHANNEL;
const float WEIGHTS[5] = {-2, -1, 0, 1, 2};

struct Params { float kp, ki, kd; int base, maxs, line; };
Params P, saved;                       // P = en uso (RAM); saved = lo que hay en flash

enum Alarm { AL_NONE, AL_CLP, AL_NEAR, AL_LOST };
const char* ALARM_NAME[] = {"none", "clp", "near", "lost"};

volatile bool running = false, reqStart = false, reqStop = false;
volatile int  alarmState = AL_NONE;   // (no se llama "alarm": choca con una función del sistema)

// Métricas: las escribe la tarea de control y las lee el servidor web
struct Metrics {
  uint8_t raw, on; int count;
  float err, p, i, d, u; int l, r;
  int clp, nearV;
  float loopHz; uint32_t execUs, execMaxUs;
  uint32_t lostEvents, alarms, runMs;
} M;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

Preferences prefs;
WebServer server(80);
volatile bool wifiUp = false, wifiLost = false;

bool dirty = false;
unsigned long lastChange = 0, lastSave = 0;
bool savedThisSession = false;
uint32_t saveCount = 0;

// ---------------- PWM compatible con core 2.x y 3.x ----------------
void pwmInit(int ch) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(EN_PIN[ch], PWM_FREQ, PWM_BITS);
#else
  ledcSetup(ch, PWM_FREQ, PWM_BITS);
  ledcAttachPin(EN_PIN[ch], ch);
#endif
}
void pwmWrite(int ch, int duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(EN_PIN[ch], duty);
#else
  ledcWrite(ch, duty);
#endif
}

// ---------------- Motores ----------------
void driveChannel(int ch, int speed, bool invert) {
  if (invert) speed = -speed;
  digitalWrite(INA_PIN[ch], speed > 0 ? HIGH : LOW);
  digitalWrite(INB_PIN[ch], speed < 0 ? HIGH : LOW);
  pwmWrite(ch, abs(speed));
}
int saturate(int v, int maxs) { return constrain(v, ALLOW_REVERSE ? -maxs : 0, maxs); }
void stopMotors() { driveChannel(CH_A, 0, false); driveChannel(CH_B, 0, false); }

// ---------------- Sensores ----------------
// onLine[] queda ordenado de IZQUIERDA a DERECHA. raw = bits crudos S1..S5.
int readLine(bool onLine[5], uint8_t& raw, uint8_t& onBits, int lineColor) {
  int count = 0; raw = 0; onBits = 0;
  for (int i = 0; i < 5; i++) if (digitalRead(SENSOR_PINS[i])) raw |= (1 << (4 - i));
  for (int i = 0; i < 5; i++) {
    int s = S1_IS_LEFT ? i : 4 - i;
    bool isBlack = (((raw >> (4 - s)) & 1) == BLACK_VALUE);
    onLine[i] = (lineColor == LINE_BLACK) ? isBlack : !isBlack;
    if (onLine[i]) { count++; onBits |= (1 << (4 - i)); }
  }
  return count;
}

float lastError = 0, integral = 0;
float computeError(const bool onLine[5], int count) {
  if (count == 0) return (lastError >= 0) ? LOST_ERROR : -LOST_ERROR;
  if (count == 5) return 0;
  float sum = 0;
  for (int i = 0; i < 5; i++) if (onLine[i]) sum += WEIGHTS[i];
  return sum / count;
}

// ---------------- Tarea de control (núcleo 1, periodo fijo) ----------------
unsigned long lastSeen = 0, runSince = 0;

void raiseAlarm(int a) {
  running = false; stopMotors(); alarmState = a;
  portENTER_CRITICAL(&mux); M.alarms++; portEXIT_CRITICAL(&mux);
}

void checkButton() {
  static bool prev = HIGH; static unsigned long t = 0;
  bool now = digitalRead(START_BUTTON);
  if (now != prev && millis() - t > 50) {
    t = millis(); prev = now;
    if (now == LOW) { if (running) reqStop = true; else reqStart = true; }
  }
}

void controlStep() {
  unsigned long now = millis();
  checkButton();
  Params p = P;                                  // copia local, consistente en este ciclo
  int maxs = constrain(p.maxs, 0, 255);
  int base = min(p.base, maxs);

  bool onLine[5]; uint8_t raw, onBits;
  int count = readLine(onLine, raw, onBits, p.line);
  float e = computeError(onLine, count);

  if (reqStop) { reqStop = false; running = false; stopMotors(); }
  if (reqStart) {
    reqStart = false;
    if (alarmState == AL_NONE || alarmState == AL_LOST) {
      alarmState = AL_NONE; running = true;
      integral = 0; lastError = e; lastSeen = now; runSince = now;
    }
  }

  float tp = 0, ti = 0, td = 0, u = 0; int l = 0, r = 0;
  int clp = digitalRead(CLP_PIN), nearV = digitalRead(NEAR_PIN);
  static bool wasLost = false;

  if (running) {
    if (USE_BUMPER && clp == CLP_ACTIVE_VALUE)       raiseAlarm(AL_CLP);
    else if (USE_NEAR && nearV == NEAR_ACTIVE_VALUE)  raiseAlarm(AL_NEAR);
    else if (count == 0 && now - lastSeen > LOST_TIMEOUT_MS) raiseAlarm(AL_LOST);
    else {
      if (count > 0) lastSeen = now;
      if (count == 0 && !wasLost) { portENTER_CRITICAL(&mux); M.lostEvents++; portEXIT_CRITICAL(&mux); }
      integral = constrain(integral + e, -INTEGRAL_LIMIT, INTEGRAL_LIMIT);
      tp = p.kp * e; ti = p.ki * integral; td = p.kd * (e - lastError);
      u = tp + ti + td;
      l = saturate(base + (int)u, maxs);
      r = saturate(base - (int)u, maxs);
      driveChannel(LEFT_CHANNEL,  l, LEFT_INVERT);
      driveChannel(RIGHT_CHANNEL, r, RIGHT_INVERT);
    }
  }
  wasLost = (count == 0);
  lastError = e;
  digitalWrite(LED_PIN, running);

  portENTER_CRITICAL(&mux);
  M.raw = raw; M.on = onBits; M.count = count; M.err = e;
  M.p = tp; M.i = ti; M.d = td; M.u = u; M.l = l; M.r = r; M.clp = clp; M.nearV = nearV;
  if (running) M.runMs += LOOP_MS;
  portEXIT_CRITICAL(&mux);
}

void controlTask(void*) {
  TickType_t last = xTaskGetTickCount();
  uint32_t n = 0, maxUs = 0; unsigned long tWin = millis();
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(LOOP_MS));
    uint32_t t0 = micros();
    controlStep();
    uint32_t us = micros() - t0;
    n++; if (us > maxUs) maxUs = us;
    unsigned long now = millis();
    if (now - tWin >= 1000) {                    // ventana de 1 s para Hz y máximo
      portENTER_CRITICAL(&mux);
      M.loopHz = n * 1000.0f / (now - tWin); M.execMaxUs = maxUs;
      portEXIT_CRITICAL(&mux);
      n = 0; maxUs = 0; tWin = now;
    }
    portENTER_CRITICAL(&mux); M.execUs = us; portEXIT_CRITICAL(&mux);
  }
}

// ---------------- Memoria (NVS) ----------------
void setDefaults(Params& p) { p = {DEF_KP, DEF_KI, DEF_KD, DEF_BASE, DEF_MAX, DEF_LINE}; }

void loadParams() {
  prefs.begin("robot", false);
  if (prefs.getUChar("ver", 0) != CONFIG_VERSION) {   // primera vez o versión distinta
    prefs.clear();
    prefs.putUChar("ver", CONFIG_VERSION);
  }
  P.kp   = prefs.getFloat("kp",  DEF_KP);
  P.ki   = prefs.getFloat("ki",  DEF_KI);
  P.kd   = prefs.getFloat("kd",  DEF_KD);
  P.base = prefs.getInt("base",  DEF_BASE);
  P.maxs = prefs.getInt("max",   DEF_MAX);
  P.line = prefs.getInt("line",  DEF_LINE);
  saveCount = prefs.getUInt("saves", 0);
  prefs.end();
  saved = P;
}

void saveParams() {
  prefs.begin("robot", false);
  int n = 0;
  if (P.kp   != saved.kp)   { prefs.putFloat("kp", P.kp);   n++; }
  if (P.ki   != saved.ki)   { prefs.putFloat("ki", P.ki);   n++; }
  if (P.kd   != saved.kd)   { prefs.putFloat("kd", P.kd);   n++; }
  if (P.base != saved.base) { prefs.putInt("base", P.base); n++; }
  if (P.maxs != saved.maxs) { prefs.putInt("max", P.maxs);  n++; }
  if (P.line != saved.line) { prefs.putInt("line", P.line); n++; }
  if (n) { saveCount++; prefs.putUInt("saves", saveCount); }
  prefs.end();
  saved = P; dirty = false; lastSave = millis(); savedThisSession = true;
  Serial.printf("Guardado en flash (%d valores cambiados, %u guardados en total)\n", n, saveCount);
}

void markDirty() {
  dirty = !(P.kp == saved.kp && P.ki == saved.ki && P.kd == saved.kd &&
            P.base == saved.base && P.maxs == saved.maxs && P.line == saved.line);
  lastChange = millis();
}

long saveInMs() {
  if (!dirty) return 0;
  unsigned long now = millis();
  long a = (long)SAVE_DELAY_MS - (long)(now - lastChange);
  long b = savedThisSession ? (long)SAVE_MIN_GAP_MS - (long)(now - lastSave) : 0;
  return max(0L, max(a, b));
}

void maybeSave() { if (dirty && saveInMs() == 0) saveParams(); }

// ---------------- Servidor web ----------------
Metrics snapshot() { Metrics m; portENTER_CRITICAL(&mux); m = M; portEXIT_CRITICAL(&mux); return m; }

void bits(uint8_t v, char* out) { for (int i = 0; i < 5; i++) out[i] = (v >> (4 - i)) & 1 ? '1' : '0'; out[5] = 0; }

void sendJson(const char* s) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

void handleState() {
  Metrics m = snapshot(); char on[6]; bits(m.on, on);
  char buf[420];
  snprintf(buf, sizeof buf,
    "{\"name\":\"%s\",\"ssid\":\"%s\",\"run\":%d,\"alarm\":\"%s\",\"s\":\"%s\",\"e\":%.2f,\"l\":%d,\"r\":%d,"
    "\"kp\":%.2f,\"ki\":%.3f,\"kd\":%.1f,\"base\":%d,\"max\":%d,\"line\":%d,\"dirty\":%d,\"saveIn\":%ld}",
    ROBOT_NAME, WIFI_SSID, running ? 1 : 0, ALARM_NAME[alarmState], on, m.err, m.l, m.r,
    P.kp, P.ki, P.kd, P.base, P.maxs, P.line, dirty ? 1 : 0, saveInMs());
  sendJson(buf);
}

void handleMetrics() {
  Metrics m = snapshot(); char raw[6], on[6]; bits(m.raw, raw); bits(m.on, on);
  char buf[900];
  snprintf(buf, sizeof buf,
    "{\"fw\":\"%s\",\"up\":%lu,\"run\":%d,\"alarm\":\"%s\",\"raw\":\"%s\",\"on\":\"%s\",\"count\":%d,"
    "\"err\":%.2f,\"p\":%.2f,\"i\":%.2f,\"d\":%.2f,\"u\":%.2f,\"l\":%d,\"r\":%d,\"clp\":%d,\"near\":%d,"
    "\"loop_hz\":%.1f,\"exec_us\":%u,\"exec_max_us\":%u,\"lost_events\":%u,\"alarms\":%u,\"run_ms\":%u,"
    "\"kp\":%.2f,\"ki\":%.3f,\"kd\":%.1f,\"base\":%d,\"max\":%d,\"line\":%d,"
    "\"heap\":%u,\"heap_min\":%u,\"rssi\":%d,\"ssid\":\"%s\",\"ip\":\"%s\",\"ipmode\":\"%s\","
    "\"saves\":%u,\"dirty\":%d,\"last_save_s\":%ld}",
    FW_VERSION, millis(), running ? 1 : 0, ALARM_NAME[alarmState], raw, on, m.count,
    m.err, m.p, m.i, m.d, m.u, m.l, m.r, m.clp, m.nearV,
    m.loopHz, (unsigned)m.execUs, (unsigned)m.execMaxUs, (unsigned)m.lostEvents, (unsigned)m.alarms, (unsigned)m.runMs,
    P.kp, P.ki, P.kd, P.base, P.maxs, P.line,
    (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (int)WiFi.RSSI(),
    WIFI_SSID, WiFi.localIP().toString().c_str(), USE_STATIC_IP ? "fija" : "DHCP",
    (unsigned)saveCount, dirty ? 1 : 0, savedThisSession ? (long)((millis() - lastSave) / 1000) : -1L);
  sendJson(buf);
}

void handleSet() {
  String k = server.arg("k");
  float v = server.arg("v").toFloat();
  if      (k == "kp")   P.kp   = constrain(v, 0.0f, KP_LIMIT);
  else if (k == "ki")   P.ki   = constrain(v, 0.0f, KI_LIMIT);
  else if (k == "kd")   P.kd   = constrain(v, 0.0f, KD_LIMIT);
  else if (k == "base") P.base = constrain((int)v, 0, 255);
  else if (k == "max")  P.maxs = constrain((int)v, 0, 255);
  else if (k == "line") P.line = ((int)v == LINE_WHITE) ? LINE_WHITE : LINE_BLACK;
  else { server.send(400, "text/plain", "parametro desconocido"); return; }
  markDirty();
  handleState();
}

void handleRun() {
  if (server.arg("on") == "1") reqStart = true; else reqStop = true;
  delay(LOOP_MS * 2);                          // deja que la tarea de control lo aplique
  handleState();
}

void handleAck() { if (!running) alarmState = AL_NONE; handleState(); }

void handleReset() {
  setDefaults(P);
  prefs.begin("robot", false);
  prefs.clear();
  prefs.putUChar("ver", CONFIG_VERSION);
  saveCount++; prefs.putUInt("saves", saveCount);
  prefs.end();
  saved = P; dirty = false; lastSave = millis(); savedThisSession = true;
  handleState();
}

void setupServer() {
  server.on("/",            HTTP_GET,  [] { server.send(200, "text/html", PAGE_PANEL); });
  server.on("/metrics",     HTTP_GET,  [] { server.send(200, "text/html", PAGE_METRICS); });
  server.on("/api/state",   HTTP_GET,  handleState);
  server.on("/api/metrics", HTTP_GET,  handleMetrics);
  server.on("/api/set",     HTTP_POST, handleSet);
  server.on("/api/run",     HTTP_POST, handleRun);
  server.on("/api/ack",     HTTP_POST, handleAck);
  server.on("/api/reset",   HTTP_POST, handleReset);
  server.onNotFound([] {                       // cualquier otra ruta lleva al panel
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });
  server.begin();
}

void printIp() {
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("Conectado a \"%s\" (%d dBm). IP %s: %s  ->  abre http://%s\n",
                  WIFI_SSID, (int)WiFi.RSSI(), USE_STATIC_IP ? "fija" : "por DHCP",
                  WiFi.localIP().toString().c_str(), WiFi.localIP().toString().c_str());
  else
    Serial.printf("Sin conexión a \"%s\". Reintentando...\n", WIFI_SSID);
}

// Los eventos llegan desde la tarea del WiFi: aquí solo se marcan banderas,
// y loop() imprime (y detiene el robot si corresponde).
void onWifiEvent(WiFiEvent_t event) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) { wifiUp = true; }
  else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED || event == ARDUINO_EVENT_WIFI_STA_LOST_IP) {
    if (wifiUp) wifiLost = true;
    wifiUp = false;
  }
}

void setupNetwork() {
  WiFi.setHostname(ROBOT_NAME);                // así aparece en la lista de equipos del router
  WiFi.mode(WIFI_STA);
  if (USE_STATIC_IP && !WiFi.config(STATIC_IP, STATIC_GW, STATIC_MASK, STATIC_GW))
    Serial.println("No se pudo aplicar la IP fija; se usará DHCP.");
  WiFi.onEvent(onWifiEvent);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  WiFi.setSleep(false);                        // respuesta más rápida del panel

  Serial.printf("Conectando a \"%s\"", WIFI_SSID);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_CONNECT_WAIT_MS) { delay(250); Serial.print('.'); }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED)
    Serial.println("Aún sin WiFi: el robot funciona con el botón BOOT y sigue intentando conectar.");
}

// Imprime cambios de conexión y, con STOP_ON_WIFI_LOSS, detiene el robot si se cae la red.
void checkNetwork() {
  static bool shown = false;
  if (wifiUp && !shown) { shown = true; printIp(); }
  if (wifiLost) {
    wifiLost = false; shown = false;
    Serial.println("Se perdió el WiFi. Reconectando...");
    if (STOP_ON_WIFI_LOSS && running) { reqStop = true; Serial.println("Robot detenido por pérdida de WiFi."); }
  }
  // Enter (o cualquier texto) en el Monitor Serie vuelve a mostrar la IP
  if (Serial.available()) { while (Serial.available()) Serial.read(); printIp(); }
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 5; i++) pinMode(SENSOR_PINS[i], INPUT);
  pinMode(CLP_PIN, INPUT);
  pinMode(NEAR_PIN, INPUT);
  pinMode(START_BUTTON, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  for (int ch = CH_A; ch <= CH_B; ch++) {
    pinMode(INA_PIN[ch], OUTPUT);
    pinMode(INB_PIN[ch], OUTPUT);
    pwmInit(ch);
  }
  stopMotors();
  loadParams();
  Serial.printf("Parámetros: Kp=%.2f Ki=%.3f Kd=%.1f base=%d max=%d linea=%s\n",
                P.kp, P.ki, P.kd, P.base, P.maxs, P.line ? "blanca" : "negra");
  setupNetwork();
  setupServer();
  // Control en el núcleo 1 con prioridad alta: el WiFi y la web no lo frenan.
  xTaskCreatePinnedToCore(controlTask, "control", 4096, nullptr, 3, nullptr, 1);
}

void loop() {
  checkNetwork();
  server.handleClient();
  maybeSave();
  delay(2);
}
