#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include "qrcode.h"
#include "QRCode_Library.h"
#include "MFRC522.h"
#include <Preferences.h>
#include <WiFiMulti.h>
#include <ArtronShop_SCB_API.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>

// ---------------- TFT Display Configuration ----------------
#define TFT_CS   5    // TFT Chip Select → GPIO5
#define TFT_DC   2    // TFT Data/Command → GPIO2
#define TFT_RST  4    // TFT Reset → GPIO4
// SPI: SCK → GPIO18, MOSI → GPIO23, MISO → GPIO19, BLK → 3.3V

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// ---------------- WiFi & SCB API Configuration ----------------
#define WIFI_SSID "XXXXX"
#define WIFI_PASS "XXXXX"
#define FETCH_URL "http://XXXXXXXX/fetch_user.php"  // สำหรับดึงข้อมูลผู้ใช้ดูจาก IP ของ fecth_user

WiFiMulti wifiMulti;

#define API_KEY    "l7c34846e7f79e4981a0954ff3a48ef386"
#define API_SECRET "c3c98be3e1b7488f8dbb9d89f2da1239"
ArtronShop_SCB_API SCB_API(API_KEY, API_SECRET);

// ---------------- Web Server & RFID Configuration ----------------
WebServer server(80);
Preferences preferences;

#define RFID_RST_PIN 26
#define RFID_SS_PIN 15
MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);

// ตัวแปรเก็บ RFID + ข้อมูลผู้ใช้
String lastRFID = "";
String userInfo = "No RFID scanned yet";

// ตัวแปรเก็บ IP address (ใช้สร้าง QR Code)
String ipAddress;

// ---------------- WebSocket Server ----------------
WebSocketsServer webSocket = WebSocketsServer(81);  // พอร์ต 81 สำหรับ WebSocket

// ---------------- Utility Functions ----------------
String urlEncode(const String &src) {
  String encoded = "";
  for (int i = 0; i < src.length(); i++) {
    char c = src[i];
    if (c == ' ')
      encoded += "%20";
    else
      encoded += c;
  }
  return encoded;
}

String randomString() {
  uint8_t buff[8];
  esp_fill_random(buff, sizeof(buff));
  String str = "";
  for (int i = 0; i < sizeof(buff); i++) {
    str += String(buff[i], HEX);
  }
  str.toUpperCase();
  return str;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    retry++;
    if (retry > 20) {
      Serial.println("\nFailed to connect. Restarting...");
      ESP.restart();
    }
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  ipAddress = WiFi.localIP().toString();
}

void displayQRCode() {
  tft.fillScreen(ST77XX_WHITE);
  String qrText = "http://" + ipAddress + "/";
  Serial.print("QR Code Text: ");
  Serial.println(qrText);

  const uint8_t qrVersion = 2;
  uint8_t qrcodeData[qrcode_getBufferSize(qrVersion)];
  QRCode qrcode;
  qrcode_initText(&qrcode, qrcodeData, qrVersion, 0, qrText.c_str());

  int qrSize = qrcode.size;
  int margin = 3;
  int totalModules = qrSize + margin * 2;
  
  int moduleSize = min(tft.width() / totalModules, tft.height() / totalModules);
  int xOffset = (tft.width() - (totalModules * moduleSize)) / 2;
  int yOffset = (tft.height() - (totalModules * moduleSize)) / 2;

  for (int y = 0; y < qrSize; y++) {
    for (int x = 0; x < qrSize; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        tft.fillRect(xOffset + (x + margin) * moduleSize,
                     yOffset + (y + margin) * moduleSize,
                     moduleSize, moduleSize, ST77XX_BLACK);
      }
    }
  }
}

// ฟังก์ชันตรวจสอบการชำระเงิน (Payment Confirm)
void handlePaymentConfirm() {
  bool confirm = false;
  if (SCB_API.checkPaymentConfirm(&confirm)) {
    String response = "{\"type\":\"paymentConfirm\",\"confirm\":";
    response += (confirm ? "true" : "false");
    response += "}";
    server.send(200, "application/json", response);
  } else {
    server.send(500, "application/json", "{\"error\":\"Check failed\"}");
  }
}

