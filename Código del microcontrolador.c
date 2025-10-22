#include <Arduino.h>
#include <DHT.h>
#include <WiFi.h>
#include "time.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Definición de pines
#define DHTPIN 21
#define MQ2PIN 32
#define MQ3PIN 33
#define MQ7PIN 34
#define MQ135PIN 35
#define DHTTYPE DHT22
#define NEXTION_RX 16
#define NEXTION_TX 17
#define ANEMOMETER_PIN 2
#define SIREN_PIN 23

// Configuración WiFi y NTP
const char* ssid = "Zamora";
const char* password = "Multitech2023#";
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -21600;
const char* googleSheetsURL = "https://script.google.com/macros/s/AKfycbyq9sduYJPMhysMamuNR0rQ24dEpdK8tey3FHlyQmWLiFwxpnYxYlsbvp5-4-W9hpU_4Q/exec";

// Credenciales de Twilio
const char* twilioAccountSID = "xxxxxxxxxxxxxxxxxxxxxx";
const char* twilioAuthToken = "xxxxxxxxxxxxxxxxxxxxxx";
const char* twilioFromNumber = "whatsapp:+15186608165";           
const char* twilioToNumber = "whatsapp:+503xxxxxxxx";             

// Parámetros de sensores y alarma
const float VOLTAJE_ESP = 3.3;
const float RESOLUCION_ADC = 4095.0;
const float RESISTENCIA_CARGA_MQ2 = 5.0;
const float RESISTENCIA_CARGA_MQ3 = 200.0;
const float RESISTENCIA_CARGA_MQ7 = 10.0;
const float RESISTENCIA_CARGA_MQ135 = 20.0;
const int SMOKE_THRESHOLD = 400;
const int CO_THRESHOLD = 100;
const int LPG_THRESHOLD = 1000;
const float LPG_A = 574.25, LPG_B = -2.222;
const float SMOKE_A = 305.33, SMOKE_B = -3.401;
const float H2_A = 98.866, H2_B = -2.732;
const float ALCOHOL_A = 0.4, ALCOHOL_B = -1.5;
const float BENZENE_A = 0.2, BENZENE_B = -1.4;
const float CO_A = 99.042, CO_B = -1.518;
const float TOLUENE_A = 4.83, TOLUENE_B = -2.62;
const float AMMONIA_A = 102.2, AMMONIA_B = -2.473;
const float CO2_A = 116.602, CO2_B = -2.769;


// Variables globales
float R0_MQ2 = 0, R0_MQ3 = 0, R0_MQ7 = 0, R0_MQ135 = 0;
DHT dht(DHTPIN, DHTTYPE);

float temperatura, humedad, windSpeed;
int humo_ppm, lpg_ppm, h2_ppm;
float alcohol_mgL, benceno_mgL;
int co_ppm, co2_ppm;
int tolueno_ppm, amoniaco_ppm, ica_valor;

volatile int pulseCount = 0;
char buffer[100];
bool alarmState = false;

// Banderas para pausar las gráficas 
bool p2_s0_paused = false, p2_s1_paused = false;
bool p3_s0_paused = false, p4_s0_paused = false;
bool p5_s0_paused = false, p7_s0_paused = false;
bool p8_s0_paused = false, p8_s1_paused = false;
bool p10_s0_paused = false; // Añadido para el anemómetro

// Temporizadores para que las diferentes funciones se realicen independientemente del proceso
unsigned long previousMillisGS = 0;
const long intervalGS = 5000; 
unsigned long previousMillisSensors = 0;
const long intervalSensors = 2000;
unsigned long previousMillisAnemometer = 0;
const long intervalAnemometer = 1000;

//  Prototipos de funciones
void checkAlarms();
void readAllSensors();
void updateNextionDisplay();
void sendToGoogleSheet();
void sendTwilioNotification(String message);
void checkNextionCommands(); // <-- NUEVO PROTOTIPO

void IRAM_ATTR countPulse() {
  pulseCount++;
}

