// Body Muscle Evaluator (BME)
// Dibuat oleh
// Michael Liebing / 18323016
// Jonathan Otto / 18323017
// Jerry Alexander Tjoa / 18323026
// Sarjana Teknik Biomedis
// Sekolah Teknik Elektro dan Informatika Rekayasa
// Institut Teknologi Bandung

// Library
#include <Arduino.h>
#include <WiFi.h>
#include <driver/rtc_io.h>
#include <Preferences.h>

// Deklarasi Pin
const int batteryPin = A0; 
const int EMGPin = A1;     
const int ledPin1 = D8;    
const int ledPin2 = D9;    
const int ledPin3 = D10;   
const int PMOSPin = D5;    
const int LDO_EN_Pin = D7; 
const int touchPin = D2; 

// Variabel Wifi
const int DEVICE_ID = 1;
const char* ssid = "BMEhost";
const char* password = "BMEhosting";
const uint16_t port = 8080;
const float wifiTimeoutSeconds = 30.0;
const unsigned long wifiTimeoutMs = (unsigned long)(wifiTimeoutSeconds * 1000);
WiFiClient client;

// Variabel Sistem
Preferences pref;
int srR = 0, srG = 0, srB = 12; 
int cnR = 0, cnG = 12, cnB = 0; 
bool syncSent = false; 
float smoothedVbatt = -1.0; 
const float V_BATT_MIN = 3.30f; 
const float V_BATT_MAX = 4.03f; 
bool systemActive = false;
bool greenLedDone = false;
unsigned long connectedMillis = 0;
unsigned long lastBatteryMillis = 0;
unsigned long lastDisconnectTime = 0; 
const int dimIntensity = 12;

// Variabel Sinyal EMG
const float samplingFrequency = 2500.0;
const int PACKET_SIZE = 50; 
float emgBuffer[PACKET_SIZE];
float rectBuffer[PACKET_SIZE];
int bufferIndex = 0;
const unsigned long intervalMicros = (unsigned long)(1000000.0 / samplingFrequency);
unsigned long previousMicros = 0;
const int emg_offset = 2022;

// Variabel Filter
double x_bp[3] = {0}, y_bp[3] = {0};
const double b_bp[3] = { 0.37496517, 0.0, -0.37496517 };
const double a_bp[3] = { 1.0, -1.21081103, 0.25006966 };
const int MA_WINDOW = 125; 
float ma_buffer[MA_WINDOW] = {0};
int ma_index = 0;
float ma_sum = 0;


// Variabel Deteksi Onset
const float threshold_detection = 15;
const unsigned long debounce_samples = 1250;
unsigned long last_onset_idx = 0;
unsigned long sampleCount = 0;
bool is_active = false;
bool onset_state_changed = false;

// Deklarasi Fungsi
void setLED(int r, int g, int b);
void updateAnalogPower(bool state);
void goToSleep();
void tryReconnect();
double Filter_BPF(double inputVal);
double Filter_LPF(double inputVal);

// Fungsi Setup
void setup() 
{
    Serial.begin(115200);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    pinMode(ledPin1, OUTPUT); 
    pinMode(ledPin2, OUTPUT); 
    pinMode(ledPin3, OUTPUT);
    pinMode(touchPin, INPUT); 
    pinMode(PMOSPin, OUTPUT); 
    pinMode(LDO_EN_Pin, OUTPUT);
    updateAnalogPower(false);
    pref.begin("bme_cfg", false);
    srR = pref.getInt("srR", 0); 
    srG = pref.getInt("srG", 0); 
    srB = pref.getInt("srB", dimIntensity);
    cnR = pref.getInt("cnR", 0); 
    cnG = pref.getInt("cnG", 12); 
    cnB = pref.getInt("cnB", 0);
    uint32_t initVsum = 0;
    for(int i = 0; i < 100; i++) 
    { 
        initVsum += analogRead(batteryPin); 
        delay(1);
    }
    smoothedVbatt = (2.0f * ((float)initVsum / 100.0f) / 4095.0f) * 3.3f;
    WiFi.begin(ssid, password);
    unsigned long startAttempt = millis();
    while (millis() - startAttempt < wifiTimeoutMs) 
    {
        if (WiFi.status() == WL_CONNECTED) 
        {
            systemActive = true; 
            connectedMillis = millis(); 
            setLED(cnR, cnG, cnB); 
            delay(2500); 
            return; 
        }
        ((millis() / 1000) % 2 == 0) ? setLED(srR, srG, srB) : setLED(0, 0, 0);
        delay(100);
    }
    goToSleep();
}

