#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SimpleTimer.h>
#include <DFRobot_ESP_EC.h>
#include <WiFiManager.h>
#include <TimeLib.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

// -------------------------------------------------------
// 1) ปรับค่า Define ให้เหมาะสม
// -------------------------------------------------------
#define PH_SENSOR_PIN 36
#define EC_SENSOR_PIN 38

// ปุ่ม 4 ปุ่ม
#define BUTTON_PIN_1 5
#define BUTTON_PIN_2 19
#define BUTTON_PIN_3 18
#define BUTTON_PIN_4 23   

// ดีบั๊น + กดค้าง
#define DEBOUNCE_DELAY 30
#define LONG_PRESS_DURATION 6000  // 5 วินาที


// บอร์ดอ่านแรงดันแบตเตอรี่ (ถ้ามี)
#define BATTERY_PIN 34


// สถานะเมนูย่อย Calibration
bool inCalibrationMenu = false;
int calibrationSelection = 0; // 0 = pH, 1 = EC
const int calibrationOptions = 2;

// ค่า Calibration ปัจจุบัน
float currentPHCalibration = 0.0f;
float currentECCalibration = 0.0f;

// ค่าคงที่สำหรับคำนวณ pH
float calibration_value = 21.34 - 1;


// -------------------------------------------------------
// 2) ตัวแปร Global
// -------------------------------------------------------
SimpleTimer timer;
DFRobot_ESP_EC ec;
Adafruit_SSD1306 display(128, 64, &Wire, -1);
WiFiManager wifiManager;

// ปุ่ม
bool button1Pressed = false;
bool button2Pressed = false;
bool button3Pressed = false;
bool button4Pressed = false;

// แยกตัวแปร static สำหรับแต่ละปุ่ม
static unsigned long pressStartTime[4] = {0, 0, 0, 0 };
static bool longPressDetected[4]       = {false, false, false, false};

// สถานะระบบ
bool systemOn     = true;
bool pHSelected   = false;
bool ECSelected   = false;
bool displayOn    = true;
bool onHomePage   = true;
bool inWiFiMenu   = false;
bool onSaveScreen = false;
bool inUserSelection = false; 
bool inFarmSelection = false;  
bool usersFetched = false;  
bool fetchDataMenu = false;
bool inFetchMenu = false;
bool inSavedUserSelection = false;
bool inSavedFarmSelection = false;
bool inSavedFarmData = false;
bool indeisplaysavedfarmdata = false;

// ค่าคงที่สำหรับคำนวณ pH
// float calibration_value = 21.34 - 1;

// ตัวแปรเก็บค่าที่คำนวณได้
float ph_act   = 0.0f;
float ec_value = 0.0f;

// สำหรับหน้า Homepage สไลด์
const int totalPages = 4;
int currentPage      = 0;

// จำนวนครั้งที่จะอ่านค่าเซนเซอร์เพื่อทำ Moving Average
static const int readingCount = 10;
// -------------------------------------------------------
// ตัวแปรเก็บค่าฟาร์ม
// -------------------------------------------------------
const int MAX_FARMS = 20;  // Maximum number of farms
const int MAX_USERS = 20;

int userCount = 0;              // จำนวนผู้ใช้ทั้งหมด
int selectedUserIndex = 0;      // ผู้ใช้ที่เลือกปัจจุบัน
String users[MAX_USERS];      // Array to store usernames
String farms[MAX_USERS][MAX_FARMS];  // 2D array to store farm names
String farmIds[MAX_USERS][MAX_FARMS];   // ฟาร์มของผู้ใช้แต่ละคน [userIndex][farmIndex]
int farmCounts[10];             // จำนวนฟาร์มของแต่ละผู้ใช้
int selectedFarmIndex = 0;      // ฟาร์มที่เลือกปัจจุบัน
int userPage = 0;
int farmPage = 0;

// Ip ดึงข้อมูล 
const char* serverIP = "https://plain-dragons-kneel.loca.lt/update2/";

// ค่าสถานะ Ec ph 
bool inPH_EC_Selection = false;  
int ph_ec_selection = 0;         // 0 = pH, 1 = EC

// เพิ่มตัวแปรเพื่อเก็บข้อมูลแบบออฟไลน์
struct Measurement {
    String farmId;
    float phValue;
    float ecValue;
    String timestamp;
    bool hasPh = false;  // Flag to indicate if pH is set
    bool hasEc = false;
    String source;  // Flag to indicate if EC is set
};


Measurement offlineData[10][10];  // เก็บข้อมูลออฟไลน์สูงสุด 3 ครั้งต่อฟาร์ม
int offlineCount[10] = {0};      // จำนวนข้อมูลที่บันทึกต่อฟาร์ม

String getCurrentTimestamp() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("❌ Failed to obtain time for timestamp!");
        return "1970-01-01 00:00:00";  // ค่าเริ่มต้นถ้าไม่มีเวลา
    }

    char timestampStr[20];
    snprintf(timestampStr, sizeof(timestampStr), "%04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    Serial.print("📅 Generated Timestamp: ");
    Serial.println(timestampStr);  // แสดงค่า timestamp ที่ได้
    
    return String(timestampStr);
}



String loadUserDataFromSPIFFS() {
    if (!SPIFFS.exists("/user_data.json")) {
        Serial.println("No saved user data found in SPIFFS.");
        return "";
    }

    File file = SPIFFS.open("/user_data.json", FILE_READ);
    if (!file) {
        Serial.println("Failed to open user data file for reading!");
        return "";
    }

    String jsonString = file.readString();
    file.close();

    // ✅ พิมพ์ค่าที่อ่านออกมาเพื่อตรวจสอบ
    Serial.println("Raw JSON String from SPIFFS:");
    Serial.println(jsonString);

    return jsonString;
}


// bool fromSavedData = false;  // ตรวจจับว่ากำลังเข้าจาก Saved Data
int selectedEC_PH_Index = 0; // ตำแหน่งของค่าที่เลือก (0-2) เพื่อแก้ไข
// bool editingValue = false;   // ตรวจสอบว่ากำลังแก้ไขค่า


// ตั้งค่า Timezone GMT+7 (ประเทศไทย)
const char* ntpServer = "time.google.com";  // ใช้เซิร์ฟเวอร์เวลา NTP ทั่วโลก
const long  gmtOffset_sec = 7 * 3600;    // GMT+7 (ประเทศไทย)
const int   daylightOffset_sec = 0;      // ไม่ใช้เวลาออมแสง (DST)


// -------------------------------------------------------
// ประกาศฟังก์ชันล่วงหน้า
// -------------------------------------------------------
void handleButtonPresses();
void debounceButton(int buttonPin, int index, bool &buttonState, void (*shortPressAction)(), void (*longPressAction)());
void readSensorValues();
void display_pH_EC_Values();

void returnToHomepage();
void enterWiFiMenu();
void WiFiConnect();
void WiFiReset();
void displayHomepage();
void toggleSystem();
void displaySavedFarmData();
void syncOfflineData();
void loadSavedDataFromSPIFFS();
void saveCalibrationData(float phCalibration, float ecCalibration);
void saveUserDataToSPIFFS(String jsonString);
void displaySavedUsersPage();
void displaySavedFarmsForUser();

void handleButton1ShortPress();
void handleButton1LongPress();
void handleButton2ShortPress();
void handleButton2LongPress();
void handleButton3ShortPress();
void handleButton3LongPress();
void handleButton4ShortPress();
void handleButton4LongPress();

void displayUsers();
void displayFarmsForUser(int userIndex);
void fetchUsers(); 
void startFetchingUsers();


void displayBatteryLevel();
void displayPage(int page);
void selectCurrentPage();


void saveCalibrationData(float phCalibration, float ecCalibration);
float loadPHCalibrationFromSPIFFS();
float loadECCalibrationFromSPIFFS();




// ฟังก์ชันเพิ่มเติมสำหรับ Calibration
void displayCalibrationMenu();
void calibratePH();
void calibrateEC();
void returnToCalibrationMenu(const char* message);
void selectCurrentPage();
void displayBatteryLevel();
void displayPage(int page);

// ฟังก์ชันสำหรับแสดงตัวเลือก pH/EC และแสดงค่าแบบเรียลไทม์
void displayPH_EC_Options();
void displayRealTimePH();
void displayRealTimeEC();
void savePH(String farmName, float phValue);
void saveEC(String farmName, float ecValue);
int sendDataToServer(String postData, String apiURL);

// ฟังก์ชันสำหรับการบันทึกและซิงค์ข้อมูลแบบออฟไลน์
void saveOfflineData(String farmId, float phValue, float ecValue, String source);
void syncOfflineData();
void uploadDataToServer(Measurement data);
int getFarmIndex(String farmName);


void clearSPIFFSData();
void deleteFileFromSPIFFS(const char* path);
void showCalibrationData();
// -------------------------------------------------------
// ทดสอบการกดปุ่ม 
// -------------------------------------------------------

void processSerialInput(char input) {
    switch (input) {
        case '1':
            Serial.println("Simulating Button 1 Short Press...");
            handleButton1ShortPress();
            break;
        case '2':
            Serial.println("Simulating Button 2 Short Press...");
            handleButton2ShortPress();
            break;
        case '3':
            Serial.println("Simulating Button 3 Short Press...");
            handleButton3ShortPress();
            break;
        case '4':
            Serial.println("Simulating Button 4 Short Press...");
            handleButton4ShortPress();
            break;
        case 'L':  // หากต้องการให้กดค้าง (Long Press)
            if (Serial.available() > 0) {
                char longPressInput = Serial.read();
                Serial.print("Simulating Long Press Button ");
                Serial.println(longPressInput);

                switch (longPressInput) {
                    case '1': handleButton1LongPress(); break;
                    case '2': handleButton2LongPress(); break;
                    case '3': handleButton3LongPress(); break;
                    case '4': handleButton4LongPress(); break;
                }
            }
            break;
        default:
            Serial.println("Invalid Input! Use 1, 2, 3, 4 (or L1, L2, L3, L4 for long press)");
    }
}

void checkSavedDataInSPIFFS() {
    if (SPIFFS.exists("/user_data.json")) {
        Serial.println("User data file exists.");
        File file = SPIFFS.open("/user_data.json", FILE_READ);
        if (file) {
            Serial.println("Reading user_data.json...");
            String fileContent = file.readString();
            Serial.println(fileContent);  // แสดงข้อมูลทั้งหมดที่อ่านได้
            file.close();
        } else {
            Serial.println("Failed to open user_data.json.");
        }
    } else {
        Serial.println("No user data file found in SPIFFS.");
    }
}



// -------------------------------------------------------
// 3) setup()
// -------------------------------------------------------
void setup() {
    Wire.begin();
    Serial.begin(115200);

    // IPAddress local_ip(192, 168, 42, 100);  // IP ที่ต้องการให้ ESP32
    // IPAddress gateway(192, 168, 42, 1);      // Gateway ของเครือข่าย
    // IPAddress subnet(255, 255, 255, 0);      // Subnet mask
    // WiFi.config(local_ip, gateway, subnet, IPAddress(8, 8, 8, 8));  // ตั้งค่า DNS เป็น 8.8.8.8


    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.clearDisplay();
    display.setTextColor(WHITE);

    pinMode(BUTTON_PIN_1, INPUT_PULLUP);
    pinMode(BUTTON_PIN_2, INPUT_PULLUP);
    pinMode(BUTTON_PIN_3, INPUT_PULLUP);
    pinMode(BUTTON_PIN_4, INPUT_PULLUP);

    // เปลี่ยนเป็นอัปเดตหน้าจอทุก 1 วินาที เพื่อลด Noise เห็น
    timer.setInterval(1000L, display_pH_EC_Values);

    ec.begin();

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS initialization failed!");
        return;
    } else {
        Serial.println("SPIFFS initialized successfully.");
    }
    loadSavedDataFromSPIFFS();  
    checkSavedDataInSPIFFS();
    showCalibrationData();
// Serial.print("Heap before WiFiConnect: ");
//     Serial.println(ESP.getFreeHeap());


//       WiFiConnect();


// Serial.print("Heap after WiFiConnect: ");
//     Serial.println(ESP.getFreeHeap());

    // WiFiReset();

    displayHomepage();  // แสดงหน้าแรก
}


void loop() {
    handleButtonPresses();  // ตรวจจับปุ่มกดปกติ

    if (systemOn) {
        // readSensorValues();  // ถ้าเปิดระบบ อ่านค่าเซนเซอร์
    }

    timer.run();  // ทำงานตาม timer
    // **อ่านค่าจาก Serial Monitor**
    if (Serial.available() > 0) {
        char input = Serial.read();  // อ่านตัวอักษรที่พิมพ์มา
        processSerialInput(input);
    }
}


