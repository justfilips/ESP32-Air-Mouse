#include <Wire.h>                     // I2C communication library
#include <MPU6050.h>                  // MPU6050 accelerometer/gyroscope sensor
#include <Adafruit_GFX.h>             // Core graphics library for displays
#include <Adafruit_ST7735.h>          // TFT display library
#include "fonts/digital_715pt7b.h"          // Digital font for display
#include "fonts/Orbitron_Bold9pt7b.h"       // Bold font for headers
#include <BleMouse.h>                 // Bluetooth mouse library

// --- Constants and Pin Definitions ---
#define MPU6050_ADDR 0x68             // Default I2C address for MPU6050
#define SPEED 1                       

// Joystick, buttons, and potentiometer pins
const int VRxPin = 34;
const int VRyPin = 35; 
const int leftClickPin = 25;
const int rightClickPin =14;
const int swPin = 32;
const int keyboardButtonPin = 26;
const int redButtonPin = 27;
const int potPin = 33; 

// Display pins
#define TFT_CS     5
#define TFT_RST    17
#define TFT_DC     16
#define TFT_SCLK   18
#define TFT_MOSI   23

// Bluetooth pins
#define BT_RX_PIN 4
#define BT_TX_PIN 13

// Movement scaling for normal and precision mode
const float NORMAL_SCALE = 1.0;
const float PRECISION_SCALE = 0.05;
const float NORMAL_ALPHA = 0.2;
const float PRECISION_ALPHA = 0.05;
const unsigned long PRECISION_CLICK_TIME_MS = 200; // Freeze duration for precision clicks

// --- Global Variables ---
MPU6050 mpu;                 // MPU6050 object
BleMouse bleMouse;           // BLE mouse object

bool isClicking = false;     // Track left mouse click state
bool isRightClicking = false;// Track right mouse click state

// Calibration values for initial orientation of MPU6050
float initialAccX = 0, initialAccY = 0, initialAccZ = 0;
float initialGyroX = 0, initialGyroY = 0, initialGyroZ = 0;

// Raw sensor readings
int16_t ax, ay, az;
int16_t gx, gy, gz;

// Smoothing and movement accumulation
static float smoothX = 0, smoothY = 0;
static float accumX = 0, accumY = 0;

// Display object
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// Predefined colors
#define BLACK    tft.color565(0, 0, 0)   
#define RED      tft.color565(255, 0, 0)   
#define BLUE     tft.color565(0, 0, 255)  
#define WHITE    tft.color565(255, 255, 255)

// Bluetooth serial
HardwareSerial mySerial(1);

// Mode tracking
int lastMode = -1; // Previous mode for detecting changes
String incomingCommand = "";

// Animation and UI flags
bool precisionMode = false;
bool showScrollPulse = false;
int scrollDirection = 0; // 1 = up, -1 = down
static bool showZoomAnim = false;
static unsigned long zoomAnimStart = 0;
static int zoomDirection = 0; // 1 = in, -1 = out
static bool holdFinalFrame = false;
static unsigned long finalFrameStart = 0;
unsigned long scrollPulseStart = 0;

String currentTime = "00:00";  
String lastDisplayedTime = "";     
bool showKeyboardIcon = false;
unsigned long keyboardIconStart = 0;

unsigned long lastTime = 0; // Timer for delays
unsigned long waitDuration = 500; 

bool isAnimationActive = false;
static bool clockShown = false; 

int cx = tft.width() / 2;   // Screen center X
int cy = tft.height() / 2;  // Screen center Y

int volume = 0;
bool forceDrawVolumeBar = false;

// Timer / countdown variables
unsigned long lastSendTime = 0;  
const unsigned long sendInterval = 100;  
bool startShutdownCountdown = false;
static int timerValue = 600;
unsigned long previousMillis = 0;  
const long interval = 1000;         
bool countdownInProgress = false; 

bool modeThreeHeader = true;
bool canceledTimerOrExitTimerMode = false;

// ------------------------ Display Functions ------------------------

// Draw a volume bar on the display
void drawVolumeBar(int volume, uint16_t backgroundColor = ST77XX_BLACK) {
    int barWidth = 120;
    int barHeight = 20;
    int barX = (tft.width() - barWidth) / 2;
    int barY = 90;

    static int lastVolume = -1;

    if (volume != lastVolume) {
        int filledWidth = map(volume, 0, 100, 0, barWidth);
        int lastFilledWidth = map(lastVolume, 0, 100, 0, barWidth);

        // Draw border once
        tft.drawRect(barX - 1, barY - 1, barWidth + 2, barHeight + 2, ST77XX_WHITE);

        // Clear area if volume decreased
        if (filledWidth < lastFilledWidth) {
            tft.fillRect(barX + filledWidth, barY, lastFilledWidth - filledWidth, barHeight, backgroundColor);
        }

        // Draw colored bar
        drawBarSegment(barX, barY, filledWidth, barHeight, volume);
        lastVolume = volume;
    }
}

