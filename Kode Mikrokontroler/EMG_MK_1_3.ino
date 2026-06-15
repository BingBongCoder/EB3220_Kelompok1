#include <Arduino.h>
#include <WiFi.h>
#include <driver/rtc_io.h>
#include <Preferences.h>

// =============================================================
// DEKLARASI PIN
// =============================================================
const int batteryPin = A0; 
const int EMGPin = A1;     
const int ledPin1 = D8;    
const int ledPin2 = D9;    
const int ledPin3 = D10;   
const int PMOSPin = D5;    
const int touchPin = D2; 

// =============================================================
// KONFIGURASI WIFI & SERVER
// =============================================================
const int DEVICE_ID = 1;
const char* ssid = "BMEhost";
const char* password = "BMEhosting";
const uint16_t port = 8080;

const float wifiTimeoutSeconds = 30.0;
const unsigned long wifiTimeoutMs = (unsigned long)(wifiTimeoutSeconds * 1000);
const float transmissionFrequency = 50.0;

// =============================================================
// KONFIGURASI SAMPLING EMG
// =============================================================
const float samplingFrequency = 2500.0;
const int PACKET_SIZE = 50; 
float emgBuffer[PACKET_SIZE];
int bufferIndex = 0;

const unsigned long intervalMicros = (unsigned long)(1000000.0 / samplingFrequency);
unsigned long previousMicros = 0;

// =============================================================
// VARIABEL SISTEM
// =============================================================
Preferences pref;
int srR = 0;
int srG = 0;
int srB = 12; 
int cnR = 0;
int cnG = 12;
int cnB = 0; 
bool syncSent = false; 

float smoothedVbatt = -1.0; 
const float V_BATT_MIN = 3.30f; // Voltase saat 0%
const float V_BATT_MAX = 4.03f; // Voltase saat 100% (sesuai hasil pengukuranmu)
bool systemActive = false;
bool greenLedDone = false;
unsigned long connectedMillis = 0;
unsigned long lastBatteryMillis = 0;
unsigned long lastDisconnectTime = 0; 
const int dimIntensity = 12; 

WiFiClient client;

// =============================================================
// KOEFISIEN FILTER
// =============================================================
float x_lpf[3], y_lpf[3], x_hpf[3], y_hpf[3];
float filteredEnvelope = 0;

// Konstanta LPF Orde 2
const float b_lpf[3] = {0.2065720838f, 0.4131441677f, 0.2065720838f};
const float a_lpf[3] = {1.0000000000f, -0.3695273774f, 0.1958157127f};

// Konstanta HPF Orde 2
const float b_hpf[3] = {0.9650809863f, -1.9301619727f, 0.9650809863f};
const float a_hpf[3] = {1.0000000000f, -1.9289422633f, 0.9313816821f};

// =============================================================
// DEKLARASI FUNGSI
// =============================================================
void setLED(int r, int g, int b);
void updateMosfet(bool state);
void goToSleep();
void tryReconnect();
float Filter_LPF(float inputVal);
float Filter_HPF(float inputVal);

// =============================================================
// FUNGSI SETUP
// =============================================================
void setup() {
    Serial.begin(115200);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    
    pinMode(ledPin1, OUTPUT);
    pinMode(ledPin2, OUTPUT);
    pinMode(ledPin3, OUTPUT);
    pinMode(touchPin, INPUT); 
    pinMode(PMOSPin, OUTPUT);
    
    updateMosfet(false);

    pref.begin("bme_cfg", false);
    srR = pref.getInt("srR", 0); 
    srG = pref.getInt("srG", 0); 
    srB = pref.getInt("srB", dimIntensity);
    
    cnR = pref.getInt("cnR", 0); 
    cnG = pref.getInt("cnG", 12); 
    cnB = pref.getInt("cnB", 0);

    // INISIALISASI BATERAI
    uint32_t initVsum = 0;
    const int initNumSamples = 100;
    
    for(int i = 0; i < initNumSamples; i++) {
        initVsum += analogRead(batteryPin);
        delay(1); 
    }
    
    // Hardening: Eksplisit cast ke float untuk mencegah int division error
    smoothedVbatt = (2.0f * ((float)initVsum / (float)initNumSamples) / 4095.0f) * 3.3f;
    
    WiFi.begin(ssid, password);
    unsigned long startAttempt = millis();
    
    while (millis() - startAttempt < wifiTimeoutMs) {
        if (WiFi.status() == WL_CONNECTED) {
            systemActive = true;
            connectedMillis = millis();
            setLED(cnR, cnG, cnB); 
            delay(2500); 
            return; 
        }
        
        if ((millis() / 1000) % 2 == 0) {
            setLED(srR, srG, srB);
        } else {
            setLED(0, 0, 0);
        }
        
        delay(100);
    }
    
    goToSleep();
}

