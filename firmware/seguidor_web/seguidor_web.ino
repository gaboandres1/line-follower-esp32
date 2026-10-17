/*
 * SEGUIDOR DE LÍNEA PID + PANEL WEB — ESP32 + BFD-1000 + L298N
 * -------------------------------------------------------------
 * - El ESP32 crea su propia red WiFi (punto de acceso) con DHCP en una red /25.
 * - Un servidor DNS propio hace que http://mirobot.com apunte al robot.
 * - Panel web en  /         : arrancar/detener, sliders PID, alertas CLP/NEAR.
 * - Métricas en   /metrics  : estado técnico en vivo (y JSON en /api/metrics).
 * - Los parámetros se guardan en flash (NVS) con espera anti-sobreescritura.
 *
 * Solo usa librerías incluidas en el soporte ESP32 de Arduino IDE
 * (WiFi, WebServer, DNSServer, Preferences). No hay que instalar nada.
 * La interfaz está en web_ui.h (en la misma carpeta, se abre como pestaña).
 */

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "web_ui.h"

#define FW_VERSION "web-1.0"

// =============================================================================
// 0) RED
// =============================================================================
const char* AP_SSID     = "MiRobot";      // nombre de la red WiFi del robot
const char* AP_PASS     = "robot1234";    // mínimo 8 caracteres
const int   AP_CHANNEL  = 6;              // canal WiFi (1-13)
const int   AP_MAX_CLIENTS = 1;           // cuántos dispositivos pueden conectarse a la vez
const IPAddress AP_IP   (192, 168, 4, 1);         // IP del robot (y puerta de enlace)
const IPAddress AP_MASK (255, 255, 255, 128);     // /25: hosts .1 a .126
const char* DNS_NAME    = "mirobot.com";  // nombre que se escribe en el navegador
const bool  DNS_CATCH_ALL = false;        // true: CUALQUIER nombre apunta al robot (portal cautivo)
const char* ROBOT_NAME  = "mirobot";      // nombre que muestra el panel

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
const unsigned long LOST_TIMEOUT_MS = 1000;   // tras este tiempo sin línea: busca (o se detiene)
const bool  SEARCH_ON_LOST  = true;   // true: al perder la línea gira en el lugar hasta encontrarla; false: se detiene
const int   SEARCH_SPEED    = 120;    // PWM del giro de búsqueda (también lo limita la velocidad máxima)
const unsigned long SEARCH_TIMEOUT_MS = 0;  // tiempo máximo buscando antes de detenerse; 0 = sin límite
const bool  USE_BUMPER      = true;   // CLP detiene el robot (emergencia)
const bool  USE_NEAR        = true;   // NEAR detiene el robot (emergencia)
const int   INPUT_FILTER_N  = 3;      // filtro de CLP/NEAR: lecturas activas netas para alarmar (3 x 10 ms)

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
volatile bool searching = false;      // girando en el lugar para reencontrar la línea
volatile int  alarmState = AL_NONE;   // (no se llama "alarm": choca con una función del sistema)

// Métricas: las escribe la tarea de control y las lee el servidor web
struct Metrics {
  uint8_t raw, on; int count;
  float err, p, i, d, u; int l, r;
  int clp, nearV;
  float loopHz; uint32_t execUs, execMaxUs;
  uint32_t lostEvents, searches, alarms, runMs;
} M;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

Preferences prefs;
WebServer server(80);
DNSServer dns;

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
    if (now == LOW) {
      if (running) reqStop = true;
      else if (alarmState == AL_CLP || alarmState == AL_NEAR) alarmState = AL_NONE;  // 1ª pulsación: confirma la emergencia
      else reqStart = true;
    }
  }
}

