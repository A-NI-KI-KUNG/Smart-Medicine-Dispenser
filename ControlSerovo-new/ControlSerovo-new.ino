#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>  // ไลบรารี WebSocket Client

WebSocketsClient webSocket;

// ----- Global Variables -----
const char* ssid = "XXXXX";
const char* password = "XXXXX";
const char* board1IP = "xxx.xxx.xxx.xx;  // Global IP ของ Board 1 ของหน้าเว็บแสดงข้อมูล

// สำหรับควบคุม reconnect WebSocket
unsigned long lastWebSocketReconnect = 0;   
const unsigned long reconnectInterval = 5000; // 10 วินาที

bool systemRunning = false;         // สถานะระบบ: กำลังทำงานหรือไม่
int outOfRangeCount = 0;            // ตัวนับจำนวนครั้งที่ Ultrasonic >= 4 cm

// ----- ปุ่มสำหรับเริ่มระบบ ----- 
#define BUTTON_PIN 2  // ใช้ GPIO2 (active low)

// ----- Servo Configuration -----
// servo1: ใช้สำหรับควบคุมผ่านปุ่ม (GPIO14)
// servo2, servo3, servo4, servo5: สำหรับงานอื่น ๆ
Servo servo1;  // GPIO14
Servo servo2;  // GPIO27
Servo servo3;  // GPIO32
Servo servo4;  // GPIO33
Servo servo5;  // GPIO25

int currentServo1Angle = 0; // เก็บตำแหน่งปัจจุบันของ servo1

// ----- Variables สำหรับการควบคุม servo1 ผ่านกระบวนการ ----- 
// ลำดับตำแหน่งที่ servo1 เคลื่อนที่: 180, 90, 0
int servo1Positions[] = {180, 90, 0};
int servo1NumPositions = sizeof(servo1Positions) / sizeof(servo1Positions[0]);

// สำหรับ debounce ปุ่ม
int servo1_lastButtonState = HIGH;
unsigned long servo1_lastDebounceTime = 0;
const unsigned long servo1_debounceDelay = 50;
bool servo1_buttonProcessed = false;

// ----- TCS3200 Sensor Pins -----
#define S0 4
#define S1 5
#define S2 18
#define S3 19
#define SENSOR_OUT 21

// ----- ค่ามาตรฐานสำหรับการสอบเทียบสี -----
#define BLUE_RED_STANDARD    315
#define BLUE_GREEN_STANDARD  330
#define BLUE_BLUE_STANDARD   203

#define ORANGE_RED_STANDARD   43
#define ORANGE_GREEN_STANDARD  185
#define ORANGE_BLUE_STANDARD  152

#define WHITE_RED_STANDARD    57
#define WHITE_GREEN_STANDARD  67
#define WHITE_BLUE_STANDARD   55

// ----- HC‑SR04 Configuration -----
#define US_TRIGGER_PIN 16
#define US_ECHO_PIN 17

// ----- Enum สำหรับสีที่ตรวจจับ -----
enum DetectedColor {
  COLOR_UNKNOWN,
  COLOR_RED,
  COLOR_ORANGE,
  COLOR_BLUE,
  COLOR_WHITE,
  COLOR_BLACK
};

// ----------------------------------------------------------------------
// ฟังก์ชันอ่านความถี่จาก TCS3200 (เฉลี่ยหลายๆ ครั้ง)
// ----------------------------------------------------------------------
long readColorFrequency(int s2Value, int s3Value) {
  digitalWrite(S2, s2Value);
  digitalWrite(S3, s3Value);
  const int numSamples = 5;
  long sum = 0;
  for (int i = 0; i < numSamples; i++) {
    delay(3);
    long duration = pulseIn(SENSOR_OUT, LOW);
    sum += duration;
  }
  return sum / numSamples;
}

