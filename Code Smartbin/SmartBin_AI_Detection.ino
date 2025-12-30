#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <SmartBin_inferencing.h>

// ================= WIFI CONFIG =================
const char* ssid = "iQOO Z7x 5G";
const char* password = "haha1234";

WebServer server(80);

// ================= PIN DEFINITIONS =================
#define FLASH_LED_PIN      4  
#define SERVO_FOOD_PIN    12
#define SERVO_RECYCLE_PIN 13
#define PIR_PIN           14  
#define IR_FOOD_STORAGE   15  // IR Sensor for Food bin
#define IR_RECYCLE_STORAGE 2  // IR Sensor for Recycle bin

// Camera Pin (AI THINKER)
#define PWDN_GPIO_NUM      32
#define RESET_GPIO_NUM     -1
#define XCLK_GPIO_NUM       0
#define SIOD_GPIO_NUM      26
#define SIOC_GPIO_NUM      27
#define Y9_GPIO_NUM        35
#define Y8_GPIO_NUM        34
#define Y7_GPIO_NUM        39
#define Y6_GPIO_NUM        36
#define Y5_GPIO_NUM        21
#define Y4_GPIO_NUM        19
#define Y3_GPIO_NUM        18
#define Y2_GPIO_NUM         5
#define VSYNC_GPIO_NUM     25
#define HREF_GPIO_NUM      23
#define PCLK_GPIO_NUM      22

// ================= GLOBAL VARIABLES =================
static uint8_t *snapshot_buf = NULL;
String latest_result = "SYSTEM READY";
float food_conf = 0.0;
float recycle_conf = 0.0;

Servo servoFood;
Servo servoRecycle;

int foodClose = 70;
int foodOpen = 110;
int recycleClose = 110;
int recycleOpen = 60;

unsigned long servoTimer = 0;
bool isServoActive = false;
int activeServoType = 0; 

// ================= CAMERA UTILITIES =================
bool ei_camera_capture(uint32_t width, uint32_t height, uint8_t *out_buf) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return false;
  memcpy(out_buf, fb->buf, width * height * 2);
  esp_camera_fb_return(fb);
  return true;
}

int ei_camera_get_data(size_t offset, size_t length, float *out_ptr) {
  size_t pixel_ix = offset * 2;
  for (size_t i = 0; i < length; i++) {
    uint16_t pixel = (snapshot_buf[pixel_ix + 1] << 8) | snapshot_buf[pixel_ix];
    uint8_t r = ((pixel >> 11) & 0x1F) << 3;
    uint8_t g = ((pixel >> 5)  & 0x3F) << 2;
    uint8_t b = ( pixel         & 0x1F) << 3;
    *out_ptr++ = (r << 16) | (g << 8) | b;
    pixel_ix += 2;
  }
  return 0;
}

// ================= CORE DETECTION LOGIC =================
void processDetection() {
  Serial.println(">>> PIR Triggered! Starting AI Analysis...");
  latest_result = "ANALYZING...";
  digitalWrite(FLASH_LED_PIN, HIGH); 
  delay(500); 

  float max_food_found = 0.0;
  float max_recycle_found = 0.0;

  for(int i = 0; i < 3; i++) {
    if (ei_camera_capture(EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT, snapshot_buf)) {
      ei::signal_t signal;
      ei_impulse_result_t result;
      signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
      signal.get_data = &ei_camera_get_data;

      if (run_classifier(&signal, &result, false) == EI_IMPULSE_OK) {
        for (size_t j = 0; j < result.bounding_boxes_count; j++) {
          auto bb = result.bounding_boxes[j];
          if (strcmp(bb.label, "Food_Waste") == 0 && bb.value > max_food_found) max_food_found = bb.value;
          else if (strcmp(bb.label, "Recycle") == 0 && bb.value > max_recycle_found) max_recycle_found = bb.value;
        }
      }
    }
  }

  food_conf = max_food_found;
  recycle_conf = max_recycle_found;
  digitalWrite(FLASH_LED_PIN, LOW);

  if (max_food_found > max_recycle_found && max_food_found > 0.5) {
    latest_result = "FOOD WASTE DETECTED";
    servoFood.write(foodOpen);
    activeServoType = 1;
    isServoActive = true;
    servoTimer = millis();
  } 
  else if (max_recycle_found > max_food_found && max_recycle_found > 0.5) {
    latest_result = "RECYCLE DETECTED";
    servoRecycle.write(recycleOpen);
    activeServoType = 2;
    isServoActive = true;
    servoTimer = millis(); 
  } 
  else {
    latest_result = "UNKNOWN OBJECT";
  }
}

