#include <Arduino.h>
#include <DHT.h>
#include <WiFi.h>
#include "time.h"
#include <ThingESP.h>

#define DHTPIN 5
#define MQ2PIN 26
#define MQ3PIN 25
#define MQ7PIN 32
#define MQ135PIN 33

#define DHTTYPE DHT11
#define NEXTION_RX 16
#define NEXTION_TX 17

// Configuración WiFi, NTP y ThingESP
ThingESP32 thing("RAZA19", "LABSense", "Promo2025");
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -21600;
const int daylightOffset_sec = 0;

// Parámetros de Sensores
const float VOLTAJE_ESP = 3.3;
const float RESOLUCION_ADC = 4095.0;
const float RESISTENCIA_CARGA_MQ2 = 5.0; 
const float RESISTENCIA_CARGA_MQ3 = 200.0; 
const float RESISTENCIA_CARGA_MQ7 = 10.0;
const float RESISTENCIA_CARGA_MQ135 = 20.0;

// Curvas de Sensibilidad
const float LPG_A = 574.25;   const float LPG_B = -2.222;
const float SMOKE_A = 305.33; const float SMOKE_B = -3.401;
const float H2_A = 98.866;    const float H2_B = -2.732;
const float ALCOHOL_A = 0.4; const float ALCOHOL_B = -1.5;
const float BENZENE_A = 0.2; const float BENZENE_B = -1.4;
const float CO_A = 99.042; const float CO_B = -1.518;
const float TOLUENE_A = 4.83; const float TOLUENE_B = -2.62;
const float AMMONIA_A = 102.2; const float AMMONIA_B = -2.473;
const float CO2_A = 116.602; const float CO2_B = -2.769; 

// Variables Globales
float R0_MQ2 = 0, R0_MQ3 = 0, R0_MQ7 = 0, R0_MQ135 = 0;
DHT dht(DHTPIN, DHTTYPE);

float temperatura, humedad;
int humo_ppm, lpg_ppm, h2_ppm;
float alcohol_mgL, benceno_mgL;
int co_ppm, co2_ppm;
int tolueno_ppm, amoniaco_ppm, ica_valor;

bool p2_s0_paused = false, p2_s1_paused = false;
bool p3_s0_paused = false;
bool p4_s0_paused = false;
bool p5_s0_paused = false;
bool p7_s0_paused = false;
bool p8_s0_paused = false, p8_s1_paused = false;

char buffer[100];

String HandleResponse(String query);

void sendCommandToNextion(String cmd) {
    Serial2.print(cmd);
    Serial2.write(0xFF); Serial2.write(0xFF); Serial2.write(0xFF);
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
        delay(500);
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
    
    ica_valor = max({humo_ppm, co_ppm, tolueno_ppm, amoniaco_ppm});
    ica_valor = constrain(ica_valor, 0, 500);
}