// Draw individual segment with color based on volume
void drawBarSegment(int x, int y, int width, int height, int volume) {
    int r, g, b;

    if (volume < 50) {
        r = map(volume, 0, 50, 255, 255);
        g = map(volume, 0, 50, 0, 255);
    } else {
        r = map(volume, 50, 100, 255, 0);
        g = map(volume, 50, 100, 255, 255);
    }
    b = 0;

    uint16_t color = tft.color565(b, g, r);  // SWAP for specific display
    tft.fillRect(x, y, width, height, color);
}

// Display shutdown timer screen
void displayShutdownTimer(int timerValue) {
    int minutes = timerValue / 60;
    int seconds = timerValue % 60;

    tft.setTextSize(1);
    tft.setFont(&Orbitron_Bold9pt7b);  
    tft.fillScreen(RED);

    // Display header
    tft.setTextColor(WHITE);
    tft.setCursor(30, 23);
    tft.print("Shutdown");
    tft.setCursor(50, 42);
    tft.print("Timer");

    // Display timer
    tft.setFont(&digital_715pt7b);  
    tft.setTextSize(2);  
    tft.setCursor(30, 90);  
    tft.print(minutes);
    tft.setTextSize(1);
    tft.setFont(&Orbitron_Bold9pt7b);
    tft.print(" min ");
}

// ------------------------ Setup Function ------------------------
void setup() {
    Serial.begin(38400);
    mySerial.begin(38400, SERIAL_8N1, BT_RX_PIN, BT_TX_PIN); 
    mySerial.println("im here"); 
    Serial.println("Bluetooth Communication Initialized");

    Wire.begin();     // I2C for MPU6050
    bleMouse.begin(); // Initialize BLE mouse

    // Button input setup
    pinMode(leftClickPin, INPUT_PULLUP);
    pinMode(rightClickPin, INPUT_PULLUP);
    pinMode(swPin, INPUT_PULLUP);
    pinMode(keyboardButtonPin, INPUT_PULLUP);
    pinMode(redButtonPin, INPUT_PULLUP);

    // Display setup
    SPI.begin(TFT_SCLK, -1, TFT_MOSI);
    tft.initR(INITR_BLACKTAB);
    tft.setRotation(3);
    tft.fillScreen(ST77XX_BLACK);

    // MPU6050 setup
    mpu.initialize();
    Serial.println("MPU6050 initialized!");
    mpu.getAcceleration(&ax, &ay, &az);
    mpu.getRotation(&gx, &gy, &gz);

    // Calibrate initial orientation
    initialAccX = ax / 16384.0;
    initialAccY = ay / 16384.0;
    initialAccZ = az / 16384.0;

    initialGyroX = gx / 131.0;
    initialGyroY = gy / 131.0;
    initialGyroZ = gz / 131.0;

    delay(200);  // Small delay to stabilize sensors
}

