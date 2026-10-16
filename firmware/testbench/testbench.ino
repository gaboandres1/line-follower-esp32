/*
 * TESTBENCH DE CALIBRACIÓN — Seguidor de línea ESP32 + BFD-1000 + L298N
 * ---------------------------------------------------------------------
 * Sirve para descubrir las CONSTANTES DE HARDWARE que luego se pegan en
 * seguidor_pid.ino:
 *   BLACK_VALUE, S1_IS_LEFT, LEFT_CHANNEL, LEFT_INVERT, RIGHT_INVERT,
 *   CLP_ACTIVE_VALUE, NEAR_ACTIVE_VALUE
 *
 * Uso:
 *   1. Sube este sketch. Abre el Monitor Serie a 115200 baudios
 *      con el ajuste de línea en "Nueva línea".
 *   2. LEVANTA EL ROBOT (ruedas en el aire) antes de probar motores.
 *   3. Escribe 1 + Enter para el asistente guiado, o ? para ver el menú.
 *   4. Al final copia el bloque que imprime ('p') a seguidor_pid.ino.
 */

// ======================= PINES (deben coincidir con seguidor_pid.ino) =======================
const int SENSOR_PINS[5] = {21, 19, 18, 17, 16};   // S1..S5 (U1,U2,U4,U5,U6). S3 = centro
#define CLP_PIN   22
#define NEAR_PIN  23

#define CH_A 0   // canal A del L298N: ENA, IN1, IN2
#define CH_B 1   // canal B del L298N: ENB, IN3, IN4
const int EN_PIN[2]  = {25, 14};   // ENA, ENB (PWM)
const int INA_PIN[2] = {26, 33};   // IN1, IN3
const int INB_PIN[2] = {27, 13};   // IN2, IN4

#define PWM_FREQ  1000
#define PWM_BITS  8
#define TEST_MS   1500             // duración de cada prueba de motor

int testSpeed = 160;               // 0..255, se cambia con "v 200"

// ======================= RESULTADOS DE LA CALIBRACIÓN (en RAM) =======================
// -1 = sin calibrar todavía
int blackValue = -1;   // lectura cruda sobre negro
int s1IsLeft   = -1;   // 1 = S1 está a la izquierda
int leftCh     = -1;   // CH_A o CH_B
int invertCh[2] = {-1, -1};   // por canal: 1 = hay que invertir para ir adelante
int clpActive  = -1;
int nearActive = -1;

// ======================= PWM (compatible core ESP32 2.x y 3.x) =======================
#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 3
#endif
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

// Mueve un canal "crudo": speed > 0 => INA=HIGH, INB=LOW (convención "sin invertir")
void driveChannel(int ch, int speed) {
  speed = constrain(speed, -255, 255);
  digitalWrite(INA_PIN[ch], speed > 0 ? HIGH : LOW);
  digitalWrite(INB_PIN[ch], speed < 0 ? HIGH : LOW);
  pwmWrite(ch, abs(speed));
}
void stopAll() { driveChannel(CH_A, 0); driveChannel(CH_B, 0); }

// ======================= ENTRADA SERIAL =======================
char lineBuf[32];

// Lee una línea completa. Funciona con "Nueva línea", "CR" o "Ambos".
const char* readLine() {
  int n = 0;
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\r' || c == '\n') {
        if (c == '\r') { delay(2); if (Serial.peek() == '\n') Serial.read(); }
        break;
      }
      if (n < (int)sizeof(lineBuf) - 1) lineBuf[n++] = tolower(c);
    }
    delay(1);
  }
  lineBuf[n] = 0;
  return lineBuf;
}

void waitEnter(const char* msg) {
  Serial.print(msg);
  Serial.println("  -> envía Enter");
  readLine();
}

// Pregunta con respuestas de una letra. Repite hasta recibir una válida.
char ask(const char* question, const char* valid) {
  while (true) {
    Serial.print(question); Serial.print(" ["); Serial.print(valid); Serial.println("]");
    const char* r = readLine();
    if (r[0] && strchr(valid, r[0])) return r[0];
    Serial.println("   Respuesta no válida, intenta otra vez.");
  }
}

bool keyPressed() {
  if (!Serial.available()) return false;
  while (Serial.available()) Serial.read();
  return true;
}

// ======================= SENSORES =======================
// Lee cada sensor N veces y devuelve el valor mayoritario (filtra ruido)
void sampleSensors(int out[5], int n = 40) {
  int ones[5] = {0, 0, 0, 0, 0};
  for (int k = 0; k < n; k++) {
    for (int i = 0; i < 5; i++) ones[i] += digitalRead(SENSOR_PINS[i]);
    delay(2);
  }
  for (int i = 0; i < 5; i++) out[i] = (ones[i] * 2 > n) ? 1 : 0;
}