// ----------------------------------------------------------------------
// ฟังก์ชันตรวจจับสี (เรียกใช้เมื่อ servo1 = 90)
// ----------------------------------------------------------------------
DetectedColor detectColor() {
  long redFrequency   = readColorFrequency(LOW, LOW);
  long greenFrequency = readColorFrequency(HIGH, HIGH);
  long blueFrequency  = readColorFrequency(LOW, HIGH);

  Serial.print("Red Frequency: ");   Serial.print(redFrequency);
  Serial.print(" | Green Frequency: "); Serial.print(greenFrequency);
  Serial.print(" | Blue Frequency: ");  Serial.println(blueFrequency);

  long diffRed    = abs(redFrequency - 40) + abs(greenFrequency - 10) + abs(blueFrequency - 10);
  long diffOrange = abs(redFrequency - ORANGE_RED_STANDARD) + abs(greenFrequency - ORANGE_GREEN_STANDARD) + abs(blueFrequency - ORANGE_BLUE_STANDARD);
  long diffBlue   = abs(redFrequency - BLUE_RED_STANDARD)   + abs(greenFrequency - BLUE_GREEN_STANDARD)   + abs(blueFrequency - BLUE_BLUE_STANDARD);
  long diffWhite  = abs(redFrequency - WHITE_RED_STANDARD)  + abs(greenFrequency - WHITE_GREEN_STANDARD)  + abs(blueFrequency - WHITE_BLUE_STANDARD);

  Serial.print("Diff Red: ");    Serial.print(diffRed);
  Serial.print(" | Diff Orange: "); Serial.print(diffOrange);
  Serial.print(" | Diff Blue: ");   Serial.print(diffBlue);
  Serial.print(" | Diff White: ");  Serial.println(diffWhite);

  if(diffOrange < diffRed && diffOrange < diffBlue && diffOrange < diffWhite) {
    Serial.println("Detected Color: Orange");
    return COLOR_ORANGE;
  } else if(diffBlue < diffRed && diffBlue < diffOrange && diffBlue < diffWhite) {
    Serial.println("Detected Color: Blue");
    return COLOR_BLUE;
  } else if(diffRed < diffOrange && diffRed < diffBlue && diffRed < diffWhite) {
    Serial.println("Detected Color: Red");
    return COLOR_RED;
  } else if(diffWhite < diffRed && diffWhite < diffOrange && diffWhite < diffBlue) {
    Serial.println("Detected Color: White");
    return COLOR_WHITE;
  } else {
    Serial.println("Detected Color: Unknown");
    return COLOR_UNKNOWN;
  }
}

// ----------------------------------------------------------------------
// ฟังก์ชันวัดระยะจาก HC‑SR04 (cm)
// ----------------------------------------------------------------------
float readUltrasonicDistance() {
  digitalWrite(US_TRIGGER_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(US_TRIGGER_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(US_TRIGGER_PIN, LOW);
  long duration = pulseIn(US_ECHO_PIN, HIGH, 30000);
  float distance = duration * 0.034 / 2;
  return distance;
}

// ----------------------------------------------------------------------
// ฟังก์ชันเคลื่อน servo แบบค่อยๆ (สำหรับ servo2, servo3, servo4, servo5)
// ----------------------------------------------------------------------
void moveServoGradually(Servo &servo, int startAngle, int endAngle, int stepDelay) {
  if(startAngle < endAngle) {
    for (int angle = startAngle; angle <= endAngle; angle++) {
      servo.write(angle);
      delay(stepDelay);
    }
  } else {
    for (int angle = startAngle; angle >= endAngle; angle--) {
      servo.write(angle);
      delay(stepDelay);
    }
  }
}

// ----------------------------------------------------------------------
// ฟังก์ชัน delay แบบง่ายที่เรียก webSocket.loop() ด้วย
// ----------------------------------------------------------------------
void simpleDelay(unsigned long totalDelay) {
  unsigned long startTime = millis();
  while (millis() - startTime < totalDelay) {
    webSocket.loop();
    delay(10);
  }
}

// ----------------------------------------------------------------------
// ฟังก์ชันตรวจสอบและรีเชื่อมต่อ WebSocket (ลดความถี่ reconnect)
// ----------------------------------------------------------------------
void checkWebSocket() {
  if (!webSocket.isConnected()) {
    unsigned long currentMillis = millis();
    if (currentMillis - lastWebSocketReconnect >= reconnectInterval) {
      lastWebSocketReconnect = currentMillis;
      Serial.println("Reconnecting WebSocket...");
      webSocket.begin(board1IP, 81, "/");
    }
  }
}

// ----------------------------------------------------------------------
// WebSocket Callback
// ----------------------------------------------------------------------
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("WebSocket Disconnected");
      break;
    case WStype_CONNECTED:
      Serial.println("WebSocket Connected");
      break;
    case WStype_TEXT: {
      String message = String((char*)payload);
      Serial.print("WebSocket Message Received: ");
      Serial.println(message);
      DynamicJsonDocument doc(512);
      DeserializationError error = deserializeJson(doc, message);
      if (!error) {
        const char* msgType = doc["type"];
        if(strcmp(msgType, "finished") == 0) {
          Serial.println(">> Board 2 รับข้อมูล finished จาก Board 1 <<");
          String lockerStr = doc["locker_number"] | "0";
          int lockerVal = lockerStr.toInt();
          if(lockerVal == 1) {
            Serial.println("locker_number=1 => Moving servo3 to 90°");
            moveServoGradually(servo3, 0, 90, 20);
            delay(10000);
            Serial.println("servo3 back to 0°");
            moveServoGradually(servo3, 90, 0, 20);
          } else if(lockerVal == 2) {
            Serial.println("locker_number=2 => Moving servo4 to 90°");
            moveServoGradually(servo4, 0, 90, 20);
            delay(10000);
            Serial.println("servo4 back to 0°");
            moveServoGradually(servo4, 90, 0, 20);
          } else if(lockerVal == 3) {
            Serial.println("locker_number=3 => Moving servo5 to 90°");
            moveServoGradually(servo5, 0, 90, 20);
            delay(10000);
            Serial.println("servo5 back to 0°");
            moveServoGradually(servo5, 90, 0, 20);
          } else {
            Serial.println("locker_number out of range or 0, no action on servos.");
          }
        }
        else if(strcmp(msgType, "paymentConfirm") == 0) {
          bool isConfirmed = doc["confirm"] | false;
          if(isConfirmed) {
            Serial.println("Payment Confirmed!");
          }
        }
        else {
          Serial.println("Message type not recognized.");
        }
      }
      else {
        Serial.println("JSON parse error from WebSocket message");
      }
      break;
    }
    case WStype_BIN:
      Serial.println("Received Binary Message (not supported)");
      break;
    default:
      break;
  }
}

