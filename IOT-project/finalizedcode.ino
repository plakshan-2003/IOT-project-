/*
 * ===============================================
 * ATTENDANCE SYSTEM WITH SALARY CALCULATION
 * ===============================================
 * 
 * FEATURES:
 * - RFID card authentication
 * - Fingerprint biometric verification
 * - Automatic check-in/check-out tracking
 * - Salary calculation based on hours worked
 * - Motion-activated OLED display
 * - Permanent user storage (survives restarts)
 * - Blynk IoT cloud monitoring
 * - Automated door control
 * 
 * HARDWARE:
 * - ESP32 Dev Module
 * - 1.3" OLED Display (128x64, I2C)
 * - RFID RC522 Module
 * - Fingerprint Sensor (R307/AS608)
 * - IR Motion Sensor (HC-SR501)
 * - Servo Motor (with external 5V power)
 * - LEDs (Green & Red)
 * - Buzzer
 * 
 * ===============================================
 */

// ========== BLYNK CONFIGURATION (MUST BE AT TOP!) ==========
// These defines configure Blynk IoT cloud connection
#define BLYNK_TEMPLATE_ID "TMPL6fDUXC2UD"
#define BLYNK_TEMPLATE_NAME "Attendance System"
#define BLYNK_AUTH_TOKEN "JOV2UH6xpvTJnVHFF65QtqqfXDv0q7He"
#define BLYNK_PRINT Serial  // Enable Blynk debug messages on Serial Monitor

// ========== LIBRARY INCLUDES ==========
// Include all necessary libraries for hardware and cloud connectivity
#include <Wire.h>                      // I2C communication for OLED
#include <Adafruit_GFX.h>              // Graphics library for OLED
#include <Adafruit_SSD1306.h>          // OLED display driver
#include <SPI.h>                       // SPI communication for RFID
#include <MFRC522.h>                   // RFID reader library
#include <Adafruit_Fingerprint.h>     // Fingerprint sensor library
#include <ESP32Servo.h>                // Servo motor control
#include <WiFi.h>                      // WiFi connectivity
#include <BlynkSimpleEsp32.h>          // Blynk IoT platform
#include <Preferences.h>               // Permanent storage (EEPROM alternative)
#include <time.h>                      // Time functions for NTP

// ========== PERMANENT STORAGE ==========
// Preferences object for saving user data to flash memory
// This allows enrolled users to survive ESP32 restarts
Preferences preferences;

// ========== NTP TIME CONFIGURATION ==========
// Network Time Protocol - Gets real time from internet (no RTC module needed!)
const char* ntpServer = "pool.ntp.org";        // NTP server address
const long  gmtOffset_sec = 19800;             // GMT+5:30 for Sri Lanka (5.5 hours = 19800 seconds)
const int   daylightOffset_sec = 0;            // No daylight saving in Sri Lanka

// Time structure
struct tm timeinfo;

// ========== WIFI CREDENTIALS ==========
// Replace these with your WiFi network details
char auth[] = "JOV2UH6xpvTJnVHFF65QtqqfXDv0q7He";  // Blynk auth token
char ssid[] = "4G-MIFI-FB77";                       // WiFi network name
char pass[] = "1234567890";                         // WiFi password

// ========== OLED DISPLAY CONFIGURATION ==========
// 1.3 inch OLED display - 128x64 pixels
#define SCREEN_WIDTH 128               // Display width in pixels
#define SCREEN_HEIGHT 64               // Display height in pixels
#define OLED_RESET -1                  // Reset pin (-1 if sharing ESP32 reset)
#define SCREEN_ADDRESS 0x3C            // I2C address (try 0x3D if 0x3C doesn't work)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ========== IR MOTION SENSOR ==========
// Detects motion to wake up the OLED display
// Active LOW: Output is LOW when motion detected, HIGH when no motion
#define IR_PIN 4                       // IR sensor connected to GPIO 4

// ========== RFID READER (RC522) ==========
// Reads RFID cards for user identification
#define SS_PIN 5                       // SDA/SS pin (Chip Select)
#define RST_PIN 22                     // Reset pin
MFRC522 mfrc522(SS_PIN, RST_PIN);     // Create RFID instance

// ========== FINGERPRINT SENSOR ==========
// Biometric verification for enhanced security
HardwareSerial fingerSerial(1);                    // Use Serial1 for communication
Adafruit_Fingerprint finger(&fingerSerial);       // Create fingerprint instance

// ========== OUTPUT DEVICES ==========
// LEDs and buzzer for visual/audio feedback
#define GREEN_LED 32                   // Green LED - Access granted indicator
#define RED_LED   33                   // Red LED - Access denied indicator
#define BUZZER    27                   // Buzzer for audio alerts
#define SERVO_PIN 21                   // Servo motor for door lock

// ========== SERVO MOTOR ==========
// Controls door lock mechanism (requires external 5V power supply)
Servo doorServo;

// ========== ADMIN CONFIGURATION ==========
// Admin RFID card UID for enrollment/removal mode
// Replace with your admin card's UID
#define ADMIN_UID "7B999404"

// ========== SCREEN POWER MANAGEMENT ==========
// Controls when OLED display turns on/off to save power
unsigned long lastMotionTime = 0;              // Timestamp of last motion detection
const unsigned long SCREEN_ON_TIME = 5000;    // Display timeout: 5 seconds (5000ms)
bool screenActive = false;                     // Current display state (true = ON, false = OFF)

// ========== ATTENDANCE COUNTERS ==========
// Track daily attendance statistics
int usersToday = 0;                           // Count of successful check-ins today
int deniedCount = 0;                          // Count of denied access attempts today

// ========== BLYNK VIRTUAL PINS ==========
// Virtual pins for sending data to Blynk mobile app and web dashboard
#define VPIN_STATUS V0                // System status (Online/Offline/Motion/etc)
#define VPIN_USER_ID V1               // Current user's fingerprint ID
#define VPIN_ALERT V2                 // Alert messages
#define VPIN_LAST_ATTENDANCE V3       // Last check-in/out event
#define VPIN_USERS_TODAY V4           // Total users checked in today
#define VPIN_DENIED_COUNT V5          // Total access denied count
#define VPIN_CHECK_IN_TIME V6         // Check-in timestamp
#define VPIN_CHECK_OUT_TIME V7        // Check-out timestamp
#define VPIN_HOURS_WORKED V8          // Hours worked in current session
#define VPIN_SALARY_TODAY V9          // Salary earned today
#define VPIN_MONTHLY_HOURS V11        // Total hours worked this month
#define VPIN_MONTHLY_SALARY V12       // Total salary earned this month
#define VPIN_USER_NAME V13            // Current user's name

