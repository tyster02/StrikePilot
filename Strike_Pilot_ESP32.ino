//Includes
#include <ESP32Servo.h>
#include <IRremote.hpp>
#include <Wire.h>
#include <MD_MAX72xx.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <Adafruit_VL53L0X.h>
#include "Adafruit_Thermal.h"

//States
enum SystemState {
  DEVELOPER,
  CALIBRATION,
  MEASUREMENT,
  CHANGE_TARGET,
  PRINT_ROUTINE,
  ERROR_STATE
};
// Initialize the current state
SystemState currentState = CALIBRATION;



//Initialize Devices

//1. Servo
const int ServoPin = 12;
int ServoPos = 0;
Servo PinServo;


//2. IR Receiver
#define IRPin 13
int initial_IR = 0;


//3.LED Matrix
#define HARDWARE_TYPE MD_MAX72XX::GENERIC_HW  // Change if needed
#define MAX_DEVICES 1
#define CLK_PIN 18
#define DATA_PIN 23
#define CS_PIN 15
MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);
byte font3x5[10][5] = {
  // 0
  {B00000111,
   B00000101,
   B00000101,
   B00000101,
   B00000111},
  
  // 1
  {B00000010,
   B00000011,
   B00000010,
   B00000010,
   B00000111},
  
  // 2
  {B00000111,
   B00000100,
   B00000111,
   B00000001,
   B00000111},
  
  // 3
  {B00000111,
   B00000100,
   B00000111,
   B00000100,
   B00000111},
  
  // 4
  {B00000101,
   B00000101,
   B00000111,
   B00000100,
   B00000100},
  
  // 5
  {B00000111,
   B00000001,
   B00000111,
   B00000100,
   B00000111},
  
  // 6
  {B00000111,
   B00000001,
   B00000111,
   B00000101,
   B00000111},
  
  // 7
  {B00000111,
   B00000100,
   B00000010,
   B00000010,
   B00000010},
  
  // 8
  {B00000111,
   B00000101,
   B00000111,
   B00000101,
   B00000111},
  
  // 9
  {B00000111,
   B00000101,
   B00000111,
   B00000100,
   B00000111}
};
byte customChars[4][8] = {
  // 96 - Calibration (heart/target)
  {B00111100,
  B01110010,
  B11110001,
  B11110001,
  B10001111,
  B10001111,
  B01001110,
  B00111100},
  
  // 97 - GO
  {B00000000,
   B11110111,
   B10000101,
   B10110101,
   B10010101,
   B11110111,
   B00000000,
   B00000000},
  
  // 98 - Printer
  {B00000000,
   B00000000,
   B01111100,
   B01000100,
   B01000100,
   B11111110,
   B11111010,
   B11111110},
  
  // 99 - Trash can
  {B00000000,
   B00111000,
   B11111110,
   B01010100,
   B01010100,
   B01010100,
   B01010100,
   B01111100}
};

void Display(int num) {
  if (num < 0 || num > 99) {
    return; // Invalid number
  }
  
  mx.clear();
  
  // Handle custom characters (96-99)
  if (num >= 96) {
    int customIndex = num - 96;
    for (int row = 0; row < 8; row++) {
      for (int col = 0; col < 8; col++) {
        mx.setPoint(row, 7 - col, bitRead(customChars[customIndex][row], 7 - col));
      }
    }
    return;
  }
  
  // Handle numbers 0-95
  if (num < 10) {
    // Single digit - center it on display (starting at column 2, but reversed)
    for (int row = 0; row < 5; row++) {
      for (int col = 0; col < 3; col++) {
        mx.setPoint(row + 1, 7 - (col + 2), bitRead(font3x5[num][row], col));
      }
    }
  } else {
    // Double digit - split and display
    int tens = num / 10;
    int ones = num % 10;
    
    // Display tens digit (left side when reversed, starting at column 0)
    for (int row = 0; row < 5; row++) {
      for (int col = 0; col < 3; col++) {
        mx.setPoint(row + 1, 7 - col, bitRead(font3x5[tens][row], col));
      }
    }
    
    // Display ones digit (right side when reversed, starting at column 4)
    for (int row = 0; row < 5; row++) {
      for (int col = 0; col < 3; col++) {
        mx.setPoint(row + 1, 7 - (col + 4), bitRead(font3x5[ones][row], col));
      }
    }
  }
}


//4. SD Card
#define SD_CS   5