// -------------------------------------------------------
// 4) จัดการปุ่ม + ดีบั๊น
// -------------------------------------------------------
void handleButtonPresses() {
    if (inCalibrationMenu) {
        debounceButton(BUTTON_PIN_1, 0, button1Pressed, handleButton1ShortPress, handleButton1LongPress);
        debounceButton(BUTTON_PIN_2, 1, button2Pressed, handleButton2ShortPress, handleButton2LongPress);
        debounceButton(BUTTON_PIN_3, 2, button3Pressed, handleButton3ShortPress, handleButton3LongPress);
        debounceButton(BUTTON_PIN_4, 3, button4Pressed, handleButton4ShortPress, handleButton4LongPress);
        bool btn2 = (digitalRead(BUTTON_PIN_2) == LOW);
        if (btn2 && !button2Pressed) {
            delay(DEBOUNCE_DELAY);
            if (digitalRead(BUTTON_PIN_2) == LOW) {
                button2Pressed = true;
                handleButton2ShortPress();  
            }
        } else if (!btn2 && button2Pressed) {
            button2Pressed = false;
        }
     
    } 
    else if (inPH_EC_Selection) {  // ตรวจสอบว่ากำลังอยู่ในหน้าเลือก Measurement (pH/EC)
        debounceButton(BUTTON_PIN_1, 0, button1Pressed, handleButton1ShortPress, handleButton1LongPress);
        debounceButton(BUTTON_PIN_2, 1, button2Pressed, handleButton2ShortPress, handleButton2LongPress);
        debounceButton(BUTTON_PIN_3, 2, button3Pressed, handleButton3ShortPress, handleButton3LongPress);
        debounceButton(BUTTON_PIN_4, 3, button4Pressed, handleButton4ShortPress, handleButton4LongPress);

        bool btn2 = (digitalRead(BUTTON_PIN_2) == LOW);
        if (btn2 && !button2Pressed) {
            delay(DEBOUNCE_DELAY);
            if (digitalRead(BUTTON_PIN_2) == LOW) {
                button2Pressed = true;
                handleButton2ShortPress();  // กดปุ่ม 2 เพื่อเลือก pH หรือ EC
            }
        } else if (!btn2 && button2Pressed) {
            button2Pressed = false;
        }
    }
   else if (inUserSelection || inFarmSelection) {  
    debounceButton(BUTTON_PIN_1, 0, button1Pressed, handleButton1ShortPress, handleButton1LongPress);
    debounceButton(BUTTON_PIN_2, 1, button2Pressed, handleButton2ShortPress, handleButton2LongPress);
    debounceButton(BUTTON_PIN_3, 2, button3Pressed, handleButton3ShortPress, handleButton3LongPress);
    debounceButton(BUTTON_PIN_4, 3, button4Pressed, handleButton4ShortPress, handleButton4LongPress);

    if (digitalRead(BUTTON_PIN_2) == LOW && !button2Pressed) {
        delay(DEBOUNCE_DELAY);
        if (digitalRead(BUTTON_PIN_2) == LOW) {
            Serial.println("Button 2 Pressed in User/Farm Selection");
            button2Pressed = true;
            handleButton2ShortPress();
        }
    } else if (digitalRead(BUTTON_PIN_2) == HIGH && button2Pressed) {
        button2Pressed = false;
    }
}
    else if (inWiFiMenu) {  
        debounceButton(BUTTON_PIN_1, 0, button1Pressed, handleButton1ShortPress, handleButton1LongPress);
        debounceButton(BUTTON_PIN_2, 1, button2Pressed, handleButton2ShortPress, handleButton2LongPress);
        debounceButton(BUTTON_PIN_3, 2, button3Pressed, handleButton3ShortPress, handleButton3LongPress);
        debounceButton(BUTTON_PIN_4, 3, button4Pressed, handleButton4ShortPress, handleButton4LongPress);
        
    } 
    else {  
        debounceButton(BUTTON_PIN_1, 0, button1Pressed, handleButton1ShortPress, handleButton1LongPress);
        debounceButton(BUTTON_PIN_2, 1, button2Pressed, handleButton2ShortPress, handleButton2LongPress);
        debounceButton(BUTTON_PIN_3, 2, button3Pressed, handleButton3ShortPress, handleButton3LongPress);
        debounceButton(BUTTON_PIN_4, 3, button4Pressed, handleButton4ShortPress, handleButton4LongPress);

        bool btn2 = (digitalRead(BUTTON_PIN_2) == LOW);
        if (btn2 && !button2Pressed) {
            delay(DEBOUNCE_DELAY);
            if (digitalRead(BUTTON_PIN_2) == LOW) {
                button2Pressed = true;
                selectCurrentPage();
            }
        } else if (!btn2 && button2Pressed) {
            button2Pressed = false;
        }
    }
    
}


void debounceButton(int buttonPin, int index, bool &buttonState,
                    void (*shortPressAction)(), void (*longPressAction)()) {
    bool currentState = (digitalRead(buttonPin) == LOW);
    unsigned long currentTime = millis();

    if (currentState && !buttonState) {
        delay(DEBOUNCE_DELAY);  // กัน Noise
        if (digitalRead(buttonPin) == LOW) {
            buttonState = true;
            pressStartTime[index] = currentTime;
            longPressDetected[index] = false;
        }
    }
    else if (!currentState && buttonState) {
        delay(DEBOUNCE_DELAY); // ลดจาก 30 เป็น 27 มิลลิวินาที
        if (digitalRead(buttonPin) == HIGH) {
            buttonState = false;
            if (longPressDetected[index]) {
                
            } else {
                
                if ((currentTime - pressStartTime[index]) < LONG_PRESS_DURATION) {
                    shortPressAction();
                }
            }
        }
    }

    // ตรวจจับ long press
    if (currentState && !longPressDetected[index]) {
        if ((currentTime - pressStartTime[index]) >= LONG_PRESS_DURATION) {
            longPressDetected[index] = true;
            longPressAction();
        }
    }
}






// -------------------------------------------------------
// 5) ฟังก์ชันกดปุ่มแบบสั้น/ยาว (ตัวอย่างปุ่ม1, ปุ่ม3)
// -------------------------------------------------------
void handleButton1ShortPress() {
    if (inPH_EC_Selection) {  
        ph_ec_selection--;
        if (ph_ec_selection < 0) ph_ec_selection = 1;
        displayPH_EC_Options();
    }
    else if (inUserSelection) {  
        if (selectedUserIndex > 0) {  
            selectedUserIndex--;  
        } 
        if (selectedUserIndex < userPage * 5) {  
            if (userPage > 0) {
                userPage--;  
                selectedUserIndex = userPage * 5 + 4;  // ✅ เลือกตัวเลือกสุดท้ายของหน้าก่อนหน้า
            }
        }
        displayUsers();
    }
    else if (inFarmSelection) {  
        if (selectedFarmIndex > 0) {  
            selectedFarmIndex--;
        } 
        if (selectedFarmIndex < farmPage * 5) {  
            if (farmPage > 0) {
                farmPage--;  
                selectedFarmIndex = farmPage * 5 + 4;  // ✅ เลือกตัวเลือกสุดท้ายของหน้าก่อนหน้า
            }
        }
        displayFarmsForUser(selectedUserIndex);
    }
    else if (inSavedUserSelection ) {  
        if (selectedUserIndex > 0) {  
            selectedUserIndex--;  
        } 
        if (selectedUserIndex < userPage * 5) {  
            if (userPage > 0) {
                userPage--;  
                selectedUserIndex = userPage * 5 + 4;  // ✅ เลือกตัวเลือกสุดท้ายของหน้าก่อนหน้า
            }
        }
        displaySavedUsersPage();
    }
    else if (inSavedFarmSelection ) {  
        if (selectedFarmIndex > 0) {  
            selectedFarmIndex--;
        } 
        if (selectedFarmIndex < farmPage * 5) {  
            if (farmPage > 0) {
                farmPage--;  
                selectedFarmIndex = farmPage * 5 + 4;  // ✅ เลือกตัวเลือกสุดท้ายของหน้าก่อนหน้า
            }
        }
        displaySavedFarmsForUser();
    }
    else if (onHomePage) {  
        currentPage--;
        if (currentPage < 0) {
            currentPage = totalPages - 1; // วนกลับไปหน้าสุดท้าย
        }
        displayHomepage();
    }
    // else if (fromSavedData) {  
    //     selectedEC_PH_Index--;
    //     if (selectedEC_PH_Index < 0) {
    //         selectedEC_PH_Index = offlineCount[getFarmIndex(farms[selectedUserIndex][selectedFarmIndex])] - 1;
    //     }
    //     displaySavedFarmData();
    // }
    else if (inWiFiMenu){
        WiFi.disconnect();
        display.clearDisplay();
        display.setCursor(10, 10);
        display.println("disconnect wifi");
        display.display();

        delay(500); 
        displayHomepage();  
    }else if (inCalibrationMenu) {  
        calibrationSelection--;
        if (calibrationSelection < 0) {
            calibrationSelection = calibrationOptions - 1;
        }
        displayCalibrationMenu();
}


else if (indeisplaysavedfarmdata) {  // Navigate saved farm data (UP)
    if (selectedEC_PH_Index > 0) {  
        selectedEC_PH_Index--;  
    } else {  
        selectedEC_PH_Index = offlineCount[getFarmIndex(farmIds[selectedUserIndex][selectedFarmIndex])] - 1;  // Wrap to last
    }
    displaySavedFarmData();
}



}


void handleButton1LongPress(){}

void handleButton2ShortPress() {
    Serial.print("Button 2 Short Press Detected | currentPage = ");
    Serial.println(currentPage);
    Serial.println(fetchDataMenu);
    Serial.println(inUserSelection);
    if (inCalibrationMenu) {
        if (calibrationSelection == 0) {
            calibratePH();
        } else {
            calibrateEC();
        }
    } 
   else if (fetchDataMenu && currentPage == 1) {
        if (usersFetched) {
            Serial.println("Users already fetched. Entering User Selection...");
            inUserSelection = true;
            currentPage = 2;  // Update the current page
            displayUsers();    // Show users
        } else {
            fetchDataMenu = false;
            display.clearDisplay();
            display.setCursor(10, 23);
            display.println("Fetching Users...");
            display.display();
            fetchUsers();  // Fetch users if not already fetched
        }
    } else if (inUserSelection) {
        Serial.print("Switching to Farm Selection for user: ");
        Serial.println(users[selectedUserIndex]);

        // Change state and update current page
        currentPage = 3;  // Set to the farm selection page
        inUserSelection = false;
        inFarmSelection = true;
        selectedFarmIndex = 0;

        Serial.println("Calling displayFarmsForUser()");
        displayFarmsForUser(selectedUserIndex);
    } else if (inFarmSelection) {
        Serial.print("Selected Farm: ");
        Serial.println(farms[selectedUserIndex][selectedFarmIndex]);

        // Change state and update current page
        currentPage = 4;  // Set to the pH/EC selection page
        inFarmSelection = false;
        inPH_EC_Selection = true;
        displayPH_EC_Options();
    } else if (inPH_EC_Selection) {
        if (ph_ec_selection == 0) {
            displayRealTimePH();
        } else {
            displayRealTimeEC();
    }


    } else if (onHomePage && currentPage == 0) {
        Serial.println("Entering WiFi Menu...");
        enterWiFiMenu();
    } else if (inWiFiMenu) {
        Serial.println("Connecting to WiFi...");
        WiFiConnect();
    } else if (onHomePage && currentPage == 2) {
        Serial.println("Entering Caribation Menu...");
        displayCalibrationMenu();
    }
    
    
    else if (inSavedUserSelection) {
        inSavedUserSelection = false;
        inSavedFarmSelection = true;
        displaySavedFarmsForUser();
    } else if (inSavedFarmSelection) {
        inSavedFarmSelection = false;
        inSavedFarmData = true;
        displaySavedFarmData();
    } else if (currentPage == 3) {
        Serial.println("Entering Saved Data Menu...");
        //fromSavedData = true;
        displaySavedUsersPage();
    } else {
        Serial.println("Unhandled Button 2 press state.");
   
}
}


void handleButton2LongPress() {
    display.clearDisplay();
    display.setCursor(10, 10);

    if (WiFi.status() != WL_CONNECTED) {
        display.println("No WiFi Connection");
        display.display();
        delay(2000);  // Display the message for 2 seconds
        returnToHomepage();  // Return to the home page
        return;
    }

    display.println("Syncing data...");
    display.display();

    syncOfflineData();  // Call the function to sync data

    display.clearDisplay();
    display.setCursor(10, 10);
    display.println("Sync Complete!");
    display.display();
    delay(2000);  // Display the result after syncing for 2 seconds

    returnToHomepage();  // Return to the home page after syncing
}