// ========== USER DATABASE ==========
// Stores up to 50 users with RFID-Fingerprint mapping and salary tracking
#define MAX_USERS 50                  // Maximum number of enrolled users

// User structure - Contains all information for each enrolled user
struct User {
  String rfidUID;                     // RFID card unique identifier
  uint8_t fingerID;                   // Fingerprint ID (1-127)
  bool active;                        // User account status (true = enrolled)
  String name;                        // User's display name
  float hourlyRate;                   // Salary rate ($/hour)
  
  // Time tracking - Session data (resets on power cycle)
  unsigned long checkInTime;          // Timestamp when user checked in
  unsigned long checkOutTime;         // Timestamp when user checked out
  bool isCheckedIn;                   // Current status (true = at work)
  float totalHoursToday;              // Total hours worked today
  float totalSalaryToday;             // Total salary earned today
  
  // NEW: Overtime and Late tracking
  float regularHoursToday;            // Regular hours (up to 8 hrs)
  float overtimeHoursToday;           // Overtime hours (beyond 8 hrs)
  int lateCountToday;                 // Number of late arrivals today
  float lateChargesDeducted;          // Total late fees deducted
  
  // NEW: Day-off and Early Leave tracking
  int daysOffUsed;                    // Days off used this month (max 5)
  int earlyLeaveCount;                // Count of early leaves
  float earlyLeavePenalties;          // Penalties for leaving early
  
  // Monthly totals - Accumulates over time
  float monthlyHours;                 // Total hours worked this month
  float monthlySalary;                // Total salary earned this month
  float monthlyOvertimeHours;         // Total OT hours this month
  int monthlyLateCount;               // Total late arrivals this month
  int monthlyEarlyLeaves;             // Total early leaves this month
};

User userDatabase[MAX_USERS];         // Array to store all users
int totalUsers = 0;                   // Current number of enrolled users

// ========== SALARY CONFIGURATION ==========
#define DEFAULT_HOURLY_RATE 10.0         // Default hourly wage: $10/hour
#define MAX_HOURLY_RATE 100.0            // Maximum hourly rate: $100/hour
#define STANDARD_WORK_HOURS 8.0          // Standard work hours per day  
#define OT_MULTIPLIER 1.5                // Overtime rate: 1.5x normal pay
#define LATE_PENALTY_PER_MINUTE 2.0      // Late charge: $2 per minute late
#define WORK_START_TIME "09:00"          // Expected start time (9:00 AM)
#define LATE_GRACE_MINUTES 15            // Grace period: 15 minutes (9:00-9:15 OK)
#define OT_MULTIPLIER 1.5             // Overtime pay rate (1.5x normal rate)
#define LATE_THRESHOLD_MINUTES 15     // Consider late if check-in > 15 min after start time
#define WORK_START_HOUR 9             // Standard work start time: 9:00 AM
#define WORK_START_MINUTE 0           // Work starts at 9:00 AM
#define LATE_FEE_PER_INCIDENT 5.0     // Charge $5 per late arrival

// ========== DAY OFF & EARLY LEAVE CONFIGURATION ==========
#define DAYS_OFF_PER_MONTH 5          // Each employee gets 5 days off per month
#define WORK_END_HOUR 17              // Standard work end time: 5:00 PM (17:00)
#define WORK_END_MINUTE 0             // Work ends at 17:00
#define EARLY_LEAVE_THRESHOLD_MINUTES 30  // Consider "early leave" if leaving >30 min before 5 PM
#define EARLY_LEAVE_PENALTY 10.0      // Penalty for leaving early: $10

// ========== NOTIFICATION SETTINGS ==========
#define ENABLE_NOTIFICATIONS true     // Enable/disable push notifications
#define ENABLE_EMAIL_ALERTS true      // Enable/disable email alerts

// ========== OLED HELPER FUNCTIONS ==========
void oledClear() {
  display.clearDisplay();
  display.display();
}

void oledPrint(String text, int x = 0, int y = 0, int size = 2) {
  display.setTextSize(size);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(x, y);
  display.println(text);
  display.display();
}

void oledPrintTwoLines(String line1, String line2, int size = 2) {
  display.clearDisplay();
  display.setTextSize(size);
  display.setTextColor(SSD1306_WHITE);
  
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(line1, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 10);
  display.println(line1);
  
  display.getTextBounds(line2, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 35);
  display.println(line2);
  
  display.display();
}

void oledDisplayOn() {
  display.ssd1306_command(SSD1306_DISPLAYON);
}

void oledDisplayOff() {
  display.ssd1306_command(SSD1306_DISPLAYOFF);
}

// ========== USER DATABASE FUNCTIONS ==========
void initUserDatabase() {
  for (int i = 0; i < MAX_USERS; i++) {
    userDatabase[i].rfidUID = "";
    userDatabase[i].fingerID = 0;
    userDatabase[i].active = false;
    userDatabase[i].name = "";
    userDatabase[i].hourlyRate = DEFAULT_HOURLY_RATE;
    userDatabase[i].checkInTime = 0;
    userDatabase[i].checkOutTime = 0;
    userDatabase[i].isCheckedIn = false;
    userDatabase[i].totalHoursToday = 0;
    userDatabase[i].totalSalaryToday = 0;
    userDatabase[i].regularHoursToday = 0;
    userDatabase[i].overtimeHoursToday = 0;
    userDatabase[i].lateCountToday = 0;
    userDatabase[i].lateChargesDeducted = 0;
    userDatabase[i].daysOffUsed = 0;
    userDatabase[i].earlyLeaveCount = 0;
    userDatabase[i].earlyLeavePenalties = 0;
    userDatabase[i].monthlyHours = 0;
    userDatabase[i].monthlySalary = 0;
    userDatabase[i].monthlyOvertimeHours = 0;
    userDatabase[i].monthlyLateCount = 0;
    userDatabase[i].monthlyEarlyLeaves = 0;
  }
  totalUsers = 0;
  Serial.println("User database initialized");
}

int findUserByRFID(String uid) {
  for (int i = 0; i < MAX_USERS; i++) {
    if (userDatabase[i].active && userDatabase[i].rfidUID == uid) {
      return i;
    }
  }
  return -1;
}

int findUserByFingerID(uint8_t fingerID) {
  for (int i = 0; i < MAX_USERS; i++) {
    if (userDatabase[i].active && userDatabase[i].fingerID == fingerID) {
      return i;
    }
  }
  return -1;
}