// ---------------- Modified UI for Root Page ----------------
void handleRoot() {
  // เพิ่ม CSS สำหรับ .paymentButton ให้เป็นปุ่มสวยๆ
  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>User Info</title>
  <link href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.0.0-beta3/css/all.min.css" rel="stylesheet">
  <link href="https://fonts.googleapis.com/css2?family=Nunito:wght@400;600;700&display=swap" rel="stylesheet">
  <style>
    /* CSS styles สำหรับหน้า root */
    body {
      font-family: 'Nunito', sans-serif;
      background: linear-gradient(135deg, #f5f7fa 0%, #c3cfe2 100%);
      margin: 0;
      padding: 20px;
      min-height: 100vh;
      display: flex;
      justify-content: center;
      align-items: center;
    }
    .container {
      max-width: 900px;
      margin: 0 auto;
      background: #fff;
      padding: 30px;
      border-radius: 16px;
      box-shadow: 0 10px 30px rgba(0,0,0,0.08);
      width: 100%;
    }
    h1 {
      text-align: center;
      color: #4a6cfa;
      font-size: 2.2em;
      margin-bottom: 30px;
      letter-spacing: -0.5px;
    }
    .rfid-status {
      background: #f8f9fa;
      padding: 15px;
      border-radius: 12px;
      margin-bottom: 30px;
      border-left: 5px solid #4a6cfa;
      display: flex;
      align-items: center;
    }
    .rfid-status i {
      font-size: 1.5em;
      color: #4a6cfa;
      margin-right: 12px;
    }
    .rfid-status p {
      margin: 0;
      font-size: 1.1em;
      color: #555;
    }
    .rfid-status strong {
      color: #333;
      margin-left: 8px;
    }

    /* ปุ่มสวยๆ สำหรับ Proceed to Payment */
    .paymentButton {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      padding: 12px 25px;
      background: #3498db;
      color: #fff;
      text-decoration: none;
      border-radius: 50px;
      margin-top: 20px;
      cursor: pointer;
      font-weight: 600;
      transition: all 0.3s ease;
      box-shadow: 0 4px 15px rgba(52, 152, 219, 0.3);
      border: none;
      font-size: 1em;
    }
    .paymentButton:hover {
      background: #2980b9;
      transform: translateY(-2px);
      box-shadow: 0 7px 15px rgba(52, 152, 219, 0.4);
    }
    .paymentButton i {
      margin-right: 8px;
    }
  </style>
  <script>
    var ws;
    function initWebSocket() {
      ws = new WebSocket("ws://" + location.hostname + ":81/");
      ws.onopen = function() {
        console.log("WebSocket connected");
      };
      ws.onmessage = function(event) {
        try {
          var msg = JSON.parse(event.data);
          if(msg.type === "userUpdate") {
            var info = msg.data;
            var resultEl = document.getElementById("userResult");
            resultEl.innerHTML = "";
            if(info.status === "success" && Array.isArray(info.data)) {
              var records = info.data;
              if(records.length === 0) {
                resultEl.innerHTML = `
                  <div class="noData">
                    <i class="fas fa-search"></i>
                    <p>No records found for this RFID</p>
                  </div>`;
                return;
              }
              var grouped = {};
              records.forEach(function(record){
                var userName = record.user || "Unknown";
                if(!grouped[userName]) grouped[userName] = [];
                grouped[userName].push(record);
              });
              var html = "";
              for(var user in grouped) {
                html += "<div class='userSection'>";
                html += "<h2><i class='fas fa-user-circle'></i> " + user + "</h2>";
                grouped[user].forEach(function(r){
                  html += "<div class='recordCard'><ul>";
                  for(var key in r) {
                    var lowerKey = key.toLowerCase();
                    if(["id","rfid","created_at"].includes(lowerKey)) continue;
                    html += "<li><strong>" + key + ":</strong> " + (r[key] || "") + "</li>";
                  }
                  html += "</ul></div>";
                });
                html += "</div>";
              }
              html += `<a href='/payment.html' class='paymentButton'>
                        <i class="fas fa-credit-card"></i> Proceed to Payment
                      </a>`;
              resultEl.innerHTML = html;
            } else {
              resultEl.innerHTML = `
                <div class="noData">
                  <i class="fas fa-exclamation-triangle"></i>
                  <p>No data available</p>
                </div>`;
            }
          }
        } catch(e) {
          console.error("Error parsing message:", e);
        }
      };
      ws.onerror = function(error) {
        console.error("WebSocket error:", error);
      };
      ws.onclose = function() {
        console.log("WebSocket closed. Reconnecting...");
        setTimeout(initWebSocket, 2000);
      };
    }
    window.onload = function() {
      initWebSocket();
    };
  </script>
</head>
<body>
  <div class="container">
    <h1><i class="fas fa-id-card"></i>User Information System</h1>
    
    <div class="rfid-status">
      <i class="fas fa-wifi"></i>
      <p>Last Scanned RFID:<strong id="lastRFID">%LAST_RFID%</strong></p>
    </div>
    
    <div id="userResult">
      <div class="loader">
        <div></div>
        <div></div>
        <div></div>
      </div>
    </div>
  </div>
</body>
</html>
)rawliteral";
  page.replace("%LAST_RFID%", lastRFID);
  server.send(200, "text/html", page);
}