// Filtro integrador para entradas digitales con ruido (CLP, NEAR): el acumulador
// sube con cada lectura activa y baja con cada inactiva. Solo da "activo" al
// llegar a INPUT_FILTER_N, así un pico aislado o un rebote no disparan la alarma.
bool filterInput(uint8_t& acc, bool active) {
  if (active) { if (acc < INPUT_FILTER_N) acc++; }
  else if (acc > 0) acc--;
  return acc >= INPUT_FILTER_N;
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
  static uint8_t clpAcc = 0, nearAcc = 0;        // se filtran siempre, para que estén al día al arrancar
  bool clpHit  = filterInput(clpAcc,  clp == CLP_ACTIVE_VALUE);
  bool nearHit = filterInput(nearAcc, nearV == NEAR_ACTIVE_VALUE);
  static bool wasLost = false;

  if (running) {
    if (USE_BUMPER && clpHit)       raiseAlarm(AL_CLP);
    else if (USE_NEAR && nearHit)   raiseAlarm(AL_NEAR);
    else if (count == 0 && now - lastSeen > LOST_TIMEOUT_MS) {
      // Sin línea más de LOST_TIMEOUT_MS: gira en el lugar hacia donde la vio por
      // última vez (el signo de e) hasta encontrarla, o se detiene si no hay búsqueda.
      bool expired = SEARCH_TIMEOUT_MS && now - lastSeen > LOST_TIMEOUT_MS + SEARCH_TIMEOUT_MS;
      if (!SEARCH_ON_LOST || expired) raiseAlarm(AL_LOST);
      else {
        if (!searching) { searching = true; portENTER_CRITICAL(&mux); M.searches++; portEXIT_CRITICAL(&mux); }
        int s = constrain(SEARCH_SPEED, 0, maxs), back = ALLOW_REVERSE ? -s : 0;
        l = (e > 0) ? s : back;                  // e > 0: la línea quedó a la derecha → gira a la derecha
        r = (e > 0) ? back : s;
        driveChannel(LEFT_CHANNEL,  l, LEFT_INVERT);
        driveChannel(RIGHT_CHANNEL, r, RIGHT_INVERT);
      }
    }
    else {
      if (count > 0) lastSeen = now;
      if (searching) { searching = false; integral = 0; lastError = e; }   // la encontró: retoma el PID sin saltos
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
  if (!running) searching = false;
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

// Deja cada valor dentro de sus límites; si no es un número (NaN/inf) usa el de fábrica.
// Devuelve true si tuvo que corregir algo.
bool sanitize(Params& p) {
  Params d, o = p; setDefaults(d);
  p.kp   = isfinite(p.kp) ? constrain(p.kp, 0.0f, KP_LIMIT) : d.kp;
  p.ki   = isfinite(p.ki) ? constrain(p.ki, 0.0f, KI_LIMIT) : d.ki;
  p.kd   = isfinite(p.kd) ? constrain(p.kd, 0.0f, KD_LIMIT) : d.kd;
  p.base = constrain(p.base, 0, 255);
  p.maxs = constrain(p.maxs, 0, 255);
  p.line = (p.line == LINE_WHITE) ? LINE_WHITE : LINE_BLACK;
  return !(p.kp == o.kp && p.ki == o.ki && p.kd == o.kd &&
           p.base == o.base && p.maxs == o.maxs && p.line == o.line);
}

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
  Serial.printf("Guardado en flash (%d valores cambiados, %lu guardados en total)\n", n, (unsigned long)saveCount);
}

void markDirty() {
  dirty = !(P.kp == saved.kp && P.ki == saved.ki && P.kd == saved.kd &&
            P.base == saved.base && P.maxs == saved.maxs && P.line == saved.line);
  lastChange = millis();
}

long saveInMs() {
  if (!dirty) return 0;
  if (running) return -1;                        // corriendo: no se escribe la flash (se guarda al detener)
  unsigned long now = millis();
  long a = (long)SAVE_DELAY_MS - (long)(now - lastChange);
  long b = savedThisSession ? (long)SAVE_MIN_GAP_MS - (long)(now - lastSave) : 0;
  return max(0L, max(a, b));
}

// La escritura en flash pausa ambos núcleos unos ms: solo se hace con el robot detenido.
void maybeSave() { if (dirty && !running && saveInMs() == 0) saveParams(); }

// ---------------- Servidor web ----------------
Metrics snapshot() { Metrics m; portENTER_CRITICAL(&mux); m = M; portEXIT_CRITICAL(&mux); return m; }

void bits(uint8_t v, char* out) { for (int i = 0; i < 5; i++) out[i] = (v >> (4 - i)) & 1 ? '1' : '0'; out[5] = 0; }

// Copia s escapando " y \ (y quitando caracteres de control) para que no rompan el JSON
const char* jsonEsc(const char* s, char* out, size_t n) {
  size_t j = 0;
  for (; *s && j + 2 < n; s++) {
    if ((uint8_t)*s < 0x20) continue;
    if (*s == '"' || *s == '\\') out[j++] = '\\';
    out[j++] = *s;
  }
  out[j] = 0;
  return out;
}

void sendJson(const char* s) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

void handleState() {
  Metrics m = snapshot(); char on[6]; bits(m.on, on);
  char name[70], ssid[70]; jsonEsc(ROBOT_NAME, name, sizeof name); jsonEsc(AP_SSID, ssid, sizeof ssid);
  char buf[520];
  snprintf(buf, sizeof buf,
    "{\"name\":\"%s\",\"ssid\":\"%s\",\"run\":%d,\"search\":%d,\"alarm\":\"%s\",\"s\":\"%s\",\"e\":%.2f,\"l\":%d,\"r\":%d,"
    "\"kp\":%.2f,\"ki\":%.3f,\"kd\":%.1f,\"base\":%d,\"max\":%d,\"line\":%d,\"dirty\":%d,\"saveIn\":%ld}",
    name, ssid, running ? 1 : 0, searching ? 1 : 0, ALARM_NAME[alarmState], on, m.err, m.l, m.r,
    P.kp, P.ki, P.kd, P.base, P.maxs, P.line, dirty ? 1 : 0, saveInMs());
  sendJson(buf);
}

void handleMetrics() {
  Metrics m = snapshot(); char raw[6], on[6]; bits(m.raw, raw); bits(m.on, on);
  char ssid[70]; jsonEsc(AP_SSID, ssid, sizeof ssid);
  char buf[1024];
  snprintf(buf, sizeof buf,
    "{\"fw\":\"%s\",\"up\":%lu,\"run\":%d,\"search\":%d,\"alarm\":\"%s\",\"raw\":\"%s\",\"on\":\"%s\",\"count\":%d,"
    "\"err\":%.2f,\"p\":%.2f,\"i\":%.2f,\"d\":%.2f,\"u\":%.2f,\"l\":%d,\"r\":%d,\"clp\":%d,\"near\":%d,"
    "\"loop_hz\":%.1f,\"exec_us\":%u,\"exec_max_us\":%u,\"lost_events\":%u,\"searches\":%u,\"alarms\":%u,\"run_ms\":%u,"
    "\"kp\":%.2f,\"ki\":%.3f,\"kd\":%.1f,\"base\":%d,\"max\":%d,\"line\":%d,"
    "\"heap\":%u,\"heap_min\":%u,\"clients\":%d,\"ssid\":\"%s\",\"ip\":\"%s\",\"dns\":\"%s\","
    "\"saves\":%u,\"dirty\":%d,\"last_save_s\":%ld}",
    FW_VERSION, millis(), running ? 1 : 0, searching ? 1 : 0, ALARM_NAME[alarmState], raw, on, m.count,
    m.err, m.p, m.i, m.d, m.u, m.l, m.r, m.clp, m.nearV,
    m.loopHz, (unsigned)m.execUs, (unsigned)m.execMaxUs, (unsigned)m.lostEvents, (unsigned)m.searches, (unsigned)m.alarms, (unsigned)m.runMs,
    P.kp, P.ki, P.kd, P.base, P.maxs, P.line,
    (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), WiFi.softAPgetStationNum(),
    ssid, AP_IP.toString().c_str(), DNS_NAME,
    (unsigned)saveCount, dirty ? 1 : 0, savedThisSession ? (long)((millis() - lastSave) / 1000) : -1L);
  sendJson(buf);
}

void handleSet() {
  String k = server.arg("k");
  char vbuf[24]; strlcpy(vbuf, server.arg("v").c_str(), sizeof vbuf);
  char* end; float v = strtof(vbuf, &end);
  // rechaza vacío, texto sobrante y NaN/inf: un NaN haría girar los motores sin control
  if (end == vbuf || *end || !isfinite(v)) { server.send(400, "text/plain", "valor invalido"); return; }
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
  if (running) { markDirty(); handleState(); return; }   // corriendo: se guarda al detener
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

void setupNetwork() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
  WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, 0, AP_MAX_CLIENTS);
  WiFi.setSleep(false);                        // respuesta más rápida del panel
  dns.start(53, DNS_CATCH_ALL ? "*" : DNS_NAME, AP_IP);
  Serial.printf("WiFi \"%s\" listo. Abre http://%s  o  http://%s\n", AP_SSID, DNS_NAME, AP_IP.toString().c_str());
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
  if (sanitize(P)) { Serial.println("Había valores guardados inválidos: corregidos."); saveParams(); }
  Serial.printf("Parámetros: Kp=%.2f Ki=%.3f Kd=%.1f base=%d max=%d linea=%s\n",
                P.kp, P.ki, P.kd, P.base, P.maxs, P.line ? "blanca" : "negra");
  setupNetwork();
  setupServer();
  // Control en el núcleo 1 con prioridad alta: el WiFi y la web no lo frenan.
  xTaskCreatePinnedToCore(controlTask, "control", 4096, nullptr, 3, nullptr, 1);
}

void loop() {
  dns.processNextRequest();
  server.handleClient();
  maybeSave();
  delay(2);
}