bool addUser(String uid, uint8_t fingerID) {
  if (totalUsers >= MAX_USERS) {
    Serial.println("Database full!");
    return false;
  }
  
  for (int i = 0; i < MAX_USERS; i++) {
    if (!userDatabase[i].active) {
      userDatabase[i].rfidUID = uid;
      userDatabase[i].fingerID = fingerID;
      userDatabase[i].active = true;
      userDatabase[i].name = "User" + String(fingerID);
      userDatabase[i].hourlyRate = DEFAULT_HOURLY_RATE;
      userDatabase[i].isCheckedIn = false;
      userDatabase[i].totalHoursToday = 0;
      userDatabase[i].totalSalaryToday = 0;
      userDatabase[i].monthlyHours = 0;
      userDatabase[i].monthlySalary = 0;
      totalUsers++;
      Serial.println("User added - RFID: " + uid + " FingerID: " + String(fingerID));
      
      // Save to permanent memory
      saveUserDatabase();
      
      return true;
    }
  }
  return false;
}

bool removeUserFromDatabase(String uid) {
  int index = findUserByRFID(uid);
  if (index >= 0) {
    userDatabase[index].active = false;
    userDatabase[index].rfidUID = "";
    totalUsers--;
    Serial.println("User removed from database");
    
    // Save to permanent memory
    saveUserDatabase();
    
    return true;
  }
  return false;
}

void printDatabase() {
  Serial.println("\n=== User Database ===");
  Serial.println("Total Users: " + String(totalUsers));
  for (int i = 0; i < MAX_USERS; i++) {
    if (userDatabase[i].active) {
      Serial.print("Slot ");
      Serial.print(i);
      Serial.print(": RFID=");
      Serial.print(userDatabase[i].rfidUID);
      Serial.print(" FingerID=");
      Serial.print(userDatabase[i].fingerID);
      Serial.print(" Name=");
      Serial.print(userDatabase[i].name);
      Serial.print(" Rate=$");
      Serial.print(userDatabase[i].hourlyRate);
      Serial.print("/hr");
      if (userDatabase[i].isCheckedIn) {
        Serial.print(" [CHECKED IN]");
      }
      Serial.println();
    }
  }
  Serial.println("===================\n");
}

// ========== EEPROM STORAGE FUNCTIONS ==========
void saveUserDatabase() {
  preferences.begin("attendance", false);
  
  // Save total users count
  preferences.putInt("totalUsers", totalUsers);
  
  // Save each user
  for (int i = 0; i < MAX_USERS; i++) {
    if (userDatabase[i].active) {
      String keyPrefix = "u" + String(i) + "_";
      
      preferences.putString((keyPrefix + "rfid").c_str(), userDatabase[i].rfidUID);
      preferences.putUChar((keyPrefix + "fid").c_str(), userDatabase[i].fingerID);
      preferences.putBool((keyPrefix + "act").c_str(), userDatabase[i].active);
      preferences.putString((keyPrefix + "name").c_str(), userDatabase[i].name);
      preferences.putFloat((keyPrefix + "rate").c_str(), userDatabase[i].hourlyRate);
    }
  }
  
  preferences.end();
  Serial.println("Database saved to memory!");
}

void loadUserDatabase() {
  preferences.begin("attendance", true);  // Read-only mode
  
  // Load total users
  totalUsers = preferences.getInt("totalUsers", 0);
  
  // Load each user
  for (int i = 0; i < MAX_USERS; i++) {
    String keyPrefix = "u" + String(i) + "_";
    
    bool isActive = preferences.getBool((keyPrefix + "act").c_str(), false);
    
    if (isActive) {
      userDatabase[i].rfidUID = preferences.getString((keyPrefix + "rfid").c_str(), "");
      userDatabase[i].fingerID = preferences.getUChar((keyPrefix + "fid").c_str(), 0);
      userDatabase[i].active = true;
      userDatabase[i].name = preferences.getString((keyPrefix + "name").c_str(), "User" + String(i));
      userDatabase[i].hourlyRate = preferences.getFloat((keyPrefix + "rate").c_str(), DEFAULT_HOURLY_RATE);
      
      // Reset daily/session data
      userDatabase[i].isCheckedIn = false;
      userDatabase[i].checkInTime = 0;
      userDatabase[i].checkOutTime = 0;
      userDatabase[i].totalHoursToday = 0;
      userDatabase[i].totalSalaryToday = 0;
    }
  }
  
  preferences.end();
  
  if (totalUsers > 0) {
    Serial.println("Database loaded from memory!");
    Serial.println("Found " + String(totalUsers) + " enrolled users");
    printDatabase();
  } else {
    Serial.println("No users found in memory - starting fresh");
  }
}

void clearUserDatabase() {
  preferences.begin("attendance", false);
  preferences.clear();
  preferences.end();
  
  initUserDatabase();
  Serial.println("All users cleared from memory!");
}

// ========== TIME & SALARY FUNCTIONS ==========
// Get current time from NTP server
String getCurrentTime() {
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return "00:00:00";
  }
  
  char timeString[9];
  strftime(timeString, sizeof(timeString), "%H:%M:%S", &timeinfo);
  return String(timeString);
}

// Get current date
String getCurrentDate() {
  if (!getLocalTime(&timeinfo)) {
    return "0000-00-00";
  }
  
  char dateString[11];
  strftime(dateString, sizeof(dateString), "%Y-%m-%d", &timeinfo);
  return String(dateString);
}