void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);
  
  File root = fs.open(dirname);
  if(!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if(!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }
  
  File file = root.openNextFile();
  while(file) {
    if(file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if(levels) {
        listDir(fs, file.path(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}
// Function to write to a file
void writeFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Writing file: %s\n", path);
  
  File file = fs.open(path, FILE_WRITE);
  if(!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if(file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}
// Function to read from a file
void readFile(fs::FS &fs, const char *path) {
  Serial.printf("Reading file: %s\n", path);
  
  File file = fs.open(path);
  if(!file) {
    Serial.println("Failed to open file for reading");
    return;
  }
  
  Serial.print("Read from file: ");
  while(file.available()) {
    Serial.write(file.read());
  }
  Serial.println();
  file.close();
}
// Function to append to a file
void appendFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Appending to file: %s\n", path);
  
  File file = fs.open(path, FILE_APPEND);
  if(!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if(file.print(message)) {
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  file.close();
}
// Function to delete a file
void deleteFile(fs::FS &fs, const char *path) {
  Serial.printf("Deleting file: %s\n", path);  // Disable LED Matrix
  delay(10);
  
  if(fs.remove(path)) {
    Serial.println("File deleted");
  } else {
    Serial.println("Delete failed");
  }
}
// Function to rename a file
void renameFile(fs::FS &fs, const char *path1, const char *path2) {
  Serial.printf("Renaming file %s to %s\n", path1, path2);
  if(fs.rename(path1, path2)) {
    Serial.println("File renamed");
  } else {
    Serial.println("Rename failed");
  }
}
// Function to save a 2D array to SD card
// Function to save a 2D array to SD card (3 columns version)
void saveArrayToFile(fs::FS &fs, const char *path, int array[][3], int rows) {
  Serial.printf("Saving %d rows to file: %s\n", rows, path);
  
  if(fs.exists(path)) {
    deleteFile(fs, path);
  }
  
  File file = fs.open(path, FILE_WRITE);
  if(!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  
  for(int i = 0; i < rows; i++) {
    file.print(array[i][0]);
    file.print(",");
    file.print(array[i][1]);
    file.print(",");
    file.println(array[i][2]);
  }
  
  file.close();
  Serial.println("Array saved successfully");
}



//6. RTC
#define DS1307_ADDRESS 0x68
// Convert normal decimal to binary coded decimal
byte decToBcd(byte val) {
  return ((val / 10 * 16) + (val % 10));
}

// Convert binary coded decimal to normal decimal
byte bcdToDec(byte val) {
  return ((val / 16 * 10) + (val % 16));
}

void setDateTime(int year, byte month, byte day, byte hour, byte minute, byte second) {
  Wire.beginTransmission(DS1307_ADDRESS);
  Wire.write(0); // Start at register 0
  
  Wire.write(decToBcd(second));
  Wire.write(decToBcd(minute));
  Wire.write(decToBcd(hour));
  Wire.write(decToBcd(0)); // Day of week (1-7), not used here
  Wire.write(decToBcd(day));
  Wire.write(decToBcd(month));
  Wire.write(decToBcd(year - 2000)); // DS1307 stores year as offset from 2000
  
  Wire.endTransmission();
  
  Serial.println("Date and time set!");
}

// Read the date and time from the DS1307
void readDateTime(byte &second, byte &minute, byte &hour, byte &day, byte &month, int &year) {
  Wire.beginTransmission(DS1307_ADDRESS);
  Wire.write(0); // Start at register 0
  Wire.endTransmission();
  
  Wire.requestFrom(DS1307_ADDRESS, 7);
  
  second = bcdToDec(Wire.read() & 0x7F);
  minute = bcdToDec(Wire.read());
  hour = bcdToDec(Wire.read() & 0x3F);
  Wire.read(); // Day of week, not used
  day = bcdToDec(Wire.read());
  month = bcdToDec(Wire.read());
  year = bcdToDec(Wire.read()) + 2000;
}

// Display the date and time
void displayDateTime() {
  byte second, minute, hour, day, month;
  int year;
  
  readDateTime(second, minute, hour, day, month, year);
  
  // Format and print date
  Serial.print(year);
  Serial.print("/");
  if (month < 10) Serial.print("0");
  Serial.print(month);
  Serial.print("/");
  if (day < 10) Serial.print("0");
  Serial.print(day);
  Serial.print(" ");
  
  // Format and print time
  if (hour < 10) Serial.print("0");
  Serial.print(hour);
  Serial.print(":");
  if (minute < 10) Serial.print("0");
  Serial.print(minute);
  Serial.print(":");
  if (second < 10) Serial.print("0");
  Serial.println(second);
}
// Function to set date and time interactively


//7. Printer
#define RXD2 16  // Connect to printer TX (GREEN wire)
#define TXD2 17  // Connect to printer RX (YELLOW wire)
Adafruit_Thermal printer(&Serial2);  // Use Serial2

// Function to create a vertical bar chart bitmap
// width = 384 pixels (thermal printer width)
// height = depends on max count
// boards on x-axis (0-40), count on y-axis (vertical bars)
void createBoardHistogramBitmap(uint8_t* bitmap, int width, int height, int histogram[41]) {
  // Clear bitmap
  memset(bitmap, 0, width * height / 8);
  
  // Find max count for scaling
  int max_count = 0;
  for(int i = 0; i < 41; i++) {
    if(histogram[i] > max_count) max_count = histogram[i];
  }
  
  if(max_count == 0) return;  // No data
  
  // Calculate bar width (41 boards across 384 pixels)
  int bar_width = width / 41;  // ~9 pixels per bar
  if(bar_width < 2) bar_width = 2;  // Minimum 2 pixels wide
  int bar_spacing = 1;  // 1 pixel between bars
  
  // Available height for bars (leave room for axis)
  int axis_height = 10;
  int max_bar_height = height - axis_height;
  
  // Draw bars for each board (0-40)
  for(int board = 0; board <= 40; board++) {
    int count = histogram[board];
    
    // Calculate bar height based on count
    int bar_height = (count * max_bar_height) / max_count;
    
    // Calculate x position for this board
    int x_start = board * (bar_width + bar_spacing);
    int x_end = x_start + bar_width;
    
    if(x_end > width) x_end = width;
    
    // Draw bar from bottom up
    int y_start = height - axis_height - bar_height;
    int y_end = height - axis_height;
    
    for(int y = y_start; y < y_end; y++) {
      for(int x = x_start; x < x_end; x++) {
        int byte_idx = (y * width + x) / 8;
        int bit_idx = 7 - ((y * width + x) % 8);
        if(byte_idx < (width * height / 8)) {
          bitmap[byte_idx] |= (1 << bit_idx);
        }
      }
    }
  }
  
  // Draw x-axis line at bottom
  int axis_y = height - axis_height;
  for(int x = 0; x < width; x++) {
    int byte_idx = (axis_y * width + x) / 8;
    int bit_idx = 7 - ((axis_y * width + x) % 8);
    if(byte_idx < (width * height / 8)) {
      bitmap[byte_idx] |= (1 << bit_idx);
    }
  }
  
  // Optional: Draw tick marks at every 5 boards
  for(int board = 0; board <= 40; board += 5) {
    int x_pos = board * (bar_width + bar_spacing) + bar_width/2;
    if(x_pos < width) {
      for(int y = axis_y; y < axis_y + 5; y++) {
        int byte_idx = (y * width + x_pos) / 8;
        int bit_idx = 7 - ((y * width + x_pos) % 8);
        if(byte_idx < (width * height / 8)) {
          bitmap[byte_idx] |= (1 << bit_idx);
        }
      }
    }
  }
}

// Function to create deviation histogram (-5 to +5)
void createDeviationHistogramBitmap(uint8_t* bitmap, int width, int height, int histogram[11]) {
  // Clear bitmap
  memset(bitmap, 0, width * height / 8);
  
  // Find max count for scaling
  int max_count = 0;
  for(int i = 0; i < 11; i++) {
    if(histogram[i] > max_count) max_count = histogram[i];
  }
  
  if(max_count == 0) return;  // No data
  
  int bar_width = 30;   // pixels per bar
  int bar_spacing = 2;  // pixels between bars
  int total_bar_width = bar_width + bar_spacing;
  
  // Center line for zero
  int center_x = width / 2;
  
  // Available height for bars
  int label_height = 20;
  int max_bar_height = height - label_height - 10;
  
  // Draw bars (-5 to +5)
  for(int dev = -5; dev <= 5; dev++) {
    int idx = dev + 5;  // Index 0 = -5, Index 5 = 0, Index 10 = +5
    int x_pos = center_x + (dev * total_bar_width) - bar_width/2;
    
    if(x_pos < 0 || x_pos + bar_width > width) continue;
    
    // Calculate bar height
    int bar_height = (histogram[idx] * max_bar_height) / max_count;
    
    // Draw bar from bottom up
    int y_start = height - label_height - bar_height;
    
    for(int y = y_start; y < height - label_height; y++) {
      for(int x = x_pos; x < x_pos + bar_width; x++) {
        int byte_idx = (y * width + x) / 8;
        int bit_idx = 7 - ((y * width + x) % 8);
        bitmap[byte_idx] |= (1 << bit_idx);
      }
    }
  }
  
  // Draw center line at x = center_x
  for(int y = 0; y < height - label_height; y++) {
    int byte_idx = (y * width + center_x) / 8;
    int bit_idx = 7 - ((y * width + center_x) % 8);
    bitmap[byte_idx] |= (1 << bit_idx);
  }
}


//8. RGBLED
#define RED  14   
#define GREEN  27 
#define BLUE  26

//9. Buttons
#define ButtonA 34 
#define ButtonB 35


//Geometrical Dimensions and Variables
int tof1_a = 66; //height of top sensor above equator [mm]
int tof2_b = 0;// height of bottom sensor above equatora [mm]
float lane_gap = 0; // [mm]
uint16_t radius = 108; // [mm]
uint16_t calibration_board = 15; // dimensionless
uint16_t cal_board_dist = (calibration_board - 0.5)*25.4;
int board_max = 30;
uint16_t sensor_threshold = 900;
uint16_t distance1 = 0;
uint16_t distance2 = 0;
const int MAX_THROWS = 500;
int throw_count = 0;
int throw_data[MAX_THROWS][4];
int current_target = 10;
bool ball_detected_last_loop = false; 
int deviation=0;

void handleDateTimeSetup() {
  // Variables for date/time components
  int year = 26;    // Start at 2026
  int month = 1;
  int day = 1;
  int hour = 0;
  int minute = 0;
  
  // State machine for date/time entry
  enum DateTimeState {
    SET_YEAR,
    SET_MONTH,
    SET_DAY,
    SET_HOUR,
    SET_MINUTE,
    COMPLETE
  };
  
  DateTimeState dt_state = SET_YEAR;
  
  // Purple/Magenta light for setup mode
  analogWrite(RED, 127);
  analogWrite(GREEN, 255);
  analogWrite(BLUE, 127);
  
  bool button_b_was_pressed = false;
  bool button_a_was_pressed = false;
  
  while(dt_state != COMPLETE) {
    // Read button states
    bool button_b_pressed = (digitalRead(ButtonB) == LOW);
    bool button_a_pressed = (digitalRead(ButtonA) == LOW);
    
    // Button B - Increment value
    if(button_b_pressed && !button_b_was_pressed) {
      switch(dt_state) {
        case SET_YEAR:
          year++;
          if(year > 99) year = 26;  // Wrap from 99 to 26
          Display(year);
          break;
          
        case SET_MONTH:
          month++;
          if(month > 12) month = 1;  // Wrap from 12 to 1
          Display(month);
          break;
          
        case SET_DAY:
          day++;
          if(day > 31) day = 1;  // Wrap from 31 to 1
          Display(day);
          break;
          
        case SET_HOUR:
          hour++;
          if(hour > 23) hour = 0;  // Wrap from 23 to 0
          Display(hour);
          break;
          
        case SET_MINUTE:
          minute++;
          if(minute > 59) minute = 0;  // Wrap from 59 to 0
          Display(minute);
          break;
          
        case COMPLETE:
          break;
      }
      delay(200);  // Debounce
    }
    
    // Button A - Confirm and move to next field
    if(button_a_pressed && !button_a_was_pressed) {
      switch(dt_state) {
        case SET_YEAR:
          Serial.print("Year set to: 20");
          Serial.println(year);
          dt_state = SET_MONTH;
          mx.clear();
          delay(300);
          // Display 'M' indicator - using custom pattern
          // You could add a custom character or just show the value
          Display(month);
          break;
          
        case SET_MONTH:
          Serial.print("Month set to: ");
          Serial.println(month);
          dt_state = SET_DAY;
          mx.clear();
          delay(300);
          Display(day);
          break;
          
        case SET_DAY:
          Serial.print("Day set to: ");
          Serial.println(day);
          dt_state = SET_HOUR;
          mx.clear();
          delay(300);
          Display(hour);
          break;
          
        case SET_HOUR:
          Serial.print("Hour set to: ");
          Serial.println(hour);
          dt_state = SET_MINUTE;
          mx.clear();
          delay(300);
          Display(minute);
          break;
          
        case SET_MINUTE:
          Serial.print("Minute set to: ");
          Serial.println(minute);
          dt_state = COMPLETE;
          break;
          
        case COMPLETE:
          break;
      }
      delay(300);  // Debounce
    }
    
    button_b_was_pressed = button_b_pressed;
    button_a_was_pressed = button_a_pressed;
    
    delay(50);  // Small delay for loop
  }
  
  // Save the date/time to RTC
  setDateTime(2000 + year, month, day, hour, minute, 0);
  
  Serial.println("Date and time saved to RTC!");
  
  // Green flash to confirm
  analogWrite(RED, 255);
  analogWrite(GREEN, 0);
  analogWrite(BLUE, 255);
  Display(99);  // Smiley face
  delay(2000);
  
  // Jump to calibration mode
  currentState = CALIBRATION;
}
// Function to save bowling session data to SD card as CSV
void saveBowlingSessionToSD() {
  // Generate filename with timestamp
  byte second, minute, hour, day, month;
  int year;
  readDateTime(second, minute, hour, day, month, year);
  
  char filename[50];
  sprintf(filename, "/bowling_%04d%02d%02d_%02d%02d.csv", year, month, day, hour, minute);
  
  Serial.printf("Saving session to: %s\n", filename);
  
  // Open file for writing
  File file = SD.open(filename, FILE_WRITE);
  if(!file) {
    Serial.println("Failed to open file for writing bowling session");
    return;
  }
  
  // Write header
  file.println("Bowling Session");
  file.print(year);
  file.print("/");
  if (month < 10) file.print("0");
  file.print(month);
  file.print("/");
  if (day < 10) file.print("0");
  file.print(day);
  file.print(" ");
  if (hour < 10) file.print("0");
  file.print(hour);
  file.print(":");
  if (minute < 10) file.print("0");
  file.println(minute);
  file.println();
  
  // Write column headers
  file.println("Throw #,Board Hit,Target Board,Deviation");
  
  // Write data rows
  for(int i = 0; i < throw_count; i++) {
    file.print(throw_data[i][0]);  // Throw number
    file.print(",");
    file.print(throw_data[i][1]);  // Board hit
    file.print(",");
    file.print(throw_data[i][2]);  // Target board
    file.print(",");
    file.println(throw_data[i][3]); // Deviation
  }
  
  file.close();
  Serial.println("Bowling session saved to SD card!");
}


//5. TOF Sensor
#define I2C_SDA_1 21
#define I2C_SCL_1 22
#define I2C_SDA_2 25
#define I2C_SCL_2 33


TwoWire I2C_1 = TwoWire(0);
TwoWire I2C_2 = TwoWire(1);
Adafruit_VL53L0X tof1 = Adafruit_VL53L0X();
Adafruit_VL53L0X tof2 = Adafruit_VL53L0X();

// Function to get simultaneous measurements from both sensors, output in mm
bool getSimultaneousMeasurements(uint16_t &dist1, uint16_t &dist2) {
  int tof1_calibration = 0;
  int tof2_calibration = 0;
  bool tof1_ready = tof1.isRangeComplete();
  bool tof2_ready = tof2.isRangeComplete();
  
  if (tof1_ready && tof2_ready) {
    dist1 = tof1.readRange()+tof1_calibration;
    dist2 = tof2.readRange()+tof2_calibration;
    return true;
  }
  
  return false;
}

void setup() {
  // Optional: Start serial monitor for debugging
  Serial.begin(115200);
  delay(2000);
  
  // Initialize button pins FIRST (before checking them)
  pinMode(ButtonA, INPUT_PULLUP);
  pinMode(ButtonB, INPUT_PULLUP);
  
  // Check if both buttons are pressed on startup
  bool both_buttons_pressed = (digitalRead(ButtonA) == LOW && digitalRead(ButtonB) == LOW);
  
  // Initialize all other pins and devices...
  
  //SD Card
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(100);
  Serial.println("\n3. Initializing SD Card...");  
  if(!SD.begin(SD_CS)) {
    Serial.println("SD Card Mount Failed!");
  } else {
    Serial.println("SD Card mounted successfully!");  
  }

  //LED Matrix
  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);
  delay(100);
  mx.begin();
  mx.control(MD_MAX72XX::INTENSITY, 2);
  mx.clear();
  
  //Servo
  pinMode(ServoPin, OUTPUT);
  PinServo.attach(ServoPin, 500, 2400);

  //IR Receiver
  IrReceiver.begin(IRPin, ENABLE_LED_FEEDBACK);

  //TOF Sensor
  Serial.println("Initializing VL53L0X sensor...");
  I2C_1.begin(I2C_SDA_1, I2C_SCL_1, 400000);
  I2C_2.begin(I2C_SDA_2, I2C_SCL_2, 400000);
  if (!tof1.begin(VL53L0X_I2C_ADDR, false, &I2C_1)) {
   Serial.println("Failed to boot sensor 1!");
   while(1);
  }
  Serial.println("Sensor 1 initialized!");

  if (!tof2.begin(VL53L0X_I2C_ADDR, false, &I2C_2)) {
   Serial.println("Failed to boot sensor 2!");
   while(1);
  }
  Serial.println("Sensor 2 initialized!");

  tof1.setMeasurementTimingBudgetMicroSeconds(20000);
  tof2.setMeasurementTimingBudgetMicroSeconds(20000);
  tof1.startRangeContinuous();
  tof2.startRangeContinuous();

  //Printer
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  delay(500);
  printer.begin();
  printer.justify('C');
  printer.boldOn();
  printer.setSize('L');
  printer.sleep();
  printer.wake(); 
  
  //RGBLED
  pinMode(RED, OUTPUT);
  pinMode(GREEN, OUTPUT);
  pinMode(BLUE, OUTPUT);

  // SD Card re-initialization
  Serial.println("\nTesting CS pins:");
  Serial.print("CS_PIN (LED): "); Serial.println(CS_PIN);
  Serial.print("SD_CS (SD): "); Serial.println(SD_CS);

  digitalWrite(SD_CS, HIGH);
  delay(100);
  Display(88);
  delay(2000);

  mx.control(MD_MAX72XX::SHUTDOWN, true);
  digitalWrite(CS_PIN, HIGH);
  delay(100);

  SD.end();
  delay(100);
  SPI.end();
  delay(100);
  SPI.begin();
  delay(100);

  if(!SD.begin(SD_CS)) {
    Serial.println("SD re-init failed!");
  } else {
    Serial.println("SD re-initialized!");
    File test = SD.open("/test.txt", FILE_WRITE);
    if(test) {
      Serial.println("SD write works!");
      test.close();
    } else {
      Serial.println("SD write fails!");
    }
  }
  
  // Check if we should enter date/time setup mode
  if(both_buttons_pressed) {
    Serial.println("Both buttons pressed - entering date/time setup");
    handleDateTimeSetup();  // This will set currentState = CALIBRATION when done
  }
}

void loop() {
  // State machine - execute code based on current state
  switch (currentState) {
    case DEVELOPER:
      handleDeveloper();
      break;
  
    case CALIBRATION:
      handleCalibration();
      break;
     
    case MEASUREMENT:
      handleMeasurement();
      break;
     
      
    case CHANGE_TARGET:
      handleChangeTarget();
      break;
    
    case PRINT_ROUTINE:
      handlePrintRoutine();
      break;
  /*    
    case STATE_ERROR:
      handleError();
      break;
      

  */    
  }
}

// ===== STATE HANDLER FUNCTIONS =====

void handleDeveloper() {
  Serial.println(digitalRead(ButtonA));
  delay(500);

}

void handleCalibration() {
  // Orange LED (calibration mode)
  Serial.println("Calibration Start");
  analogWrite(RED, 0);     // Full Red
  analogWrite(GREEN, 100);  // Partial Green = Orange
  analogWrite(BLUE, 255);  // No Blue


  digitalWrite(CS_PIN, HIGH);
  delay(50);
  digitalWrite(SD_CS, HIGH);  // Make sure SD card is deselected
  delay(50);
  
  mx.begin();                                // Reinitialize from scratch
  mx.control(MD_MAX72XX::SHUTDOWN, false);   // Make sure it's not in shutdown
  mx.control(MD_MAX72XX::INTENSITY, 2);      // Set brightness
  mx.clear();                                 // Clear display
  
  delay(200);
  Display(96);  // Displays calibration symbol
  
  delay(2000);
  Display(15);  // Displays the board to hit

  distance1 = 0;
  distance2 = 0;

  // Wait for initial calibration measurement
  do {
    if(getSimultaneousMeasurements(distance1, distance2)) {
      delayMicroseconds(100);
    }
    Serial.println("no button");
  }
  while(digitalRead(ButtonA) == HIGH);

  Serial.println("buttonpushed");
  
  // Calculate gap from sensors to lane edge [mm]
  float raw_distance = ((distance1*distance1) - (distance2*distance2) + (tof1_a*tof1_a) - (tof2_b*tof2_b)) / (2.0*(distance1-distance2));
  lane_gap = raw_distance - ((calibration_board - 0.5) * 25.4);
  delay(2000);
  
  // Live calibration loop
  do {
    if(getSimultaneousMeasurements(distance1, distance2)) {
      delayMicroseconds(500);
      
      raw_distance = ((distance1*distance1) - (distance2*distance2) + (tof1_a*tof1_a) - (tof2_b*tof2_b)) / (2.0*(distance1-distance2));
      float distance_from_edge = raw_distance - lane_gap;
      int live_cal_board = (int)(distance_from_edge / 25.4) + 1;
      
      Display(live_cal_board);
      
      if(digitalRead(ButtonB) == LOW) {
        raw_distance = ((distance1*distance1) - (distance2*distance2) + (tof1_a*tof1_a) - (tof2_b*tof2_b)) / (2.0*(distance1-distance2));
        lane_gap = raw_distance - ((calibration_board - 0.5) * 25.4);
        Display(15);
        delay(300);
      }
    }
  }
  while(digitalRead(ButtonA) == HIGH);
  Serial.println("Calibration complete");
  analogWrite(RED, 255); 
  analogWrite(GREEN, 0);
  analogWrite(BLUE, 255);
  delay(2000);
  analogWrite(GREEN, 255);
  //currentState = MEASUREMENT;
  return;
}
void handleMeasurement(){
  // Teal blue LED (measurement mode)
  analogWrite(RED, 255);   // No Red
  analogWrite(GREEN, 32);  // Partial Green
  analogWrite(BLUE, 0);    // Full Blue = Teal
  Display(97); // Display "GO" icon (was 98)
  
  // Check for IR input (condition 1)
  if(IrReceiver.decode()){
    initial_IR = IrReceiver.decodedIRData.command;
    IrReceiver.resume();
    
    bool valid_target_button = false;
    switch(initial_IR) {
      case 12:  // 1
      case 24:  // 2
      case 94:  // 3
      case 8:   // 4
      case 28:  // 5
      case 90:  // 6
      case 66:  // 7
      case 82:  // 8
      case 74:  // 9
      case 22:  // 0
      // Add your delete button code here when found
        valid_target_button = true;
        break;
    }
    
    if(valid_target_button) {
      Serial.print("IR button pressed: ");
      Serial.println(initial_IR);
      currentState = CHANGE_TARGET;
      return;
    }
  }

  // Check for button A press (condition 2)
  if(digitalRead(ButtonA) == LOW){
    currentState = PRINT_ROUTINE;
    return;
  }

  // Check TOF sensors (condition 3)
  if(getSimultaneousMeasurements(distance1, distance2)) {
    if(distance1 < sensor_threshold && distance2 < sensor_threshold){
      float raw_distance = ((distance1*distance1) - (distance2*distance2) + 
                           (tof1_a*tof1_a) - (tof2_b*tof2_b)) / (2.0*(distance1-distance2));
      float distance_from_edge = raw_distance - lane_gap;
      int live_board = (int)(distance_from_edge / 25.4) + 1;
      
      if (live_board < 40 && live_board > 0){
        if (!ball_detected_last_loop) {
          throw_count++;
          
          if (throw_count <= MAX_THROWS) {
            deviation = live_board - current_target;
            throw_data[throw_count-1][0] = throw_count;
            throw_data[throw_count-1][1] = live_board;
            throw_data[throw_count-1][2] = current_target;
            throw_data[throw_count-1][3] = deviation;
          }
          
          // Servo action based on deviation
          int servo_angle = 0;
          if(deviation == 0) {
            servo_angle = 90;  // On target
            analogWrite(RED, 255);
            analogWrite(GREEN, 0);   // Green
            analogWrite(BLUE, 255);
          } else if(deviation == -1) {
            servo_angle = 112;  // 1 board less
          } else if(deviation == 1) {
            servo_angle = 67;   // 1 board more
          } else if(deviation == -2) {
            servo_angle = 135;  // 2 boards less
          } else if(deviation == 2) {
            servo_angle = 45;   // 2 boards more
          } else if(deviation <= -3) {
            servo_angle = 157;  // 3+ boards less
          } else if(deviation >= 3) {
            servo_angle = 22;   // 3+ boards more (22.5 rounded to 22)
          }
          
          PinServo.write(servo_angle);
          delay(500);
          PinServo.write(0);   // Return to rest
          
          Serial.print("Throw #");
          Serial.print(throw_count);
          Serial.print(": Board ");
          Serial.print(live_board);
          Serial.print(" (Target: ");
          Serial.print(current_target);
          Serial.print(", Dev: ");
          Serial.print(deviation);
          Serial.print(", Servo: ");
          Serial.print(servo_angle);
          Serial.println(")");
        }
        
        Display(live_board);
        ball_detected_last_loop = true;
        
      } else {
        // Invalid board number
        analogWrite(RED, 0);
        analogWrite(GREEN, 255);
        analogWrite(BLUE, 255);
        Display(99); // ERROR - Shows trash can
        ball_detected_last_loop = true;
      }
    } else {
      ball_detected_last_loop = false;
    }
  } else {
    ball_detected_last_loop = false;
  }
}
void handleChangeTarget() {
  // Yellow LED (target change mode)
  analogWrite(RED, 0);     // Full Red
  analogWrite(GREEN, 0);   // Full Green = Yellow
  analogWrite(BLUE, 255);  // No Blue
  
  String input_buffer = "";
  int displayed_value = 0;
  bool waiting_for_input = true;
  bool delete_mode = false;
  
  int digit = -1;
  switch(initial_IR) {
    case 12: digit = 1; break;
    case 24: digit = 2; break;
    case 94: digit = 3; break;
    case 8:  digit = 4; break;
    case 28: digit = 5; break;
    case 90: digit = 6; break;
    case 66: digit = 7; break;
    case 82: digit = 8; break;
    case 74: digit = 9; break;
    case 22: digit = 0; break;
    // Add your delete button case here:
    // case XX:
    //   delete_mode = true;
    //   Display(99);  // Show trash can
    //   analogWrite(RED, 0);
    //   analogWrite(GREEN, 255);
    //   analogWrite(BLUE, 255);
    //   break;
  }
  
  if(digit >= 0) {
    input_buffer += String(digit);
    displayed_value = input_buffer.toInt();
    Display(displayed_value);
    Serial.print("Initial input: ");
    Serial.println(input_buffer);
  }
  
  unsigned long timeout_start = millis();
  const unsigned long TIMEOUT = 30000;
  
  while(waiting_for_input) {
    if(millis() - timeout_start > TIMEOUT) {
      Serial.println("Target change timeout");
      currentState = MEASUREMENT;
      return;
    }
    
    if(IrReceiver.decode()) {
      int command = IrReceiver.decodedIRData.command;
      IrReceiver.resume();
      timeout_start = millis();
      
      Serial.print("IR Command: ");
      Serial.println(command);
      
      if(delete_mode) {
        if(command == 64) {  // ENTER - confirm delete
          if(throw_count > 0) {
            throw_count--;
            Serial.print("Deleted throw. New count: ");
            Serial.println(throw_count);
            
            // Flash green to confirm
            analogWrite(RED, 255);
            analogWrite(GREEN, 0);
            analogWrite(BLUE, 255);
            delay(500);
          }
          delete_mode = false;
          currentState = MEASUREMENT;
          return;
        }
        else {
          delete_mode = false;
          Display(displayed_value);
          analogWrite(RED, 0);
          analogWrite(GREEN, 0);
          analogWrite(BLUE, 255);
        }
      }
      else {
        digit = -1;
        
        switch(command) {
          case 12: digit = 1; break;
          case 24: digit = 2; break;
          case 94: digit = 3; break;
          case 8:  digit = 4; break;
          case 28: digit = 5; break;
          case 90: digit = 6; break;
          case 66: digit = 7; break;
          case 82: digit = 8; break;
          case 74: digit = 9; break;
          case 22: digit = 0; break;
          case 64: // ENTER
            if(input_buffer.length() > 0) {
              current_target = displayed_value;
              
              if(current_target > 20) current_target = 20;
              if(current_target < 0) current_target = 0;
              
              Serial.print("Target set to: ");
              Serial.println(current_target);
              
              // Green confirmation flash
              analogWrite(RED, 255);
              analogWrite(GREEN, 0);
              analogWrite(BLUE, 255);
              Display(current_target);
              delay(1000);
              
              currentState = MEASUREMENT;
              return;
            }
            break;
          // Add delete button here when found
        }
        
        if(digit >= 0) {
          if(input_buffer.length() >= 2) {
            input_buffer = "";
          }
          
          input_buffer += String(digit);
          displayed_value = input_buffer.toInt();
          Display(displayed_value);
          
          Serial.print("Input: ");
          Serial.println(input_buffer);
        }
      }
    }
    
    if(digitalRead(ButtonB) == LOW) {
      Serial.println("Target change cancelled");
      delay(300);
      currentState = MEASUREMENT;
      return;
    }
    
    delay(50);
  }
}
void handlePrintRoutine() {
  // Royal blue LED (print mode)
  analogWrite(RED, 200);   // Minimal Red
  analogWrite(GREEN, 255); // No Green
  analogWrite(BLUE, 0);    // Full Blue = Royal Blue
  
  // Properly select LED matrix, deselect SD
  digitalWrite(SD_CS, HIGH);
  delay(10);
  digitalWrite(CS_PIN, LOW);
  mx.control(MD_MAX72XX::SHUTDOWN, false);
  Display(98);  // Show printer icon
  digitalWrite(CS_PIN, HIGH);
  delay(50);
  
  // Save to SD card FIRST (in case printer fails)
  // Properly select SD card, deselect LED matrix
  digitalWrite(CS_PIN, HIGH);
  delay(10);
  digitalWrite(SD_CS, LOW);
  saveBowlingSessionToSD();
  digitalWrite(SD_CS, HIGH);
  delay(10);
  
  // Wake up printer
  printer.wake();
  delay(50);
  
  // Print header
  printer.justify('C');
  printer.setSize('L');
  printer.boldOn();
  printer.println("Bowling Session");
  printer.boldOff();
  printer.println();
  
  // Print date from RTC
  byte second, minute, hour, day, month;
  int year;
  readDateTime(second, minute, hour, day, month, year);
  
  printer.setSize('M');
  printer.print(year);
  printer.print("/");
  if (month < 10) printer.print("0");
  printer.print(month);
  printer.print("/");
  if (day < 10) printer.print("0");
  printer.print(day);
  printer.print(" ");
  if (hour < 10) printer.print("0");
  printer.print(hour);
  printer.print(":");
  if (minute < 10) printer.print("0");
  printer.println(minute);
  printer.println();
  
  // Calculate statistics
  float sum_board = 0;
  float sum_deviation = 0;
  for(int i = 0; i < throw_count; i++) {
    sum_board += throw_data[i][1];
    sum_deviation += throw_data[i][3];
  }
  float avg_board = (throw_count > 0) ? sum_board / throw_count : 0;
  float avg_deviation = (throw_count > 0) ? sum_deviation / throw_count : 0;
  
  // Print stats
  printer.justify('L');
  printer.setSize('S');
  printer.print("Throws: ");
  printer.println(throw_count);
  printer.print("Avg Board: ");
  printer.print(avg_board, 1);
  printer.println();
  printer.print("Avg Dev: ");
  printer.print(avg_deviation, 1);
  printer.println();
  printer.println();
  
  // Create board histogram data (boards 0-40)
  int board_histogram[41] = {0};
  for(int i = 0; i < throw_count; i++) {
    int board = throw_data[i][1];
    if(board >= 0 && board <= 40) {
      board_histogram[board]++;
    }
  }
  
  // Create deviation histogram data (-5 to +5)
  int dev_histogram[11] = {0};
  for(int i = 0; i < throw_count; i++) {
    int dev = throw_data[i][3];
    if(dev >= -5 && dev <= 5) {
      dev_histogram[dev + 5]++;
    }
  }
  
  // Print Board Histogram
  printer.justify('C');
  printer.setSize('M');
  printer.boldOn();
  printer.println("Board Distribution");
  printer.println("(0 to 40)");
  printer.boldOff();
  printer.println();
  
  // Create bitmap for board histogram
  int board_bmp_width = 384;
  int board_bmp_height = 150;
  int board_bmp_bytes = board_bmp_width * board_bmp_height / 8;
  uint8_t* board_bitmap = (uint8_t*)malloc(board_bmp_bytes);
  
  if(board_bitmap != NULL) {
    createBoardHistogramBitmap(board_bitmap, board_bmp_width, board_bmp_height, board_histogram);
    printer.printBitmap(board_bmp_width, board_bmp_height, board_bitmap);
    free(board_bitmap);
  }
  
  printer.println();
  printer.println();
  
  // Print Deviation Histogram
  printer.justify('C');
  printer.setSize('M');
  printer.boldOn();
  printer.println("Deviation from Target");
  printer.println("(-5 to +5)");
  printer.boldOff();
  printer.println();
  
  // Create bitmap for deviation histogram
  int dev_bmp_width = 384;
  int dev_bmp_height = 150;
  int dev_bmp_bytes = dev_bmp_width * dev_bmp_height / 8;
  uint8_t* dev_bitmap = (uint8_t*)malloc(dev_bmp_bytes);
  
  if(dev_bitmap != NULL) {
    createDeviationHistogramBitmap(dev_bitmap, dev_bmp_width, dev_bmp_height, dev_histogram);
    printer.printBitmap(dev_bmp_width, dev_bmp_height, dev_bitmap);
    free(dev_bitmap);
  }
  
  // Footer
  printer.println();
  printer.feed(3);
  
  // Put printer back to sleep
  printer.sleep();
  
  // Make sure SD card is deselected before returning
  digitalWrite(SD_CS, HIGH);
  delay(10);
  
  // Return to measurement
  currentState = MEASUREMENT;
}