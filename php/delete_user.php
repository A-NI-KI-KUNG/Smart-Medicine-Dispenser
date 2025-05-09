<?php
header("Access-Control-Allow-Origin: *");
header("Content-Type: application/json; charset=UTF-8");

// ตั้งค่าการเชื่อมต่อฐานข้อมูล
$servername = "localhost";
$username = "root";
$password = "";
$dbname = "user_esp32";

// สร้างการเชื่อมต่อกับฐานข้อมูล
$conn = new mysqli($servername, $username, $password, $dbname);

// ตรวจสอบการเชื่อมต่อ
if ($conn->connect_error) {
    die(json_encode([
        "status" => "error",
        "message" => "Connection failed: " . $conn->connect_error
    ]));
}

if ($_SERVER["REQUEST_METHOD"] == "GET") {
    if (isset($_GET["rfid"]) && !empty($_GET["rfid"])) {
        $rfid = $_GET["rfid"];
        // ลบข้อมูลในตาราง users ที่มีค่า rfid ตรงกัน
        $sql = "DELETE FROM users WHERE rfid = '$rfid'";
        if ($conn->query($sql) === TRUE) {
            echo json_encode([
                "status" => "success",
                "message" => "Records deleted"
            ]);
        } else {
            echo json_encode([
                "status" => "error",
                "message" => "Error deleting records: " . $conn->error
            ]);
        }
    } else {
        echo json_encode([
            "status" => "error",
            "message" => "RFID parameter missing"
        ]);
    }
} else {
    echo json_encode([
        "status" => "error",
        "message" => "Invalid Request Method"
    ]);
}

$conn->close();
?>