// Get current date and time combined
String getCurrentDateTime() {
  if (!getLocalTime(&timeinfo)) {
    return "N/A";
  }
  
  char dateTimeString[20];
  strftime(dateTimeString, sizeof(dateTimeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(dateTimeString);
}

// Check if user is late (checked in after 9:15 AM)
bool isUserLate() {
  if (!getLocalTime(&timeinfo)) {
    return false;
  }
  
  int checkInHour = timeinfo.tm_hour;
  int checkInMinute = timeinfo.tm_min;
  
  // Calculate minutes since midnight
  int checkInMinutes = checkInHour * 60 + checkInMinute;
  int startTimeMinutes = WORK_START_HOUR * 60 + WORK_START_MINUTE;
  int lateThreshold = startTimeMinutes + LATE_THRESHOLD_MINUTES;
  
  // User is late if check-in is after threshold (e.g., 9:15 AM)
  return checkInMinutes > lateThreshold;
}

// Calculate overtime and regular hours
void calculateOvertimeHours(float totalHours, float &regularHours, float &overtimeHours) {
  if (totalHours <= STANDARD_WORK_HOURS) {
    regularHours = totalHours;
    overtimeHours = 0;
  } else {
    regularHours = STANDARD_WORK_HOURS;
    overtimeHours = totalHours - STANDARD_WORK_HOURS;
  }
}

// ========== NOTIFICATION FUNCTIONS ==========
// Cooldown tracking to prevent notification spam
unsigned long lastNotificationTime = 0;
unsigned long lastEmailTime = 0;
const unsigned long NOTIFICATION_COOLDOWN = 10000; // 10 seconds between notifications
const unsigned long EMAIL_COOLDOWN = 30000;        // 30 seconds between emails

// Send Blynk push notification
void sendNotification(String title, String message) {
  if (!ENABLE_NOTIFICATIONS) {
    Serial.println("❌ Notifications disabled in config");
    return;
  }
  
  if (!Blynk.connected()) {
    Serial.println("❌ Blynk not connected - notification not sent");
    return;
  }
  
  // Check cooldown
  unsigned long now = millis();
  if (now - lastNotificationTime < NOTIFICATION_COOLDOWN) {
    Serial.println("⏳ Notification on cooldown - waiting " + 
                   String((NOTIFICATION_COOLDOWN - (now - lastNotificationTime)) / 1000) + " seconds");
    return;
  }
  
  // Send notification
  String fullMessage = title + ": " + message;
  Blynk.logEvent("attendance_alert", fullMessage);
  lastNotificationTime = now;
  
  Serial.println("📢 NOTIFICATION SENT: " + fullMessage);
  Serial.println("✅ Next notification available in 10 seconds");
}

// Send email alert via Blynk
void sendEmailAlert(String subject, String body) {
  if (!ENABLE_EMAIL_ALERTS) {
    Serial.println("❌ Email alerts disabled in config");
    return;
  }
  
  if (!Blynk.connected()) {
    Serial.println("❌ Blynk not connected - email not sent");
    return;
  }
  
  // Check cooldown
  unsigned long now = millis();
  if (now - lastEmailTime < EMAIL_COOLDOWN) {
    Serial.println("⏳ Email on cooldown - waiting " + 
                   String((EMAIL_COOLDOWN - (now - lastEmailTime)) / 1000) + " seconds");
    return;
  }
  
  // Send email
  String fullMessage = subject + " | " + body;
  Blynk.logEvent("email_alert", fullMessage);
  lastEmailTime = now;
  
  Serial.println("📧 EMAIL SENT: " + subject);
  Serial.println("✅ Next email available in 30 seconds");
}

// Check if user is leaving early (before 4:30 PM)
bool isLeavingEarly() {
  if (!getLocalTime(&timeinfo)) {
    return false;
  }
  
  int checkOutHour = timeinfo.tm_hour;
  int checkOutMinute = timeinfo.tm_min;
  
  // Calculate minutes since midnight
  int checkOutMinutes = checkOutHour * 60 + checkOutMinute;
  int endTimeMinutes = WORK_END_HOUR * 60 + WORK_END_MINUTE;
  int earlyThreshold = endTimeMinutes - EARLY_LEAVE_THRESHOLD_MINUTES;
  
  // User is leaving early if check-out is before threshold (e.g., before 4:30 PM)
  return checkOutMinutes < earlyThreshold;
}

// Convert time string to minutes since midnight
int timeToMinutes(String timeStr) {
  int h = timeStr.substring(0, 2).toInt();
  int m = timeStr.substring(3, 5).toInt();
  return h * 60 + m;
}

float calculateHours(unsigned long startTime, unsigned long endTime) {
  unsigned long diff = endTime - startTime;
  return (float)diff / 3600000.0;  // Convert milliseconds to hours
}

float calculateSalary(float hours, float hourlyRate) {
  return hours * hourlyRate;
}

void checkInUser(int userIndex) {
  if (!userDatabase[userIndex].isCheckedIn) {
    userDatabase[userIndex].checkInTime = millis();
    userDatabase[userIndex].isCheckedIn = true;
    
    // Get real time from NTP
    String timeStr = getCurrentTime();
    String dateStr = getCurrentDate();
    
    // Check if user is late
    bool late = isUserLate();
    if (late) {
      userDatabase[userIndex].lateCountToday++;
      userDatabase[userIndex].monthlyLateCount++;
      Serial.println("⚠️ LATE ARRIVAL DETECTED!");
      Serial.println("Late count: " + String(userDatabase[userIndex].lateCountToday));
      
      // Send notification to admin for late arrival
      sendNotification("⏰ LATE ARRIVAL", 
                      userDatabase[userIndex].name + " arrived late at " + getCurrentTime() + 
                      " (after " + String(WORK_START_HOUR) + ":" + 
                      String(WORK_START_MINUTE < 10 ? "0" : "") + String(WORK_START_MINUTE) + 
                      " + " + String(LATE_THRESHOLD_MINUTES) + " min grace)");
      
      // Send email for repeated offenders (3+ times)
      if (userDatabase[userIndex].lateCountToday >= 3) {
        sendEmailAlert("⚠️ REPEATED LATE ARRIVAL - " + userDatabase[userIndex].name,
                      userDatabase[userIndex].name + " has been late " + 
                      String(userDatabase[userIndex].lateCountToday) + " times today. " +
                      "Last arrival: " + getCurrentDateTime());
      }
    }
    
    Serial.println("Check-IN at: " + dateStr + " " + timeStr + (late ? " [LATE]" : " [ON TIME]"));
    
    // Send to Blynk in format the web dashboard expects
    if (Blynk.connected()) {
      Blynk.virtualWrite(VPIN_CHECK_IN_TIME, timeStr);
      Blynk.virtualWrite(VPIN_USER_NAME, userDatabase[userIndex].name);
      
      String alertMsg = userDatabase[userIndex].name + " checked IN";
      if (late) {
        alertMsg += " [LATE]";
      }
      Blynk.virtualWrite(VPIN_ALERT, alertMsg);
      
      // Format: "ID:X IN:HH:MM:SS" for web dashboard
      String dashboardMsg = "ID:" + String(userDatabase[userIndex].fingerID) + " IN:" + timeStr;
      Blynk.virtualWrite(VPIN_LAST_ATTENDANCE, dashboardMsg);
    }
  }
}

void checkOutUser(int userIndex) {
  if (userDatabase[userIndex].isCheckedIn) {
    userDatabase[userIndex].checkOutTime = millis();
    userDatabase[userIndex].isCheckedIn = false;
    
    // Calculate total hours worked
    float hoursWorked = calculateHours(
      userDatabase[userIndex].checkInTime, 
      userDatabase[userIndex].checkOutTime
    );
    
    // Check if leaving early
    bool earlyLeave = isLeavingEarly();
    float earlyLeavePenalty = 0;
    
    if (earlyLeave) {
      userDatabase[userIndex].earlyLeaveCount++;
      userDatabase[userIndex].monthlyEarlyLeaves++;
      earlyLeavePenalty = EARLY_LEAVE_PENALTY;
      userDatabase[userIndex].earlyLeavePenalties += earlyLeavePenalty;
      
      Serial.println("⚠️ EARLY LEAVE DETECTED!");
      
      // Send notification for early leave
      sendNotification("⚠️ EARLY LEAVE ALERT", 
                      userDatabase[userIndex].name + " left at " + getCurrentTime() + 
                      " (before " + String(WORK_END_HOUR) + ":00)");
      sendEmailAlert("Early Leave - " + userDatabase[userIndex].name, 
                    userDatabase[userIndex].name + " checked out early at " + getCurrentDateTime());
    }
    
    // Calculate regular and overtime hours
    float regularHours = 0;
    float overtimeHours = 0;
    calculateOvertimeHours(hoursWorked, regularHours, overtimeHours);
    
    // Calculate salary components
    float regularPay = regularHours * userDatabase[userIndex].hourlyRate;
    float overtimePay = overtimeHours * userDatabase[userIndex].hourlyRate * OT_MULTIPLIER;
    float lateCharges = userDatabase[userIndex].lateCountToday * LATE_FEE_PER_INCIDENT;
    
    // Total salary = regular + OT - late charges - early leave penalty
    float salaryEarned = regularPay + overtimePay - lateCharges - earlyLeavePenalty;
    
    // Update daily totals
    userDatabase[userIndex].totalHoursToday += hoursWorked;
    userDatabase[userIndex].regularHoursToday += regularHours;
    userDatabase[userIndex].overtimeHoursToday += overtimeHours;
    userDatabase[userIndex].lateChargesDeducted = lateCharges;
    userDatabase[userIndex].totalSalaryToday += salaryEarned;
    
    // Update monthly totals
    userDatabase[userIndex].monthlyHours += hoursWorked;
    userDatabase[userIndex].monthlyOvertimeHours += overtimeHours;
    userDatabase[userIndex].monthlySalary += salaryEarned;
    
    // Get real time from NTP
    String timeStr = getCurrentTime();
    String dateStr = getCurrentDate();
    
    Serial.println("Check-OUT at: " + dateStr + " " + timeStr + (earlyLeave ? " [EARLY]" : ""));
    Serial.println("─────── WORK SUMMARY ───────");
    Serial.println("Total Hours: " + String(hoursWorked, 2) + " hrs");
    Serial.println("Regular Hours: " + String(regularHours, 2) + " hrs × $" + String(userDatabase[userIndex].hourlyRate, 2) + " = $" + String(regularPay, 2));
    
    if (overtimeHours > 0) {
      Serial.println("Overtime Hours: " + String(overtimeHours, 2) + " hrs × $" + String(userDatabase[userIndex].hourlyRate * OT_MULTIPLIER, 2) + " = $" + String(overtimePay, 2));
    }
    
    if (lateCharges > 0) {
      Serial.println("Late Charges: " + String(userDatabase[userIndex].lateCountToday) + " × $" + String(LATE_FEE_PER_INCIDENT, 2) + " = -$" + String(lateCharges, 2));
    }
    
    if (earlyLeavePenalty > 0) {
      Serial.println("Early Leave Penalty: -$" + String(earlyLeavePenalty, 2));
    }
    
    Serial.println("────────────────────────────");
    Serial.println("NET SALARY: $" + String(salaryEarned, 2));
    Serial.println("────────────────────────────");
    
    // Send to Blynk
    if (Blynk.connected()) {
      Blynk.virtualWrite(VPIN_CHECK_OUT_TIME, timeStr);
      Blynk.virtualWrite(VPIN_HOURS_WORKED, String(hoursWorked, 2) + " hrs");
      Blynk.virtualWrite(VPIN_SALARY_TODAY, "$" + String(userDatabase[userIndex].totalSalaryToday, 2));
      Blynk.virtualWrite(VPIN_MONTHLY_HOURS, String(userDatabase[userIndex].monthlyHours, 2) + " hrs");
      Blynk.virtualWrite(VPIN_MONTHLY_SALARY, "$" + String(userDatabase[userIndex].monthlySalary, 2));
      
      // Alert with OT and penalties info
      String alertMsg = userDatabase[userIndex].name + " checked OUT";
      if (overtimeHours > 0) {
        alertMsg += " [OT: " + String(overtimeHours, 1) + "h]";
      }
      if (lateCharges > 0) {
        alertMsg += " [Late: -$" + String(lateCharges, 2) + "]";
      }
      if (earlyLeave) {
        alertMsg += " [EARLY: -$" + String(earlyLeavePenalty, 2) + "]";
      }
      Blynk.virtualWrite(VPIN_ALERT, alertMsg);
      
      // Format: "ID:X OUT:HH:MM:SS" for web dashboard
      String dashboardMsg = "ID:" + String(userDatabase[userIndex].fingerID) + " OUT:" + timeStr;
      Blynk.virtualWrite(VPIN_LAST_ATTENDANCE, dashboardMsg);
    }
  }
}

// ========== BLYNK CONNECTED ==========
BLYNK_CONNECTED() {
  Serial.println("Blynk connected!");
  Blynk.virtualWrite(VPIN_STATUS, "Online");
  Blynk.virtualWrite(VPIN_USERS_TODAY, usersToday);
  Blynk.virtualWrite(VPIN_DENIED_COUNT, deniedCount);
  Blynk.syncVirtual(VPIN_USERS_TODAY, VPIN_DENIED_COUNT);
}

// ========== SETUP ==========
void setup() {
  // 3 second delay for easier uploading
  delay(3000);
  
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== Starting System ===");
  
  esp_reset_reason_t reason = esp_reset_reason();
  Serial.print("Reset reason: ");
  switch(reason) {
    case ESP_RST_POWERON: Serial.println("Power on"); break;
    case ESP_RST_SW: Serial.println("Software reset"); break;
    case ESP_RST_PANIC: Serial.println("Exception/panic"); break;
    case ESP_RST_INT_WDT: Serial.println("Watchdog timeout"); break;
    case ESP_RST_TASK_WDT: Serial.println("Task watchdog"); break;
    case ESP_RST_WDT: Serial.println("Other watchdog"); break;
    case ESP_RST_BROWNOUT: Serial.println("BROWNOUT - Power issue!"); break;
    default: Serial.println("Unknown"); break;
  }

  Wire.begin(25, 26);
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    while(1) { delay(1000); }
  }
  
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 25);
  display.println("Starting");
  display.display();
  Serial.println("OLED OK");

  pinMode(IR_PIN, INPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, LOW);
  digitalWrite(BUZZER, LOW);

  delay(1000);

  display.clearDisplay();
  display.setCursor(5, 25);
  display.println("WiFi...");
  display.display();
  Serial.println("Connecting WiFi...");
  WiFi.begin(ssid, pass);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK");
    display.clearDisplay();
    display.setCursor(15, 25);
    display.println("WiFi OK");
    display.display();
    delay(1000);

    // ========== INITIALIZE NTP TIME ==========
    // Configure time from internet (no RTC module needed!)
    display.clearDisplay();
    display.setCursor(10, 25);
    display.println("Get Time");
    display.display();
    Serial.println("Configuring NTP time...");
    
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    // Wait for time to be set
    int timeoutCounter = 0;
    while (!getLocalTime(&timeinfo) && timeoutCounter < 10) {
      delay(500);
      Serial.print(".");
      timeoutCounter++;
    }
    
    if (getLocalTime(&timeinfo)) {
      Serial.println("\nTime synchronized!");
      Serial.println("Current time: " + getCurrentDateTime());
      display.clearDisplay();
      display.setCursor(10, 25);
      display.println("Time OK");
      display.display();
      delay(1000);
    } else {
      Serial.println("\nTime sync failed - will use system time");
      display.clearDisplay();
      display.setCursor(5, 25);
      display.println("Time Err");
      display.display();
      delay(1000);
    }

    display.clearDisplay();
    display.setCursor(10, 25);
    display.println("Blynk...");
    display.display();
    Serial.println("Connecting Blynk...");
    Blynk.config(auth);
    
    if (Blynk.connect()) {
      Serial.println("Blynk OK");
      display.clearDisplay();
      display.setCursor(10, 25);
      display.println("Blynk OK");
      display.display();
      
      Blynk.virtualWrite(VPIN_STATUS, "Online");
      Blynk.virtualWrite(VPIN_USER_ID, 0);
      Blynk.virtualWrite(VPIN_USERS_TODAY, 0);
      Blynk.virtualWrite(VPIN_DENIED_COUNT, 0);
      
      delay(1000);
    }
  }

  display.clearDisplay();
  display.setCursor(20, 25);
  display.println("RFID..");
  display.display();
  SPI.begin();
  mfrc522.PCD_Init();
  Serial.println("RFID OK");
  delay(1000);

  display.clearDisplay();
  display.setCursor(5, 25);
  display.println("Finger..");
  display.display();
  fingerSerial.begin(57600, SERIAL_8N1, 13, 14);
  finger.begin(57600);
  
  if (!finger.verifyPassword()) {
    Serial.println("Finger ERROR!");
    display.clearDisplay();
    display.setCursor(0, 20);
    display.println("Finger");
    display.setCursor(15, 40);
    display.println("Error!");
    display.display();
    while(1) { delay(1000); }
  }
  Serial.println("Finger OK");
  delay(1000);

  display.clearDisplay();
  display.setCursor(10, 25);
  display.println("Servo..");
  display.display();
  ESP32PWM::allocateTimer(0);
  doorServo.attach(SERVO_PIN);
  doorServo.write(0);
  Serial.println("Servo OK");
  delay(1000);

  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(20, 25);
  display.println("Ready!");
  display.display();
  Serial.println("=== READY ===\n");
  delay(1000);
  
  oledClear();
  oledDisplayOff();
  screenActive = false;
  
  // Load user database from permanent memory
  Serial.println("\nLoading user database...");
  initUserDatabase();  // Initialize structure first
  loadUserDatabase();  // Then load saved data
  
  // Test notification system
  Serial.println("\n========================================");
  Serial.println("NOTIFICATION TEST");
  Serial.println("========================================");
  Serial.println("Type 'TEST' in Serial Monitor to test notifications");
  Serial.println("Type 'STATUS' to check notification status");
  Serial.println("========================================\n");
}