// Endpoint /finished
void handleFinished() {
  if (server.method() == HTTP_OPTIONS) {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "*");
    server.send(204);
    return;
  }

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "*");

  if (server.hasArg("rfid")) {
    String rfid = server.arg("rfid");
    
    String dbUser = "";
    String lockerVal = "N/A";

    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      String encodedRFID = urlEncode(rfid);
      String url = String(FETCH_URL) + "?rfid=" + encodedRFID;
      Serial.println("handleFinished() -> Request URL: " + url);
      http.begin(url);
      int httpResponseCode = http.GET();
      if (httpResponseCode > 0) {
        String fetchedData = http.getString();
        Serial.println("handleFinished() -> Fetched user info:");
        Serial.println(fetchedData);

        DynamicJsonDocument doc(2048);
        DeserializationError err = deserializeJson(doc, fetchedData);
        if (!err) {
          JsonObject root = doc.as<JsonObject>();
          if (root.containsKey("data")) {
            JsonArray arr = root["data"].as<JsonArray>();
            if (arr.size() > 0) {
              JsonObject firstRec = arr[0];
              if (firstRec.containsKey("user")) {
                dbUser = firstRec["user"].as<String>();
              }
              if (firstRec.containsKey("locker_number")) {
                lockerVal = firstRec["locker_number"].as<String>();
              }
            }
          }
        } else {
          Serial.print("handleFinished() -> JSON parse error: ");
          Serial.println(err.f_str());
        }
      } else {
        Serial.print("handleFinished() -> Error fetching data: ");
        Serial.println(http.errorToString(httpResponseCode));
      }
      http.end();
    } else {
      Serial.println("handleFinished() -> WiFi not connected, cannot fetch data.");
    }

    String payload = "{\"type\":\"finished\"";
    payload += ",\"user\":\"" + dbUser + "\"";
    payload += ",\"locker_number\":\"" + lockerVal + "\"";
    payload += ",\"rfid\":\"" + rfid + "\"";
    payload += "}";
    
    Serial.print("Broadcasting finished message: ");
    Serial.println(payload);
    webSocket.broadcastTXT(payload);
    Serial.println("Finished data sent to servo board via WebSocket.");
    
    HTTPClient httpDel;
    String deleteUrl = "http://xxx.xxx.xxx.xx//delete_user.php?rfid=" + urlEncode(rfid);
    Serial.print("Calling delete URL: ");
    Serial.println(deleteUrl);
    httpDel.begin(deleteUrl);
    int httpDelCode = httpDel.GET();
    if (httpDelCode > 0) {
      String delResponse = httpDel.getString();
      Serial.print("Delete response: ");
      Serial.println(delResponse);
      Serial.println("Records deleted successfully.");
    } else {
      Serial.print("Delete request failed: ");
      Serial.println(httpDel.errorToString(httpDelCode));
    }
    httpDel.end();

    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Finished data sent and records deleted\"}");
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing parameters\"}");
  }
}

void fetchUserData(String rfid) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String encodedRFID = urlEncode(rfid);
    String url = String(FETCH_URL) + "?rfid=" + encodedRFID;
    Serial.println("Request URL: " + url);
    http.begin(url);
    int httpResponseCode = http.GET();
    if (httpResponseCode > 0) {
      userInfo = http.getString();
      Serial.println("Fetched user info:");
      Serial.println(userInfo);
      String msg = "{\"type\":\"userUpdate\",\"data\":" + userInfo + "}";
      webSocket.broadcastTXT(msg);
    } else {
      userInfo = "Error fetching data";
      Serial.print("Error fetching data: ");
      Serial.println(http.errorToString(httpResponseCode));
    }
    http.end();
  } else {
    userInfo = "WiFi not connected";
    Serial.println("WiFi not connected. Cannot fetch data.");
  }
}