void sendCommandToNextion(String cmd) {
  Serial2.print(cmd);
  Serial2.write(0xFF);
  Serial2.write(0xFF);
  Serial2.write(0xFF);
}
int mapLog(int value, int in_min, int in_max, int out_min, int out_max) {
  if (value <= in_min) return out_min;
  if (value >= in_max) return out_max;
  double log_in_min = log10(in_min);
  double log_in_max = log10(in_max);
  double log_value = log10(value);
  double mapped_value = out_min + (double)(out_max - out_min) * (log_value - log_in_min) / (log_in_max - log_in_min);
  return (int)mapped_value;
}
float calcularResistenciaSensor(int pin, float rl_value) {
  int valorAnalogico = analogRead(pin);
  float voltajeMedido = valorAnalogico * (VOLTAJE_ESP / RESOLUCION_ADC);
  if (voltajeMedido == 0) return 0;
  return rl_value * (VOLTAJE_ESP - voltajeMedido) / voltajeMedido;
}
float calibrarSensor(int pin, float rl_value) {
  float Rs_sum = 0;
  for (int i = 0; i < 50; i++) {
    Rs_sum += calcularResistenciaSensor(pin, rl_value);
    delay(100);
  }
  return Rs_sum / 50.0;
}
float leerSensor(float A, float B, float R0, int pin, float rl_value) {
  float Rs = calcularResistenciaSensor(pin, rl_value);
  if (Rs <= 0 || R0 <= 0) return 0;
  float ratio = Rs / R0;
  if (ratio <= 0) return 0;
  return A * pow(ratio, B);
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, NEXTION_RX, NEXTION_TX);

  pinMode(SIREN_PIN, OUTPUT);
  digitalWrite(SIREN_PIN, LOW);
  pinMode(ANEMOMETER_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ANEMOMETER_PIN), countPulse, FALLING);

  sendCommandToNextion("page 0"); delay(50); sendCommandToNextion("page0.j_loading.val=0"); delay(50);
  pinMode(MQ2PIN, INPUT); pinMode(MQ3PIN, INPUT); pinMode(MQ7PIN, INPUT); pinMode(MQ135PIN, INPUT); dht.begin();
  sendCommandToNextion("page0.t_status.txt=\"Conectando a WiFi...\""); WiFi.begin(ssid, password);
  unsigned long wifiStartTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStartTime < 20000) {
      long progress = map(millis() - wifiStartTime, 0, 20000, 12, 25); sprintf(buffer, "page0.j_loading.val=%ld", progress);
      sendCommandToNextion(buffer); delay(500);
  }
  if (WiFi.status() == WL_CONNECTED) {
      sendCommandToNextion("page0.t_status.txt=\"WiFi Conectado\""); configTime(gmtOffset_sec, 0, ntpServer);
  } else { sendCommandToNextion("page0.t_status.txt=\"Error de Conexion\""); }
  delay(1000);
  sendCommandToNextion("page0.t_status.txt=\"Calibrando Sensores...\"");
  R0_MQ2 = calibrarSensor(MQ2PIN, RESISTENCIA_CARGA_MQ2);
  R0_MQ3 = calibrarSensor(MQ3PIN, RESISTENCIA_CARGA_MQ3);
  R0_MQ7 = calibrarSensor(MQ7PIN, RESISTENCIA_CARGA_MQ7);
  R0_MQ135 = calibrarSensor(MQ135PIN, RESISTENCIA_CARGA_MQ135);
  sendCommandToNextion("page0.t_status.txt=\"Iniciando...\""); delay(1000); sendCommandToNextion("page 1");
}

void loop() {
  checkNextionCommands();
  
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillisAnemometer >= intervalAnemometer) {
    previousMillisAnemometer = currentMillis;
    noInterrupts(); int count = pulseCount; pulseCount = 0; interrupts();
    windSpeed = (count * 8.75) / 100.0;
  }

  if (currentMillis - previousMillisSensors >= intervalSensors) {
    previousMillisSensors = currentMillis;
    readAllSensors();
    checkAlarms();
    updateNextionDisplay();
  }

  if (currentMillis - previousMillisGS >= intervalGS) {
    previousMillisGS = currentMillis;
    if (WiFi.status() == WL_CONNECTED) {
      sendToGoogleSheet();
    }
  }
}

