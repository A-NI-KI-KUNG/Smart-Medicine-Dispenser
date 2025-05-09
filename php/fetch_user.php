<?php
header("Access-Control-Allow-Origin: *");
header("Content-Type: application/json; charset=UTF-8");

// ตั้งค่าการเชื่อมต่อฐานข้อมูล
$servername = "localhost";
$username = "root";
$password = "";
$dbname = "user_esp32";

$conn = new mysqli($servername, $username, $password, $dbname);
if ($conn->connect_error) {
    die(json_encode(["status" => "error", "message" => "Connection failed: " . $conn->connect_error]));
}

if ($_SERVER["REQUEST_METHOD"] == "GET") {
    if (isset($_GET["rfid"]) && !empty($_GET["rfid"])) {
        $rfid = $_GET["rfid"];
        // ดึงข้อมูลทุก record ที่ rfid ตรงกัน
        $sql = "SELECT * FROM users WHERE rfid = '$rfid'";
    } else {
        // ถ้าไม่มี rfid ส่งมา จะดึงข้อมูลทั้งหมด
        $sql = "SELECT * FROM users";
    }
    
    $result = $conn->query($sql);
    $data = [];

    if ($result && $result->num_rows > 0) {
        while ($row = $result->fetch_assoc()) {
            $data[] = $row;
        }
        echo json_encode(["status" => "success", "data" => $data]);
    } else {
        echo json_encode(["status" => "empty", "message" => "No records found"]);
    }
} else {
    echo json_encode(["status" => "error", "message" => "Invalid Request Method"]);
}

$conn->close();
?>