void handleButton3ShortPress() {
    if (inPH_EC_Selection) {  
        ph_ec_selection++;
        if (ph_ec_selection > 1) ph_ec_selection = 0;
        displayPH_EC_Options();
    }
    else   if (inUserSelection) {  
        if (selectedUserIndex < userCount - 1) {  
            selectedUserIndex++;
        }
        if (selectedUserIndex >= (userPage + 1) * 5) {  
            userPage++;  
            selectedUserIndex = userPage * 5;  // ✅ ไปที่ตัวเลือกแรกของหน้าถัดไป
        }
        displayUsers();
    }
    else if (inFarmSelection) {  
        if (selectedFarmIndex < farmCounts[selectedUserIndex] - 1) {
            selectedFarmIndex++;
        }
        if (selectedFarmIndex >= (farmPage + 1) * 5) {  
            farmPage++;  
            selectedFarmIndex = farmPage * 5;  // ✅ ไปที่ตัวเลือกแรกของหน้าถัดไป
        }
        displayFarmsForUser(selectedUserIndex);
    }
    else   if (inSavedUserSelection ) {  
        if (selectedUserIndex < userCount - 1) {  
            selectedUserIndex++;
        }
        if (selectedUserIndex >= (userPage + 1) * 5) {  
            userPage++;  
            selectedUserIndex = userPage * 5;  // ✅ ไปที่ตัวเลือกแรกของหน้าถัดไป
        }
        displaySavedUsersPage();
    }
    else if (inSavedFarmSelection ) {  
        if (selectedFarmIndex < farmCounts[selectedUserIndex] - 1) {
            selectedFarmIndex++;
        }
        if (selectedFarmIndex >= (farmPage + 1) * 5) {  
            farmPage++;  
            selectedFarmIndex = farmPage * 5;  // ✅ ไปที่ตัวเลือกแรกของหน้าถัดไป
        }
        displaySavedFarmsForUser();
    }
    else if (onHomePage) {  
        currentPage++;
        if (currentPage >= totalPages) {
            currentPage = 0; // วนกลับไปหน้าแรก
        }
        displayHomepage();
    }
    // else if (fromSavedData) {  
    //     selectedEC_PH_Index++;
    //     if (selectedEC_PH_Index >= offlineCount[getFarmIndex(farms[selectedUserIndex][selectedFarmIndex])]) {
    //         selectedEC_PH_Index = 0;
    //     }
    //     displaySavedFarmData();}
    else if (inWiFiMenu) {  
        WiFiReset();
} else if (inCalibrationMenu) {  
    calibrationSelection++;
    if (calibrationSelection >= calibrationOptions) {
        calibrationSelection = 0;
    }
    displayCalibrationMenu();
}


else if (indeisplaysavedfarmdata) {  // Navigate saved farm data (DOWN)
    if (selectedEC_PH_Index < offlineCount[getFarmIndex(farmIds[selectedUserIndex][selectedFarmIndex])] - 1) {
        selectedEC_PH_Index++;
    } else {
        selectedEC_PH_Index = 0;  // Wrap back to first record
    }
    displaySavedFarmData();
}





}


void handleButton3LongPress (){
    displayPH_EC_Options();
    }


    
void handleButton4ShortPress() {
    Serial.println("handleButton4ShortPress called!");

    if (inWiFiMenu) {
        Serial.println("Exiting WiFi Menu to Home Page");
        inWiFiMenu = false;
        onHomePage = true;
        currentPage = 0;

        display.clearDisplay();
        display.setCursor(10, 10);
        display.println("Returning to Home...");
        display.display();

        delay(1000);  // แสดงข้อความก่อนเปลี่ยนหน้า
        displayHomepage();  // กลับไปหน้าแรก
    } else if (inUserSelection) {
        Serial.println("Exiting UserSelection to Home Page");
        inUserSelection = false;
        onHomePage = true;
        currentPage = 1;

        display.clearDisplay();
        display.setCursor(10, 10);
        display.println("Returning to Home...");
        display.display();

        delay(1000);
        displayHomepage();  
    } else if (inFarmSelection) {
        Serial.println("Exiting FarmSelection to UserSelection");
        inFarmSelection = false;
        inUserSelection = true;

        display.clearDisplay();
        display.setCursor(10, 10);
        display.println("Returning to UserSelection...");
        display.display();

        delay(1000);
        displayUsers();}
        else if (inSavedUserSelection) {
            Serial.println("Exiting UserSelection to Home Page");
            inSavedUserSelection = false;
            onHomePage = true;
            currentPage = 3;
    
            display.clearDisplay();
            display.setCursor(10, 10);
            display.println("Returning to Home...");
            display.display();
    
            delay(1000);
            displayHomepage();  
        } else if (inSavedFarmSelection) {
            Serial.println("Exiting FarmSelection to UserSelection");
            inSavedFarmSelection = false;
            inSavedUserSelection = true;
    
            display.clearDisplay();
            display.setCursor(10, 10);
            display.println("Returning to UserSelection...");
            display.display();
    
            delay(1000);
            displaySavedUsersPage();  
    } else if (inPH_EC_Selection) {
        Serial.println("Exiting PH/EC Selection to FarmSelection");
        inPH_EC_Selection = false;
        inFarmSelection = true;

        display.clearDisplay();
        display.setCursor(10, 10);
        display.println("Returning to FarmSelection...");
        display.display();

        delay(1000);
        displayFarmsForUser(selectedUserIndex);  
    } else if (inCalibrationMenu) { 
        Serial.println("Exiting Calibration Menu to Home Page");
        inCalibrationMenu = false;
        onHomePage = true;
        currentPage = 2;

        display.clearDisplay();
        display.setCursor(10, 10);
        display.println("Returning to Home...");
        display.display();

        delay(1000);
        displayHomepage();  
    }else if (indeisplaysavedfarmdata) { 
        Serial.println("Exiting displaysavedfarmdata ");
        indeisplaysavedfarmdata = false;
        inSavedFarmSelection = true;
        

        display.clearDisplay();
        display.setCursor(10, 10);
        display.println("Returning to SavedFarmSelection ...");
        display.display();

        delay(1000);
        displaySavedFarmsForUser();
}
}


void handleButton4LongPress() {
    Serial.println("⚠️ Long Press Detected - Clearing SPIFFS Data...");
    
    // แสดงข้อความบนหน้าจอ OLED
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(10, 20);
    display.println("Clearing Data...");
    display.display();
    
    clearSPIFFSData();   // ลบข้อมูลจาก SPIFFS
    
    delay(2000);  // รอ 2 วินาทีให้ผู้ใช้เห็นข้อความ
    
    Serial.println("🔄 Restarting ESP32...");
    ESP.restart();  // รีสตาร์ท ESP32
}


// -------------------------------------------------------
// 6) อ่านค่าเซนเซอร์ pH/EC พร้อม Moving Average
// -------------------------------------------------------
// void readSensorValues() {
//     float sumPHRaw = 0.0f;
//     float sumECRaw = 0.0f;

    
//     for (int i = 0; i < readingCount; i++) {
//         int rawPH = analogRead(PH_SENSOR_PIN);
//         int rawEC = analogRead(EC_SENSOR_PIN);

//         sumPHRaw += rawPH;
//         sumECRaw += rawEC;

//         delay(20); // หน่วงกัน Noise ระหว่างอ่านแต่ละครั้ง
//     }

//     float avgPHRaw = sumPHRaw / readingCount;
//     float avgECRaw = sumECRaw / readingCount;

//     // ตัวอย่างคำนวณสมการ pH ตามสูตรเบื้องต้น
//     float voltPH = (avgPHRaw * 3.3f) / 4096.0f;
//     ph_act = -5.6f * voltPH + calibration_value;

//     // if (ph_act > 5)
//     // {
//     //   ph_act -= 1.65;
//     // }
//     // ตัวอย่างคำนวณ EC แบบง่าย
    
//     ec_value = avgECRaw; // ยังไม่ได้ปรับ scale
//     float AD, voltage, ecValue;
//     float temperature = 25.0;
//     for (int i = 0; i < 20; i++) {
//         sumECRaw += analogRead(EC_SENSOR_PIN);
//         delay(20);
//     }
//     AD = sumECRaw / 20;
//     voltage = (AD / 4096.0) * 5000;
//     ecValue = ec.readEC(voltage, temperature);
//     if (ecValue > 0) {
//         ec_value = ecValue;
//     }

// }


void readPHValue() {
    float sumPHRaw = 0.0f;

    // อ่านค่าจาก ADC หลายครั้งแล้วเฉลี่ยค่า
    for (int i = 0; i < readingCount; i++) {
        int rawPH = analogRead(PH_SENSOR_PIN);
        sumPHRaw += rawPH;
        delay(20);  // ลด noise ด้วย delay เล็กน้อย
    }

    float avgPHRaw = sumPHRaw / readingCount;

    // คำนวณค่าแรงดัน pH (ปรับตาม ESP32 หรือ Arduino)
    float voltPH = (avgPHRaw * 3.3f) / 4096.0f;

    // ใช้สมการพิจารณาค่า pH
    ph_act = -5.6f * voltPH + calibration_value;

}

void readECValue() {
    float sumECRaw = 0.0f;
    float AD, voltage, ecValue;
    float temperature = 25.0;  // อุณหภูมิอ้างอิง (ต้องเปลี่ยนหากมีเซ็นเซอร์อุณหภูมิ)

    // อ่านค่า ADC หลายครั้งแล้วเฉลี่ยค่า
    for (int i = 0; i < 20; i++) {
        sumECRaw += analogRead(EC_SENSOR_PIN);
        delay(20);
    }

    AD = sumECRaw / 20;
    voltage = (AD / 4096.0) * 5000;  // แปลงค่า ADC เป็นแรงดัน (ESP32)

    ecValue = ec.readEC(voltage, temperature);
    
    // ป้องกันค่า EC ติดลบ
    if (ecValue > 0) {
        ec_value = ecValue;
    }


}


// -------------------------------------------------------
// 7) แสดงค่า pH/EC
// -------------------------------------------------------
void display_pH_EC_Values() {
    // เรียกใช้ทุก 1 วินาทีตาม Timer
    // ถ้าไม่ต้องการแสดงค่าตลอดเวลา สามารถเพิ่มเงื่อนไขอื่น ๆ ได้
    if (displayOn && (pHSelected || ECSelected)) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);

        if (pHSelected) {
            display.print("pH: ");
            display.println(ph_act, 2); // ทศนิยม 2 ตำแหน่ง
        }
        else if (ECSelected) {
            display.print("EC: ");
            display.print(ec_value, 1);
            // ใส่หน่วยจริงเช่น ms/cm ถ้ามีการคำนวณ
            display.println(" (raw)");
        }

        
        display.display();
    }
}

// -------------------------------------------------------
// 8) ฟังก์ชันสไลด์หน้า Homepage + เมนู
// -------------------------------------------------------
void displayHomepage() {
    displayPage(currentPage);
}

void displayPage(int page) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);

    switch(page) {
        case 0:
            display.setCursor(12, 23);
            display.println("1. WiFi Menu");
            displayBatteryLevel();
            break;
        case 1:
            display.setCursor(12, 23);
            display.println("2. Fetch User Data");
            fetchDataMenu = true;
            displayBatteryLevel();
            break;
         case 2:
            display.setCursor(10, 23);
            display.println("3. Calibrate Sensor");
            displayBatteryLevel();
            break;
        case 3:
            display.setCursor(10, 23);
            display.println("4. Saved Data");
            displayBatteryLevel();
            break;
        default:
            break;
    }

    
    display.display();

    onHomePage = true;
}