void checkNextionCommands() {
  if (Serial2.available()) {
    String command = Serial2.readStringUntil('\0'); // Lee hasta el terminador nulo
    command.trim();
    
    if (command == "p2_s0_toggle") p2_s0_paused = !p2_s0_paused;
    if (command == "p2_s1_toggle") p2_s1_paused = !p2_s1_paused;
    if (command == "p3_toggle") p3_s0_paused = !p3_s0_paused;
    if (command == "p4_toggle") p4_s0_paused = !p4_s0_paused;
    if (command == "p5_toggle") p5_s0_paused = !p5_s0_paused;
    if (command == "p7_toggle") p7_s0_paused = !p7_s0_paused;
    if (command == "p8_s0_toggle") p8_s0_paused = !p8_s0_paused;
    if (command == "p8_s1_toggle") p8_s1_paused = !p8_s1_paused;
    if (command == "p10_toggle") p10_s0_paused = !p10_s0_paused;
  }
}

void readAllSensors() {
  humedad = dht.readHumidity();
  temperatura = dht.readTemperature();
  lpg_ppm = (int)leerSensor(LPG_A, LPG_B, R0_MQ2, MQ2PIN, RESISTENCIA_CARGA_MQ2);
  humo_ppm = (int)leerSensor(SMOKE_A, SMOKE_B, R0_MQ2, MQ2PIN, RESISTENCIA_CARGA_MQ2);
  h2_ppm = (int)leerSensor(H2_A, H2_B, R0_MQ2, MQ2PIN, RESISTENCIA_CARGA_MQ2);
  alcohol_mgL = leerSensor(ALCOHOL_A, ALCOHOL_B, R0_MQ3, MQ3PIN, RESISTENCIA_CARGA_MQ3);
  benceno_mgL = leerSensor(BENZENE_A, BENZENE_B, R0_MQ3, MQ3PIN, RESISTENCIA_CARGA_MQ3);
  co_ppm = (int)leerSensor(CO_A, CO_B, R0_MQ7, MQ7PIN, RESISTENCIA_CARGA_MQ7);
  tolueno_ppm = (int)leerSensor(TOLUENE_A, TOLUENE_B, R0_MQ135, MQ135PIN, RESISTENCIA_CARGA_MQ135);
  amoniaco_ppm = (int)leerSensor(AMMONIA_A, AMMONIA_B, R0_MQ135, MQ135PIN, RESISTENCIA_CARGA_MQ135);
  co2_ppm = (int)leerSensor(CO2_A, CO2_B, R0_MQ135, MQ135PIN, RESISTENCIA_CARGA_MQ135);
  ica_valor = max({ humo_ppm, co_ppm, tolueno_ppm, amoniaco_ppm });
  ica_valor = constrain(ica_valor, 0, 500);
}

void checkAlarms() {
  bool isAlarmActive = false;
  
  if (humo_ppm > SMOKE_THRESHOLD) {
    isAlarmActive = true;
    if (!alarmState) {
      sendTwilioNotification("⚠️ ¡Alerta de Humo! Nivel detectado: " + String(humo_ppm) + " ppm");
    }
  } else if (co_ppm > CO_THRESHOLD) {
    isAlarmActive = true;
    if (!alarmState) {
      sendTwilioNotification("⚠️ ¡Alerta de Monóxido de Carbono (CO)! Nivel detectado: " + String(co_ppm) + " ppm");
    }
  } else if (lpg_ppm > LPG_THRESHOLD) {
    isAlarmActive = true;
    if (!alarmState) {
      sendTwilioNotification("⚠️ ¡Alerta de Fuga de Gas (LPG)! Nivel detectado: " + String(lpg_ppm) + " ppm");
    }
  }

  if (isAlarmActive) {
    digitalWrite(SIREN_PIN, HIGH);
    alarmState = true;
  } else {
    digitalWrite(SIREN_PIN, LOW);
    alarmState = false;
  }
}

