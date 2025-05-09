#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <SPI.h>
#include "MFRC522.h"
#include <Preferences.h>

#define WIFI_SSID "XXXX"
#define WIFI_PASS "XXXXXX"
#define SERVER_URL "http://xxx.xxx.xxx.xx/insert_data.php" // เปลี่ยนเป็น URL จริงที่ใช้ในการเปิด myadminphp บรรทัก 199 ด้วย

// กำหนดพินสำหรับ RFID (MFRC522)
#define RST_PIN 26
#define SS_PIN 5
MFRC522 mfrc522(SS_PIN, RST_PIN);

// ตัวแปรสำหรับเก็บสถานะ RFID (Authorized = true เมื่อสแกนได้ RFID ที่ต้องการ)
bool isRFIDAuthorized = false;

WebServer server(80);
Preferences preferences;

const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Smart Medicine Dispenser</title>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Montserrat:wght@300;400;600&display=swap');

    /* พื้นหลังแบบ animated gradient */
    body {
      font-family: 'Montserrat', sans-serif;
      background: linear-gradient(135deg, #ff9a9e, #fad0c4, #fbc2eb, #a18cd1);
      background-size: 400% 400%;
      animation: gradientAnimation 10s ease infinite;
      margin: 0;
      padding: 0;
      color: #333;
    }
    @keyframes gradientAnimation {
      0% { background-position: 0% 50%; }
      50% { background-position: 100% 50%; }
      100% { background-position: 0% 50%; }
    }
    .container {
      width: 90%;
      max-width: 900px;
      background: #ffffff;
      padding: 40px;
      margin: 50px auto;
      border-radius: 15px;
      box-shadow: 0 10px 20px rgba(0,0,0,0.2);
      animation: fadeIn 1s ease-out;
    }
    @keyframes fadeIn {
      from { opacity: 0; transform: translateY(20px); }
      to { opacity: 1; transform: translateY(0); }
    }
    h1 {
      text-align: center;
      color: #333;
      margin-bottom: 30px;
      font-weight: 600;
    }
    h2 {
      text-align: center;
      color: #444;
      margin-bottom: 20px;
      font-weight: 600;
    }
    .user-section {
      border: 1px solid #eee;
      padding: 25px;
      margin-bottom: 30px;
      border-radius: 10px;
      background: #f8f8f8;
      transition: transform 0.3s ease, box-shadow 0.3s ease;
    }
    .user-section:hover {
      transform: translateY(-3px);
      box-shadow: 0 8px 16px rgba(0,0,0,0.15);
    }
    .form-group {
      margin-bottom: 20px;
    }
    .form-group label {
      display: block;
      margin-bottom: 6px;
      font-weight: 500;
      color: #555;
    }
    .form-group input,
    .form-group select {
      width: 100%;
      padding: 12px;
      border-radius: 8px;
      border: 1px solid #ccc;
      font-size: 16px;
      transition: all 0.3s ease;
      font-family: 'Montserrat', sans-serif;
    }
    .form-group input:focus,
    .form-group select:focus {
      border-color: #5563DE;
      box-shadow: 0 0 5px rgba(85, 99, 222, 0.5);
      outline: none;
    }
    .inline-group {
      display: flex;
      gap: 20px;
    }
    .inline-group .form-group {
      flex: 1;
      margin-bottom: 0;
    }
    .inline-group .form-group + .form-group {
      margin-top: 0;
    }
    .checkbox-container {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-top: 10px;
      margin-bottom: 20px;
    }
    .checkbox-container label {
      font-size: 16px;
      color: #555;
      display: flex;
      align-items: center;
      gap: 5px;
    }
    button {
      width: 100%;
      padding: 14px;
      margin-top: 10px;
      border-radius: 8px;
      border: none;
      font-size: 18px;
      font-weight: 600;
      background: #5563DE;
      color: #fff;
      cursor: pointer;
      transition: background 0.3s ease, transform 0.2s ease;
    }
    button:hover {
      background: #4054b2;
      transform: translateY(-2px);
    }
    button:active {
      transform: scale(0.98);
    }
    .status {
      margin-top: 12px;
      font-weight: bold;
      color: green;
    }
    /* ซ่อนฟิลด์ RFID */
    .rfid-group {
      display: none;
    }
  </style>
  <script>
    // ฟังก์ชันตรวจสอบสถานะ RFID ผ่าน endpoint /rfidStatus
    function checkRFIDAndSend(user) {
      fetch("/rfidStatus")
      .then(response => response.text())
      .then(status => {
         if(status.trim() !== "authorized") {
           alert("RFID ไม่ถูกต้อง! กรุณาสแกนบัตรที่ถูกต้องก่อน");
           return;
         }
         sendData(user);
      })
      .catch(error => {
         console.error("Error checking RFID status:", error);
      });
    }
    function sendData(user) {
      var data = new FormData();
      data.append("user", user);
      data.append("medicine_type", document.getElementById("medicineType_" + user).value);
      data.append("quantity", document.getElementById("quantity_" + user).value);
      data.append("food_time", document.getElementById("foodTime_" + user).value);
      data.append("price", document.getElementById("price_" + user).value);
      data.append("locker_number", document.getElementById("lockerNumber_" + user).value);
      var timeSelections = document.querySelectorAll('input[name="timeOfDay_' + user + '"]:checked');
      var times = [];
      timeSelections.forEach(function(checkbox) {
        times.push(checkbox.value);
      });
      data.append("time_of_day", times.join(","));
      // ส่งค่า RFID (สำหรับบันทึกข้อมูลในระบบ)
      var rfidValue = document.getElementById("rfid_" + user).value;
      data.append("rfid", rfidValue);
      document.getElementById("status_" + user).innerHTML = "กำลังส่งข้อมูล...";
      fetch("http://xxx.xxx.xxx.xx/insert_data.php", { 
        method: "POST",
        body: data
      })
      .then(response => response.text())
      .then(responseText => {
        document.getElementById("status_" + user).innerHTML = responseText;
      })
      .catch(error => {
        console.error("Error:", error);
        document.getElementById("status_" + user).innerHTML = "เกิดข้อผิดพลาดในการส่งข้อมูล";
      });
    }
  </script>
</head>
<body>
  <div class="container">
    <h1>Smart Medicine Dispenser</h1>

    <!-- User 1 Section -->
    <div class="user-section" id="user1">
      <h2>User 1</h2>
      <!-- ฟิลด์ RFID (ซ่อน) -->
      <div class="form-group rfid-group">
        <label>RFID</label>
        <input type="text" id="rfid_1" value="73 DF A9 D">
      </div>
      <div class="form-group">
        <label>Medicine Type</label>
        <select id="medicineType_1">
          <option>Paracetamol</option>
          <option>Ibuprofen</option>
          <option>Antibiotic</option>
          <option>Vitamin C</option>
        </select>
      </div>
      <div class="inline-group">
        <div class="form-group">
          <label>Quantity</label>
          <input type="number" id="quantity_1" min="1" max="50" value="1">
        </div>
        <div class="form-group">
          <label>Food Time</label>
          <select id="foodTime_1">
            <option value="Before">before meals</option>
            <option value="After">post meal</option>
          </select>
        </div>
      </div>
      <div class="form-group">
        <label>Price (บาท)</label>
        <input type="number" id="price_1" min="0" max="10000" value="0">
      </div>
      <label>Time of Day</label>
      <div class="checkbox-container">
        <label><input type="checkbox" name="timeOfDay_1" value="Morning"> Morning</label>
        <label><input type="checkbox" name="timeOfDay_1" value="Afternoon"> Afternoon</label>
        <label><input type="checkbox" name="timeOfDay_1" value="Evening"> Evening</label>
      </div>
      <div class="form-group">
        <label>Locker #</label>
        <select id="lockerNumber_1">
          <option value="1">1</option>
          <option value="2">2</option>
          <option value="3">3</option>
        </select>
      </div>
      <button onclick="checkRFIDAndSend(1)">Save User 1 Data</button>
      <p id="status_1" class="status"></p>
    </div>

    <!-- User 2 Section -->
    <div class="user-section" id="user2">
      <h2>User 2</h2>
      <div class="form-group rfid-group">
        <label>RFID</label>
        <input type="text" id="rfid_2" value="83 BF 77 F">
      </div>
      <div class="form-group">
        <label>Medicine Type</label>
        <select id="medicineType_2">
          <option>Paracetamol</option>
          <option>Ibuprofen</option>
          <option>Antibiotic</option>
          <option>Vitamin C</option>
        </select>
      </div>
      <div class="inline-group">
        <div class="form-group">
          <label>Quantity</label>
          <input type="number" id="quantity_2" min="1" max="50" value="1">
        </div>
        <div class="form-group">
          <label>Food Time</label>
          <select id="foodTime_2">
            <option value="Before">before meals</option>
            <option value="After">post meal</option>
          </select>
        </div>
      </div>
      <div class="form-group">
        <label>Price (บาท)</label>
        <input type="number" id="price_2" min="0" max="10000" value="0">
      </div>
      <label>Time of Day</label>
      <div class="checkbox-container">
        <label><input type="checkbox" name="timeOfDay_2" value="Morning"> Morning</label>
        <label><input type="checkbox" name="timeOfDay_2" value="Afternoon"> Afternoon</label>
        <label><input type="checkbox" name="timeOfDay_2" value="Evening"> Evening</label>
      </div>
      <div class="form-group">
        <label>Locker #</label>
        <select id="lockerNumber_2">
          <option value="1">1</option>
          <option value="2">2</option>
          <option value="3">3</option>
        </select>
      </div>
      <button onclick="checkRFIDAndSend(2)">Save User 2 Data</button>
      <p id="status_2" class="status"></p>
    </div>

    <!-- User 3 Section -->
    <div class="user-section" id="user3">
      <h2>User 3</h2>
      <div class="form-group rfid-group">
        <label>RFID</label>
        <input type="text" id="rfid_3" value="E3 D6 9B FC">
      </div>
      <div class="form-group">
        <label>Medicine Type</label>
        <select id="medicineType_3">
          <option>Paracetamol</option>
          <option>Ibuprofen</option>
          <option>Antibiotic</option>
          <option>Vitamin C</option>
        </select>
      </div>
      <div class="inline-group">
        <div class="form-group">
          <label>Quantity</label>
          <input type="number" id="quantity_3" min="1" max="50" value="1">
        </div>
        <div class="form-group">
          <label>Food Time</label>
          <select id="foodTime_3">
            <option value="Before">before meals</option>
            <option value="After">post meal</option>
          </select>
        </div>
      </div>
      <div class="form-group">
        <label>Price (บาท)</label>
        <input type="number" id="price_3" min="0" max="10000" value="0">
      </div>
      <label>Time of Day</label>
      <div class="checkbox-container">
        <label><input type="checkbox" name="timeOfDay_3" value="Morning"> Morning</label>
        <label><input type="checkbox" name="timeOfDay_3" value="Afternoon"> Afternoon</label>
        <label><input type="checkbox" name="timeOfDay_3" value="Evening"> Evening</label>
      </div>
      <div class="form-group">
        <label>Locker #</label>
        <select id="lockerNumber_3">
          <option value="1">1</option>
          <option value="2">2</option>
          <option value="3">3</option>
        </select>
      </div>
      <button onclick="checkRFIDAndSend(3)">Save User 3 Data</button>
      <p id="status_3" class="status"></p>
    </div>
  </div>
</body>
</html>
)rawliteral";