void selectCurrentPage() {
//    if (!onHomePage) {
//         if (inWiFiMenu) {
//             WiFiConnect();
//         } else if (inFetchMenu) {  
//             startFetchingUsers();  
//         }
//         return;
//     }

    switch(currentPage) {
        case 0:
           enterWiFiMenu();
            break;
        case 1:
        if (usersFetched) {
            // ถ้าดึงข้อมูลแล้ว ให้ข้ามไปแสดงผู้ใช้ทันที
            inUserSelection = true;
            onHomePage = false;
            displayUsers();
        } else {
            fetchDataMenu = false;  
            display.clearDisplay();  
            display.setCursor(10, 23);
            display.println("Fetching Users...");
            display.display();
            fetchUsers();  
        }
        break;

        case 2:
            // เข้าสู่เมนู Calibration
            inCalibrationMenu = true;
            calibrationSelection = 0; // เริ่มที่ตัวเลือกแรก
            displayCalibrationMenu();
            break;
        case 3:
        Serial.println("Entering Saved Data Menu...");
        
            // ถ้าเคยดึงข้อมูลจาก API แล้ว
            if (usersFetched) {
                inSavedUserSelection = true;
                onHomePage = false;
                displaySavedUsersPage();
            } else {
                // ลองโหลดข้อมูลจาก SPIFFS ถ้าไม่มีการดึงข้อมูลจาก API มาก่อน
                Serial.println("Loading Saved Data from SPIFFS...");
                String storedJson = loadUserDataFromSPIFFS();
                if (storedJson.length() > 0) {
                    DynamicJsonDocument doc(8192);
                    DeserializationError error = deserializeJson(doc, storedJson);
                    if (!error && strcmp(doc["status"], "success") == 0) {
                        JsonArray userArray = doc["users"];
                        userCount = userArray.size();
                        for (int i = 0; i < userCount; i++) {
                            users[i] = userArray[i].as<String>();
                            JsonArray userFarms = doc[users[i]];
                            farmCounts[i] = userFarms.size();
                            for (int j = 0; j < farmCounts[i]; j++) {
                                farms[i][j] = userFarms[j]["farm_name"].as<String>();
                            }
                        }
                        usersFetched = true;
                        inSavedUserSelection = true;
                        onHomePage = false;
                        Serial.println("Saved Data Loaded Successfully!");
                        displaySavedUsersPage();
                    } else {
                        currentPage = 3; 
                        returnToHomepage();
                    }
                } else {
                    currentPage = 3; 
                    returnToHomepage();
                }
            }
            break;
        
        default:
            break;
    }
}


// -------------------------------------------------------
// 9) Display Battery Level
// -------------------------------------------------------
// void displayBatteryLevel() {
//     int raw = analogRead(BATTERY_PIN);
//     float voltage = (raw / 4095.0f) * 3.3f * 2.0f;
//     int batteryPercent = map((int)(voltage * 100), 300, 420, 0, 100);
//     if (batteryPercent > 100) batteryPercent = 100;
//     if (batteryPercent < 0)  batteryPercent = 0;

//     display.setCursor(0, 50);
//     display.print("Batterly: ");
//     display.print(batteryPercent);
//     display.println("%");
// }
void displayBatteryLevel() {
    int raw = analogRead(BATTERY_PIN);
    float voltage = (raw / 4095.0f) * 3.3f * 2.0f; // คำนวณแรงดันแบตเตอรี่

    // ตรวจสอบแรงดันแบตเตอรี่ในช่วง 3.0V - 4.2V และแปลงเป็นเปอร์เซ็นต์
    int batteryPercent = (int) ((voltage - 3.0f) / (4.2f - 3.0f) * 100.0f);

    // จำกัดค่าแบตเตอรี่ให้อยู่ในช่วง 0% - 100%
    batteryPercent = constrain(batteryPercent, 0, 100);

    display.setCursor(0, 50);
    display.print("Battery: ");
    display.print(batteryPercent);
    display.println("%");
}

// -------------------------------------------------------
// 10) กลับ HomePage + WiFi Menu
// -------------------------------------------------------
void returnToHomepage() {
    // Reset all necessary states to ensure the menu works correctly when re-entered
    inWiFiMenu        = false;
    inUserSelection   = false;
    inFarmSelection   = false;
    inPH_EC_Selection = false;
    inCalibrationMenu = false;
    onSaveScreen      = false;

    selectedUserIndex = 0;   // Reset the user index
    selectedFarmIndex = 0;   // Reset the farm index

    pHSelected = false;
    ECSelected = false;
    onHomePage = true;
    currentPage = 0;

    displayHomepage(); // Ensure the homepage or the desired screen is shown
}



void enterWiFiMenu() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(10, 10);
    display.println("WiFi Menu");

    display.setCursor(10, 25);
    display.print("Status: ");
    if (WiFi.status() == WL_CONNECTED) {
        display.println("Connected");
        display.print("SSID: ");
        display.println(WiFi.SSID());
    } else {
        display.println("Disconnected");
    }

    
    display.display();

    onHomePage = false;
    inWiFiMenu = true;
}

void WiFiConnect() {
    Serial.print("Heap before WiFiConnect: ");
    Serial.println(ESP.getFreeHeap());
    Serial.println("Attempting to connect to WiFi...");

    display.clearDisplay();
    display.setCursor(10, 10);
    display.println("Connecting WiFi...");
    display.display();

    WiFiManager wifiManager;
    wifiManager.setTimeout(30);  

    if (wifiManager.autoConnect("ESP32OLEDWifiKit")) {
        Serial.println("✅ WiFi Connected!");
        
        display.clearDisplay();
        display.setCursor(10, 10);
        display.println("Connected!");
        display.print("SSID: ");
        display.println(WiFi.SSID());
        display.display();

        Serial.print("Heap after WiFiConnect: ");
        Serial.println(ESP.getFreeHeap());

        // ตั้งค่าเวลา NTP
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        Serial.println("⌛ Fetching NTP Time...");
        struct tm timeinfo;
        if (!getLocalTime(&timeinfo)) {
            Serial.println("❌ Failed to obtain time!");
        } else {
            setTime(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                    timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);

            Serial.println("✅ Time updated!");
            Serial.printf("%02d:%02d:%02d %02d/%02d/%04d\n",
                          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                          timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
        }

        delay(2000);
        returnToHomepage();  // 🔹 กลับไปหน้าโฮมหลังเชื่อมต่อสำเร็จ

    } else {
        Serial.println("❌ WiFi Failed to Connect (Timeout reached)");

        display.clearDisplay();
        display.setCursor(10, 10);
        display.println("WiFi Failed!");
        display.display();

        delay(2000);
        returnToHomepage();  // 🔹 กลับไปหน้าโฮมหลังเชื่อมต่อล้มเหลว
    }
}


// void WiFiConnect() {
 
//     Serial.print("Heap before WiFiConnect: ");
//     Serial.println(ESP.getFreeHeap());
//     Serial.println("Attempting to connect to WiFi...");
//     display.clearDisplay();
//     display.setCursor(10, 10);
//     display.println("Connecting WiFi...");
//     display.display();

//     if (wifiManager.autoConnect("ESP32OLEDWifiKit")) {
//         Serial.println("WiFi Connected!");
//         display.clearDisplay();
//         display.setCursor(10, 10);
//         display.println("Connected!");
//         display.print("SSID: ");
//         display.println(WiFi.SSID());
//         Serial.print("Heap after WiFiConnect: ");
//         Serial.println(ESP.getFreeHeap());
//         configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
//         Serial.println("⌛ Fetching NTP Time...");
//         struct tm timeinfo;
//         if (!getLocalTime(&timeinfo)) {
//             Serial.println("❌ Failed to obtain time!");
//             return;
//         }

        
//         setTime(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
//                 timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);

//         Serial.println("✅ Time updated!");
//         Serial.printf("%02d:%02d:%02d %02d/%02d/%04d\n",
//           timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
//           timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
        
       

//     } else {
//         Serial.println("WiFi Failed to Connect");
//         display.clearDisplay();
//         display.setCursor(10, 10);
//         display.println("WiFi Failed!");
//     }
    
//     display.display();
//     delay(2000);
//     returnToHomepage();
//  }
void WiFiReset(){
    display.clearDisplay();
    display.setCursor(10, 10);
    display.println("Reset WiFi...");
    display.display();

    wifiManager.resetSettings();
    delay(1000);

    display.clearDisplay();
    display.setCursor(10, 10);
    display.println("WiFi Reset!");
    display.display();
    delay(2000);

    returnToHomepage();
}

// -------------------------------------------------------
// 11) toggleSystem()
 // -------------------------------------------------------
void toggleSystem() {
    systemOn = !systemOn;
    display.clearDisplay();
    display.setCursor(10, 10);
    if (systemOn) {
        display.println("System On");
        Serial.println("System On");
    } else {
        display.println("System Off");
        Serial.println("System Off");
    }
    
    display.display();
    delay(2000);
    returnToHomepage();
}

// -------------------------------------------------------
// 13) ฟังก์ชันบันทึกค่า pH / EC
// -------------------------------------------------------
// void savePHValue(float pH) {
//     // Timestamp ที่แสดงคือเวลาปัจจุบัน ไม่ได้บันทึกลง EEPROM จริง
//     String timestamp = String(year()) + "-" + String(month()) + "-" + String(day()) + " "
//                     + String(hour()) + ":" + String(minute()) + ":" + String(second());

//                     saveCalibrationData(0, pH);

//     display.clearDisplay();
//     display.setCursor(10, 10);
//     display.println("pH Saved:");
//     display.println(timestamp);
//     display.display();
//     onSaveScreen = true;
// }

// void saveECValue(float ec) {
//     String timestamp = String(year()) + "-" + String(month()) + "-" + String(day()) + " "
//                     + String(hour()) + ":" + String(minute()) + ":" + String(second());

//                     saveCalibrationData(-999.9f, ec);  // อัปเดตเฉพาะค่า EC

//     display.clearDisplay();
//     display.setCursor(10, 10);
//     display.println("EC Saved:");
//     display.println(timestamp);
//     display.display();
//     onSaveScreen = true;
// }

// -------------------------------------------------------
// 14) แสดงข้อมูลที่บันทึก
// -------------------------------------------------------
void displaySavedFarmData() {
    indeisplaysavedfarmdata = true;
    String farmId = farmIds[selectedUserIndex][selectedFarmIndex];
    int farmIndex = getFarmIndex(farmId);

    if (farmIndex == -1) {
        Serial.println("Farm not found!");
        return;
    }

    Serial.println("📂 Displaying Saved Data for Farm: " + farmId);

    if (offlineCount[farmIndex] == 0) {
        display.clearDisplay();
        display.setCursor(10, 20);
        display.println("No saved data!");
        display.display();
        delay(2000);
        displaySavedFarmsForUser();  // Go back if no data
        return;
    }

    // Ensure selectedEC_PH_Index is within the valid range
    if (selectedEC_PH_Index < 0) {
        selectedEC_PH_Index = offlineCount[farmIndex] - 1;  // Go to last entry if out of bounds
    }
    if (selectedEC_PH_Index >= offlineCount[farmIndex]) {
        selectedEC_PH_Index = 0;  // Wrap around to the first entry
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Saved Data for Farm:");

    Measurement data = offlineData[farmIndex][selectedEC_PH_Index];

    display.setCursor(10, 20);
    if (data.hasPh) {
        display.print("pH: ");
        display.println(data.phValue, 2);
    } else if (data.hasEc) {
        display.print("EC: ");
        display.println(data.ecValue, 2);
    }

    display.setCursor(10, 35);
    display.print("Time: ");
    display.println(data.timestamp);

    display.setCursor(10, 50);
    display.print("Record ");
    display.print(selectedEC_PH_Index + 1);
    display.print("/");
    display.println(offlineCount[farmIndex]);

    display.display();
}






// void editSavedData() {
//     int farmIndex = getFarmIndex(farms[selectedUserIndex][selectedFarmIndex]);

//     editingValue = true;
//     float newPH = offlineData[farmIndex][selectedEC_PH_Index].phValue;
//     float newEC = offlineData[farmIndex][selectedEC_PH_Index].ecValue;

//     while (editingValue) {
//         display.clearDisplay();
//         display.setCursor(0, 0);
//         display.println("Edit Saved Data");

//         display.setCursor(0, 20);
//         display.print("pH: ");
//         display.print(newPH, 2);
//         if (selectedEC_PH_Index == 0) display.print("  <");

//         display.setCursor(0, 35);
//         display.print("EC: ");
//         display.print(newEC, 2);
//         if (selectedEC_PH_Index == 1) display.print("  <");

//         display.display();

//         if (digitalRead(BUTTON_PIN_1) == LOW) { // เพิ่มค่า
//             if (selectedEC_PH_Index == 0) newPH += 0.1;
//             else newEC += 0.1;
//             delay(200);
//         }
//         if (digitalRead(BUTTON_PIN_3) == LOW) { // ลดค่า
//             if (selectedEC_PH_Index == 0) newPH -= 0.1;
//             else newEC -= 0.1;
//             delay(200);
//         }
//         if (digitalRead(BUTTON_PIN_2) == LOW) { // บันทึก
//             offlineData[farmIndex][selectedEC_PH_Index].phValue = newPH;
//             offlineData[farmIndex][selectedEC_PH_Index].ecValue = newEC;
//             saveOfflineData(farms[selectedUserIndex][selectedFarmIndex], newPH, newEC);
//             editingValue = false;
//             displaySavedFarmData();
//         }
//         if (digitalRead(BUTTON_PIN_4) == LOW) { // ออกจากโหมดแก้ไข
//             editingValue = false;
//             displaySavedFarmData();
//         }
//     }
// }


// -------------------------------------------------------
// 15) ฟังก์ชัน Calibration
// -------------------------------------------------------
void displayCalibrationMenu() {
     onHomePage = false;
     inCalibrationMenu = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(10, 0);
    display.println("Select Calibration:");

    const char* options[calibrationOptions] = {"pH Calibration", "EC Calibration"};

    for (int i = 0; i < calibrationOptions; i++) {
        display.setCursor(10, 20 + (i * 15));
        if (i == calibrationSelection) {
            display.print("> ");  // แสดงลูกศรหน้าตัวเลือกที่ถูกเลือก
        } else {
            display.print("  ");
        }
        display.println(options[i]);
    }

    display.display();
}


void calibratePH() {
    float newPHCalibration = loadPHCalibrationFromSPIFFS(); // โหลดค่าจาก SPIFFS
    bool calibrating = true;

    unsigned long lastDebounceTime = 0;
    const unsigned long debounceInterval = DEBOUNCE_DELAY;

    while (calibrating) {
        // เคลียร์หน้าจอทุกครั้งที่เริ่มลูป
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(WHITE);

        display.setCursor(10, 10);
        display.println("Calibrate pH");

        // แสดงค่าการปรับเทียบปัจจุบัน
        display.setCursor(10, 30);
        display.print("pH Calibration: ");
        display.print(newPHCalibration, 2);

        // แสดงบนหน้าจอ OLED
        display.display();

        // ตรวจสอบการกดปุ่ม1 เพิ่มค่า
        if (digitalRead(BUTTON_PIN_1) == LOW && millis() - lastDebounceTime > debounceInterval) {
            lastDebounceTime = millis();
            newPHCalibration += 0.1f; // เพิ่มค่า
            if (newPHCalibration > 10.0f) newPHCalibration = 10.0f; // จำกัดค่า
        }

        // ตรวจสอบการกดปุ่ม3 ลดค่า
        if (digitalRead(BUTTON_PIN_3) == LOW && millis() - lastDebounceTime > debounceInterval) {
            lastDebounceTime = millis();
            newPHCalibration -= 0.1f; // ลดค่า
            if (newPHCalibration < -10.0f) newPHCalibration = -10.0f; // จำกัดค่า
        }

        // ตรวจสอบการกดปุ่ม2 บันทึกค่า
        if (digitalRead(BUTTON_PIN_2) == LOW && millis() - lastDebounceTime > debounceInterval) {
            lastDebounceTime = millis();
            currentPHCalibration = newPHCalibration;
            saveCalibrationData(currentPHCalibration, -999.9f);
            calibrating = false;
            returnToCalibrationMenu("pH Calibration Saved!");
        }

        // ออกจากโหมด Calibration เมื่อกดปุ่ม 4
        if (digitalRead(BUTTON_PIN_4) == LOW && millis() - lastDebounceTime > debounceInterval) {
            lastDebounceTime = millis();
            Serial.println("Exiting Calibration PH");
            inCalibrationMenu = true;
            calibrating = false;
            returnToCalibrationMenu("Cancelled!");
        }

        delay(90);
    }
}


void calibrateEC() {
    float newECCalibration = loadECCalibrationFromSPIFFS(); // โหลดค่าจาก SPIFFS
    bool calibrating = true;
    
    unsigned long lastDebounceTime = 0;
    const unsigned long debounceInterval = DEBOUNCE_DELAY;

    while (calibrating) {
        // เคลียร์หน้าจอทุกครั้งที่เริ่มลูป
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(WHITE);

        display.setCursor(10, 10);
        display.println("Calibrate EC");

        display.setCursor(10, 30);
        display.print("EC Calibration: ");
        display.print(newECCalibration, 2);

        display.display();

        // ตรวจสอบการกดปุ่ม1 เพิ่มค่า
        if (digitalRead(BUTTON_PIN_1) == LOW && millis() - lastDebounceTime > debounceInterval) {
            lastDebounceTime = millis();
            newECCalibration += 0.1f; // เพิ่มค่า
            if (newECCalibration > 10.0f) newECCalibration = 10.0f; // กำหนดขอบเขต
        }

        // ตรวจสอบการกดปุ่ม3 ลดค่า
        if (digitalRead(BUTTON_PIN_3) == LOW && millis() - lastDebounceTime > debounceInterval) {
            lastDebounceTime = millis();
            newECCalibration -= 0.1f; // ลดค่า
            if (newECCalibration < -10.0f) newECCalibration = -10.0f; // กำหนดขอบเขต
        }

        // ตรวจสอบการกดปุ่ม2 บันทึกค่า
        if (digitalRead(BUTTON_PIN_2) == LOW && millis() - lastDebounceTime > debounceInterval) {
            lastDebounceTime = millis();
            currentECCalibration = newECCalibration;
            saveCalibrationData(-999.9f, currentECCalibration);
            calibrating = false;
            returnToCalibrationMenu("EC Calibration Saved!");
        }

        // ออกจากโหมด Calibration เมื่อกดปุ่ม 4
        if (digitalRead(BUTTON_PIN_4) == LOW && millis() - lastDebounceTime > debounceInterval) {
            lastDebounceTime = millis();
            Serial.println("Exiting Calibration EC");
            inCalibrationMenu = true;
            calibrating = false;
            returnToCalibrationMenu("Cancelled!");
        }

        delay(90);
    }
}



float loadPHCalibrationFromSPIFFS() {
    if (!SPIFFS.exists("/calibration_data.json")) {
        Serial.println("No calibration file found, using default pH: 0.0");
        return 0.0f; // ไม่มีไฟล์ ให้เริ่มจาก 0.0
    }

    File file = SPIFFS.open("/calibration_data.json", FILE_READ);
    if (!file) {
        Serial.println("Failed to open calibration file, using default pH: 0.0");
        return 0.0f;
    }

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.println("Failed to parse calibration data, using default pH: 0.0");
        return 0.0f;
    }

    if (doc.containsKey("pH_Calibration")) {
        return doc["pH_Calibration"].as<float>(); // โหลดค่าที่บันทึกไว้
    } else {
        Serial.println("pH_Calibration key not found, using default pH: 0.0");
        return 0.0f;
    }
}


float loadECCalibrationFromSPIFFS() {
    if (!SPIFFS.exists("/calibration_data.json")) {
        Serial.println("No calibration file found, using default EC: 0.0");
        return 0.0f; // ไม่มีไฟล์ ให้เริ่มจาก 0.0
    }

    File file = SPIFFS.open("/calibration_data.json", FILE_READ);
    if (!file) {
        Serial.println("Failed to open calibration file, using default EC: 0.0");
        return 0.0f;
    }

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.println("Failed to parse calibration data, using default EC: 0.0");
        return 0.0f;
    }

    if (doc.containsKey("EC_Calibration")) {
        return doc["EC_Calibration"].as<float>(); // โหลดค่าที่บันทึกไว้
    } else {
        Serial.println("EC_Calibration key not found, using default EC: 0.0");
        return 0.0f;
    }
}