void updateNextionDisplay() {
  // Página 1 (Dashboard)
  sprintf(buffer, "page1.temp.txt=\"%.1f\"", temperatura); sendCommandToNextion(buffer);
  sprintf(buffer, "page1.hum.txt=\"%.1f\"", humedad); sendCommandToNextion(buffer);
  sprintf(buffer, "page1.wind.txt=\"%.2f\"", windSpeed); sendCommandToNextion(buffer);
  sprintf(buffer, "page1.smoke.txt=\"%d\"", humo_ppm); sendCommandToNextion(buffer);
  sprintf(buffer, "page1.lpg.txt=\"%d\"", lpg_ppm); sendCommandToNextion(buffer);
  sprintf(buffer, "page1.h2.txt=\"%d\"", h2_ppm); sendCommandToNextion(buffer);
  sprintf(buffer, "page1.Alcohol.txt=\"%.2f\"", alcohol_mgL); sendCommandToNextion(buffer);
  sprintf(buffer, "page1.Benceno.txt=\"%.2f\"", benceno_mgL); sendCommandToNextion(buffer);
  sprintf(buffer, "page1.CO.txt=\"%d\"", co_ppm); sendCommandToNextion(buffer);
  sprintf(buffer, "page1.CO2.txt=\"%d\"", co2_ppm); sendCommandToNextion(buffer);
  sprintf(buffer, "page1.Tolueno.txt=\"%d\"", tolueno_ppm); sendCommandToNextion(buffer);
  sprintf(buffer, "page1.Amoniaco.txt=\"%d\"", amoniaco_ppm); sendCommandToNextion(buffer);
  sprintf(buffer, "page1.ICA.txt=\"%d\"", ica_valor); sendCommandToNextion(buffer);
  long temp_progress = map(temperatura, -10, 60, 0, 100);
  sprintf(buffer, "page1.j1.val=%ld", temp_progress); sendCommandToNextion(buffer);
  long lpg_angle = map(lpg_ppm, 0, 1000, 0, 180);
  sprintf(buffer, "glpg.val=%ld", lpg_angle); sendCommandToNextion(buffer);
  long alcohol_angle_dash = map(alcohol_mgL * 1000, 0, 1500, 0, 180);
  sprintf(buffer, "galc.val=%ld", alcohol_angle_dash); sendCommandToNextion(buffer);
  long ica_angle_dash = map(ica_valor, 0, 500, 0, 180);
  sprintf(buffer, "gica.val=%ld", ica_angle_dash); sendCommandToNextion(buffer);
  int co2_dash_waveform = map(co2_ppm, 0, 1000, 0, 255);
  sprintf(buffer, "add page1.gco2.id,0,%d", constrain(co2_dash_waveform, 0, 255));
  sendCommandToNextion(buffer);

  // Página 2 (Sensor DHT22)
  sprintf(buffer, "page2.temp.txt=\"%.1f\"", temperatura); sendCommandToNextion(buffer);
  sprintf(buffer, "page2.hum.txt=\"%.1f\"", humedad); sendCommandToNextion(buffer);
  long temp_progress_p2 = map(temperatura, -10, 60, 0, 100);
  long hum_progress_p2 = map(humedad, 0, 100, 0, 100);
  sprintf(buffer, "page2.j0.val=%ld", temp_progress_p2); sendCommandToNextion(buffer);
  sprintf(buffer, "page2.j1.val=%ld", hum_progress_p2); sendCommandToNextion(buffer);
  if (!p2_s0_paused) {
    int temp_waveform = map(temperatura, -10, 60, 0, 255);
    sprintf(buffer, "add page2.s0.id,0,%d", constrain(temp_waveform, 0, 255));
    sendCommandToNextion(buffer);
  }
  if (!p2_s1_paused) {
    int hum_waveform = map(humedad, 0, 100, 0, 255);
    sprintf(buffer, "add page2.s1.id,0,%d", constrain(hum_waveform, 0, 255));
    sendCommandToNextion(buffer);
  }

  // Página 3 (MQ2)
  sprintf(buffer, "page3.Smoke.txt=\"%d\"", humo_ppm); sendCommandToNextion(buffer);
  sprintf(buffer, "page3.LPG.txt=\"%d\"", lpg_ppm); sendCommandToNextion(buffer);
  sprintf(buffer, "page3.H2.txt=\"%d\"", h2_ppm); sendCommandToNextion(buffer);
  long smoke_angle_p3 = map(humo_ppm, 0, 1000, 0, 180);
  sprintf(buffer, "g_smoke_p3.val=%ld", smoke_angle_p3); sendCommandToNextion(buffer);
  if (!p3_s0_paused) {
    int smoke_waveform = mapLog(humo_ppm, 100, 10000, 0, 255);
    int lpg_waveform = mapLog(lpg_ppm, 100, 10000, 0, 255);
    int h2_waveform = mapLog(h2_ppm, 100, 10000, 0, 255);
    sprintf(buffer, "add page3.s0.id,0,%d", smoke_waveform); sendCommandToNextion(buffer);
    sprintf(buffer, "add page3.s0.id,1,%d", lpg_waveform); sendCommandToNextion(buffer);
    sprintf(buffer, "add page3.s0.id,2,%d", h2_waveform); sendCommandToNextion(buffer);
  }

  // Página 4 (MQ-3)
  sprintf(buffer, "page4.Alcohol.txt=\"%.2f\"", alcohol_mgL); sendCommandToNextion(buffer);
  sprintf(buffer, "page4.Benceno.txt=\"%.2f\"", benceno_mgL); sendCommandToNextion(buffer);
  long alcohol_angle_p4 = map(alcohol_mgL * 1000, 0, 1500, 0, 180);
  sprintf(buffer, "g_alc_p4.val=%ld", alcohol_angle_p4); sendCommandToNextion(buffer);
  if (!p4_s0_paused) {
    int alcohol_waveform = map(alcohol_mgL, 0, 10, 0, 255);
    int benceno_waveform = map(benceno_mgL, 0, 10, 0, 255);
    sprintf(buffer, "add page4.s0.id,0,%d", constrain(alcohol_waveform, 0, 255)); sendCommandToNextion(buffer);
    sprintf(buffer, "add page4.s0.id,1,%d", constrain(benceno_waveform, 0, 255)); sendCommandToNextion(buffer);
  }

  // Página 5 (MQ-3)
  sprintf(buffer, "page5.Alcohol.txt=\"%.2f\"", alcohol_mgL); sendCommandToNextion(buffer);
  sprintf(buffer, "page5.Benceno.txt=\"%.2f\"", benceno_mgL); sendCommandToNextion(buffer);
  long alcohol_angle_p5 = map(alcohol_mgL * 1000, 0, 1500, 0, 180);
  sprintf(buffer, "g_alc_p5.val=%ld", alcohol_angle_p5); sendCommandToNextion(buffer);
  if (!p5_s0_paused) {
    int alcohol_waveform = map(alcohol_mgL, 0, 4, 0, 255);
    int benceno_waveform = map(benceno_mgL, 0, 4, 0, 255);
    sprintf(buffer, "add page5.s0.id,0,%d", constrain(alcohol_waveform, 0, 255)); sendCommandToNextion(buffer);
    sprintf(buffer, "add page5.s0.id,1,%d", constrain(benceno_waveform, 0, 255)); sendCommandToNextion(buffer);
  }

  // Página 7 (MQ-7)
  sprintf(buffer, "page7.CO.txt=\"%d\"", co_ppm); sendCommandToNextion(buffer);
  sprintf(buffer, "page7.CO2.txt=\"%d\"", co2_ppm); sendCommandToNextion(buffer);
  if (!p7_s0_paused) {
    int co_waveform = map(co_ppm, 0, 1000, 0, 255);
    int co2_waveform = map(co2_ppm, 0, 1000, 0, 255);
    sprintf(buffer, "add page7.s0.id,0,%d", constrain(co_waveform, 0, 255)); sendCommandToNextion(buffer);
    sprintf(buffer, "add page7.s0.id,1,%d", constrain(co2_waveform, 0, 255)); sendCommandToNextion(buffer);
  }

  // Página 8 (MQ-135 Gráficas)
  sprintf(buffer, "page8.Tolueno.txt=\"%d\"", tolueno_ppm); sendCommandToNextion(buffer);
  sprintf(buffer, "page8.Amoniaco.txt=\"%d\"", amoniaco_ppm); sendCommandToNextion(buffer);
  sprintf(buffer, "page8.ICA.txt=\"%d\"", ica_valor); sendCommandToNextion(buffer);
  if (!p8_s0_paused) {
    int tolueno_waveform = mapLog(tolueno_ppm, 100, 10000, 0, 255);
    int amoniaco_waveform = mapLog(amoniaco_ppm, 100, 10000, 0, 255);
    sprintf(buffer, "add page8.s0.id,0,%d", tolueno_waveform); sendCommandToNextion(buffer);
    sprintf(buffer, "add page8.s0.id,1,%d", amoniaco_waveform); sendCommandToNextion(buffer);
  }
  if (!p8_s1_paused) {
    int ica_waveform = map(ica_valor, 0, 500, 0, 255);
    sprintf(buffer, "add page8.s1.id,0,%d", constrain(ica_waveform, 0, 255));
    sendCommandToNextion(buffer);
  }

  // Página 9 (MQ-135 Indicadores)
  sprintf(buffer, "page9.Tolueno.txt=\"%d\"", tolueno_ppm); sendCommandToNextion(buffer);
  sprintf(buffer, "page9.Amoniaco.txt=\"%d\"", amoniaco_ppm); sendCommandToNextion(buffer);
  sprintf(buffer, "page9.ICA.txt=\"%d\"", ica_valor); sendCommandToNextion(buffer);
  long tolueno_progress = constrain((long)tolueno_ppm / 100, 0, 100);
  long amoniaco_progress = constrain((long)amoniaco_ppm / 100, 0, 100);
  sprintf(buffer, "page9.j0.val=%ld", tolueno_progress); sendCommandToNextion(buffer);
  sprintf(buffer, "page9.j1.val=%ld", amoniaco_progress); sendCommandToNextion(buffer);
  long ica_angle = map(ica_valor, 0, 500, 0, 180);
  sprintf(buffer, "g_ica.val=%ld", ica_angle); sendCommandToNextion(buffer);
  
  // Página 10 (Anemómetro)
  sprintf(buffer, "page10.velocidad.txt=\"%.2f\"", windSpeed); sendCommandToNextion(buffer);
  if (!p10_s0_paused) {
    int wind_waveform = map(windSpeed, 0, 70, 0, 255);
    sprintf(buffer, "add page10.s0.id,0,%d", constrain(wind_waveform, 0, 255));
    sendCommandToNextion(buffer);
  }

  // Actualización de hora y fecha
  if (WiFi.status() == WL_CONNECTED) {
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    strftime(buffer, sizeof(buffer), "%H:%M", &timeinfo);
    String time_str(buffer);
    sendCommandToNextion("page1.time.txt=\"" + time_str + "\"");
    
    strftime(buffer, sizeof(buffer), "%Y-%m-%d", &timeinfo);
    String date_str(buffer);
    sendCommandToNextion("page1.date.txt=\"" + date_str + "\"");
  }
}