void setup() {
    Serial.begin(115200); 
    Serial2.begin(9600, SERIAL_8N1, NEXTION_RX, NEXTION_TX);
    
    sendCommandToNextion("page 0");
    delay(50);
    sendCommandToNextion("page0.j_loading.val=0");
    delay(50);

    pinMode(MQ2PIN, INPUT);
    pinMode(MQ3PIN, INPUT);
    pinMode(MQ7PIN, INPUT);
    pinMode(MQ135PIN, INPUT);
    dht.begin();
    
    sendCommandToNextion("page0.t_status.txt=\"Conectando a WiFi...\"");
    thing.SetWiFi("Zamora", "Multitech2023#");
    thing.initDevice(); // Se conecta a WiFi y ThingESP

    sendCommandToNextion("page0.t_status.txt=\"WiFi Conectado\"");
    sendCommandToNextion("page0.j_loading.val=25");
    delay(500);
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    sendCommandToNextion("page0.t_status.txt=\"Calibrando MQ-2...\"");
    sendCommandToNextion("page0.j_loading.val=37");
    R0_MQ2 = calibrarSensor(MQ2PIN, RESISTENCIA_CARGA_MQ2);
    
    sendCommandToNextion("page0.t_status.txt=\"Calibrando MQ-3...\"");
    sendCommandToNextion("page0.j_loading.val=50");
    R0_MQ3 = calibrarSensor(MQ3PIN, RESISTENCIA_CARGA_MQ3);

    sendCommandToNextion("page0.t_status.txt=\"Calibrando MQ-7...\"");
    sendCommandToNextion("page0.j_loading.val=62");
    R0_MQ7 = calibrarSensor(MQ7PIN, RESISTENCIA_CARGA_MQ7);

    sendCommandToNextion("page0.t_status.txt=\"Calibrando MQ-135...\"");
    sendCommandToNextion("page0.j_loading.val=75");
    R0_MQ135 = calibrarSensor(MQ135PIN, RESISTENCIA_CARGA_MQ135);

    sendCommandToNextion("page0.t_status.txt=\"Calibracion Completa\"");
    sendCommandToNextion("page0.j_loading.val=100");
    delay(1000);
    
    sendCommandToNextion("page0.t_status.txt=\"Iniciando...\"");
    delay(1000);
    sendCommandToNextion("page 1");
}

// Función para dar formato a todas las respuestas del chatbot
String formatResponse(String sensorName, String data) {
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
        return "Error al obtener la hora.";
    }
    char timeString[20];
    strftime(timeString, sizeof(timeString), "%d/%m/%Y %H:%M:%S", &timeinfo);
    String response = "📊 **Datos del Sensor " + sensorName + "**\n";
    response += "-----------------------------------\n";
    response += data + "\n";
    response += "-----------------------------------\n";
    response += "📅 **Medición:** " + String(timeString);
    response += "\n Medición realizada por: **LABSense (2025)** | ""LS"" ©️";
    return response;
}

// Función que se encarga de procesar los comandos del chatbot y dar respuestas al monitor serial
String HandleResponse(String query)
{
    query.toLowerCase();
    if (query == "/data dht11") {
        float h = dht.readHumidity();
        float t = dht.readTemperature();
        if (isnan(h) || isnan(t)) {
            return "Error: No se pudo leer la información del sensor DHT11.";
        }
        return formatResponse("DHT11", "🌡️ Temperatura: " + String(t, 2) + " °C\n💧 Humedad: " + String(h, 2) + " %");
    }

    else if (query == "/data mq2") {
        int lpg = (int)leerSensor(LPG_A, LPG_B, R0_MQ2, MQ2PIN, RESISTENCIA_CARGA_MQ2);
        int humo = (int)leerSensor(SMOKE_A, SMOKE_B, R0_MQ2, MQ2PIN, RESISTENCIA_CARGA_MQ2);
        int h2 = (int)leerSensor(H2_A, H2_B, R0_MQ2, MQ2PIN, RESISTENCIA_CARGA_MQ2);
        
        String data = "💨 LPG: " + String(lpg) + " ppm\n";
        data += "🔥 Humo: " + String(humo) + " ppm\n";
        data += "⚛️ Hidrógeno: " + String(h2) + " ppm";
        return formatResponse("MQ-2", data);
    }

    else if (query == "/data mq3") {
        float alcohol = leerSensor(ALCOHOL_A, ALCOHOL_B, R0_MQ3, MQ3PIN, RESISTENCIA_CARGA_MQ3);
        float benceno = leerSensor(BENZENE_A, BENZENE_B, R0_MQ3, MQ3PIN, RESISTENCIA_CARGA_MQ3);
        
        String data = "🍺 Alcohol: " + String(alcohol, 2) + " mg/L\n";
        data += "🧪 Benceno: " + String(benceno, 2) + " mg/L";
        return formatResponse("MQ-3", data);
    }

    else if (query == "/data mq7") {
        int co = (int)leerSensor(CO_A, CO_B, R0_MQ7, MQ7PIN, RESISTENCIA_CARGA_MQ7);

        String data = "☠️ Monóxido de Carbono (CO): " + String(co) + " ppm";
        return formatResponse("MQ-7", data);
    }

    else if (query == "/data mq135") {
        int tolueno = (int)leerSensor(TOLUENE_A, TOLUENE_B, R0_MQ135, MQ135PIN, RESISTENCIA_CARGA_MQ135);
        int amoniaco = (int)leerSensor(AMMONIA_A, AMMONIA_B, R0_MQ135, MQ135PIN, RESISTENCIA_CARGA_MQ135);
        int co2 = (int)leerSensor(CO2_A, CO2_B, R0_MQ135, MQ135PIN, RESISTENCIA_CARGA_MQ135);

        String data = "🏭 Tolueno: " + String(tolueno) + " ppm\n";
        data += "💨 Amoniaco: " + String(amoniaco) + " ppm\n";
        data += "☁️ Dióxido de Carbono (CO2): " + String(co2) + " ppm";
        return formatResponse("MQ-135", data);
    }

    else if (query == "/status") {
        String ipAddress = WiFi.localIP().toString();
        String statusMessage = "✅ ¡Sistema *LABSense* en línea!\n";
        statusMessage += "    Dirección IP: " + ipAddress;
        return formatResponse("Estado del Sistema", statusMessage);
    }

    else {
        return "Comando no válido. Prueba con:\n*/Status\n/Data dht11\n/Data mq2\n/Data mq3\n/Data mq7\n/Data mq135*";
    }
}