// ========== GET UID ==========
String getUID() {
  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

// ========== ENROLL USER ==========
void enrollUser(String userRFID) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(5, 25);
  display.println("Enroll");
  display.display();
  Serial.println("Enrolling RFID: " + userRFID);
  delay(1000);

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(10, 20);
  display.println("Place Finger");
  display.setCursor(25, 40);
  display.println("on Sensor");
  display.display();
  
  int p = -1;
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    Blynk.run();
  }

  if (finger.image2Tz(1) != FINGERPRINT_OK) {
    oledPrintTwoLines("Error", "Try Again", 2);
    delay(1000);
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(15, 20);
  display.println("Remove");
  display.setCursor(20, 40);
  display.println("Finger");
  display.display();
  delay(2000);

  display.clearDisplay();
  display.setCursor(5, 20);
  display.println("Place Finger");
  display.setCursor(20, 40);
  display.println("Again");
  display.display();
  
  p = -1;
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    Blynk.run();
  }

  if (finger.image2Tz(2) != FINGERPRINT_OK) {
    oledPrintTwoLines("Error", "Try Again", 2);
    delay(1000);
    return;
  }

  if (finger.createModel() != FINGERPRINT_OK) {
    oledPrintTwoLines("Create", "Failed", 2);
    delay(1000);
    return;
  }

  // Find next available fingerprint ID (1-127)
  uint8_t id = 1;
  bool idFound = false;
  
  // Check which IDs are already used
  for (id = 1; id <= 127; id++) {
    bool inUse = false;
    
    // Check if this ID is already in database
    for (int i = 0; i < MAX_USERS; i++) {
      if (userDatabase[i].active && userDatabase[i].fingerID == id) {
        inUse = true;
        break;
      }
    }
    
    // Found an unused ID
    if (!inUse) {
      idFound = true;
      break;
    }
  }
  
  if (!idFound) {
    oledPrintTwoLines("No Free", "ID Slots", 2);
    delay(2000);
    return;
  }
  
  Serial.println("Assigning Fingerprint ID: " + String(id));
  
  if (finger.storeModel(id) != FINGERPRINT_OK) {
    oledPrintTwoLines("Store", "Failed", 2);
    delay(1000);
    return;
  }

  // Store in database
  if (addUser(userRFID, id)) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(10, 15);
    display.println("Success");
    display.setTextSize(1);
    display.setCursor(35, 45);
    display.print("ID: ");
    display.println(id);
    display.display();
    Serial.println("User enrolled - FingerID: " + String(id));
    printDatabase();
  } else {
    oledPrintTwoLines("Database", "Full", 2);
  }
  
  delay(2000);
  oledClear();
}