bool paymentAlreadyConfirmed = false;
unsigned long lastCheckTime = 0;
unsigned long lastRfidFetchTime = 0;
const unsigned long fetchInterval = 3000; // 3 วินาที

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  // สามารถเพิ่มโค้ดสำหรับ debug ได้ที่นี่หากต้องการ
}

// ---------------- Modified UI for Payment Page ----------------
void handlePayment() {
  // สร้างค่าอ้างอิงและคำนวณยอดรวม
  String ref1 = randomString();
  String ref2 = randomString();
  String ref3 = "FFTIOT";
  float sumAmount = 0;
  {
    DynamicJsonDocument doc(2048);
    DeserializationError jsonError = deserializeJson(doc, userInfo);
    if (!jsonError) {
      JsonObject root = doc.as<JsonObject>();
      if (root.containsKey("data")) {
        JsonArray arr = root["data"].as<JsonArray>();
        for (JsonObject obj : arr) {
          if (obj.containsKey("Price")) {
            const char* priceStr = obj["Price"];
            float priceVal = atof(priceStr);
            sumAmount += priceVal;
          }
        }
      } else {
        Serial.println("No 'data' key found in JSON");
        sumAmount = 20;
      }
    } else {
      Serial.println("Failed to parse userInfo JSON");
      Serial.print("Error: ");
      Serial.println(jsonError.f_str());
      sumAmount = 20;
    }
  }
  if (sumAmount <= 0) {
    sumAmount = 1.0;
  }

  // สร้าง QR
  String qrRawData;
  String qrMessage; // เก็บไว้เผื่อ debug หรือใช้งานภายใน
  if (SCB_API.QRCodeTag30Create("950931468658646", sumAmount, ref1, ref2, ref3, &qrRawData)) {
    qrMessage = "QR Data: " + qrRawData;
    Serial.println(qrMessage);
  } else {
    qrMessage = "Failed to create QR Code";
    Serial.println(qrMessage);
  }
  
  Serial.print("SumAmount = ");
  Serial.println(sumAmount, 2);

  // สร้างหน้าเว็บ (HTML) สำหรับหน้า Payment
  // จุดสำคัญ:
  // 1) ลบการแสดง “QR Data: ...” ออกไป
  // 2) แสดง “Payment Successful” ในรูปแบบ Pop-up
  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Payment Page</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <link href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.0.0-beta3/css/all.min.css" rel="stylesheet">
  <link href="https://fonts.googleapis.com/css2?family=Nunito:wght@400;600;700&display=swap" rel="stylesheet">
  <style>
    /* ---------- พื้นหลังและ Container หลัก ---------- */
    body {
      font-family: 'Nunito', sans-serif;
      background: linear-gradient(120deg, #a1c4fd 0%, #c2e9fb 100%);
      margin: 0;
      padding: 20px;
      display: flex;
      justify-content: center;
      align-items: center;
      min-height: 100vh;
    }
    .container {
      background: #fff;
      padding: 30px;
      border-radius: 16px;
      box-shadow: 0 10px 30px rgba(0,0,0,0.1);
      text-align: center;
      max-width: 500px;
      width: 100%;
      position: relative;
    }
    h1 {
      color: #2c3e50;
      margin-bottom: 20px;
      font-size: 2em;
      position: relative;
      display: inline-block;
    }
    h1:after {
      content: '';
      display: block;
      width: 60px;
      height: 4px;
      background: #3498db;
      margin: 10px auto 0;
      border-radius: 2px;
    }

    /* ---------- ส่วนแสดง QR Code ---------- */
    #qrcode {
      margin: 30px auto;
      width: 256px;
      height: 256px;
      padding: 15px;
      background: white;
      border-radius: 10px;
      box-shadow: 0 5px 25px rgba(0,0,0,0.05);
      position: relative;
    }

    /* ---------- รายละเอียดการชำระเงิน ---------- */
    .payment-details {
      margin: 20px 0;
      padding: 20px;
      background: #f8fafc;
      border-radius: 12px;
      color: #4a5568;
      box-shadow: inset 0 2px 4px rgba(0,0,0,0.05);
    }
    .payment-details p {
      margin: 10px 0;
      font-size: 1.1em;
    }
    .amount {
      font-size: 1.8em;
      font-weight: 700;
      color: #3498db;
      display: block;
      margin: 15px 0;
    }

    /* ---------- ปุ่ม ---------- */
    .button {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      padding: 12px 25px;
      background: #3498db;
      color: #fff;
      text-decoration: none;
      border-radius: 50px;
      margin-top: 20px;
      cursor: pointer;
      font-weight: 600;
      transition: all 0.3s ease;
      box-shadow: 0 4px 15px rgba(52, 152, 219, 0.3);
      border: none;
      font-size: 1em;
    }
    .button:hover {
      background: #2980b9;
      transform: translateY(-2px);
      box-shadow: 0 7px 15px rgba(52, 152, 219, 0.4);
    }
    .button i {
      margin-right: 8px;
    }

    /* ---------- Modal Popup สำหรับ Payment Done ---------- */
    #paymentModal {
      display: none;
      position: fixed;
      z-index: 999;
      left: 0; top: 0;
      width: 100%; height: 100%;
      background-color: rgba(0,0,0,0.5);
    }
    #paymentModalContent {
      background: #fff;
      margin: 10% auto;
      padding: 30px;
      border-radius: 16px;
      width: 80%;
      max-width: 400px;
      text-align: center;
      position: relative;
      box-shadow: 0 10px 40px rgba(0,0,0,0.2);
    }
    .success-icon {
      font-size: 4em;
      color: #2ecc71;
      margin-bottom: 20px;
    }
  </style>
  <script src="https://cdnjs.cloudflare.com/ajax/libs/qrcodejs/1.0.0/qrcode.min.js"></script>