// ------------------------ Main Loop ------------------------
void loop() {
    unsigned long currentTimeForMonitor = millis();

    // Read mouse button states
    int leftClick = digitalRead(leftClickPin) == LOW ? 1 : 0;
    int rightClick = digitalRead(rightClickPin) == LOW ? 1 : 0;

    // Only move mouse if BLE is connected
    if (bleMouse.isConnected()) {
        mpu.getAcceleration(&ax, &ay, &az);
        mpu.getRotation(&gx, &gy, &gz);

        // Convert raw sensor values to g's and deg/s
        float accX = ax / 16384.0 - initialAccX;
        float accY = ay / 16384.0 - initialAccY;
        float gyroX = gx / 131.0 - initialGyroX;
        float gyroZ = gz / 131.0 - initialGyroZ;

        float scale = precisionMode ? 0.3 : 1.0;
        float alpha = precisionMode ? 0.2 : 0.5;

        // Combine gyro and accelerometer for movement
        float rawX = gyroZ * scale + accX * (scale / 5);
        float rawY = gyroX * -scale + accY * (scale / 5);

        // Smooth movement
        smoothX = smoothX + alpha * (rawX - smoothX);
        smoothY = smoothY + alpha * (rawY - smoothY);

        // Deadzone for tiny movements
        if (abs(smoothX) < 0.05) smoothX = 0;
        if (abs(smoothY) < 0.05) smoothY = 0;

        // Reduce sensitivity for micro-movements
        float movementMag = sqrt(smoothX * smoothX + smoothY * smoothY);
        float dynamicScale = scale;
        if (movementMag < 0.2) dynamicScale = scale * 0.1;

        smoothX *= dynamicScale / scale;
        smoothY *= dynamicScale / scale;

        accumX += smoothX;
        accumY += smoothY;

        int moveX = (int)accumX;
        int moveY = (int)accumY;

        // Left click logic with freeze
        bool leftPressed = digitalRead(leftClickPin) == LOW;
        if (leftPressed && !isClicking) {
            bleMouse.press(MOUSE_LEFT);
            isClicking = true;
            leftClickStartTime = millis();
            accumX = 0;
            accumY = 0;
        } else if (!leftPressed && isClicking) {
            bleMouse.release(MOUSE_LEFT);
            isClicking = false;
        } else if (leftPressed && millis() - leftClickStartTime < PRECISION_CLICK_TIME_MS) {
            accumX = 0;
            accumY = 0; // Freeze movement during precision click
        }

        // Right click handling
        bool rightPressed = digitalRead(rightClickPin) == LOW;
        if (rightPressed && !isRightClicking) {
            bleMouse.press(MOUSE_RIGHT);
            isRightClicking = true;
        } else if (!rightPressed && isRightClicking) {
            bleMouse.release(MOUSE_RIGHT);
            isRightClicking = false;
        }

        // Move mouse if allowed
        if ((moveX != 0 || moveY != 0) && (!isClicking || (isClicking && millis() - leftClickStartTime > PRECISION_CLICK_TIME_MS))) {
            bleMouse.move(moveX, moveY);
            accumX -= moveX;
            accumY -= moveY;
        }

        delay(10); // small delay to stabilize
    }

    // --- Potentiometer and Mode Handling ---
    int potValue = analogRead(potPin);
    int currentMode = 1;
    if (potValue < 1365) currentMode = 1;
    else if (potValue < 2730) currentMode = 2;
    else currentMode = 3;

    // Volume display mode
    if (currentMode == 2) {
        if (forceDrawVolumeBar) {
            drawVolumeBar(-1, BLUE);
            forceDrawVolumeBar = false;
        } else {
            drawVolumeBar(volume, BLUE);
        }
    }

    // Detect mode change and update header
    if (currentMode != lastMode) {
        clockShown = false;
        lastMode = currentMode;
        tft.setFont(&Orbitron_Bold9pt7b);  

        uint16_t bgColor, textColor;
        String header;

        if (currentMode == 1) { bgColor = BLACK; textColor = WHITE; header = "Navigation"; }
        else if (currentMode == 2) { bgColor = BLUE; textColor = WHITE; header = "Media"; forceDrawVolumeBar = true; }
        else if (currentMode == 3) { bgColor = RED; textColor = WHITE; header = "Shortcuts"; }

        tft.fillScreen(bgColor);
        tft.setTextColor(textColor);
        tft.setCursor(30, 23);
        tft.print(header);
    }

    // --- Joystick Input Mapping ---
    int joystickX = analogRead(VRxPin);
    int joystickY = analogRead(VRyPin);
    int mappedX = map(joystickX, 0, 4095, -10, 10);
    int mappedY = map(joystickY, 0, 4095, -10, 10);
    if (abs(mappedX) < 4) mappedX = 0;
    if (abs(mappedY) < 4) mappedY = 0;

    // --- Serial Commands from Bluetooth ---
    while (mySerial.available()) {
        char c = mySerial.read();
        if (c == '\n') {
            if (incomingCommand.startsWith("time:")) {
                currentTime = incomingCommand.substring(5);
            } else if (incomingCommand.startsWith("VOL:")) {
                volume = incomingCommand.substring(4).toInt();
                drawVolumeBar(volume, BLUE);
            } else if (incomingCommand.startsWith("ENTER_TIMER_MODE:")) {
                modeThreeHeader = false;
                timerValue = incomingCommand.substring(17).toInt();
                isAnimationActive = true;
                clockShown = false;
                displayShutdownTimer(timerValue);
            } else if (incomingCommand.startsWith("UPDATE_TIMER:")) {
                timerValue = incomingCommand.substring(13).toInt();
                displayShutdownTimer(timerValue);
            } else if (incomingCommand.startsWith("START_TIMER:")) {
                tft.fillScreen(RED);
                timerValue = incomingCommand.substring(12).toInt();
                startShutdownCountdown = true;
                isAnimationActive = false;
                clockShown = false;
            } else if (incomingCommand == "CANCEL_TIMER" || incomingCommand == "EXIT_TIMER_MODE") {
                startShutdownCountdown = false;
                tft.fillScreen(RED);
                isAnimationActive = false;
                clockShown = false;
                modeThreeHeader = false;
                canceledTimerOrExitTimerMode = true;
            }
            incomingCommand = "";
        } else {
            incomingCommand += c;
        }
    }

    // --- Clock Display Update ---
    if (!isAnimationActive || currentMode != lastMode) {
        tft.setFont(&digital_715pt7b);
        if (!clockShown) {
            uint16_t bgColor = currentMode == 1 ? BLACK : (currentMode == 2 ? BLUE : RED);
            tft.fillRect(cx - 65, cy - 55, 165, 60, bgColor);
            clockShown = true;
            tft.setCursor(cx - 41, cy - 5);
            tft.setTextSize(2);
            tft.setTextColor(WHITE);
            tft.print(currentTime);
            lastDisplayedTime = currentTime;
        }
        if (currentTime != lastDisplayedTime) {
            uint16_t bgColor = currentMode == 1 ? BLACK : (currentMode == 2 ? BLUE : RED);
            tft.fillRect(cx - 65, cy - 55, 165, 60, bgColor);
            tft.setCursor(cx - 41, cy - 5);
            tft.setTextSize(2);
            tft.setTextColor(WHITE);
            tft.print(currentTime);
            lastDisplayedTime = currentTime;
        }
    }

    // --- Shutdown Countdown ---
    if (startShutdownCountdown) {
        unsigned long currentMillis = millis();
        if (currentMillis - previousMillis >= interval) {
            previousMillis = currentMillis;
            if (timerValue > 0) timerValue--;
            if (currentMode == 3) {
                tft.fillRect(0, 100, 80, 50, RED);
                tft.setFont(&digital_715pt7b);
                tft.setTextSize(1);
                tft.setCursor(5, 120);
                tft.print(timerValue / 60);
                tft.print(":");
                tft.print(timerValue % 60);
                tft.setTextSize(1);
            }
        }
    }

    // --- Restore Shortcuts Header ---
    if ((!modeThreeHeader && startShutdownCountdown) || (!modeThreeHeader && canceledTimerOrExitTimerMode)) {
        tft.setFont(&Orbitron_Bold9pt7b);
        tft.setTextSize(1);
        tft.setCursor(32, 23);
        tft.print("Shortcuts");
        modeThreeHeader = true;
        canceledTimerOrExitTimerMode = false;
    }
  // ------------------------ Send Sensor Data via Bluetooth ------------------------
  if (currentTimeForMonitor - lastSendTime >= sendInterval) {
      lastSendTime = currentTimeForMonitor;  // Update the last send timestamp

      // --- Read current button states ---
      int leftBtnState     = digitalRead(leftClickPin) == LOW ? 1 : 0;
      int rightBtnState    = digitalRead(rightClickPin) == LOW ? 1 : 0;
      int switchState      = digitalRead(swPin) == LOW ? 1 : 0;
      int keyboardBtnState = digitalRead(keyboardButtonPin) == LOW ? 1 : 0;
      int redBtnState      = digitalRead(redButtonPin) == LOW ? 1 : 0;

      // --- Compose and send comma-separated sensor data ---
      // Format: ax,ay,az,gx,gy,gz,joystickX,joystickY,leftBtn,rightBtn,switch,keyboard,redButton,mode
      mySerial.print(ax); mySerial.print(",");
      mySerial.print(ay); mySerial.print(",");
      mySerial.print(az); mySerial.print(",");
      mySerial.print(gx); mySerial.print(",");
      mySerial.print(gy); mySerial.print(",");
      mySerial.print(gz); mySerial.print(",");
      mySerial.print(mappedX); mySerial.print(",");
      mySerial.print(mappedY); mySerial.print(",");
      mySerial.print(leftBtnState); mySerial.print(",");
      mySerial.print(rightBtnState); mySerial.print(",");
      mySerial.print(switchState); mySerial.print(",");
      mySerial.print(keyboardBtnState); mySerial.print(",");
      mySerial.print(redBtnState); mySerial.print(",");
      mySerial.println(currentMode);  // Terminate the record with newline
  }