// ========== REMOVE USER ==========
void removeUser(String userRFID) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(10, 25);
  display.println("Remove");
  display.display();
  Serial.println("Removing user: " + userRFID);
  delay(1000);

  // Find user in database
  int userIndex = findUserByRFID(userRFID);
  
  if (userIndex >= 0) {
    uint8_t fingerID = userDatabase[userIndex].fingerID;
    
    // Delete from fingerprint sensor
    if (finger.deleteModel(fingerID) == FINGERPRINT_OK) {
      // Remove from database
      removeUserFromDatabase(userRFID);
      
      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(10, 15);
      display.println("Deleted");
      display.setTextSize(1);
      display.setCursor(25, 45);
      display.print("ID: ");
      display.println(fingerID);
      display.display();
      Serial.println("User deleted - FingerID: " + String(fingerID));
      printDatabase();
    } else {
      oledPrintTwoLines("Delete", "Failed", 2);
    }
  } else {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(25, 15);
    display.println("User");
    display.setCursor(10, 40);
    display.println("Not Found");
    display.display();
    Serial.println("User not found in database");
  }
  
  delay(2000);
  oledClear();
}

// ========== ACCESS GRANTED ==========
void accessGranted(uint8_t userID) {
  Serial.println("ACCESS GRANTED: " + String(userID));
  
  // Find user in database
  int userIndex = findUserByFingerID(userID);
  if (userIndex < 0) {
    Serial.println("ERROR: User not found in database!");
    return;
  }
  
  // Determine if this is check-in or check-out
  bool isCheckIn = !userDatabase[userIndex].isCheckedIn;
  
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(15, 15);
  display.println("Access");
  display.setCursor(10, 40);
  display.println("Granted");
  display.display();
  
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(BUZZER, HIGH);
  delay(300);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(BUZZER, LOW);

  usersToday++;
  Serial.print("Users today: ");
  Serial.println(usersToday);
  
  // Update Blynk
  if (WiFi.status() == WL_CONNECTED) {
    if (Blynk.connected()) {
      Blynk.virtualWrite(VPIN_STATUS, isCheckIn ? "Checked IN" : "Checked OUT");
      Blynk.virtualWrite(VPIN_USER_ID, userID);
      Blynk.virtualWrite(VPIN_USERS_TODAY, usersToday);
      
      String msg = userDatabase[userIndex].name + (isCheckIn ? " IN" : " OUT");
      Blynk.virtualWrite(VPIN_LAST_ATTENDANCE, msg);
      
      Blynk.run();
      delay(200);
      
      Serial.println("Blynk updated successfully");
    }
  }

  delay(500);

  // Handle check-in or check-out
  if (isCheckIn) {
    checkInUser(userIndex);
  } else {
    checkOutUser(userIndex);
  }

  Serial.println("Starting door unlock sequence...");
  doorServo.write(90);
  Serial.println("Servo -> 90");
  
  // Show simple welcome/goodbye message
  display.clearDisplay();
  display.setTextSize(2);
  
  if (isCheckIn) {
    display.setCursor(10, 15);
    display.println("Welcome!");
    display.setCursor(15, 40);
    display.setTextSize(1);
    display.print(userDatabase[userIndex].name);
  } else {
    display.setCursor(10, 15);
    display.println("Goodbye!");
    display.setCursor(15, 40);
    display.setTextSize(1);
    display.print(userDatabase[userIndex].name);
  }
  display.display();
  
  for (int i = 0; i < 60; i++) {
    Blynk.run();
    delay(50);
    
    if (i % 10 == 0) {
      Serial.print(".");
    }
  }
  Serial.println();
  
  doorServo.write(0);
  Serial.println("Servo -> 0");
  Serial.println("Door locked");

  delay(2000);
  oledClear();
  
  Serial.println("Access granted sequence completed!");
}