void printSensors(const int v[5]) {
  for (int i = 0; i < 5; i++) { Serial.print(" S"); Serial.print(i + 1); Serial.print("="); Serial.print(v[i]); }
}

void liveSensors() {
  Serial.println("\nLECTURA EN VIVO (envía cualquier tecla para salir)");
  Serial.println("  S1 S2 S3 S4 S5 | CLP NEAR | vista (# = lee 1)");
  while (!keyPressed()) {
    Serial.print("  ");
    char bar[6];
    for (int i = 0; i < 5; i++) {
      int v = digitalRead(SENSOR_PINS[i]);
      Serial.print(v); Serial.print("  ");
      bar[i] = v ? '#' : '.';
    }
    bar[5] = 0;
    Serial.print("|  "); Serial.print(digitalRead(CLP_PIN));
    Serial.print("    "); Serial.print(digitalRead(NEAR_PIN));
    Serial.print("   | ["); Serial.print(bar); Serial.println("]");
    delay(150);
  }
}

// Espera hasta `ms` a que un pin cambie respecto a `idle`. Devuelve true si cambió.
bool waitForChange(int pin, int idle, unsigned long ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    int changes = 0;
    for (int k = 0; k < 5; k++) { if (digitalRead(pin) != idle) changes++; delay(2); }
    if (changes >= 4) return true;
  }
  return false;
}

// ======================= PRUEBAS DE MOTOR =======================
void spinChannel(int ch, int dir) {
  Serial.print("   Girando canal "); Serial.print(ch == CH_A ? "A (ENA/IN1/IN2)" : "B (ENB/IN3/IN4)");
  Serial.print(dir > 0 ? " sentido +" : " sentido -");
  Serial.print(" a velocidad "); Serial.println(testSpeed);
  driveChannel(ch, dir * testSpeed);
  unsigned long t0 = millis();
  while (millis() - t0 < TEST_MS && !keyPressed()) delay(5);
  driveChannel(ch, 0);
}

// Usa la configuración calibrada: role 0 = izquierdo, 1 = derecho. speed > 0 = adelante.
bool driveRole(int role, int speed) {
  if (leftCh < 0) return false;
  int ch = (role == 0) ? leftCh : 1 - leftCh;
  if (invertCh[ch] < 0) return false;
  driveChannel(ch, invertCh[ch] ? -speed : speed);
  return true;
}

void testConfig() {
  if (leftCh < 0 || invertCh[0] < 0 || invertCh[1] < 0) {
    Serial.println("   Primero calibra los motores (asistente '1' o comando 'm').");
    return;
  }
  const char* names[4] = {"rueda IZQUIERDA adelante", "rueda DERECHA adelante",
                          "ambas ADELANTE", "ambas ATRÁS"};
  for (int step = 0; step < 4; step++) {
    Serial.print("   Prueba: "); Serial.println(names[step]);
    int l = 0, r = 0;
    if (step == 0) l = testSpeed;
    if (step == 1) r = testSpeed;
    if (step == 2) { l = testSpeed; r = testSpeed; }
    if (step == 3) { l = -testSpeed; r = -testSpeed; }
    driveRole(0, l); driveRole(1, r);
    unsigned long t0 = millis();
    while (millis() - t0 < TEST_MS && !keyPressed()) delay(5);
    stopAll();
    delay(400);
  }
  Serial.println("   Si todo coincidió con lo anunciado, la configuración está bien.");
}

// ======================= ASISTENTE =======================
void calibrateSensors() {
  int white[5], black[5];
  Serial.println("\n--- PASO 1: COLOR DE LA LÍNEA ---");
  waitEnter("Pon los 5 sensores sobre BLANCO (a la altura de trabajo)");
  sampleSensors(white);
  Serial.print("   Blanco:"); printSensors(white); Serial.println();
  waitEnter("Pon los 5 sensores sobre NEGRO (cinta aislante o la pista)");
  sampleSensors(black);
  Serial.print("   Negro: "); printSensors(black); Serial.println();

  int ok = 0, onesOnBlack = 0;
  for (int i = 0; i < 5; i++) {
    if (white[i] != black[i]) ok++;
    else { Serial.print("   ATENCIÓN: S"); Serial.print(i + 1); Serial.println(" no distingue blanco de negro. Ajusta su potenciómetro o la altura."); }
    onesOnBlack += black[i];
  }
  blackValue = (onesOnBlack >= 3) ? 1 : 0;
  Serial.print("   => BLACK_VALUE = "); Serial.print(blackValue);
  Serial.print("   ("); Serial.print(ok); Serial.println("/5 sensores responden bien)");
}