</head>
<body>
  <div class="container">
    <h1><i class="fas fa-qrcode"></i> QR Payment</h1>
    
    <!-- ตำแหน่งแสดง QR Code -->
    <div id="qrcode"></div>
    
    <!-- รายละเอียดการชำระเงิน -->
    <div class="payment-details">
      <p>Please scan this QR code with your banking app to complete the payment.</p>
      <span class="amount">฿%AMOUNT%</span>
    </div>
    
    <!-- ปุ่ม Back to Home -->
    <a href="/" class="button">
      <i class="fas fa-arrow-left"></i> Back to Home
    </a>
  </div>

  <!-- Modal popup สำหรับ "Payment Done" -->
  <div id="paymentModal">
    <div id="paymentModalContent">
      <i class="fas fa-check-circle success-icon"></i>
      <h2>Payment Successful</h2>
      <p>Your transaction has been completed successfully.</p>
      <div class="button" id="modalButton">
        <i class="fas fa-check"></i> Finished
      </div>
    </div>
  </div>

  <script>
    // สร้าง QR Code
    var qrData = "%QR_DATA%";
    new QRCode(document.getElementById("qrcode"), {
      text: qrData,
      width: 256,
      height: 256,
      colorDark : "#000000",
      colorLight : "#ffffff",
      correctLevel : QRCode.CorrectLevel.H
    });

    // สร้าง WebSocket สำหรับรับแจ้งเตือน paymentConfirm
    var ws = new WebSocket("ws://" + location.hostname + ":81/");
    ws.onopen = function() {
      console.log("WebSocket connected (Payment page)");
    };
    ws.onmessage = function(event) {
      try {
        var msg = JSON.parse(event.data);
        if(msg.type === "paymentConfirm" && msg.confirm === true) {
          // แสดง Popup Modal
          document.getElementById("paymentModal").style.display = "block";
        }
      } catch(e) {
        console.error("Error parsing ws message:", e);
      }
    };
    ws.onerror = function(error) {
      console.error("WebSocket error:", error);
    };

    // เมื่อกดปุ่มใน modal
    document.getElementById("modalButton").addEventListener("click", function(){
      var button = document.getElementById("modalButton");
      if(button.innerText.includes("Finished")){
        var rfid = "%LAST_RFID%";
        button.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Processing...';
        button.style.pointerEvents = 'none';
        
        // 1) เรียก /finished?rfid=... เพื่อแจ้งให้ระบบทราบว่าทำรายการเสร็จ
        fetch("http://" + location.hostname + "/finished?rfid=" + encodeURIComponent(rfid))
        .then(response => response.json())
        .then(data => {
          console.log("Finished endpoint response: ", data);
          // 2) จากนั้นเรียก delete_user.php เพื่อลบข้อมูล
          return fetch("http://xxx.xxx.xxx.xx//delete_user.php?rfid=" + encodeURIComponent(rfid));
        })
        .then(response => response.json())
        .then(data => {
          if(data.status === "success") {
            document.querySelector('#paymentModalContent h2').textContent = 'All Done!';
            document.querySelector('#paymentModalContent p').textContent = 'Records have been processed successfully.';
          } else {
            document.querySelector('#paymentModalContent h2').textContent = 'Warning';
            document.querySelector('#paymentModalContent p').textContent = 'Error deleting records: ' + data.message;
          }
          button.innerHTML = '<i class="fas fa-check"></i> OK';
          button.style.pointerEvents = 'auto';
        })
        .catch(error => {
          console.error("Error:", error);
          document.querySelector('#paymentModalContent h2').textContent = 'Error';
          document.querySelector('#paymentModalContent p').textContent = 'Something went wrong. Please try again.';
          button.innerHTML = '<i class="fas fa-check"></i> OK';
          button.style.pointerEvents = 'auto';
        });
      } else if(button.innerText.includes("OK")){
        window.location.href = "/";
      }
    });
  </script>