// Fungsi Loop
void loop()
{
    unsigned long currentMillis = millis();
    unsigned long currentMicros = micros();
    if (currentMillis - lastBatteryMillis >= 1000) 
    {
        lastBatteryMillis = currentMillis;
        float rawADC = (float)analogRead(batteryPin);
        float currentVbatt = (2.0f * (rawADC / 4095.0f) * 3.3f);
        smoothedVbatt = (0.9f * smoothedVbatt) + (0.1f * currentVbatt);
        if (systemActive && WiFi.status() == WL_CONNECTED && client.connected()) {
            int percentage = (int)constrain(((smoothedVbatt - V_BATT_MIN) / (V_BATT_MAX - V_BATT_MIN)) * 100.0f, 0.0f, 100.0f);
            client.print("B"); 
            client.print(DEVICE_ID); 
            client.print(":"); 
            client.println(percentage);
        }
    }
    if (systemActive && !greenLedDone && currentMillis - connectedMillis >= 2000) 
    {
        setLED(0, 0, 0); 
        greenLedDone = true;
    }
    if (systemActive && WiFi.status() != WL_CONNECTED) 
    {
        updateAnalogPower(false); 
        if (lastDisconnectTime == 0) 
        {
            lastDisconnectTime = currentMillis;
        }
        if (currentMillis - lastDisconnectTime > 3000) 
        {
            lastDisconnectTime = 0; tryReconnect();
        }
    }
    if (systemActive && WiFi.status() == WL_CONNECTED) 
    {
        if (!client.connected()) 
        {
            updateAnalogPower(false); 
            client.connect(WiFi.gatewayIP(), port); syncSent = false;
        } 
        else 
        {
            if (!syncSent) 
            {
                client.printf("SYNC%d:%d,%d,%d,%d,%d,%d\n", DEVICE_ID, srR, srG, srB, cnR, cnG, cnB);
                syncSent = true;
            }
            static String requestBuffer = "";
            while (client.available() > 0) 
            {
                char c = client.read();
                if (c == '\n') {
                    requestBuffer.trim();
                    if (requestBuffer.startsWith("MOS:1")) 
                    {
                        updateAnalogPower(true);
                    }
                    else if (requestBuffer.startsWith("MOS:0")) 
                    {
                        updateAnalogPower(false);
                    }
                    else if (requestBuffer.startsWith("PWM1:")) 
                    {
                        analogWrite(ledPin1, requestBuffer.substring(5).toInt());
                    }
                    else if (requestBuffer.startsWith("PWM2:")) 
                    {
                        analogWrite(ledPin2, requestBuffer.substring(5).toInt());
                    }
                    else if (requestBuffer.startsWith("PWM3:")) 
                    {
                        analogWrite(ledPin3, requestBuffer.substring(5).toInt());
                    }
                    else if (requestBuffer.startsWith("1_SR_R:")) 
                    { 
                        srR = requestBuffer.substring(7).toInt(); 
                        pref.putInt("srR", srR); 
                    }
                    else if (requestBuffer.startsWith("1_SR_G:")) 
                    {
                        srG = requestBuffer.substring(7).toInt();
                        pref.putInt("srG", srG);
                    }
                    else if (requestBuffer.startsWith("1_SR_B:")) 
                    {
                        srB = requestBuffer.substring(7).toInt();
                        pref.putInt("srB", srB);
                    }
                    else if (requestBuffer.startsWith("1_CN_R:")) 
                    {
                        cnR = requestBuffer.substring(7).toInt();
                        pref.putInt("cnR", cnR);
                    }
                    else if (requestBuffer.startsWith("1_CN_G:")) 
                    {
                        cnG = requestBuffer.substring(7).toInt();
                        pref.putInt("cnG", cnG);
                    }
                    else if (requestBuffer.startsWith("1_CN_B:")) 
                    {
                        cnB = requestBuffer.substring(7).toInt();
                        pref.putInt("cnB", cnB);
                    }
                    requestBuffer = ""; 
                } 
                else 
                {
                    requestBuffer += c;
                } 
            }
            
            if (currentMicros - previousMicros >= intervalMicros) 
            {
                previousMicros += intervalMicros;
                sampleCount++;
                
                int rawValue = analogRead(EMGPin);
                int centeredValue = rawValue - emg_offset;
                double bpValue = Filter_BPF((double)centeredValue);
                float emgMV = (bpValue / 4095.0f) * 3300.0f; 
                double emgRect = abs(emgMV);
                double envelope = Filter_LPF_MA(emgRect);
                if (envelope > threshold_detection) {
                    if (!is_active && (sampleCount - last_onset_idx > debounce_samples)) {
                        is_active = true;
                        last_onset_idx = sampleCount;
                    }
                } else {
                    is_active = false;
                }

                if (bufferIndex < PACKET_SIZE) 
                {
                    emgBuffer[bufferIndex] = emgMV;
                    rectBuffer[bufferIndex] = (float)envelope; // Dikirim sebagai Rectifier ke Android
                    bufferIndex++;
                }
                
                if (bufferIndex >= PACKET_SIZE) 
                {
                    char packetE[400];
                    char packetR[400];
                    int lenE = sprintf(packetE, "E%d:", DEVICE_ID);
                    int lenR = sprintf(packetR, "R%d:", DEVICE_ID);
                    for (int i = 0; i < PACKET_SIZE; i++) {
                        lenE += sprintf(packetE + lenE, "%.2f%s", emgBuffer[i], (i < PACKET_SIZE - 1) ? "," : "");
                        lenR += sprintf(packetR + lenR, "%.2f%s", rectBuffer[i], (i < PACKET_SIZE - 1) ? "," : "");
                    }
                    client.println(packetE); 
                    client.println(packetR); 
                    client.printf("O%d:%d\n", DEVICE_ID, is_active ? 1 : 0);
                    
                    bufferIndex = 0; 
                }
            }
        }
    }
}

