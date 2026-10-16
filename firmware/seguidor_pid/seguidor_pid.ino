/*
 * SEGUIDOR DE LÍNEA PID — ESP32 + BFD-1000 (5 sensores digitales) + L298N
 * -----------------------------------------------------------------------
 * Todo lo configurable está en dos bloques:
 *   1) HARDWARE: cómo está armado TU robot (se obtiene con testbench.ino).
 *   2) LÓGICA:   cómo quieres que se comporte (velocidad, PID, color de línea).
 *
 * Arranque: el robot espera quieto. Presiona el botón BOOT del ESP32 para
 * arrancar y otra vez para detener. El LED azul (GPIO 2) encendido = corriendo.
 */

// =============================================================================
// 1) CONSTANTES DE HARDWARE  (copia aquí lo que imprime testbench.ino con 'p')
// =============================================================================

// --- Sensores BFD-1000 ---
const int SENSOR_PINS[5] = {21, 19, 18, 17, 16};  // S1..S5 (U1,U2,U4,U5,U6). S3 = centro
#define CLP_PIN   22          // interruptor de choque del frente
#define NEAR_PIN  23          // sensor de obstáculo IR

#define BLACK_VALUE  1        // lectura cruda sobre NEGRO (0 o 1). Blanco = el complemento
#define S1_IS_LEFT   true     // true: S1 queda a la IZQUIERDA del robot (mirando al frente)

#define CLP_ACTIVE_VALUE   0  // lectura de CLP cuando se presiona
#define NEAR_ACTIVE_VALUE  0  // lectura de NEAR cuando hay obstáculo

// --- Motores (L298N) ---
#define CH_A 0                // canal A: ENA, IN1, IN2 -> OUT1/OUT2
#define CH_B 1                // canal B: ENB, IN3, IN4 -> OUT3/OUT4
const int EN_PIN[2]  = {25, 14};   // ENA, ENB (PWM)
const int INA_PIN[2] = {26, 33};   // IN1, IN3
const int INB_PIN[2] = {27, 13};   // IN2, IN4

#define LEFT_CHANNEL  CH_A    // canal que mueve la rueda IZQUIERDA (la derecha es el otro)
#define LEFT_INVERT   false   // true si la rueda izquierda gira al revés
#define RIGHT_INVERT  false   // true si la rueda derecha gira al revés

// --- Otros ---
#define START_BUTTON  0       // botón BOOT del ESP32 (activo en LOW)
#define LED_PIN       2       // LED azul integrado
#define PWM_FREQ      1000    // Hz, adecuado para el L298N
#define PWM_BITS      8       // resolución: velocidades de 0 a 255

// =============================================================================
// 2) CONSTANTES LÓGICAS  (ajuste de comportamiento)
// =============================================================================

#define LINE_BLACK 0
#define LINE_WHITE 1
#define LINE_COLOR   LINE_BLACK   // LINE_BLACK: línea negra sobre blanco. LINE_WHITE: al revés

const int   BASE_SPEED   = 140;   // velocidad en recta (0-255)
const int   MAX_SPEED    = 210;   // saturación: ningún motor pasa de este valor
const bool  ALLOW_REVERSE = true; // true: en curvas cerradas una rueda puede ir hacia atrás

const float KP = 45.0;            // proporcional
const float KI = 0.0;             // integral (empieza en 0)
const float KD = 120.0;           // derivativo
const float INTEGRAL_LIMIT = 50.0;// anti-windup: tope del acumulado integral

const unsigned long LOOP_MS = 10; // periodo fijo del control (ms)
const float LOST_ERROR = 3.0;     // error que se asume al perder la línea (más que el extremo, 2.0)
const unsigned long LOST_TIMEOUT_MS = 1000;  // si pierde la línea más de esto, se detiene

const bool USE_BUMPER = false;    // true: se detiene si se presiona CLP
const bool USE_NEAR   = false;    // true: se detiene ante un obstáculo
const bool DEBUG_SERIAL = true;   // imprime telemetría por el Monitor Serie
const int  DEBUG_EVERY  = 10;     // imprime 1 de cada N ciclos (10 x 10 ms = cada 100 ms)

// =============================================================================
//                         A PARTIR DE AQUÍ NO HACE FALTA TOCAR
// =============================================================================

#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 3
#endif

const int RIGHT_CHANNEL = 1 - LEFT_CHANNEL;
const float WEIGHTS[5] = {-2, -1, 0, 1, 2};   // posición de cada sensor, de izquierda a derecha

bool  running = false;
bool  justStarted = false;           // evita el salto del derivativo en el primer ciclo
float lastError = 0, integral = 0;
unsigned long lastLoop = 0, lastSeen = 0;
int debugCount = 0;

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
// speed: -255..255, positivo = adelante (ya aplica la inversión configurada)
void driveChannel(int ch, int speed, bool invert) {
  if (invert) speed = -speed;
  digitalWrite(INA_PIN[ch], speed > 0 ? HIGH : LOW);
  digitalWrite(INB_PIN[ch], speed < 0 ? HIGH : LOW);
  pwmWrite(ch, abs(speed));
}