void returnToCalibrationMenu(const char* message) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(10, 20);
    display.println(message);
    display.display();
    delay(1800);
    displayCalibrationMenu();
}

// -------------------------------------------------------
// ส่วนของการเชื่อมต่อกับ database 
// -------------------------------------------------------

void fetchUsers() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ WiFi not connected. Cannot fetch users.");
        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(12, 23);
        display.println("No WiFi");
        display.display();
        delay(2000);
        displayPage(1);  // กลับไปหน้า Fetch Data
        fetchDataMenu = true;
        return;
    }

    WiFiClientSecure client;
    HTTPClient http;
    client.setInsecure();
    client.setTimeout(15000);

    String apiURL = String(serverIP) + "fetch_user_farms.php";
    Serial.print("🌐 Fetching from: ");
    Serial.println(apiURL);

    http.begin(client, apiURL);
    http.addHeader("User-Agent", "ESP32-HTTPClient/1.0");

    int httpResponseCode = http.GET();
    Serial.print("HTTP Response Code: ");
    Serial.println(httpResponseCode);

    if (httpResponseCode == HTTP_CODE_OK) {
        String response = http.getString();
        Serial.println("📂 Raw JSON from API:");
        Serial.println(response);
    
        DynamicJsonDocument doc(8192);
        DeserializationError error = deserializeJson(doc, response);
        if (error) {
            Serial.println("❌ JSON parse error:");
            Serial.println(error.f_str());
    
            display.clearDisplay();
            display.setTextSize(1);
            display.setCursor(10, 10);
            display.println("JSON parse error");
            display.display();
            delay(2000);
            displayPage(1);
            fetchDataMenu = true;
            http.end();
            return;
        }
    
        const char* status = doc["status"];
        if (strcmp(status, "success") == 0) {
            JsonArray userArray = doc["users"];
            
            if (userArray.size() == 0) {
                Serial.println("❌ No users found in JSON.");
                return;
            }
    
            userCount = userArray.size();
            for (int i = 0; i < userCount; i++) {
                users[i] = userArray[i]["username"].as<String>(); // Store username
                JsonArray userFarms = userArray[i]["farms"];
    
                farmCounts[i] = userFarms.size();
                for (int j = 0; j < farmCounts[i]; j++) {
                    farms[i][j] = userFarms[j]["farm_name"].as<String>();  // Store farm name
                    farmIds[i][j] = userFarms[j]["farm_id"].as<String>();  // Store farm ID
                }
            }
            Serial.println("✅ Saving user data to SPIFFS...");
            saveUserDataToSPIFFS(response);
            Serial.println("✅ Users successfully fetched and saved!");
    
            // ✅ เปลี่ยนไปหน้าเลือก User ทันที
            inWiFiMenu = false;
            fetchDataMenu = false;
            usersFetched = true;
            inUserSelection = true;
            onHomePage = false;
    
            display.clearDisplay();
            display.setTextSize(1);
            display.setCursor(10, 10);
            display.println(" Data fetched!");
            display.display();
            delay(2000);
    
            displayUsers();  // แสดงหน้าเลือกผู้ใช้
        } else {
            Serial.println("❌ Fetch error: API response failed.");
            display.clearDisplay();
            display.setTextSize(1);
            display.setCursor(10, 10);
            display.println("Fetch error:");
            display.println(doc["message"].as<String>());
            display.display();
            delay(2000);
            displayPage(1);
            fetchDataMenu = true;
        }
    } else {
        // This else is correctly matched with the `if (httpResponseCode == HTTP_CODE_OK)`
        Serial.print("❌ HTTP error: ");
        Serial.println(httpResponseCode);
    
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(10, 10);
        display.println("HTTP error:");
        display.println(httpResponseCode);
        display.display();
        delay(2000);
        displayPage(1);
        fetchDataMenu = true;
    }
    http.end();
}



// void fetchUsers() {
//     // ตรวจสอบการเชื่อมต่อ WiFi ก่อน
//     if (WiFi.status() != WL_CONNECTED) {
//         display.clearDisplay();
//         display.setTextSize(1);
//         display.setCursor(10, 10);
//         display.println("No WiFi");
//         display.display();

//         delay(2000);
//         // กลับไปที่หน้าเมนูดึงข้อมูล (เช่นหน้า 1)
//         displayPage(1);
//         fetchDataMenu = true;
//         return;
//     }
    
//     WiFiClientSecure client;
//     HTTPClient http;
    
//     client.setInsecure();
//     client.setTimeout(15000);
    
//     String apiURL = String(serverIP) + "fetch_user_farms.php";
//     Serial.print("Fetching from: ");
//     Serial.println(apiURL);
//     http.begin(client, apiURL);
//     http.addHeader("User-Agent", "ESP32-HTTPClient/1.0");

//     int httpResponseCode = http.GET();
//     Serial.print("HTTP Response Code: ");
//     Serial.println(httpResponseCode);

//     if (httpResponseCode == HTTP_CODE_OK) {
//         String response = http.getString();
//         Serial.println("User and Farm Data:");
//         Serial.println(response);

//         DynamicJsonDocument doc(8192);
//         DeserializationError error = deserializeJson(doc, response);
//         if (error) {
//             display.clearDisplay();
//             display.setTextSize(1);
//             display.setCursor(10, 10);
//             display.println("JSON parse error");
//             display.display();
//             delay(2000);
//             displayPage(1);
//             fetchDataMenu = true;
//             http.end();
//             return;
//         }
        
//         const char* status = doc["status"];
//         if (strcmp(status, "success") == 0) {
//             // ดึงข้อมูลและเก็บในตัวแปร global
//             JsonArray userArray = doc["users"];
//             userCount = userArray.size();

