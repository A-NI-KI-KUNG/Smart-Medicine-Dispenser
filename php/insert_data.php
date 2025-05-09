<?php
// เพิ่ม CORS headers เพื่อรองรับการเรียกใช้งานจากโดเมนอื่น
header("Access-Control-Allow-Origin: *");
header("Access-Control-Allow-Methods: POST, GET, OPTIONS");
header("Access-Control-Allow-Headers: Content-Type");

// ถ้าเป็นการเรียก preflight (OPTIONS) ให้ส่งกลับ status 204 แล้วหยุดการประมวลผล
if ($_SERVER['REQUEST_METHOD'] == 'OPTIONS') {
    http_response_code(204);
    exit();
}

// กำหนดค่าการเชื่อมต่อฐานข้อมูล
$servername = "localhost";
$username = "root";
$password = "";
$dbname = "user_esp32";

// สร้างการเชื่อมต่อ
$conn = new mysqli($servername, $username, $password, $dbname);

// ตรวจสอบการเชื่อมต่อ
if ($conn->connect_error) {
    die("Connection failed: " . $conn->connect_error);
}

// ตรวจสอบว่ามีการส่งข้อมูลผ่าน POST
if ($_SERVER["REQUEST_METHOD"] == "POST") {
    // ตรวจสอบว่ามีข้อมูลครบถ้วนหรือไม่ (ไม่รวม payment_status)
    if (!empty($_POST["user"]) && 
        !empty($_POST["medicine_type"]) && 
        !empty($_POST["quantity"]) && 
        !empty($_POST["time_of_day"]) &&
        !empty($_POST["locker_number"]) && 
        !empty($_POST["price"]) &&
        !empty($_POST["food_time"]) &&
        !empty($_POST["rfid"])) 
    {
        $user           = $_POST["user"];
        $medicine_type  = $_POST["medicine_type"];
        $quantity       = $_POST["quantity"];
        $time_of_day    = $_POST["time_of_day"];
        $locker_number  = $_POST["locker_number"];
        $price          = $_POST["price"];
        $food_time      = $_POST["food_time"];
        $rfid           = $_POST["rfid"];

        // สร้างคำสั่ง SQL สำหรับแทรกข้อมูล (ไม่รวม payment_status)
        $sql = "INSERT INTO users 
                (user, medicine_type, quantity, time_of_day, locker_number, price, food_time, rfid)
                VALUES 
                ('$user', '$medicine_type', '$quantity', '$time_of_day', '$locker_number', '$price', '$food_time', '$rfid')";

        if ($conn->query($sql) === TRUE) {
            echo "บันทึกข้อมูลเสร็จสิ้น";
        } else {
            echo "Error: " . $conn->error;
        }
    } else {
        echo "Missing Data Fields";
    }
} else {
    echo "Invalid Request Method";
}

$conn->close();
?>


