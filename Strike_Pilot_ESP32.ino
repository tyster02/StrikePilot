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

  CALIBRATION,
  IDLE,
  WAIT_FOR_BALL,
  MEASURE_PASS,
  PROCESS_DATA,
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
byte customChars[3][8] = {
  // 97 - Calibration
  {B01100110,
   B11111111,
   B11111111,
   B11111111,
   B01111110,
   B00111100,
   B00011000,
   B00000000},
  
  // 98 - Arrow up
  {B00011000,
   B00111100,
   B01111110,
   B11111111,
   B00011000,
   B00011000,
   B00011000,
   B00011000},
  
  // 99 - Smiley
  {B00111100,
   B01000010,
   B10100101,
   B10000001,
   B10100101,
   B10011001,
   B01000010,
   B00111100}
};
void Display(int num) {
  if (num < 0 || num > 99) {
    return; // Invalid number
  }
  
  mx.clear();
  
  // Handle custom characters (97-99)
  if (num >= 97) {
    int customIndex = num - 97;
    for (int row = 0; row < 8; row++) {
      for (int col = 0; col < 8; col++) {
        mx.setPoint(row, 7 - col, bitRead(customChars[customIndex][row], 7 - col));
      }
    }
    return;
  }
  
  // Handle numbers 0-96
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

//7. Printer
#define RXD2 16  // Connect to printer TX (GREEN wire)
#define TXD2 17  // Connect to printer RX (YELLOW wire)
Adafruit_Thermal printer(&Serial2);  // Use Serial2

//8. RGBLED
#define RED  14   
#define GREEN  27 
#define BLUE  26

//9. Buttons
#define ButtonA 2
#define ButtonB 4


//Geometrical Dimensions
int tof1_a = 1; //height of top sensor above equator [mm]
int tof2_b = 0;// height of bottom sensor above equatora [mm]
uint16_t lane_gap = 0; // [mm]
uint16_t radius = 108; // [mm]
uint16_t calibration_board = 15; // dimensionless
uint16_t cal_board_dist = (calibration_board - 0.5)*25.4;

void setup() {
  // Optional: Start serial monitor for debugging
  Serial.begin(115200);
  delay(2000);
  //Wire.begin(21,22); //SDA/SCL

  // Initialize pins
  

  //SD Card
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(100);
  Serial.println("\n3. Initializing SD Card...");  
  if(!SD.begin(SD_CS)) {  // Just pass CS pin, use default SPI
    Serial.println("SD Card Mount Failed!");
  } else {
    Serial.println("SD Card mounted successfully!");  
  }


  //LED Matrix
  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);  // Ensure CS starts HIGH
  delay(100);  // Give it time to stabilize
  mx.begin();
  mx.control(MD_MAX72XX::INTENSITY, 2);  // Brightness 0-15
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

  // Set to high-speed mode (20ms per measurement)
  tof1.setMeasurementTimingBudgetMicroSeconds(20000);
  tof2.setMeasurementTimingBudgetMicroSeconds(20000);

  // Start both in continuous mode
  tof1.startRangeContinuous();
  tof2.startRangeContinuous();

  //Printer
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  delay(500);  // Give printer time to initialize
  printer.begin();
  printer.justify('C');
  printer.boldOn();
  printer.setSize('L');

  printer.sleep();
  printer.wake(); 
  
  //RGBLED
  pinMode(RED, OUTPUT);
  pinMode(GREEN,
  OUTPUT);
  pinMode(BLUE, OUTPUT);

  //Buttons
  pinMode(ButtonA, INPUT_PULLUP);
  pinMode(ButtonB, INPUT_PULLUP);
  digitalWrite(ButtonB, HIGH);
  digitalWrite(ButtonB, HIGH);

Serial.println("\nTesting CS pins:");
Serial.print("CS_PIN (LED): "); Serial.println(CS_PIN);
Serial.print("SD_CS (SD): "); Serial.println(SD_CS);

// Test LED matrix
digitalWrite(SD_CS, HIGH);
delay(100);
Display(88);
delay(2000);

// Completely release LED matrix
mx.control(MD_MAX72XX::SHUTDOWN, true);  // Shutdown the display
digitalWrite(CS_PIN, HIGH);
delay(100);

// Re-initialize SD from scratch
SD.end();  // End SD
delay(100);
SPI.end(); // End SPI completely
delay(100);
SPI.begin(); // Restart SPI
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
 /*    
      
    case STATE_PROCESS_DATA:
      handleProcessData();
      break;
      
    case STATE_DISPLAY_RESULTS:
      handleDisplayResults();
      break;
      
    case STATE_ERROR:
      handleError();
      break;
      

  */    
  }
}

// ===== STATE HANDLER FUNCTIONS =====

void handleDeveloper() {
Serial.print(cal_board_dist);
delay(1000000);
}

void handleCalibration() {

  analogWrite(RED, 0); //Full Red
  analogWrite(GREEN, 0); //Full Green -> Full Yellow
  analogWrite(BLUE,255);

  delay(200);
  Display(97); //Displays calibration symbol
  
  delay(2000);
  Display(15); //Displays the board to hit

  uint16_t distance1 = 0;
  uint16_t distance2 = 0;

  do {
    //take bot tof measurements [mm]
    if(getSimultaneousMeasurements(distance1, distance2)){
    delayMicroseconds(100);
  }
  }
  //check the button isn't being read. As soon as it goes LOW, it's read.
  while(digitalRead(ButtonA)==HIGH);
  
  // gap to lane [mm]
  uint16_t lane_gap = ((distance1*distance1)-(distance2*distance2)+(tof_a*tof_a)-(tof_b*tof_b))/(2*(distance1-distance2)) - (cal_board_dist)
  
  do {
    //take bot tof measurements
    if(getSimultaneousMeasurements(distance1, distance2)){
    delayMicroseconds(500);
    int live_cal_board = ((((distance1*distance1)-(distance2*distance2)+(tof_a*tof_a)-(tof_b*tof_b))/(2*(distance1-distance2))-(lane_gap))/25.4)
    Display(live_cal_board);
    if(digitalRead(ButtonB)== LOW){
      uint16_t lane_gap = ((distance1*distance1)-(distance2*distance2)+(tof_a*tof_a)-(tof_b*tof_b))/(2*(distance1-distance2)) - (cal_board_dist)
      Display(15);
    }
  }
  }
  //check the button isn't being read. As soon as it goes LOW, it's read.
  while(digitalRead(ButtonA)==HIGH);
  
  analogWrite(RED, 255); 
  analogWrite(GREEN, 0); //Full Green
  analogWrite(BLUE,255);
  delay(2000);
  analogWrite(GREEN, 255);
  currentState = IDLE;
}

void handleMeasurement(){
Serial.print("In Idle Mode");
delay(1000000);
currentState=IDLE;
}