//             for (int i = 0; i < userCount; i++) {
//                 users[i] = userArray[i].as<String>();
//                 JsonArray userFarms = doc[users[i]];
//                 farmCounts[i] = userFarms.size();
//                 for (int j = 0; j < farmCounts[i]; j++) {
//                     farms[i][j] = userFarms[j]["farm_name"].as<String>();
//                 }
//             }
//             // ตั้งค่า state ให้ถูกต้องหลังจากดึงข้อมูลสำเร็จ
//             inWiFiMenu      = false;
//             fetchDataMenu   = false;   // ปิดโหมด Fetch Data
//             usersFetched    = true;    // ข้อมูลถูกดึงแล้ว
//             inUserSelection = true;    // เปลี่ยนไปโหมดเลือกผู้ใช้
//             onHomePage      = false;

//             // บันทึก JSON string ลง EEPROM เพื่อให้ข้อมูลยังคงอยู่ในเครื่อง
//             saveUserDataToSPIFFS(response);

//             // แสดงข้อความแจ้งว่าดึงข้อมูลสำเร็จ
//             display.clearDisplay();
//             display.setTextSize(1);
//             display.setCursor(10, 10);
//             display.println("Data fetched!");
//             display.display();
//             delay(2000);
            
//             displayUsers();  // แสดงหน้า Select User
//         } else {
//             // กรณี API ส่งสถานะไม่ใช่ "success"
//             display.clearDisplay();
//             display.setTextSize(1);
//             display.setCursor(10, 10);
//             display.println("Fetch error:");
//             display.println(doc["message"].as<String>());
//             display.display();
//             delay(2000);
//             displayPage(1);
//             fetchDataMenu = true;
//         }
//     } else {
//         // กรณี HTTP response ไม่ใช่ 200
//         display.clearDisplay();
//         display.setTextSize(1);
//         display.setCursor(10, 10);
//         display.println("HTTP error:");
//         display.println(httpResponseCode);
//         display.display();
//         delay(2000);
//         displayPage(1);
//         fetchDataMenu = true;
//     }
//     http.end();
// }

void displayFarmsForUser(int userIndex) {
    Serial.println("Displaying Farms for User: ");
    Serial.println(users[userIndex]);

    // เช็คว่าผู้ใช้มีฟาร์มหรือไม่
    if (farmCounts[userIndex] == 0) {
        Serial.println("No farm for user!");
        
        // แสดงข้อความบนหน้าจอ OLED
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(10, 20);
        display.println("No farm for user!");
        display.display();
        
        delay(2000);  // รอ 2 วินาทีเพื่อให้ผู้ใช้เห็นข้อความ
        // กลับไปหน้าเลือกผู้ใช้
        displayUsers();
        inUserSelection = true;
        return;
    }

    // ถ้ามีฟาร์ม แสดงรายการฟาร์มตามปกติ
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Farms for: " + users[userIndex]);

    int startIdx = farmPage * 5;
    int endIdx = min(startIdx + 5, farmCounts[userIndex]);

    if (selectedFarmIndex < startIdx || selectedFarmIndex >= endIdx) {
        selectedFarmIndex = startIdx;  // Reset to the first farm on the current page
    }

    // Loop แสดงรายการฟาร์ม
    for (int i = startIdx; i < endIdx; i++) {
        if (i == selectedFarmIndex) {
            display.print("> ");  // Highlight the selected farm
        } else {
            display.print("  ");  // Indent non-selected farms
        }
        display.println(farms[userIndex][i]);  // Display farm name only
    }

    // แสดงหน้าเพจถ้ามีมากกว่า 5 ฟาร์ม
    if (farmCounts[userIndex] > 5) {
        display.setCursor(0, 55);
        display.print("Page ");
        display.print(farmPage + 1);  // Display current page (1-based)
        display.print("/");
        display.print((farmCounts[userIndex] + 4) / 5);  // Total pages calculation
    }

    display.display();  // อัปเดตหน้าจอ
}



void displayUsers() {
    Serial.println("Displaying Users...");

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Select User:");

    int startIdx = userPage * 5;
    int endIdx = min(startIdx + 5, userCount);

    // Ensure the selected user index is within the current page's range
    if (selectedUserIndex < startIdx || selectedUserIndex >= endIdx) {
        selectedUserIndex = startIdx;  // Reset to the first user on the current page
    }

    // Loop through the users on the current page and display them
    for (int i = startIdx; i < endIdx; i++) {
        if (i == selectedUserIndex) {
            display.print("> ");  // Show ">" to highlight the selected user
        } else {
            display.print("  ");  // Indent non-selected users
        }
        display.println(users[i]);  // Display the username
    }

    // Show page navigation only if there are more than 5 users
    if (userCount > 5) {
        display.setCursor(0, 55);
        display.print("Page ");
        display.print(userPage + 1);  // Display current page (1-based)
        display.print("/");
        display.print((userCount + 4) / 5);  // Total pages calculation
    }

    display.display();  // Update the display
}








void startFetchingUsers() {
    // ถ้าข้อมูลถูกดึงมาแล้ว (usersFetched == true)
    // ให้แสดงหน้า Select User โดยตรง ไม่ต้องดึงข้อมูลใหม่
    if (usersFetched) {
        inUserSelection = true;
        onHomePage = false;
        displayUsers();
        return;
    }
    
    // ถ้ายังไม่ได้ดึงข้อมูล ให้แสดงข้อความและดึงข้อมูล
    display.clearDisplay();
    display.setCursor(10, 10);
    display.println("Fetching Users...");
    display.display();

    fetchUsers();  // เมื่อ fetch สำเร็จ ฟังก์ชัน fetchUsers() จะเปลี่ยน state และเรียก displayUsers()
}



// -------------------------------------------------------
// Ec ph ในหน้าฟาร์ม 
// -------------------------------------------------------
void displayPH_EC_Options() {
    onHomePage = false;
    inPH_EC_Selection = true;

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Select Measurement:");

    if (ph_ec_selection == 0) {
        display.print("> pH");
    } else {
        display.print("  pH");
    }
    display.println();

    if (ph_ec_selection == 1) {
        display.print("> EC");
    } else {
        display.print("  EC");
    }
    display.println();

    display.display();
}

void displayRealTimePH() {
    float calibrationOffset = loadPHCalibrationFromSPIFFS();
    
    while (inPH_EC_Selection) {
        readPHValue();  // อ่านค่าจากเซนเซอร์

        float calibratedPH = ph_act + calibrationOffset;  // ปรับด้วยค่าการ Calibrate

        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(10, 20);
        display.print("pH: ");
        display.println(calibratedPH, 2);
        display.display();

        // เมื่อกดปุ่ม 2 -> บันทึกข้อมูล pH
        if (digitalRead(BUTTON_PIN_2) == LOW) {
            delay(DEBOUNCE_DELAY);
            
            // แสดงข้อความ "Saving Data..."
            display.clearDisplay();
            display.setTextSize(2);
            display.setCursor(10, 20);
            display.println("Saving...");
            display.display();

            savePH(farmIds[selectedUserIndex][selectedFarmIndex], calibratedPH);

            // แสดง "Saved!" บนหน้าจอ 2 วินาที
            display.clearDisplay();
            display.setCursor(10, 20);
            display.println("Saved!");
            display.display();
            delay(2000);

            displayPH_EC_Options();
            break;
        } 
        
        // เมื่อกดปุ่ม 4 -> ออกจากเมนู
        else if (digitalRead(BUTTON_PIN_4) == LOW) {
            delay(DEBOUNCE_DELAY);
            displayPH_EC_Options();
            break;
        }

        delay(100);
    }
}


void displayRealTimeEC() {
    float calibrationOffset = loadECCalibrationFromSPIFFS(); 
    while (inPH_EC_Selection) {
        readECValue();  // อ่านค่าจากเซนเซอร์

        float calibratedEC = ec_value + calibrationOffset;  // ปรับด้วยค่าการ Calibrate

        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(10, 20);
        display.print("EC: ");
        display.println(calibratedEC, 2);

        display.display();

        if (digitalRead(BUTTON_PIN_2) == LOW) {
            delay(DEBOUNCE_DELAY);
            
            // แสดงข้อความ "Saving Data..."
            display.clearDisplay();
            display.setTextSize(2);
            display.setCursor(10, 20);
            display.println("Saving...");
            display.display();

            saveEC(farmIds[selectedUserIndex][selectedFarmIndex], calibratedEC);

            // แสดง "Saved!" บนหน้าจอ 2 วินาที
            display.clearDisplay();
            display.setCursor(10, 20);
            display.println("Saved!");
            display.display();
            delay(2000);

            displayPH_EC_Options();
            break;
        } else if (digitalRead(BUTTON_PIN_4) == LOW) {
            delay(DEBOUNCE_DELAY);
            displayPH_EC_Options();
            break;
        }
        delay(100);
    }
}

// Function to save pH value
void savePH(String farmId, float calibratedPH) {
    // float calibrationOffset = loadPHCalibrationFromSPIFFS(); // โหลดค่าการสอบเทียบจาก SPIFFS
    // float calibratedPH = measuredPH + calibrationOffset; // คำนวณค่า pH ที่ถูกต้อง
    String source = "Esp32";
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String apiURL = serverIP + String("save_ph.php");
        String postData = "farm_id=" + farmId + 
                          "&ph=" + String(calibratedPH, 2) + 
                          "&timestamp=" + getCurrentTimestamp() + 
                          "&source=" + source;  // เพิ่ม source เข้าไป

        Serial.println("🌍 Trying to send data to server...");
        int attempts = 0;
        bool success = false;

        while (attempts < 3 && !success) {  // ลองส่งข้อมูล 3 ครั้ง
            Serial.print("🔄 Attempt ");
            Serial.println(attempts + 1);
            
            int httpResponseCode = sendDataToServer(postData, apiURL);

            if (httpResponseCode > 0) {
                Serial.println("✅ pH Saved to DB: " + String(calibratedPH, 2));
                Serial.println(http.getString());
                success = true;
            } else {
                Serial.println("❌ Error saving pH data (Attempt " + String(attempts + 1) + "): " + String(httpResponseCode));
                delay(2000); // รอ 2 วินาทีแล้วลองใหม่
                attempts++;
            }
        }

        // ถ้าส่งไม่สำเร็จ 3 ครั้ง → บันทึกออฟไลน์
        if (!success) {
            Serial.println("❌ Server Unreachable. Saving Offline...");
            saveOfflineData(farmId, calibratedPH, -1.0f,source);
        }

    } else {
        Serial.println("❌ No WiFi. Saving Offline pH...");
        saveOfflineData(farmId, calibratedPH, -1.0f,source); // บันทึกค่า pH ไว้ส่งภายหลัง
    }
}

// void savePH(String farmId, float calibratedPH) {
//     // float calibratedPH = phValue + currentPHCalibration;
//     if (WiFi.status() == WL_CONNECTED) {
//         HTTPClient http; 

//         String apiURL = serverIP + String("save_ph.php");
//         String postData = "farm_id=" + farmId + "&ph=" + String(calibratedPH, 2)+"&timestamp=" + getCurrentTimestamp();
//         Serial.print(postData);
//         int httpResponseCode = sendDataToServer(postData, apiURL);
//         Serial.print(httpResponseCode); // Use retry logic
//         if (httpResponseCode > 0) {
//             Serial.println("pH Saved to DB:");
//             Serial.print(calibratedPH);
//             Serial.println(http.getString());
//         } else {
//             Serial.print("Error saving pH data: ");
//             Serial.println(httpResponseCode);
//         }
//     } else {
//         saveOfflineData(farmId, calibratedPH, -1.0f);  // Save pH offline
//     }
// }

// Function to save EC value
void saveEC(String farmId, float calibratedEC) {
    String source = "Esp32";

    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String apiURL = serverIP + String("save_ec.php");
        String postData = "farm_id=" + farmId + 
                          "&ec=" + String(calibratedEC, 2) + 
                          "&timestamp=" + getCurrentTimestamp() + 
                          "&source=" + source;

        Serial.println("🌍 Trying to send EC data to server...");
        int attempts = 0;
        bool success = false;

        while (attempts < 3 && !success) {
            Serial.print("🔄 Attempt ");
            Serial.println(attempts + 1);
            
            
            int httpResponseCode = sendDataToServer(postData, apiURL);

            if (httpResponseCode > 0) {
                Serial.println("✅ EC Saved to DB: " + String(calibratedEC, 2));
                Serial.println(http.getString());
                success = true;
            } else {
                Serial.println("❌ Error saving EC data (Attempt " + String(attempts + 1) + "): " + String(httpResponseCode));
                delay(2000);
                attempts++;
            }
            http.end();
        }

        // ถ้าส่งไม่สำเร็จ 3 ครั้ง → บันทึกออฟไลน์
        if (!success) {
            Serial.println("❌ Server Unreachable. Saving Offline EC...");
            saveOfflineData(farmId, -1.0f, calibratedEC, source);  // ✅ บันทึก EC ถูกต้อง
        }

    } else {
        Serial.println("❌ No WiFi. Saving Offline EC...");
        saveOfflineData(farmId, -1.0f, calibratedEC, source);  // ✅ บันทึก EC ถูกต้อง
    }
}