void calibrateOrientation() {
  Serial.println("\n--- PASO 2: ORDEN DE LOS SENSORES ---");
  if (blackValue < 0) { Serial.println("   Haz primero el paso 1."); return; }
  Serial.println("Mira el robot desde atrás, con el frente alejándose de ti.");
  waitEnter("Pon la línea negra SOLO bajo el sensor del extremo IZQUIERDO");
  int v[5]; sampleSensors(v);
  Serial.print("   Lectura:"); printSensors(v); Serial.println();
  bool first = (v[0] == blackValue), last = (v[4] == blackValue);
  if (first && !last)      { s1IsLeft = 1; Serial.println("   => S1 está a la IZQUIERDA. S1_IS_LEFT = true"); }
  else if (last && !first) { s1IsLeft = 0; Serial.println("   => S5 está a la IZQUIERDA. S1_IS_LEFT = false"); }
  else Serial.println("   No quedó claro (ninguno o ambos extremos ven la línea). Repite el paso con 'o'.");
}

void calibrateMotors() {
  Serial.println("\n--- PASO 3: MOTORES ---");
  Serial.println("LEVANTA EL ROBOT para que las ruedas giren en el aire.");
  Serial.println("Convención: 'adelante' = hacia donde está el sensor BFD-1000.");
  for (int ch = CH_A; ch <= CH_B; ch++) {
    waitEnter(ch == CH_A ? "Voy a girar el canal A (OUT1/OUT2)" : "Voy a girar el canal B (OUT3/OUT4)");
    while (true) {
      spinChannel(ch, +1);
      char w = ask("   ¿Qué rueda giró? i=izquierda, d=derecha, n=ninguna, r=repetir", "idnr");
      if (w == 'r') continue;
      if (w == 'n') {
        Serial.println("   Revisa: jumper/cable de ENA o ENB, cables OUT, batería encendida, GND común.");
        Serial.println("   Prueba con más velocidad: 'v 220' y luego repite con 'm'.");
        return;
      }
      char f = ask("   ¿Esa rueda giró hacia ADELANTE? s=sí, n=no, r=repetir", "snr");
      if (f == 'r') continue;
      invertCh[ch] = (f == 's') ? 0 : 1;
      if (ch == CH_A) leftCh = (w == 'i') ? CH_A : CH_B;
      else if ((w == 'i') != (leftCh == CH_B))
        Serial.println("   ATENCIÓN: los dos canales mueven la misma rueda. Revisa el cableado OUT.");
      break;
    }
  }
  Serial.print("   => LEFT_CHANNEL = "); Serial.println(leftCh == CH_A ? "CH_A" : "CH_B");
  Serial.print("   => LEFT_INVERT  = "); Serial.println(invertCh[leftCh] ? "true" : "false");
  Serial.print("   => RIGHT_INVERT = "); Serial.println(invertCh[1 - leftCh] ? "true" : "false");
  if (ask("   ¿Probar la configuración ahora? s/n", "sn") == 's') testConfig();
}

void calibrateExtras() {
  Serial.println("\n--- PASO 4: CHOQUE (CLP) Y OBSTÁCULO (NEAR) ---");
  waitEnter("No toques el robot ni pongas nada frente a él");
  int clpIdle = digitalRead(CLP_PIN), nearIdle = digitalRead(NEAR_PIN);
  Serial.print("   En reposo: CLP="); Serial.print(clpIdle); Serial.print(" NEAR="); Serial.println(nearIdle);

  Serial.println("Tienes 6 s para PRESIONAR el interruptor de choque (CLP) del frente...");
  if (waitForChange(CLP_PIN, clpIdle, 6000)) { clpActive = !clpIdle; Serial.print("   => CLP_ACTIVE_VALUE = "); Serial.println(clpActive); }
  else Serial.println("   No detecté cambio en CLP (puedes dejarlo sin usar).");

  delay(800);
  Serial.println("Tienes 6 s para ACERCAR LA MANO a ~3 cm del frente (sensor NEAR)...");
  if (waitForChange(NEAR_PIN, nearIdle, 6000)) { nearActive = !nearIdle; Serial.print("   => NEAR_ACTIVE_VALUE = "); Serial.println(nearActive); }
  else Serial.println("   No detecté cambio en NEAR. Ajusta el potenciómetro del NEAR o déjalo sin usar.");
}