// ฟังก์ชันส่งข้อมูลไปยัง PHP script (ไม่ส่ง payment status)
void sendToServer(String user, String medicine, int quantity, String time, int locker, String price, String food_time, String rfid);

void setup() {
  Serial.begin(115200);
  Serial.println("Start setup...");
  SPI.begin();
  Serial.println("SPI initialized");
  
  // เริ่มต้นโมดูล RFID
  mfrc522.PCD_Init();
  Serial.println("RFID module initialized");

  Serial.println("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());
  delay(5000);
  Serial.println("Web Server Starting...");

  server.on("/", [](){
  isRFIDAuthorized = false;
  server.send_P(200, "text/html", webpage);
  });

  // Route สำหรับ /sendData (รับข้อมูลจากหน้าเว็บ)
  server.on("/sendData", HTTP_POST, [](){
    String user = server.arg("user");
    String medicine_type = server.arg("medicine_type");
    String quantity = server.arg("quantity");
    String time_of_day = server.arg("time_of_day");
    String locker_number = server.arg("locker_number");
    String price = server.arg("price");
    String food_time = server.arg("food_time");
    String rfid = server.arg("rfid");

    Serial.println("Received data from web:");
    Serial.println("User: " + user);
    Serial.println("Medicine: " + medicine_type);
    Serial.println("Quantity: " + quantity);
    Serial.println("Price: " + price);
    Serial.println("Time: " + time_of_day);
    Serial.println("Locker: " + locker_number);
    Serial.println("Food Time: " + food_time);
    Serial.println("RFID: " + rfid);

    sendToServer(user, medicine_type, quantity.toInt(), time_of_day, locker_number.toInt(), price, food_time, rfid);
    server.send(200, "text/plain", "Data received for user " + user);
  });

  // Endpoint สำหรับตรวจสอบสถานะ RFID
  server.on("/rfidStatus", HTTP_GET, [](){
    if(isRFIDAuthorized) {
      server.send(200, "text/plain", "authorized");
    } else {
      server.send(200, "text/plain", "unauthorized");
    }
  });

  server.begin();
  Serial.println("Web Server Started!");
}

void loop() {
  server.handleClient();

  // ตรวจสอบการ์ด RFID เมื่อมีการ์ดใหม่เข้ามา
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    String scannedRFID = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      scannedRFID += String(mfrc522.uid.uidByte[i], HEX);
      if (i < mfrc522.uid.size - 1) {
        scannedRFID += " ";
      }
    }
    scannedRFID.toUpperCase();
    Serial.print("Scanned RFID: ");
    Serial.println(scannedRFID);

    // ตรวจสอบว่าการ์ดที่สแกนได้ตรงกับ "E3 D6 9B FC" หรือไม่
    if(scannedRFID == "E3 D6 9B FC") {
      isRFIDAuthorized = true;
      Serial.println("RFID authorized");
    } else {
      isRFIDAuthorized = false;
      Serial.println("RFID not authorized");
    }
    mfrc522.PICC_HaltA();
  }
}

// ฟังก์ชันส่งข้อมูลไปยัง PHP script
void sendToServer(String user, String medicine, int quantity, String time, int locker, String price, String food_time, String rfid) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String httpRequestData = "user=" + user +
                             "&medicine_type=" + medicine +
                             "&quantity=" + String(quantity) +
                             "&time_of_day=" + time +
                             "&locker_number=" + String(locker) +
                             "&price=" + price +
                             "&food_time=" + food_time +
                             "&rfid=" + rfid;
    Serial.println("Sending data to MySQL via PHP script:");
    Serial.println(httpRequestData);

    int httpResponseCode = http.POST(httpRequestData);
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.print("MySQL Server Response: ");
      Serial.println(response);
    } else {
      Serial.print("Error sending data: ");
      Serial.println(http.errorToString(httpResponseCode).c_str());
    }
    http.end();
  } else {
    Serial.println("WiFi not connected. Cannot send data.");
  }
}