// ----------------------------------------------------------------------
// Function: Process servo1 process triggered by button
// ----------------------------------------------------------------------
void processServo1Process() {
  Serial.println("Servo1 Process (One Cycle) Started:");

  for (int i = 0; i < servo1NumPositions; i++) {
    int angle = servo1Positions[i];
    servo1.write(angle);
    Serial.print("Servo1 angle: ");
    Serial.println(angle);

    // ถ้า servo1 ไปที่ 90 ให้รอ 8 วินาที แล้วตรวจจับสี, อ่าน Ultrasonic และควบคุม servo2
    if (angle == 90) {
      delay(8000);  // รอ 8 วินาที

      Serial.println("At servo1=90, checking color...");
      DetectedColor color = detectColor();

      Serial.println("At servo1=90, reading ultrasonic distance...");
      float distance = readUltrasonicDistance();
      Serial.print("Ultrasonic Distance (servo1=90): ");
      Serial.print(distance);
      Serial.println(" cm");

      // ตรวจสอบ Ultrasonic: ถ้า distance >= 4 ให้เพิ่มตัวนับ
      if (distance >= 4.0) {
        outOfRangeCount++;
        Serial.print("outOfRangeCount = ");
        Serial.println(outOfRangeCount);
      } else {
        outOfRangeCount = 0;
      }

      // กำหนด userId จากสี
      int userId = 0;
      if (color == COLOR_ORANGE) userId = 1;
      else if (color == COLOR_BLUE) userId = 2;
      else if (color == COLOR_WHITE) userId = 3;
      Serial.print("Color => userId = ");
      Serial.println(userId);

      // ส่ง HTTP GET หาก userId != 0 และควบคุม servo2 ด้วยข้อมูลที่ได้รับ
      if (userId != 0) {
        HTTPClient http;
        String url = "http://xxx.xxx.xxx.xx/getlocker.php?user=" + String(userId);
        Serial.print("HTTP GET: ");
        Serial.println(url);
        http.begin(url);
        int httpCode = http.GET();
        if (httpCode > 0) {
          String payload = http.getString();
          Serial.print("Response: ");
          Serial.println(payload);
          
          StaticJsonDocument<400> doc;
          DeserializationError error = deserializeJson(doc, payload);
          if (!error) {
            // ถ้า data เป็น Array ใช้มุมแรกสำหรับ servo2
            if (doc["data"].is<JsonArray>()) {
              JsonArray lockers = doc["data"].as<JsonArray>();
              int servoAngles[4] = {0, 0, 0, 0};
              int index = 0;
              for (JsonVariant v : lockers) {
                int lockerVal = v.as<int>();
                int targetAngle = 0;
                if      (lockerVal == 1) targetAngle = 0;
                else if (lockerVal == 2) targetAngle = 30;
                else if (lockerVal == 3) targetAngle = 60;
                else if (lockerVal == 4) targetAngle = 90;
                if (index < 4) {
                  servoAngles[index] = targetAngle;
                }
                index++;
              }
              Serial.print("servo2: Moving to angle ");
              Serial.println(servoAngles[0]);
              moveServoGradually(servo2, 0, servoAngles[0], 20);
            } else {
              int locker = doc["data"];
              int targetAngle = 0;
              if      (locker == 1) targetAngle = 0;
              else if (locker == 2) targetAngle = 30;
              else if (locker == 3) targetAngle = 60;
              else if (locker == 4) targetAngle = 90;
              Serial.print("servo2: Moving to angle ");
              Serial.println(targetAngle);
              moveServoGradually(servo2, 0, targetAngle, 20);
            }
          } else {
            Serial.print("JSON parse error: ");
            Serial.println(error.c_str());
          }
        } else {
          Serial.print("HTTP GET Error: ");
          Serial.println(httpCode);
        }
        http.end();
      }
    }
    else {
      delay(1000);  // รอ 1 วินาทีสำหรับมุมอื่นๆ
    }
  }

  // จบรอบ: หาก outOfRangeCount >= 3 => หยุดระบบ, ไม่ทำรอบต่อไป
  if (outOfRangeCount >= 3) {
    systemRunning = false;
    servo1.write(180);
    Serial.println("System stopped (outOfRangeCount >= 3) => servo1 set to 180°");
  } else {
    servo1.write(180);
    Serial.println("One cycle finished => servo1 -> 180°, waiting for next cycle.");
  }
}