void printConfig() {
  Serial.println("\n// ===== Copia esto en seguidor_pid.ino (sección HARDWARE) =====");
  Serial.print("#define BLACK_VALUE        "); Serial.println(blackValue < 0 ? "1      // SIN CALIBRAR" : (blackValue ? "1" : "0"));
  Serial.print("#define S1_IS_LEFT         "); Serial.println(s1IsLeft < 0 ? "true   // SIN CALIBRAR" : (s1IsLeft ? "true" : "false"));
  Serial.print("#define LEFT_CHANNEL       "); Serial.println(leftCh < 0 ? "CH_A   // SIN CALIBRAR" : (leftCh == CH_A ? "CH_A" : "CH_B"));
  int li = (leftCh < 0) ? -1 : invertCh[leftCh];
  int ri = (leftCh < 0) ? -1 : invertCh[1 - leftCh];
  Serial.print("#define LEFT_INVERT        "); Serial.println(li < 0 ? "false  // SIN CALIBRAR" : (li ? "true" : "false"));
  Serial.print("#define RIGHT_INVERT       "); Serial.println(ri < 0 ? "false  // SIN CALIBRAR" : (ri ? "true" : "false"));
  Serial.print("#define CLP_ACTIVE_VALUE   "); Serial.println(clpActive < 0 ? "0      // SIN CALIBRAR" : (clpActive ? "1" : "0"));
  Serial.print("#define NEAR_ACTIVE_VALUE  "); Serial.println(nearActive < 0 ? "0      // SIN CALIBRAR" : (nearActive ? "1" : "0"));
  Serial.println("// ==============================================================");
}

void wizard() {
  Serial.println("\n========== ASISTENTE DE CALIBRACIÓN ==========");
  calibrateSensors();
  calibrateOrientation();
  calibrateMotors();
  calibrateExtras();
  printConfig();
  Serial.println("Listo. Escribe ? para ver el menú.");
}

void menu() {
  Serial.println("\n================= TESTBENCH =================");
  Serial.println(" 1      Asistente completo (recomendado)");
  Serial.println(" s      Ver sensores en vivo");
  Serial.println(" c      Paso 1: color de línea (BLACK_VALUE)");
  Serial.println(" o      Paso 2: orden de sensores (S1_IS_LEFT)");
  Serial.println(" m      Paso 3: motores (izquierdo/derecho y sentido)");
  Serial.println(" e      Paso 4: CLP y NEAR");
  Serial.println(" a / a- Girar canal A en sentido + / -");
  Serial.println(" b / b- Girar canal B en sentido + / -");
  Serial.println(" t      Probar la configuración calibrada");
  Serial.println(" v N    Velocidad de prueba (0-255), ej: v 200");
  Serial.println(" p      Imprimir constantes para copiar");
  Serial.println(" x      Parar motores");
  Serial.print(  " Velocidad de prueba actual: "); Serial.println(testSpeed);
  Serial.println("=============================================");
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 5; i++) pinMode(SENSOR_PINS[i], INPUT);
  pinMode(CLP_PIN, INPUT);
  pinMode(NEAR_PIN, INPUT);
  for (int ch = CH_A; ch <= CH_B; ch++) {
    pinMode(INA_PIN[ch], OUTPUT);
    pinMode(INB_PIN[ch], OUTPUT);
    pwmInit(ch);
  }
  stopAll();
  delay(500);
  menu();
}

void loop() {
  Serial.print("\n> ");
  const char* cmd = readLine();
  Serial.println(cmd);
  switch (cmd[0]) {
    case '1': wizard(); break;
    case 's': liveSensors(); break;
    case 'c': calibrateSensors(); break;
    case 'o': calibrateOrientation(); break;
    case 'm': calibrateMotors(); break;
    case 'e': calibrateExtras(); break;
    case 't': testConfig(); break;
    case 'p': printConfig(); break;
    case 'x': stopAll(); Serial.println("   Motores detenidos."); break;
    case 'v': {
      int v = atoi(cmd + 1);
      if (v > 0 && v <= 255) { testSpeed = v; Serial.print("   Velocidad de prueba = "); Serial.println(testSpeed); }
      else Serial.println("   Uso: v 200   (valor entre 1 y 255)");
      break;
    }
    case 'a': spinChannel(CH_A, cmd[1] == '-' ? -1 : +1); break;
    case 'b': spinChannel(CH_B, cmd[1] == '-' ? -1 : +1); break;
    case '?': case 0: menu(); break;
    default: Serial.println("   Comando desconocido. Escribe ? para ver el menú.");
  }
}
