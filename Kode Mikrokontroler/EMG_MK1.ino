#include <Arduino.h>
#include <WiFi.h>
#include <driver/rtc_io.h>

// Deklarasi Pin
const int batteryPin = A0; 
const int EMGPin = A1;     
const int ledPin1 = D8;    
const int ledPin2 = D9;    
const int ledPin3 = D10;   
const int PMOSPin = D5;    
const int touchPin = D2; 

// Variabel Sensor dan Sinyal
const unsigned long intervalMicros = 1000;
unsigned long previousMicros = 0;
float x[4] = {0, 0, 0, 0}; 
float y[4] = {0, 0, 0, 0}; 
float x_bsf[5] = {0, 0, 0, 0, 0}; 
float y_bsf[5] = {0, 0, 0, 0, 0}; 
float x_hpf[3] = {0, 0, 0};
float y_hpf[3] = {0, 0, 0};
float filteredEnvelope = 0;

// Konstanta Filter Digital
const float b[] = {0, 0.8005924035f, 1.6011848069f, 0.8005924035f};
const float a[] = {0, 1.0000000000f, 1.5610180758f, 0.6413515381f};
const float b_bsf[] = {0.991153595101663f, -3.770646770422277f, 5.568476159765916f, -3.770646770422277f, 0.991153595101663f};
const float a_bsf[] = {1.000000000000000f, -3.787399533082511f, 5.568397899355124f, -3.753894007762051f, 0.982385450614127f};
const float b_hpf[] = {0.914969144113082f, -1.829938288226165f, 0.914969144113082f};
const float a_hpf[] = {1.000000000000000f, -1.822694925196308f, 0.837181651256022f};

// Variabel Wifi
const char* ssid = "BMEhost";
const char* password = "BMEhosting";
const uint16_t port = 8080;
WiFiClient client;

// Variabel Status
bool systemActive = false;
bool greenLedDone = false;
unsigned long connectedMillis = 0;
unsigned long previousSendMillis = 0;
unsigned long lastBatteryMillis = 0;
unsigned long lastDisconnectTime = 0; 
const int dimIntensity = 12; // 5% intensitas LED

// Prototipe Fungsi
void setLED(int r, int g, int b);
void updateMosfet(bool state);
void goToSleep();
void tryReconnect();
float Filter_lowpass(float inputVal);
float Filter_BSF(float inputVal);
float Filter_HPF(float inputVal);

void setup() {
  Serial.begin(115200);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  pinMode(ledPin1, OUTPUT);
  pinMode(ledPin2, OUTPUT);
  pinMode(ledPin3, OUTPUT);
  pinMode(touchPin, INPUT); 
  updateMosfet(false);
  Serial.println("BME-> Body Muscle Evaluator");
  Serial.println("Dibuat oleh:");
  Serial.println("Michael Liebing      18323016");
  Serial.println("Jonathan Otto        18323017");
  Serial.println("Jerry Alexander Tjoa 18323026");

  WiFi.begin(ssid, password);
  unsigned long startAttempt = millis();
  while (millis() - startAttempt < 20000) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi Connected!");
      systemActive = true;
      connectedMillis = millis();
      setLED(0, dimIntensity, 0); 
      delay(2500); 
      updateMosfet(true); 
      return;
    }
    if ((millis() / 1000) % 2 == 0) setLED(0, 0, dimIntensity);
    else setLED(0, 0, 0);
    delay(100);
  }
  goToSleep();
}

void loop() {
  unsigned long currentMillis = millis();
  unsigned long currentMicros = micros();

  // LED hijau mati jika 2 detik telah berlalu sejak BME terkoneksi dengan host
  if (systemActive && !greenLedDone) {
    if (currentMillis - connectedMillis >= 2000) {
      setLED(0, 0, 0);
      greenLedDone = true;
    }
  }

  // BME melakukan koneksi dengan host
  if (systemActive && WiFi.status() != WL_CONNECTED) {
    if (lastDisconnectTime == 0) lastDisconnectTime = currentMillis;
    if (currentMillis - lastDisconnectTime > 3000) { 
        lastDisconnectTime = 0; 
        tryReconnect(); 
    }
  } else {
    lastDisconnectTime = 0; 
  }

  // BME mengirim data
  if (systemActive && WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      client.connect(WiFi.gatewayIP(), port);
    } else {
      if (client.available()) {
        String request = client.readStringUntil('\n');
        request.trim();
        if (greenLedDone) {
          if (request.startsWith("PWM1:")) analogWrite(ledPin1, request.substring(5).toInt());
          else if (request.startsWith("PWM2:")) analogWrite(ledPin2, request.substring(5).toInt());
          else if (request.startsWith("PWM3:")) analogWrite(ledPin3, request.substring(5).toInt());
        }
      }
      if (currentMicros - previousMicros >= intervalMicros) {
        previousMicros += intervalMicros;
        int rawValue = analogRead(EMGPin);
        float hpfValue = Filter_HPF((float)rawValue);
        float notchedValue = Filter_BSF(hpfValue);
        float rectifiedValue = fabs(notchedValue); 
        filteredEnvelope = Filter_lowpass(rectifiedValue);
        Serial.print(">Raw:");
        Serial.println(rawValue);
        Serial.print(">Notched:");
        Serial.println(notchedValue);
        Serial.print(">HighPassFiltered:");
        Serial.println(hpfValue);
        Serial.print(">LowPassFiltered:");
        Serial.println(filteredEnvelope);
      }
      if (currentMillis - previousSendMillis >= 20) {
        previousSendMillis = currentMillis;
        client.println(filteredEnvelope);
      }
      if (currentMillis - lastBatteryMillis >= 1000) {
        lastBatteryMillis = currentMillis;
        uint32_t Vsum = 0;
        for(int i = 0; i < 16; i++) Vsum += analogReadMilliVolts(batteryPin);
        float Vbattf = (2.0 * Vsum / 16.0) / 1000.0;
        int percentage = (int)(constrain((Vbattf - 3.2) * 100.0 / (4.2 - 3.2), 0, 100));
        client.print("\nBAT:"); 
        client.println(percentage);
      }
    }
  }
}