void sendToGoogleSheet() {
  HTTPClient http;
  http.setTimeout(5000); 
  http.begin(googleSheetsURL);
  http.addHeader("Content-Type", "application/json");
  StaticJsonDocument<512> doc;
  doc["temperatura"] = temperatura;
  doc["humedad"] = humedad;
  doc["wind_speed"] = windSpeed;
  doc["lpg_ppm"] = lpg_ppm;
  doc["h2_ppm"] = h2_ppm;
  doc["humo_ppm"] = humo_ppm;
  doc["benceno_mgL"] = benceno_mgL;
  doc["alcohol_mgL"] = alcohol_mgL;
  doc["co_ppm"] = co_ppm;
  doc["co2_ppm"] = co2_ppm;
  doc["amoniaco_ppm"] = amoniaco_ppm;
  doc["tolueno_ppm"] = tolueno_ppm;
  doc["ica_valor"] = ica_valor;
  String jsonString;
  serializeJson(doc, jsonString);
  int httpResponseCode = http.POST(jsonString);
  if (httpResponseCode > 0) {
    Serial.println("Google Sheets Response Code: " + String(httpResponseCode));
  } else {
    Serial.println("Google Sheets POST Error: " + String(httpResponseCode));
  }
  http.end();
}

void sendTwilioNotification(String message) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Sin conexión WiFi, no se puede enviar la alerta de Twilio.");
    return;
  }

  HTTPClient http;
  String url = "https://api.twilio.com/2010-04-01/Accounts/" + String(twilioAccountSID) + "/Messages.json";
  http.begin(url);
  http.setAuthorization(twilioAccountSID, twilioAuthToken);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String postData = "To=" + String(twilioToNumber) + "&From=" + String(twilioFromNumber) + "&Body=" + message;
  Serial.println("Enviando notificación de alarma a Twilio...");
  int httpResponseCode = http.POST(postData);
  if (httpResponseCode > 0) {
    Serial.println("Twilio Response Code: " + String(httpResponseCode));
  } else {
    Serial.println("Twilio POST Error: " + String(httpResponseCode));
  }

  http.end();
}