// Fungsi BPF
double Filter_BPF(double inputVal) {
    x_bp[2] = x_bp[1]; x_bp[1] = x_bp[0]; x_bp[0] = inputVal;
    y_bp[2] = y_bp[1]; y_bp[1] = y_bp[0];
    y_bp[0] = (b_bp[0]*x_bp[0] + b_bp[1]*x_bp[1] + b_bp[2]*x_bp[2]) - (a_bp[1]*y_bp[1] + a_bp[2]*y_bp[2]);
    return y_bp[0];
}

// Fungsi LPF Moving Average
float Filter_LPF_MA(float inputVal) {
    ma_sum -= ma_buffer[ma_index];
    ma_buffer[ma_index] = inputVal;
    ma_sum += ma_buffer[ma_index];
    ma_index = (ma_index + 1) % MA_WINDOW;
    return ma_sum / MA_WINDOW;
}

// Fungsi Pengaturan LED
void setLED(int r, int g, int b) 
{
    analogWrite(ledPin1, r);
    analogWrite(ledPin2, g);
    analogWrite(ledPin3, b); 
}

// Fungsi Pengaturan Daya Analog
void updateAnalogPower(bool state) 
{
    digitalWrite(PMOSPin, state ? HIGH : LOW);
    digitalWrite(LDO_EN_Pin, state ? HIGH : LOW); 
}

// Fungsi Sleep Mode
void goToSleep() 
{
    setLED(0, 0, 0); 
    updateAnalogPower(false); 
    WiFi.disconnect(true); 
    WiFi.mode(WIFI_OFF);
    rtc_gpio_init((gpio_num_t)GPIO_NUM_2); 
    rtc_gpio_set_direction((gpio_num_t)GPIO_NUM_2, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_en((gpio_num_t)GPIO_NUM_2); 
    esp_sleep_enable_ext1_wakeup(1ULL << GPIO_NUM_2, ESP_EXT1_WAKEUP_ANY_HIGH);
    esp_deep_sleep_start();
}

// Fungsi Mencari Host
void tryReconnect() 
{
    updateAnalogPower(false); 
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
        ((millis() / 1000) % 2 == 0) ? setLED(srR, srG, srB) : setLED(0, 0, 0); 
        delay(100);
    }
    goToSleep();
}