// void saveEC(String farmId, float calibratedEC) {
//     // float calibrationOffset = loadPHCalibrationFromSPIFFS(); // โหลดค่าการสอบเทียบจาก SPIFFS
//     // float calibratedPH = measuredPH + calibrationOffset; // คำนวณค่า pH ที่ถูกต้อง
//     String source = "Esp32";
//     if (WiFi.status() == WL_CONNECTED) {
//         HTTPClient http;
//         String apiURL = serverIP + String("save_ec.php");
//         String postData = "farm_id=" + farmId + 
//                           "&ec=" + String(calibratedEC, 2) + 
//                           "&timestamp=" + getCurrentTimestamp() + 
//                           "&source=" + source;  // เพิ่ม source เข้าไป

//         Serial.println("🌍 Trying to send data to server...");
//         int attempts = 0;
//         bool success = false;

//         while (attempts < 3 && !success) {  // ลองส่งข้อมูล 3 ครั้ง
//             Serial.print("🔄 Attempt ");
//             Serial.println(attempts + 1);
            
//             int httpResponseCode = sendDataToServer(postData, apiURL);

//             if (httpResponseCode > 0) {
//                 Serial.println("✅ EC Saved to DB: " + String(calibratedEC, 2));
//                 Serial.println(http.getString());
//                 success = true;
//             } else {
//                 Serial.println("❌ Error saving pH data (Attempt " + String(attempts + 1) + "): " + String(httpResponseCode));
//                 delay(2000); // รอ 2 วินาทีแล้วลองใหม่
//                 attempts++;
//             }
//         }

//         // ถ้าส่งไม่สำเร็จ 3 ครั้ง → บันทึกออฟไลน์
//         if (!success) {
//             Serial.println("❌ Server Unreachable. Saving Offline...");
//             saveOfflineData(farmId, -1.0f, calibratedEC, source);
//         }

//     } else {
//         Serial.println("❌ No WiFi. Saving Offline...");
//         saveOfflineData(farmId, -1.0f, calibratedEC, source); // บันทึกค่า pH ไว้ส่งภายหลัง
//     }
// }
// void saveEC(String farmId, float calibratedEC) {
//     // float calibratedEC = ecValue + currentECCalibration;

//     if (WiFi.status() == WL_CONNECTED) {
//         HTTPClient http; 
        
//         String apiURL = serverIP + String("save_ec.php");
//         String postData = "farm_id=" + farmId + "&ec=" + String(calibratedEC, 2)+"&timestamp=" + getCurrentTimestamp();
//         Serial.print(postData);
//         int httpResponseCode = sendDataToServer(postData, apiURL); // Use retry logic
//         if (httpResponseCode > 0) {
//             Serial.println("EC Saved to DB:");
//             Serial.print(calibratedEC);
//             Serial.println(http.getString());
//         } else {
//             Serial.print("Error saving EC data: ");
//             Serial.println(httpResponseCode);
//         }
//     } else {
//         Serial. println("save without net");
//         saveOfflineData(farmId, -1.0f, calibratedEC);  // Save EC offline
//     }
// }

int sendDataToServer(String postData, String apiURL) {
    HTTPClient http;
    http.begin(apiURL);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    // display.clearDisplay();
    // display.setCursor(10, 10);
    // display.println("Send Date...");
    // display.display();
    int httpResponseCode = http.POST(postData);
    
    // Retry logic if sending fails
    int retryCount = 3;
    while (httpResponseCode <= 0 && retryCount > 0) {
        Serial.print("Failed to send data. Retrying... ");
        delay(2000); // wait for 2 seconds before retrying
        httpResponseCode = http.POST(postData);
        retryCount--;
    }

    http.end();
    return httpResponseCode;
}

// ฟังก์ชันบันทึกข้อมูลแบบออฟไลน์

void saveOfflineData(String farmId, float phValue, float ecValue, String source) {
    int farmIndex = getFarmIndex(farmId);

    if (offlineCount[farmIndex] < 10) {  // จำกัดบันทึกออฟไลน์สูงสุด 3 รายการต่อฟาร์ม
        offlineData[farmIndex][offlineCount[farmIndex]].farmId = farmId;
        offlineData[farmIndex][offlineCount[farmIndex]].timestamp = getCurrentTimestamp();
        offlineData[farmIndex][offlineCount[farmIndex]].source = source;

        if (phValue != -1.0f) {  // ตรวจสอบว่ามีค่า pH ถูกส่งมา
            Serial.println("✅ Saving pH offline...");
            offlineData[farmIndex][offlineCount[farmIndex]].phValue = phValue;
            offlineData[farmIndex][offlineCount[farmIndex]].hasPh = true;
        } else {
            offlineData[farmIndex][offlineCount[farmIndex]].hasPh = false;
        }

        if (ecValue != -1.0f) {  // ตรวจสอบว่ามีค่า EC ถูกส่งมา
            Serial.println("✅ Saving EC offline...");
            offlineData[farmIndex][offlineCount[farmIndex]].ecValue = ecValue;
            offlineData[farmIndex][offlineCount[farmIndex]].hasEc = true;
        } else {
            offlineData[farmIndex][offlineCount[farmIndex]].hasEc = false;
        }

        offlineCount[farmIndex]++;

        Serial.println("✅ Saved offline successfully!");

        // ✅ ตรวจสอบว่า JSON ที่บันทึกมีค่า EC หรือไม่
        DynamicJsonDocument doc(8192);
        JsonArray offlineArray = doc.createNestedArray("offlineData");

        for (int i = 0; i < offlineCount[farmIndex]; i++) {
            JsonObject data = offlineArray.createNestedObject();
            data["farmId"] = offlineData[farmIndex][i].farmId;
            data["timestamp"] = offlineData[farmIndex][i].timestamp;
            data["source"] = offlineData[farmIndex][i].source;

            if (offlineData[farmIndex][i].hasPh) {
                data["ph"] = offlineData[farmIndex][i].phValue;
                data["statusPh"] = offlineData[farmIndex][i].hasPh;
            }

            if (offlineData[farmIndex][i].hasEc) {
                data["ec"] = offlineData[farmIndex][i].ecValue;
                data["statusEc"] = offlineData[farmIndex][i].hasEc;
            }
        }

        String jsonString;
        serializeJson(doc, jsonString);
        Serial.println("📂 Saving JSON to SPIFFS...");
        Serial.println(jsonString);  // ✅ Debug JSON ก่อนบันทึกลง SPIFFS
        saveUserDataToSPIFFS(jsonString);

    } else {
        Serial.println("❌ Offline storage full for this farm!");
    }
}


// void saveOfflineData(String farmId, float phValue, float ecValue, String source) {
//     int farmIndex = getFarmIndex(farmId);

//     if (offlineCount[farmIndex] < 3) {  // Limit to 3 entries per farm
//         // บันทึกค่า farmId
//         offlineData[farmIndex][offlineCount[farmIndex]].farmId = farmId;
        
//         if (phValue != -1.0f) {
//             offlineData[farmIndex][offlineCount[farmIndex]].phValue = phValue;
//             offlineData[farmIndex][offlineCount[farmIndex]].hasPh = true;  // ตั้งค่าให้รู้ว่ามี pH
//         }
//         if (ecValue != -1.0f) {
//             offlineData[farmIndex][offlineCount[farmIndex]].ecValue = ecValue;
//             offlineData[farmIndex][offlineCount[farmIndex]].hasEc = true;  // ตั้งค่าให้รู้ว่ามี EC
//         }
//         offlineData[farmIndex][offlineCount[farmIndex]].timestamp = getCurrentTimestamp();
//         offlineData[farmIndex][offlineCount[farmIndex]].source = source;  // บันทึกค่า source

//         offlineCount[farmIndex]++;
//         Serial.println("✅ Saved offline:");
//         Serial.print("Farm: "); Serial.println(farmId);
//         Serial.print("Time: "); Serial.println(getCurrentTimestamp());
//         Serial.print("Source: "); Serial.println(source);

//         // บันทึกลง SPIFFS เป็น JSON
//         DynamicJsonDocument doc(8192);  // เพิ่มขนาดหากต้องการ
//         JsonArray offlineArray = doc.createNestedArray("offlineData");

//         for (int i = 0; i < offlineCount[farmIndex]; i++) {
//             JsonObject data = offlineArray.createNestedObject();
//             data["farmId"] = offlineData[farmIndex][i].farmId;
//             if (offlineData[farmIndex][i].hasPh) {
//                 data["ph"] = offlineData[farmIndex][i].phValue;
//                 data["statusPh"] = offlineData[farmIndex][i].hasPh;
//             }
//             if (offlineData[farmIndex][i].hasEc) {
//                 data["ec"] = offlineData[farmIndex][i].ecValue;
//                 data["statusEc"] = offlineData[farmIndex][i].hasEc;
//             }
//             data["timestamp"] = offlineData[farmIndex][i].timestamp;
//             data["source"] = offlineData[farmIndex][i].source;  // เพิ่มค่า source ลงใน JSON
//         }

//         String jsonString;
//         serializeJson(doc, jsonString);
//         saveUserDataToSPIFFS(jsonString); // บันทึกลง SPIFFS
//     } else {
//         Serial.println("❌ Offline storage full for this farm!");
//     }
// }

// Modify saveOfflineData to handle both pH and EC
// void saveOfflineData(String farmId, float phValue, float ecValue) {
//     int farmIndex = getFarmIndex(farmId);

//     if (offlineCount[farmIndex] < 10) {  // Limit to 3 entries per farm
//         // Save data with -1.0f for missing values (pH or EC)
//         offlineData[farmIndex][offlineCount[farmIndex]].farmId = farmId;
//         if (phValue != -1.0f) {
//             offlineData[farmIndex][offlineCount[farmIndex]].phValue = phValue;
//             offlineData[farmIndex][offlineCount[farmIndex]].hasPh = true;  // Set the flag
//         }
//         if (ecValue != -1.0f) {
//             offlineData[farmIndex][offlineCount[farmIndex]].ecValue = ecValue;
//             offlineData[farmIndex][offlineCount[farmIndex]].hasEc = true;  // Set the flag
//         }
//         offlineData[farmIndex][offlineCount[farmIndex]].timestamp = getCurrentTimestamp();

//         offlineCount[farmIndex]++;
//         Serial.println("Saved offline:");
//         Serial.print("Farm: "); Serial.println(farmId);
//         Serial.print("Time: "); Serial.println(getCurrentTimestamp());

//         // Save to SPIFFS as JSON
//         DynamicJsonDocument doc(8192);  // Try increasing size if needed
//         JsonArray offlineArray = doc.createNestedArray("offlineData");

//         for (int i = 0; i < offlineCount[farmIndex]; i++) {
//             JsonObject data = offlineArray.createNestedObject();
//             data["farmId"] = offlineData[farmIndex][i].farmId;
//             if (offlineData[farmIndex][i].hasPh) {
//                 data["ph"] = offlineData[farmIndex][i].phValue;
//                 data["statusPh"] = offlineData[farmIndex][i].hasPh;
//             }
//             if (offlineData[farmIndex][i].hasEc) {
//                 data["ec"] = offlineData[farmIndex][i].ecValue;
//                 data["statusEc"] = offlineData[farmIndex][i].hasEc;
//             }
//             data["timestamp"] = offlineData[farmIndex][i].timestamp;
//         }

//         String jsonString;
//         serializeJson(doc, jsonString);
//         saveUserDataToSPIFFS(jsonString); // Save to SPIFFS
//     } else {
//         Serial.println("Offline storage full for this farm!");
//     }
// }


// ฟังก์ชันซิงค์ข้อมูลออฟไลน์เมื่อเชื่อมต่อ WiFi
void syncOfflineData() {
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Syncing offline data...");
        display.println("Syncing offline data...");
        for (int i = 0; i < 10; i++) {
            for (int j = 0; j < offlineCount[i]; j++) {
                uploadDataToServer(offlineData[i][j]);  
            }
            offlineCount[i] = 0;  // เคลียร์ข้อมูลหลังซิงค์
        }
        Serial.println("Offline data synced successfully!");
         fetchUsers();
    } else {
        Serial.println("WiFi not connected. Cannot sync offline data.");
    }
}