void loop() {
    thing.Handle(); // Maneja la comunicación con el chatbot de manera indefinida
    
    // La siguiente sección actualiza la pantalla Nextion
    readAllSensors();
    
    // Página 1 (Dashboard)
    sprintf(buffer, "page1.temp.txt=\"%.1f\"", temperatura); sendCommandToNextion(buffer);
    sprintf(buffer, "page1.hum.txt=\"%.1f\"", humedad); sendCommandToNextion(buffer);
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

    // Valores de las progres bar, las agujas y las funciones de onda   
    long temp_progress = map(temperatura, 0, 50, 0, 100);
    sprintf(buffer, "page1.j1.val=%ld", temp_progress); sendCommandToNextion(buffer);

    long lpg_angle = map(lpg_ppm, 0, 1000, 0, 180); 
    sprintf(buffer, "glpg.val=%ld", lpg_angle); sendCommandToNextion(buffer);
    
    long alcohol_angle_dash = map(alcohol_mgL * 1000, 0, 1500, 0, 180);
    sprintf(buffer, "galc.val=%ld", alcohol_angle_dash); sendCommandToNextion(buffer);

    long ica_angle_dash = map(ica_valor, 0, 500, 0, 180);
    sprintf(buffer, "gica.val=%ld", ica_angle_dash); sendCommandToNextion(buffer);

    int co2_dash_waveform = map(co2_ppm, 0, 1000, 0, 255);
    sprintf(buffer, "add page1.gco2.id,0,%d", constrain(co2_dash_waveform, 0, 255));
    for (int i = 0; i < 10; i++) { sendCommandToNextion(buffer); }

    // Página 2 (Sensor DHT11)
    sprintf(buffer, "page2.temp.txt=\"%.1f\"", temperatura); sendCommandToNextion(buffer);
    sprintf(buffer, "page2.hum.txt=\"%.1f\"", humedad); sendCommandToNextion(buffer);
    long temp_progress_p2 = map(temperatura, 0, 50, 0, 100);
    long hum_progress_p2 = map(humedad, 20, 80, 0, 100);
    sprintf(buffer, "page2.j0.val=%ld", temp_progress_p2); sendCommandToNextion(buffer);
    sprintf(buffer, "page2.j1.val=%ld", hum_progress_p2); sendCommandToNextion(buffer);

    if (!p2_s0_paused) {
        int temp_waveform = map(temperatura, 0, 50, 0, 255);
        sprintf(buffer, "add page2.s0.id,0,%d", constrain(temp_waveform, 0, 255));
        for (int i = 0; i < 10; i++) { sendCommandToNextion(buffer); }
    }
    if (!p2_s1_paused) {
        int hum_waveform = map(humedad, 20, 80, 0, 255);
        sprintf(buffer, "add page2.s1.id,0,%d", constrain(hum_waveform, 0, 255));
        for (int i = 0; i < 10; i++) { sendCommandToNextion(buffer); }
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
        sprintf(buffer, "add page3.s0.id,0,%d", smoke_waveform); for (int i = 0; i < 10; i++) { sendCommandToNextion(buffer); }
        sprintf(buffer, "add page3.s0.id,1,%d", lpg_waveform); for (int i = 0; i < 10; i++) { sendCommandToNextion(buffer); }
        sprintf(buffer, "add page3.s0.id,2,%d", h2_waveform); for (int i = 0; i < 10; i++) { sendCommandToNextion(buffer); }
    }

    // Página 4 (MQ-3)
    sprintf(buffer, "page4.Alcohol.txt=\"%.2f\"", alcohol_mgL); sendCommandToNextion(buffer);
    sprintf(buffer, "page4.Benceno.txt=\"%.2f\"", benceno_mgL); sendCommandToNextion(buffer);
    
    long alcohol_angle_p4 = map(alcohol_mgL * 1000, 0, 1500, 0, 180);
    sprintf(buffer, "g_alc_p4.val=%ld", alcohol_angle_p4); sendCommandToNextion(buffer);

    if (!p4_s0_paused) {
        int alcohol_waveform = map(alcohol_mgL, 0, 10, 0, 255);
        int benceno_waveform = map(benceno_mgL, 0, 10, 0, 255);
        sprintf(buffer, "add page4.s0.id,0,%d", constrain(alcohol_waveform, 0, 255)); for (int i = 0; i < 10; i++) { sendCommandToNextion(buffer); }
        sprintf(buffer, "add page4.s0.id,1,%d", constrain(benceno_waveform, 0, 255)); for (int i = 0; i < 10; i++) { sendCommandToNextion(buffer); }
    }

    // Página 5 (MQ-3)
    sprintf(buffer, "page5.Alcohol.txt=\"%.2f\"", alcohol_mgL); sendCommandToNextion(buffer);
    sprintf(buffer, "page5.Benceno.txt=\"%.2f\"", benceno_mgL); sendCommandToNextion(buffer);

    long alcohol_angle_p5 = map(alcohol_mgL * 1000, 0, 1500, 0, 180);
    sprintf(buffer, "g_alc_p5.val=%ld", alcohol_angle_p5); sendCommandToNextion(buffer);

    if (!p5_s0_paused) {
        int alcohol_waveform = map(alcohol_mgL, 0, 4, 0, 255);
        int benceno_waveform = map(benceno_mgL, 0, 4, 0, 255);
        sprintf(buffer, "add page5.s0.id,0,%d", constrain(alcohol_waveform, 0, 255)); for (int i = 0; i < 10; i++) { sendCommandToNextion(buffer); }
        sprintf(buffer, "add page5.s0.id,1,%d", constrain(benceno_waveform, 0, 255)); for (int i = 0; i < 10; i++) { sendCommandToNextion(buffer); }
    }
    
    // Página 7 (MQ-7)
    sprintf(buffer, "page7.CO.txt=\"%d\"", co_ppm); sendCommandToNextion(buffer);
    sprintf(buffer, "page7.CO2.txt=\"%d\"", co2_ppm); sendCommandToNextion(buffer);
    if (!p7_s0_paused) {
        int co_waveform = map(co_ppm, 0, 1000, 0, 255);
        int co2_waveform = map(co2_ppm, 0, 1000, 0, 255);
        sprintf(buffer, "add page7.s0.id,0,%d", constrain(co_waveform, 0, 255)); for (int i = 0; i < 10; i++) { sendCommandToNextion(buffer); }
        sprintf(buffer, "add page7.s0.id,1,%d", constrain(co2_waveform, 0, 255)); for (int i = 0; i < 10; i++) { sendCommandToNextion(buffer); }
    }

    // Página 8 (MQ-135 Gráficas)
    sprintf(buffer, "page8.Tolueno.txt=\"%d\"", tolueno_ppm); sendCommandToNextion(buffer);
    sprintf(buffer, "page8.Amoniaco.txt=\"%d\"", amoniaco_ppm); sendCommandToNextion(buffer);
    sprintf(buffer, "page8.ICA.txt=\"%d\"", ica_valor); sendCommandToNextion(buffer);
    if (!p8_s0_paused) {
        int tolueno_waveform = mapLog(tolueno_ppm, 100, 10000, 0, 255);
        int amoniaco_waveform = mapLog(amoniaco_ppm, 100, 10000, 0, 255);
        sprintf(buffer, "add page8.s0.id,0,%d", tolueno_waveform); for (int i = 0; i < 10; i++) { sendCommandToNextion(buffer); }
        sprintf(buffer, "add page8.s0.id,1,%d", amoniaco_waveform); for (int i = 0; i < 10; i++) { sendCommandToNextion(buffer); }
    }
    if (!p8_s1_paused) {
        int ica_waveform = map(ica_valor, 0, 500, 0, 255);
        sprintf(buffer, "add page8.s1.id,0,%d", constrain(ica_waveform, 0, 255));
        for (int i = 0; i < 10; i++) { sendCommandToNextion(buffer); }
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

    if (WiFi.status() == WL_CONNECTED) {
        struct tm timeinfo;
        getLocalTime(&timeinfo);
        strftime(buffer, sizeof(buffer), "%H:%M", &timeinfo);
        String time_str(buffer);
        sendCommandToNextion("page1.time.txt=\"" + time_str + "\"");
        sendCommandToNextion("page2.time.txt=\"" + time_str + "\"");
        sendCommandToNextion("page3.time.txt=\"" + time_str + "\"");
        sendCommandToNextion("page4.time.txt=\"" + time_str + "\"");
        sendCommandToNextion("page5.time.txt=\"" + time_str + "\"");
        sendCommandToNextion("page7.time.txt=\"" + time_str + "\"");
        sendCommandToNextion("page8.time.txt=\"" + time_str + "\"");
        sendCommandToNextion("page9.time.txt=\"" + time_str + "\"");
        
        strftime(buffer, sizeof(buffer), "%Y-%m-%d", &timeinfo);
        String date_str(buffer);
        sendCommandToNextion("page1.date.txt=\"" + date_str + "\"");
    } else {
        String no_time_str = "--:--";
        sendCommandToNextion("page1.time.txt=\"" + no_time_str + "\"");
        sendCommandToNextion("page2.time.txt=\"" + no_time_str + "\"");
        sendCommandToNextion("page3.time.txt=\"" + no_time_str + "\"");
        sendCommandToNextion("page4.time.txt=\"" + no_time_str + "\"");
        sendCommandToNextion("page5.time.txt=\"" + no_time_str + "\"");
        sendCommandToNextion("page7.time.txt=\"" + no_time_str + "\"");
        sendCommandToNextion("page8.time.txt=\"" + no_time_str + "\"");
        sendCommandToNextion("page9.time.txt=\"" + no_time_str + "\"");
                                
        String no_date_str = "----/--/--";
        sendCommandToNextion("page1.date.txt=\"" + no_date_str + "\"");
    }
    
    delay(250);
}