// ========== ACCESS DENIED ==========
void accessDenied() {
  Serial.println("ACCESS DENIED");
  
  // 🚨 SEND CRITICAL SECURITY ALERT
  sendNotification("🚨 SECURITY ALERT", "Unauthorized access attempt at " + getCurrentTime());
  sendEmailAlert("Security Alert - Access Denied", 
                 "Unauthorized access attempt detected at " + getCurrentDateTime());
  
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(15, 15);
  display.println("Access");
  display.setCursor(20, 40);
  display.println("Denied");
  display.display();
  
  digitalWrite(RED_LED, HIGH);
  digitalWrite(BUZZER, HIGH);
  delay(600);
  digitalWrite(RED_LED, LOW);
  digitalWrite(BUZZER, LOW);

  deniedCount++;
  Serial.print("Denied count now: ");
  Serial.println(deniedCount);
  
  if (WiFi.status() == WL_CONNECTED) {
    if (Blynk.connected()) {
      Serial.println("Sending to Blynk...");
      
      Blynk.virtualWrite(VPIN_DENIED_COUNT, deniedCount);
      delay(50);
      
      Blynk.virtualWrite(VPIN_STATUS, "DENIED");
      delay(50);
      
      Blynk.virtualWrite(VPIN_ALERT, String("Denied! Count: ") + String(deniedCount));
      
      Blynk.run();
      delay(100);
      
      Serial.println("Successfully sent to Blynk: " + String(deniedCount));
    } else {
      Serial.println("Blynk disconnected! Reconnecting...");
      Blynk.connect();
    }
  } else {
    Serial.println("WiFi disconnected!");
  }
  
  delay(1500);
}

