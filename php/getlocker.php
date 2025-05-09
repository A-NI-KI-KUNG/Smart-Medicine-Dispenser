<?php
header("Access-Control-Allow-Origin: *");
header("Content-Type: application/json; charset=UTF-8");

// ตั้งค่าการเชื่อมต่อฐานข้อมูล
$servername = "localhost";
$username = "root";
$password = "";
$dbname = "user_esp32";

// สร้างการเชื่อมต่อฐานข้อมูล
$conn = new mysqli($servername, $username, $password, $dbname);
if ($conn->connect_error) {
    die(json_encode(["status" => "error", "message" => "Connection failed: " . $conn->connect_error]));
}

// ตรวจสอบ Method ว่าเป็น GET หรือไม่
if ($_SERVER["REQUEST_METHOD"] == "GET") {
    // ตรวจสอบว่ามีพารามิเตอร์ user ส่งมาหรือไม่
    if (isset($_GET["user"]) && !empty($_GET["user"])) {
        // แปลงเป็นตัวเลข (กรณีคอลัมน์ user ใน DB เป็น INT)
        // ถ้าคอลัมน์ user เป็นประเภท string หรือ float ให้พิจารณาใช้แบบอื่น เช่น "'$user'"
        $user = intval($_GET["user"]);
        
        // สมมติว่าใน DB มีคอลัมน์ชื่อ user (เก็บค่า user_id) และคอลัมน์ locker_number
        // และต้องการดึง locker_number
        $sql = "SELECT locker_number FROM users WHERE user = $user LIMIT 1";
        
        // หากคอลัมน์เป็นชื่ออื่น เช่น id หรือ user_id ต้องแก้ให้ตรง
        // $sql = "SELECT locker_number FROM users WHERE id = $user LIMIT 1";
        // หรือ $sql = "SELECT locker_number FROM users WHERE user_id = $user LIMIT 1";
        
        $result = $conn->query($sql);
        
        if ($result && $result->num_rows > 0) {
            $row = $result->fetch_assoc();
            // ดึงค่า locker_number
            $locker = $row["locker_number"];
            
            // ส่งคืนข้อมูลในรูปแบบ JSON
            echo json_encode([
                "status" => "success",
                "data" => $locker
            ]);
        } else {
            // ไม่พบแถวที่ตรงกับเงื่อนไข
            echo json_encode([
                "status" => "empty",
                "message" => "No locker found for user: $user"
            ]);
        }
    } else {
        // กรณีไม่ส่ง user มา
        echo json_encode([
            "status" => "error",
            "message" => "User parameter not provided"
        ]);
    }
} else {
    // ไม่ใช่ GET
    echo json_encode([
        "status" => "error",
        "message" => "Invalid request method"
    ]);
}

$conn->close();
?>