// =============================================================
// FUNGSI LOOP UTAMA
// =============================================================
void loop() {
    unsigned long currentMillis = millis();
    unsigned long currentMicros = micros();

    // PEMBACAAN BATERAI PER 1 DETIK
if (currentMillis - lastBatteryMillis >= 1000) {
    lastBatteryMillis = currentMillis;
    
    // 1. Baca ADC dan konversi ke tegangan (2.0f adalah pengali dari voltage divider)
    float rawADC = (float)analogRead(batteryPin);
    float currentVbatt = (2.0f * (rawADC / 4095.0f) * 3.3f);
    
    // 2. Low Pass Filter (Smooth)
    smoothedVbatt = (0.9f * smoothedVbatt) + (0.1f * currentVbatt);
    
    if (systemActive && WiFi.status() == WL_CONNECTED && client.connected()) {
        
        // 3. Menghitung persentase berdasarkan range custom
        // Rumus: ((V_sekarang - V_min) / (V_max - V_min)) * 100
        float range = V_BATT_MAX - V_BATT_MIN;
        float percentage = ((smoothedVbatt - V_BATT_MIN) / range) * 100.0f;
        
        // 4. Constrain agar tidak di bawah 0% atau di atas 100%
        int finalPercentage = (int)constrain(percentage, 0.0f, 100.0f);
        
        // Kirim ke client
        client.print("B"); 
        client.print(DEVICE_ID);
        client.print(":");
        client.println(finalPercentage);
    }
}

    if (systemActive && !greenLedDone) {
        if (currentMillis - connectedMillis >= 2000) {
            setLED(0, 0, 0);
            greenLedDone = true;
        }
    }
    
    if (systemActive && WiFi.status() != WL_CONNECTED) {
        updateMosfet(false); 
        
        if (lastDisconnectTime == 0) {
            lastDisconnectTime = currentMillis;
        }
        
        if (currentMillis - lastDisconnectTime > 3000) { 
            lastDisconnectTime = 0; 
            tryReconnect(); 
        }
    }
    
    if (systemActive && WiFi.status() == WL_CONNECTED) {
        if (!client.connected()) {
            updateMosfet(false);
            client.connect(WiFi.gatewayIP(), port);
            syncSent = false;
        } else {
            
            if (!syncSent) {
                client.print("SYNC");
                client.print(DEVICE_ID);
                client.print(":");
                client.printf("%d,%d,%d,%d,%d,%d\n", srR, srG, srB, cnR, cnG, cnB);
                syncSent = true;
            }

            // Pembacaan Data WiFi Non-Blocking via Buffer Statis
            static String requestBuffer = "";
            while (client.available() > 0) {
                char c = client.read();
                if (c == '\n') {
                    requestBuffer.trim();
                    
                    if (requestBuffer.startsWith("MOS:1")) {
                        updateMosfet(true);
                    } else if (requestBuffer.startsWith("MOS:0")) {
                        updateMosfet(false);
                    } else if (requestBuffer.startsWith("PWM1:")) {
                        analogWrite(ledPin1, requestBuffer.substring(5).toInt());
                    } else if (requestBuffer.startsWith("PWM2:")) {
                        analogWrite(ledPin2, requestBuffer.substring(5).toInt());
                    } else if (requestBuffer.startsWith("PWM3:")) {
                        analogWrite(ledPin3, requestBuffer.substring(5).toInt());
                    }
                    
                    String cmd_srR = String(DEVICE_ID) + "_SR_R:";
                    String cmd_srG = String(DEVICE_ID) + "_SR_G:";
                    String cmd_srB = String(DEVICE_ID) + "_SR_B:";
                    String cmd_cnR = String(DEVICE_ID) + "_CN_R:";
                    String cmd_cnG = String(DEVICE_ID) + "_CN_G:";
                    String cmd_cnB = String(DEVICE_ID) + "_CN_B:";

                    if (requestBuffer.startsWith(cmd_srR)) { 
                        srR = requestBuffer.substring(cmd_srR.length()).toInt(); 
                        pref.putInt("srR", srR); 
                    } else if (requestBuffer.startsWith(cmd_srG)) { 
                        srG = requestBuffer.substring(cmd_srG.length()).toInt(); 
                        pref.putInt("srG", srG); 
                    } else if (requestBuffer.startsWith(cmd_srB)) { 
                        srB = requestBuffer.substring(cmd_srB.length()).toInt(); 
                        pref.putInt("srB", srB); 
                    } else if (requestBuffer.startsWith(cmd_cnR)) { 
                        cnR = requestBuffer.substring(cmd_cnR.length()).toInt(); 
                        pref.putInt("cnR", cnR); 
                        setLED(cnR, cnG, cnB); 
                    } else if (requestBuffer.startsWith(cmd_cnG)) { 
                        cnG = requestBuffer.substring(cmd_cnG.length()).toInt(); 
                        pref.putInt("cnG", cnG); 
                        setLED(cnR, cnG, cnB); 
                    } else if (requestBuffer.startsWith(cmd_cnB)) { 
                        cnB = requestBuffer.substring(cmd_cnB.length()).toInt(); 
                        pref.putInt("cnB", cnB); 
                        setLED(cnR, cnG, cnB); 
                    }
                    
                    requestBuffer = ""; 
                } else {
                    requestBuffer += c; 
                }
            }
            
            // TIMING SAKLEK UNTUK EMG (2500 Hz)
            if (currentMicros - previousMicros >= intervalMicros) {
                previousMicros += intervalMicros;
                
                int rawValue = analogRead(EMGPin);
                float hpfValue = Filter_HPF((float)rawValue);
                float lpfValue = Filter_LPF(hpfValue); // Kaskade: Input mentah -> HPF -> LPF
                
                float emgMV = (lpfValue / 4095.0f) * 3300.0f;

                if (bufferIndex < PACKET_SIZE) {
                    emgBuffer[bufferIndex] = emgMV;
                    bufferIndex++;
                }

                if (bufferIndex >= PACKET_SIZE) {
                    String packetData = "";
                    packetData.reserve(400); 
                    
                    packetData += "E";
                    packetData += DEVICE_ID;
                    packetData += ":";
                    
                    for (int i = 0; i < PACKET_SIZE; i++) {
                        packetData += String(emgBuffer[i], 2); 
                        
                        if (i < PACKET_SIZE - 1) {
                            packetData += ",";
                        }
                    }
                    
                    client.println(packetData); 
                    bufferIndex = 0; 
                }
            }
        }
    }
}