// ========== MAIN LOOP ==========
// Runs continuously - handles motion detection, RFID scanning, and user authentication
void loop() {
  Blynk.run();  // Keep Blynk connection alive

  // ========== SERIAL COMMANDS FOR TESTING ==========
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command == "TEST") {
      Serial.println("\n🧪 Testing notification system...");
      Serial.println("Blynk connected: " + String(Blynk.connected() ? "YES ✅" : "NO ❌"));
      Serial.println("Notifications enabled: " + String(ENABLE_NOTIFICATIONS ? "YES ✅" : "NO ❌"));
      Serial.println("Sending test notification...\n");
      
      sendNotification("🧪 TEST", "This is a test notification at " + getCurrentTime());
      sendEmailAlert("Test Alert", "Testing email alerts at " + getCurrentDateTime());
      
      Serial.println("\n✅ Test complete! Check your phone for notification.");
    }
    else if (command == "STATUS") {
      Serial.println("\n📊 NOTIFICATION STATUS:");
      Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
      Serial.println("Blynk Connected: " + String(Blynk.connected() ? "✅ YES" : "❌ NO"));
      Serial.println("WiFi Connected: " + String(WiFi.status() == WL_CONNECTED ? "✅ YES" : "❌ NO"));
      Serial.println("Notifications Enabled: " + String(ENABLE_NOTIFICATIONS ? "✅ YES" : "❌ NO"));
      Serial.println("Email Alerts Enabled: " + String(ENABLE_EMAIL_ALERTS ? "✅ YES" : "❌ NO"));
      Serial.println("Last Notification: " + String((millis() - lastNotificationTime) / 1000) + " sec ago");
      Serial.println("Last Email: " + String((millis() - lastEmailTime) / 1000) + " sec ago");
      Serial.println("Notification Cooldown: " + String(NOTIFICATION_COOLDOWN / 1000) + " seconds");
      Serial.println("Email Cooldown: " + String(EMAIL_COOLDOWN / 1000) + " seconds");
      Serial.println("Total Denied: " + String(deniedCount));
      Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    }
  }

  // ========== MOTION DETECTION & DISPLAY WAKE ==========
  // IR sensor output: LOW = Motion detected, HIGH = No motion (Active LOW mode)
  // When motion is detected (sensor's green LED turns OFF):
  // 1. Turn on OLED display
  // 2. Show "Tap RFID" message
  // 3. Start 5-second timeout timer
  if (!digitalRead(IR_PIN)) {  // Check if IR sensor detects motion (LOW = motion)
    lastMotionTime = millis();  // Update last motion timestamp
    if (!screenActive) {         // Only wake screen if it's currently off
      oledDisplayOn();           // Turn on OLED display
      display.clearDisplay();    // Clear any previous content
      display.setTextSize(2);    // Large text for readability
      display.setCursor(30, 15); // Position "Tap" text
      display.println("Tap");
      display.setCursor(25, 40); // Position "RFID" text
      display.println("RFID");
      display.display();         // Update display with new content
      screenActive = true;       // Mark screen as active
      Blynk.virtualWrite(VPIN_STATUS, "Motion");  // Update Blynk status
      Serial.println("Motion detected");           // Debug message
    }
  }

  // ========== SCREEN TIMEOUT ==========
  // Turn off display after 5 seconds of no motion to save power
  // This prevents screen from staying on indefinitely
  if (screenActive && millis() - lastMotionTime > SCREEN_ON_TIME) {
    oledClear();                              // Clear display content
    oledDisplayOff();                         // Turn off display power
    screenActive = false;                     // Mark screen as inactive
    Blynk.virtualWrite(VPIN_STATUS, "Idle");  // Update Blynk status
    Serial.println("Screen off");             // Debug message
    return;  // Exit loop early - no need to check RFID if screen is off
  }

  // ========== SKIP RFID CHECK IF SCREEN IS OFF ==========
  // Don't waste processing power checking RFID when display is sleeping
  if (!screenActive) return;

  // ========== RFID CARD DETECTION ==========
  // Check if an RFID card is present and readable
  if (!mfrc522.PICC_IsNewCardPresent()) return;  // Exit if no card detected
  if (!mfrc522.PICC_ReadCardSerial()) return;     // Exit if card read failed

  // Get the card's unique identifier (UID)
  String uid = getUID();
  Serial.println("Card: " + uid);  // Display card UID for debugging

  // ========== ADMIN MODE ==========
  // Special mode for enrolling new users or removing existing users
  // Activated when admin card is scanned
  // Process:
  // 1. Admin scans their card → System enters admin mode
  // 2. Admin scans user's card within 10 seconds
  // 3. If user exists → Remove user | If user doesn't exist → Enroll new user
  if (uid == ADMIN_UID) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(20, 25);
    display.println("Admin");
    display.display();
    Serial.println("ADMIN MODE");
    delay(1000);

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(10, 20);
    display.println("Tap User");
    display.setCursor(25, 40);
    display.println("Card");
    display.display();
    
    // Wait for user card
    unsigned long startWait = millis();
    while (millis() - startWait < 10000) {
      if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
        String userUID = getUID();
        Serial.println("User card: " + userUID);
        
        // Check if user exists
        int userIndex = findUserByRFID(userUID);
        
        if (userIndex >= 0) {
          // User exists - remove them
          removeUser(userUID);
        } else {
          // New user - enroll them
          enrollUser(userUID);
        }
        return;
      }
      Blynk.run();
      delay(100);
    }
    
    // Timeout
    oledPrintTwoLines("Timeout", "", 2);
    delay(1000);
    oledClear();
    return;
  }

  // ========== NORMAL USER AUTHENTICATION ==========
  // Two-factor authentication: RFID card + Fingerprint must BOTH match
  // This ensures high security - card can't be used without the owner's finger
  Serial.println("Checking user access...");
  
  // STEP 1: Verify RFID card is registered in database
  // First check if this RFID card belongs to an enrolled user
  int userIndex = findUserByRFID(uid);
  
  if (userIndex < 0) {
    // RFID card not found in database - deny access immediately
    Serial.println("RFID not registered");
    accessDenied();
    delay(1000);
    return;
  }
  
  // STEP 2: Card is valid - now verify fingerprint matches
  // Get the fingerprint ID that should match this card
  uint8_t expectedFingerID = userDatabase[userIndex].fingerID;
  
  // Prompt user to place finger on sensor
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(10, 20);
  display.println("Place Finger");
  display.setCursor(25, 40);
  display.println("on Sensor");
  display.display();
  Serial.println("Waiting for fingerprint...");
  Serial.println("Expected FingerID: " + String(expectedFingerID));
  Blynk.virtualWrite(VPIN_STATUS, "Verify");
  
  // STEP 3: Wait up to 5 seconds for fingerprint scan
  uint32_t start = millis();
  bool found = false;
  
  while (millis() - start < 5000) {  // 5 second timeout
    Blynk.run();  // Keep Blynk connection alive
    
    // Try to capture and match fingerprint
    if (finger.getImage() == FINGERPRINT_OK) {
      if (finger.image2Tz() == FINGERPRINT_OK) {
        if (finger.fingerSearch() == FINGERPRINT_OK) {
          uint8_t detectedFingerID = finger.fingerID;
          Serial.println("Detected FingerID: " + String(detectedFingerID));
          
          // STEP 4: Compare detected fingerprint with expected fingerprint
          // Only grant access if fingerprint matches the RFID card
          if (detectedFingerID == expectedFingerID) {
            Serial.println("MATCH! RFID + Fingerprint verified");
            accessGranted(detectedFingerID);  // Grant access
            found = true;
            return;
          } else {
            Serial.println("MISMATCH! Wrong finger for this card");
            accessDenied();  // Deny access - wrong fingerprint
            delay(1000);
            return;
          }
        }
      }
    }
    delay(50);  // Small delay to prevent CPU overload
  }

  // STEP 5: Timeout - no fingerprint detected within 5 seconds
  if (!found) {
    Serial.println("No fingerprint detected");
    accessDenied();  // Deny access - timeout
    delay(1000);
  }
}