// ================= WEB PAGE UI =================
void handleRoot() {
  bool foodFull = (digitalRead(IR_FOOD_STORAGE) == LOW);
  bool recycleFull = (digitalRead(IR_RECYCLE_STORAGE) == LOW);

  String foodStatus = foodFull ? "<b style='color:red;'>FULL</b>" : "<b style='color:green;'>AVAILABLE (STILL HAVE SPACE)</b>";
  String recycleStatus = recycleFull ? "<b style='color:red;'>FULL</b>" : "<b style='color:green;'>AVAILABLE (STILL HAVE SPACE)</b>";

  String html =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>Smart Bin Monitor</title></head>"
    "<body style='font-family:Arial;text-align:center;padding-top:20px;background:#f4f4f9;'>"
    "<div style='background:white;width:90%;margin:auto;padding:20px;border-radius:15px;box-shadow:0px 4px 10px rgba(0,0,0,0.1);'>"
    "<h2> Smart Bin AI System</h2>"
    "<hr style='width:50%'>"
    "<h1 style='color:#2c3e50;font-size:30px;'>" + latest_result + "</h1>"
    
    // Manual Controls
    "<div style='margin-bottom:20px;'>"
    "<a href='/openFood'><button style='padding:15px; background:#e67e22; color:white; border:none; border-radius:10px; margin:5px; font-weight:bold; width:80%;'>OPEN FOOD WASTE</button></a><br>"
    "<a href='/openRecycle'><button style='padding:15px; background:#3498db; color:white; border:none; border-radius:10px; margin:5px; font-weight:bold; width:80%;'>OPEN RECYCLE</button></a>"
    "</div>"

    "<div style='font-size:18px; margin:15px; background:#eee; padding:15px; border-radius:10px;'>"
    "<strong>AI Confidence:</strong>"
    "<p> Food Waste: " + String(food_conf * 100, 1) + "%</p>"
    "<p> Recycle: " + String(recycle_conf * 100, 1) + "%</p>"
    "</div>"

    "<h3>Storage Capacity:</h3>"
    "<div style='display:flex; justify-content:space-around; font-size:16px;'>"
    "<div style='padding:10px; border:1px solid #ddd; border-radius:10px; width:45%;'>"
    "<strong>Food Waste Bin</strong><br>" + foodStatus + "</div>"
    "<div style='padding:10px; border:1px solid #ddd; border-radius:10px; width:45%;'>"
    "<strong>Recycle Bin</strong><br>" + recycleStatus + "</div>"
    "</div>"

    "<p style='font-size:12px;color:gray;margin-top:20px;'>Data updates automatically.</p>"
    "</div>"
    "<script>setTimeout(function(){ location.reload(); }, 3000);</script>" // Refresh script
    "</body></html>";

  server.send(200, "text/html", html);
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  pinMode(FLASH_LED_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(IR_FOOD_STORAGE, INPUT);
  pinMode(IR_RECYCLE_STORAGE, INPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  servoFood.setPeriodHertz(50);
  servoRecycle.setPeriodHertz(50);
  servoFood.attach(SERVO_FOOD_PIN, 500, 2400);
  servoRecycle.attach(SERVO_RECYCLE_PIN, 500, 2400);

  servoFood.write(foodClose);
  servoRecycle.write(recycleClose);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_RGB565; 
  config.frame_size   = FRAMESIZE_96X96;  
  config.fb_count     = 1;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed!");
    while (1);
  }

  snapshot_buf = (uint8_t*)malloc(EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT * 2);

  IPAddress local_IP(192, 168, 74, 195); 
  IPAddress gateway(192, 168, 74, 1);
  IPAddress subnet(255, 255, 255, 0);

  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("STA Failed to configure Static IP");
  }

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  
  Serial.println("\nWiFi Connected!");
  
  server.on("/", handleRoot);

  // Manual Open Food Waste
  server.on("/openFood", HTTP_GET, []() {
    latest_result = "MANUAL OPEN: FOOD";
    servoFood.write(110); // foodOpen value
    activeServoType = 1;
    isServoActive = true;
    servoTimer = millis();
    server.sendHeader("Location", "/");
    server.send(303); // Redirect back to home
  });

  // Manual Open Recycle
  server.on("/openRecycle", HTTP_GET, []() {
    latest_result = "MANUAL OPEN: RECYCLE";
    servoRecycle.write(60); // recycleOpen value
    activeServoType = 2;
    isServoActive = true;
    servoTimer = millis();
    server.sendHeader("Location", "/");
    server.send(303); // Redirect back to home
  });

  server.on("/status", HTTP_GET, []() {
    bool foodFull = (digitalRead(IR_FOOD_STORAGE) == LOW);
    bool recycleFull = (digitalRead(IR_RECYCLE_STORAGE) == LOW);
    String response = (foodFull ? "FULL" : "OK") + String(",") + (recycleFull ? "FULL" : "OK");
    server.send(200, "text/plain", response);
  });

  server.begin();
}

void loop() {
  server.handleClient();

  if (digitalRead(PIR_PIN) == HIGH && !isServoActive) {
    processDetection();
  }

  if (isServoActive && (millis() - servoTimer > 3000)) {
    if (activeServoType == 1) servoFood.write(foodClose);
    else if (activeServoType == 2) servoRecycle.write(recycleClose);
    
    isServoActive = false;
    activeServoType = 0;
    delay(2000); 
    latest_result = "SYSTEM READY";
  }
}