void uploadDataToServer(Measurement data) {
    HTTPClient http;
    // Create a string to hold the POST data
    String postData = "farm_id=" + data.farmId;

    // Check if 'ph' value is available
    if (data.hasPh) {
        String apiURL = serverIP + String("save_ph.php");
        http.begin(apiURL);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        postData += "&ph=" + String(data.phValue, 6);  // Use 6 decimal places for better precision
    } 
    // If 'ph' is not available, check 'EC' value
    else if (data.hasEc) {
        String apiURL = serverIP + String("save_ec.php");
        http.begin(apiURL);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        postData += "&ec=" + String(data.ecValue, 6);
    }

    // Add timestamp to post data
    postData += "&timestamp=" + data.timestamp;
    postData += "&source=Esp32";
    // Check if there's any valid data to send
    if (postData.length() > 0) {
        // Send the data to the server
        int httpResponseCode = http.POST(postData);

        // Check if the POST request was successful
        if (httpResponseCode > 0) {
            Serial.println("Data synced to DB:");
            Serial.println(http.getString());
        } else {
            Serial.print("Error syncing data: ");
            Serial.println(httpResponseCode);
        }
    } else {
        Serial.println("No valid data to send.");
    }

    // Close the HTTP connection
    http.end();
}



// หาตำแหน่งของฟาร์มในอาร์เรย์ (ใช้สำหรับบันทึกออฟไลน์)
int getFarmIndex(String farmId) {
    for (int i = 0; i < farmCounts[selectedUserIndex]; i++) {
        if (farmIds[selectedUserIndex][i] == farmId) {
            return i; // Return the index of the farm
        }
    }
    return -1; // Return -1 if the farm is not found
}


void loadSavedDataFromSPIFFS() {
    // Load the user data stored in SPIFFS
    String storedJson = loadUserDataFromSPIFFS();
    
    // If no data is found, print error message and return
    if (storedJson.length() == 0) {
        Serial.println("❌ No valid user data in SPIFFS.");
        return;
    }

    // Parse the JSON string
    DynamicJsonDocument doc(8192);  // Adjust size if necessary
    DeserializationError error = deserializeJson(doc, storedJson);

    // If JSON parsing fails, print the error and return
    if (error) {
        Serial.println("❌ Failed to parse JSON from SPIFFS!");
        Serial.println(error.f_str());  // Print the error message
        return;
    }

    // Access the "users" array in the JSON document
    JsonArray userArray = doc["users"];
    
    // Check if the "users" array is valid
    if (userArray.isNull()) {
        Serial.println("❌ users array is NULL. Possible empty or incorrect JSON format.");
        return;
    }

    // Get the total number of users
    userCount = userArray.size();
    if (userCount == 0) {
        Serial.println("❌ No users found in the JSON data.");
        return;
    }

    // Loop through each user and process their data
    for (int i = 0; i < userCount; i++) {
        users[i] = userArray[i]["username"].as<String>();  // Store the username
    
        // Access the farms for the current user
        JsonArray userFarms = userArray[i]["farms"];  // Only define once
    
        // Check if farms array exists for the current user
        if (userFarms.isNull()) {
            Serial.print("⚠️ No farms found for user: ");
            Serial.println(users[i]);
            continue;  // Skip to the next user if no farms are found
        }
    
        // Store the number of farms for this user
        farmCounts[i] = userFarms.size();
    
        // Loop through the farms and store farm names
        for (int j = 0; j < farmCounts[i]; j++) {
            farms[i][j] = userFarms[j]["farm_name"].as<String>(); 
            farmIds[i][j] = userFarms[j]["farm_id"].as<String>();  // Store farm name
            // Serial.print("Farm Name for ");
            // Serial.print(users[i]);
            // Serial.print(": ");
            // Serial.println(farms[i][j]);  // Debug output to ensure farm name is correct
        }
    }
    
    // Set the flag indicating data has been successfully fetched
    usersFetched = true;
    
    // Print success message
    Serial.println("✅ Users successfully loaded from SPIFFS!");
}




void saveCalibrationData(float phCalibration, float ecCalibration) {
    DynamicJsonDocument doc(512);

    // โหลดข้อมูล Calibration เดิม (ถ้ามี)
    if (SPIFFS.exists("/calibration_data.json")) {
        File file = SPIFFS.open("/calibration_data.json", FILE_READ);
        if (file) {
            DeserializationError error = deserializeJson(doc, file);
            if (error) {
                Serial.println("Failed to parse existing calibration data. Overwriting...");
            }
            file.close();
        }
    }

    // อัปเดตเฉพาะค่าที่ต้องการ
    if (phCalibration != -999.9f) {
        doc["pH_Calibration"] = phCalibration;
    }
    if (ecCalibration != -999.9f) {
        doc["EC_Calibration"] = ecCalibration;
    }

    // บันทึกลง SPIFFS
    File file = SPIFFS.open("/calibration_data.json", FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open calibration file for writing!");
        return;
    }

    if (serializeJson(doc, file) == 0) {
        Serial.println("Failed to write calibration data to file!");
    } else {
        Serial.println("Calibration data saved successfully.");
    }

    file.close();
}

void saveUserDataToSPIFFS(String jsonString) {
    DynamicJsonDocument doc(8192);
    Serial.println("📂 Saving JSON to SPIFFS...");
    Serial.println(jsonString);
    // โหลดข้อมูลเดิมจาก SPIFFS ถ้ามี
    if (SPIFFS.exists("/user_data.json")) {
        File file = SPIFFS.open("/user_data.json", FILE_READ);
        if (file) {
            DeserializationError error = deserializeJson(doc, file);
            if (error) {
                Serial.println("Failed to parse existing user data.");
            }
            file.close();
        }
    }

    // บันทึกข้อมูลใหม่
    File file = SPIFFS.open("/user_data.json", FILE_WRITE);
    if (!file) {
        Serial.println("❌ Failed to open user_data.json for writing!");
        return;
    }

    int bytesWritten = file.print(jsonString);
    file.close();

    if (bytesWritten == 0) {
        Serial.println("❌ Failed to write data to SPIFFS.");
    } else {
        Serial.println("✅ Successfully saved user data to SPIFFS.");
    }
}

void displaySavedUsersPage() {
    Serial.println("📂 Displaying Saved Users...");
    inSavedUserSelection = true;
    inSavedFarmSelection = false;
    inSavedFarmData = false; 
    onHomePage = false;

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Select Saved User:");


    if (userCount == 0) {
        display.clearDisplay();
        display.setCursor(10, 10);
        display.println("no save data !");
        display.display();

        delay(1000); 
        currentPage = 3;  
        displayHomepage(); 
        inSavedUserSelection = false;

    }

    int startIdx = userPage * 5;
    int endIdx = min(startIdx + 5, userCount);

    // Ensure the selected user index is within the current page's range
    if (selectedUserIndex < startIdx || selectedUserIndex >= endIdx) {
        selectedUserIndex = startIdx;  // Reset to the first user on the current page
    }

    // Loop through the users on the current page and display them
    for (int i = startIdx; i < endIdx; i++) {
        if (i == selectedUserIndex) {
            display.print("> ");  // Show ">" to highlight the selected user
        } else {
            display.print("  ");  // Indent non-selected users
        }
        display.println(users[i]);  // Display the username
    }

    // Show page navigation only if there are more than 5 users
    if (userCount > 5) {
        display.setCursor(0, 55);
        display.print("Page ");
        display.print(userPage + 1);  // Display current page (1-based)
        display.print("/");
        display.print((userCount + 4) / 5);  // Total pages calculation
    }

    display.display();
}



void deleteFileFromSPIFFS(const char* path) {
    if (SPIFFS.exists(path)) {
        if (SPIFFS.remove(path)) {
            Serial.print("✅ Deleted file: ");
            Serial.println(path);
        } else {
            Serial.print("❌ Failed to delete file: ");
            Serial.println(path);
        }
    } else {
        Serial.print("❌ File not found: ");
        Serial.println(path);
    }
}

void clearSPIFFSData() {
    Serial.println("⚠️ Deleting all stored data in SPIFFS...");

    if (!SPIFFS.begin(true)) {
        Serial.println("❌ SPIFFS Initialization Failed!");
        return;
    }

    deleteFileFromSPIFFS("/user_data.json");          // ลบข้อมูลผู้ใช้และฟาร์ม
    deleteFileFromSPIFFS("/calibration_data.json");   // ลบค่าการ Calibration (pH/EC)
    deleteFileFromSPIFFS("/offline_data.json");       // ลบข้อมูลที่บันทึกออฟไลน์

    Serial.println("✅ SPIFFS cleanup complete.");
}

// void clearSPIFFSData() {
//     Serial.println("⚠️ Deleting all stored data in SPIFFS...");

//     deleteFileFromSPIFFS("/user_data.json");          // ลบข้อมูลผู้ใช้และฟาร์ม
//     deleteFileFromSPIFFS("/calibration_data.json");   // ลบค่าการ Calibration (pH/EC)

//     Serial.println("✅ SPIFFS cleanup complete.");
// }


void displaySavedFarmsForUser() {
    Serial.println("📂 Displaying Saved Farms for selected user...");
    inSavedUserSelection = false;  // ออกจากโหมดเลือกผู้ใช้
    inSavedFarmSelection = true;   // เข้าโหมดเลือกฟาร์ม

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Select Saved Farm:");

    if (farmCounts[selectedUserIndex] == 0) {
        display.setCursor(10, 20);
        display.println("No saved farms!");
        display.display();
        delay(2000);
        displaySavedUsersPage();  // กลับไปหน้าเลือกผู้ใช้
        return;
    }

    // ✅ การแบ่งหน้า (pagination)
    int startIdx = farmPage * 5;  // คำนวณ index เริ่มต้นของหน้า
    int endIdx = min(startIdx + 5, farmCounts[selectedUserIndex]);

    // รีเซ็ต index ถ้าอยู่นอกขอบเขต
    if (selectedFarmIndex < startIdx || selectedFarmIndex >= endIdx) {
        selectedFarmIndex = startIdx;
    }

    // ✅ แสดงรายการฟาร์ม (แบบแบ่งหน้า)
    for (int i = startIdx; i < endIdx; i++) {
        if (i == selectedFarmIndex) {
            display.print("> ");  // ไฮไลต์ตัวเลือกที่เลือกอยู่
        } else {
            display.print("  ");
        }
        display.println(farms[selectedUserIndex][i]);  // แสดงชื่อฟาร์ม
    }

    // ✅ แสดงหมายเลขหน้าปัจจุบัน / จำนวนหน้าทั้งหมด (ถ้ามีมากกว่า 5 ฟาร์ม)
    if (farmCounts[selectedUserIndex] > 5) {
        display.setCursor(0, 55);
        display.print("Page ");
        display.print(farmPage + 1);  // แสดงหมายเลขหน้า (เริ่มจาก 1)
        display.print("/");
        display.print((farmCounts[selectedUserIndex] + 4) / 5);  // คำนวณจำนวนหน้าทั้งหมด
    }

    display.display();
}

// void displaySavedFarmsForUser() {
//     Serial.println("📂 Displaying Saved Farms for selected user...");
//     inSavedUserSelection = false;  // Disable user selection
//     inSavedFarmSelection = true;   // Enable farm selection

//     display.clearDisplay();
//     display.setTextSize(1);
//     display.setCursor(0, 0);
//     display.println("Select Saved Farm:");

//     if (farmCounts[selectedUserIndex] == 0) {
//         display.setCursor(10, 20);
//         display.println("No saved farms!");
//         display.display();
//         delay(2000);
//         displaySavedUsersPage();  // If no farms, go back to saved users page
//         return;
//     }

//     for (int i = 0; i < farmCounts[selectedUserIndex]; i++) {
//         if (i == selectedFarmIndex) {
//             display.print("> ");
//         } else {
//             display.print("  ");
//         }
//         display.println(farms[selectedUserIndex][i]);
//     }

//     display.display();
// }





void showCalibrationData() {
    if (!SPIFFS.exists("/calibration_data.json")) {
        Serial.println("No calibration data found.");
        return;
    }

    File file = SPIFFS.open("/calibration_data.json", FILE_READ);
    if (!file) {
        Serial.println("Failed to open calibration file.");
        return;
    }

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, file);
    if (error) {
        Serial.println("Failed to parse calibration data.");
        file.close();
        return;
    }

    Serial.println("===== Calibration Data =====");
    if (doc.containsKey("pH_Calibration")) {
        Serial.print("pH Calibration: ");
        Serial.println(doc["pH_Calibration"].as<float>());
    } else {
        Serial.println("pH Calibration not found.");
    }

    if (doc.containsKey("EC_Calibration")) {
        Serial.print("EC Calibration: ");
        Serial.println(doc["EC_Calibration"].as<float>());
    } else {
        Serial.println("EC Calibration not found.");
    }

    Serial.println("============================");
    file.close();
}