// =============================================================
// FUNGSI FILTER DAN KONTROL
// =============================================================
float Filter_LPF(float inputVal) {
    x_lpf[2] = x_lpf[1];
    x_lpf[1] = x_lpf[0];
    
    y_lpf[2] = y_lpf[1];
    y_lpf[1] = y_lpf[0];
    
    x_lpf[0] = inputVal;
    y_lpf[0] = b_lpf[0]*x_lpf[0] + b_lpf[1]*x_lpf[1] + b_lpf[2]*x_lpf[2] - a_lpf[1]*y_lpf[1] - a_lpf[2]*y_lpf[2];
    
    return y_lpf[0];
}

float Filter_HPF(float inputVal) {
    x_hpf[2] = x_hpf[1];
    x_hpf[1] = x_hpf[0];
    
    y_hpf[2] = y_hpf[1];
    y_hpf[1] = y_hpf[0];
    
    x_hpf[0] = inputVal;
    y_hpf[0] = b_hpf[0]*x_hpf[0] + b_hpf[1]*x_hpf[1] + b_hpf[2]*x_hpf[2] - a_hpf[1]*y_hpf[1] - a_hpf[2]*y_hpf[2];
    
    return y_hpf[0];
}

void setLED(int r, int g, int b) {
    analogWrite(ledPin1, r); 
    analogWrite(ledPin2, g); 
    analogWrite(ledPin3, b);
}

void updateMosfet(bool state) {
    if (state) { 
        digitalWrite(PMOSPin, HIGH); 
    } else { 
        digitalWrite(PMOSPin, LOW); 
    }
}

void goToSleep() {
    setLED(0, 0, 0); 
    updateMosfet(false);
    WiFi.disconnect(true); 
    WiFi.mode(WIFI_OFF);
    
    rtc_gpio_init((gpio_num_t)GPIO_NUM_2);
    rtc_gpio_set_direction((gpio_num_t)GPIO_NUM_2, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_en((gpio_num_t)GPIO_NUM_2);
    esp_sleep_enable_ext1_wakeup(1ULL << GPIO_NUM_2, ESP_EXT1_WAKEUP_ANY_HIGH);
    
    esp_deep_sleep_start();
}

void tryReconnect() {
    updateMosfet(false); 
    systemActive = false; 
    greenLedDone = false; 
    syncSent = false;
    
    unsigned long startAttempt = millis();
    
    while (millis() - startAttempt < wifiTimeoutMs) {
        if (WiFi.status() == WL_CONNECTED) {
            systemActive = true; 
            connectedMillis = millis();
            setLED(cnR, cnG, cnB); 
            delay(2500);
            return; 
        }
        
        if ((millis() / 1000) % 2 == 0) {
            setLED(srR, srG, srB);
        } else {
            setLED(0, 0, 0);
        }
        
        delay(100);
    }
    
    goToSleep();
}