// ----------------------------------------------------------------------
// ฟังก์ชันตรวจสอบปุ่มเพื่อเริ่มระบบ
// ----------------------------------------------------------------------
void checkStartButton() {
  int reading = digitalRead(BUTTON_PIN);
  if (reading != servo1_lastButtonState) {
    servo1_lastDebounceTime = millis();
  }
  if ((millis() - servo1_lastDebounceTime) > servo1_debounceDelay) {
    if (reading == LOW && !servo1_buttonProcessed) {
      servo1_buttonProcessed = true;
      Serial.println("Button pressed => systemRunning = true; outOfRangeCount = 0;");
      systemRunning = true;
      outOfRangeCount = 0;
    }
    if (reading == HIGH) {
      servo1_buttonProcessed = false;
    }
  }
  servo1_lastButtonState = reading;
}

// ----------------------------------------------------------------------
// setup()
// ----------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("System Starting...");

  // Connect WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");

  // Setup WebSocket Client
  webSocket.begin(board1IP, 81, "/");
  webSocket.onEvent(webSocketEvent);

  // Attach servos
  servo1.attach(14);
  servo2.attach(27);
  servo3.attach(32);
  servo4.attach(33);
  servo5.attach(15);

  // Set initial positions for all servos
  servo1.write(0);
  servo2.write(0);
  servo3.write(0);
  servo4.write(0);
  servo5.write(0);
  currentServo1Angle = 0;

  // Setup TCS3200 sensor pins
  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  pinMode(SENSOR_OUT, INPUT);
  digitalWrite(S0, HIGH);
  digitalWrite(S1, LOW);

  // Setup button (for starting system operation)
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Setup HC‑SR04 pins
  pinMode(US_TRIGGER_PIN, OUTPUT);
  pinMode(US_ECHO_PIN, INPUT);
}

// ----------------------------------------------------------------------
// loop()
// ----------------------------------------------------------------------
void loop() {
  webSocket.loop();
  checkWebSocket();

  // ถ้าระบบยังไม่เริ่ม => ตรวจสอบปุ่มเพื่อเริ่มระบบ
  if (!systemRunning) {
    checkStartButton();
  } else {
    processServo1Process();
  }
}