int saturate(int v) {
  int lo = ALLOW_REVERSE ? -MAX_SPEED : 0;
  return constrain(v, lo, MAX_SPEED);
}

void setMotors(int left, int right) {
  driveChannel(LEFT_CHANNEL,  saturate(left),  LEFT_INVERT);
  driveChannel(RIGHT_CHANNEL, saturate(right), RIGHT_INVERT);
}

void stopMotors() {
  driveChannel(CH_A, 0, false);
  driveChannel(CH_B, 0, false);
}

// ---------------- Sensores ----------------
// Devuelve, ordenado de IZQUIERDA a DERECHA, si cada sensor "ve la línea" (1) o no (0).
// Aquí se aplican BLACK_VALUE (hardware) y LINE_COLOR (lógica, una compuerta NOT).
int readLine(bool onLine[5]) {
  int count = 0;
  for (int i = 0; i < 5; i++) {
    int pin = SENSOR_PINS[S1_IS_LEFT ? i : 4 - i];
    bool isBlack = (digitalRead(pin) == BLACK_VALUE);
    onLine[i] = (LINE_COLOR == LINE_BLACK) ? isBlack : !isBlack;
    count += onLine[i];
  }
  return count;
}

// Error de posición: -2 (línea a la izquierda) ... 0 (centrada) ... +2 (a la derecha).
// Con dos sensores vecinos activos da valores intermedios (±0.5, ±1.5).
float computeError(const bool onLine[5], int count) {
  if (count == 0) {                       // línea perdida: sigue girando hacia donde la vio
    return (lastError >= 0) ? LOST_ERROR : -LOST_ERROR;
  }
  if (count == 5) return 0;               // cruce o marca transversal: sigue recto
  float sum = 0;
  for (int i = 0; i < 5; i++) if (onLine[i]) sum += WEIGHTS[i];
  return sum / count;
}

bool obstacle() {
  if (USE_BUMPER && digitalRead(CLP_PIN) == CLP_ACTIVE_VALUE) return true;
  if (USE_NEAR && digitalRead(NEAR_PIN) == NEAR_ACTIVE_VALUE) return true;
  return false;
}

// ---------------- Arranque / parada con BOOT ----------------
void checkButton() {
  static bool prev = HIGH;
  static unsigned long tChange = 0;
  bool now = digitalRead(START_BUTTON);
  if (now != prev && millis() - tChange > 50) {
    tChange = millis();
    prev = now;
    if (now == LOW) {                     // flanco de bajada = se presionó
      running = !running;
      integral = 0; lastError = 0; lastSeen = millis(); justStarted = running;
      if (!running) stopMotors();
      digitalWrite(LED_PIN, running);
      Serial.println(running ? ">> CORRIENDO" : ">> DETENIDO");
    }
  }
}

void debugPrint(const bool onLine[5], float e, float u, int l, int r) {
  if (!DEBUG_SERIAL || ++debugCount < DEBUG_EVERY) return;
  debugCount = 0;
  Serial.print("[");
  for (int i = 0; i < 5; i++) Serial.print(onLine[i] ? '#' : '.');
  Serial.print("] e="); Serial.print(e, 1);
  Serial.print(" u=");  Serial.print(u, 0);
  Serial.print(" L=");  Serial.print(l);
  Serial.print(" R=");  Serial.println(r);
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
  Serial.println("Seguidor PID listo. Presiona BOOT para arrancar.");
}

void loop() {
  checkButton();

  unsigned long now = millis();
  if (now - lastLoop < LOOP_MS) return;   // periodo fijo: el PID asume dt constante
  lastLoop = now;

  bool onLine[5];
  int count = readLine(onLine);

  if (!running) {                         // detenido: solo muestra lo que ven los sensores
    debugPrint(onLine, computeError(onLine, count), 0, 0, 0);
    return;
  }

  if (obstacle()) { stopMotors(); return; }

  if (count > 0) lastSeen = now;
  else if (now - lastSeen > LOST_TIMEOUT_MS) {   // perdió la línea demasiado tiempo
    running = false; stopMotors(); digitalWrite(LED_PIN, LOW);
    Serial.println(">> LÍNEA PERDIDA: detenido. Presiona BOOT para seguir.");
    return;
  }

  // ---------------- PID ----------------
  float error = computeError(onLine, count);
  if (justStarted) { lastError = error; justStarted = false; }
  integral = constrain(integral + error, -INTEGRAL_LIMIT, INTEGRAL_LIMIT);
  float derivative = error - lastError;
  float u = KP * error + KI * integral + KD * derivative;
  lastError = error;

  // error > 0 => línea a la derecha => la rueda izquierda acelera, la derecha frena
  int left  = saturate(BASE_SPEED + (int)u);
  int right = saturate(BASE_SPEED - (int)u);
  setMotors(left, right);

  debugPrint(onLine, error, u, left, right);
}