</body>
</html>
)rawliteral";

  // แทนที่ตัวแปรใน HTML
  page.replace("%LAST_RFID%", lastRFID);
  page.replace("%QR_DATA%", qrRawData);
  page.replace("%AMOUNT%", String(sumAmount, 2));

  // ส่งหน้า HTML กลับไปที่ client
  server.send(200, "text/html", page);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting setup...");

  SPI.begin();
  connectWiFi();

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);
  displayQRCode();
  
  if (!SCB_API.setClock()) {
    Serial.println("Sync time fail");
  }
  
  mfrc522.PCD_Init();
  Serial.println("RFID Scanner Ready");
  
  server.on("/", handleRoot);
  server.on("/payment.html", handlePayment);
  server.on("/checkPaymentConfirm", handlePaymentConfirm);
  server.on("/finished", handleFinished);
  server.begin();
  Serial.println("Web Server Started!");

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void loop() {
  // ตรวจสอบการ์ดใหม่
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    String rfid = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      rfid += String(mfrc522.uid.uidByte[i], HEX);
      if (i < mfrc522.uid.size - 1) {
        rfid += " ";
      }
    }
    rfid.toUpperCase();
    Serial.print("Scanned RFID: ");
    Serial.println(rfid);

    // ** ปิดการสื่อสารกับการ์ด เพื่อให้การอ่านเสถียรมากขึ้น **
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();

    if (rfid != lastRFID) {
      lastRFID = rfid;
      lastRfidFetchTime = millis();
      Serial.println("Updated lastRFID: " + lastRFID);
      fetchUserData(lastRFID);
    } else {
      if (millis() - lastRfidFetchTime > 3000) {
        lastRfidFetchTime = millis();
        Serial.println("Re-fetching user info for same RFID: " + lastRFID);
        fetchUserData(lastRFID);
      }
    }
  }

  server.handleClient();
  webSocket.loop();

  if (millis() - lastCheckTime > 1000) {
    lastCheckTime = millis();
    bool confirm = false;
    if (SCB_API.checkPaymentConfirm(&confirm)) {
      if (confirm && !paymentAlreadyConfirmed) {
        paymentAlreadyConfirmed = true;
        String userName = "Unknown";
        float sumPrice = 0.0;

        {
          DynamicJsonDocument doc(2048);
          DeserializationError err = deserializeJson(doc, userInfo);
          if (!err) {
            JsonObject root = doc.as<JsonObject>();
            if (root.containsKey("data")) {
              JsonArray arr = root["data"].as<JsonArray>();
              if (arr.size() > 0) {
                JsonObject firstRec = arr[0];
                if (firstRec.containsKey("user")) {
                  userName = firstRec["user"].as<String>();
                }
              }
              for (JsonObject obj : arr) {
                if (obj.containsKey("Price")) {
                  float priceVal = atof(obj["Price"]);
                  sumPrice += priceVal;
                }
              }
            }
          }
        }

        String msg = "{\"type\":\"paymentConfirm\",\"confirm\":true";
        msg += ",\"rfid\":\"" + lastRFID + "\"";
        msg += ",\"user\":\"" + userName + "\"";
        msg += ",\"sumPrice\":" + String(sumPrice, 2);
        msg += "}";
        
        Serial.println("Broadcasting paymentConfirm with extra data:");
        Serial.println(msg);
        webSocket.broadcastTXT(msg);
      }
      if (!confirm) {
        paymentAlreadyConfirmed = false;
      }
    }
  }
}