// Fungsi Filter Lowpass
float Filter_lowpass(float inputVal) {
  for (int i = 3; i > 1; i--) {
    x[i] = x[i - 1];
    y[i] = y[i - 1];
  }
  x[1] = inputVal;
  y[1] = -a[2]*y[2] - a[3]*y[3] + b[1]*x[1] + b[2]*x[2] + b[3]*x[3];
  return y[1];
}

// Fungsi Filter Bandstop
float Filter_BSF(float inputVal) {
  for (int i = 4; i > 0; i--) {
    x_bsf[i] = x_bsf[i - 1];
    y_bsf[i] = y_bsf[i - 1];
  }
  x_bsf[0] = inputVal;
  y_bsf[0] = b_bsf[0]*x_bsf[0] + b_bsf[1]*x_bsf[1] + b_bsf[2]*x_bsf[2] + b_bsf[3]*x_bsf[3] + b_bsf[4]*x_bsf[4]
             - a_bsf[1]*y_bsf[1] - a_bsf[2]*y_bsf[2] - a_bsf[3]*y_bsf[3] - a_bsf[4]*y_bsf[4];
  return y_bsf[0];
}

// Fungsi Filter High Pass 20 Hz
float Filter_HPF(float inputVal) {
  for (int i = 2; i > 0; i--) {
    x_hpf[i] = x_hpf[i - 1];
    y_hpf[i] = y_hpf[i - 1];
  }
  x_hpf[0] = inputVal;
  y_hpf[0] = b_hpf[0]*x_hpf[0] + b_hpf[1]*x_hpf[1] + b_hpf[2]*x_hpf[2]
             - a_hpf[1]*y_hpf[1] - a_hpf[2]*y_hpf[2];
  return y_hpf[0];
}

// Fungsi Mengatur Warna LED RGB
void setLED(int r, int g, int b) {
  analogWrite(ledPin1, r);
  analogWrite(ledPin2, g);
  analogWrite(ledPin3, b);
}

// Fungsi Mengatur Kondisi PMOSFET
void updateMosfet(bool state) {
  if (state) {
    pinMode(PMOSPin, OUTPUT);
    digitalWrite(PMOSPin, LOW); 
  } else {
    pinMode(PMOSPin, INPUT); 
  }
}

// Fungsi Mengatur Deep Sleep ESP32C6
void goToSleep() {
  Serial.println("\nMemasuki Deep Sleep");
  setLED(0, 0, 0);
  updateMosfet(false);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  while(digitalRead(touchPin) == HIGH) { delay(10); }
  delay(100); 
  rtc_gpio_init((gpio_num_t)GPIO_NUM_2);
  rtc_gpio_set_direction((gpio_num_t)GPIO_NUM_2, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pulldown_en((gpio_num_t)GPIO_NUM_2);
  esp_sleep_enable_ext1_wakeup(1ULL << GPIO_NUM_2, ESP_EXT1_WAKEUP_ANY_HIGH);
  Serial.flush(); 
  esp_deep_sleep_start();
}

void tryReconnect() {
  Serial.println("Koneksi host hilang. Mencari sinyal (20 detik maks) ...");
  updateMosfet(false);
  systemActive = false;
  greenLedDone = false;
  unsigned long startAttempt = millis();
  while (millis() - startAttempt < 20000) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Host terkoneksi kembali!");
      systemActive = true;
      connectedMillis = millis();
      setLED(0, dimIntensity, 0);
      delay(2500);
      updateMosfet(true); 
      return; 
    }
    if ((millis() / 1000) % 2 == 0) setLED(0, 0, dimIntensity);
    else setLED(0, 0, 0);
    delay(100);
  }
  goToSleep();
}
