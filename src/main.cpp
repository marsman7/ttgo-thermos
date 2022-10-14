#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include "esp_adc_cal.h"
#include <asyncinput.h>
#include <AHTxx.h>

#define PROJECT_NAME "Thermos"

#define MESSURE_INTERVAL  2000  // ms

#define ADC_EN          14  // ADC_EN is the ADC detection enable port, activate voltage divider
#define ADC_PIN         34  // Battery voltage ADC Input
#define BUTTON_1         0  // Right built in button
#define BUTTON_2        35  // Left built in button

//#define I2C_ADDRESS     0x40

#define COLOR_GREY        0xBDF7
#define COLOR_BACKGROUND  0x1082 // 0x2104  // 0x8410    // TFT_DARKGREY
#define BATTERY_POS_Y     210
#define BATTERY_HIGH      26
#define BATTERY_WIDTH     36

#define GAUGE1_POS_Y      30
#define GAUGE2_POS_Y      90
#define GAUGE3_POS_Y      150
#define GAUGE_HIGH        55
#define GAUGE_WIDTH       TFT_WIDTH
#define GAUGE_FRAME_W     3
#define GAUGE_TYPE_POWER  0
#define GAUGE_TYPE_SHUNT  1

void printStatus();

TFT_eSPI tft = TFT_eSPI();  // Invoke custom library
//INA219_WE ina219 = INA219_WE(I2C_ADDRESS);
AHTxx aht20(AHTXX_ADDRESS_X38, AHT2x_SENSOR); //sensor address, sensor type

uint16_t aButtonGpios[] = {
  BUTTON_1 | AsyncInput::LOWAKTIV | AsyncInput::PULL_UP,
  BUTTON_2 | AsyncInput::LOWAKTIV | AsyncInput::PULL_UP
};

AsyncInput myinputs = AsyncInput(2, aButtonGpios);

boolean initial = 1;
uint32_t targetTime = 0;
int vref = 1100;
boolean sensor_ready = false;
float temperature = 0;
float humidity = 0;

void showBattery()
{
    static uint64_t timeStamp = 0;
    if (millis() - timeStamp > 1000) {
        timeStamp = millis();
        digitalWrite(ADC_EN, 1);
        delay(100);
        uint16_t v = analogRead(ADC_PIN);
        float battery_voltage = ((float)v / 4095.0) * 2.0 * 3.3 * (vref / 1000.0);
        // printf("Voltage : %f V\n", battery_voltage);

        tft.fillRoundRect(10, BATTERY_POS_Y + 3, TFT_WIDTH - 20, BATTERY_HIGH - 6, 3, COLOR_BACKGROUND);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(TFT_SKYBLUE, COLOR_BACKGROUND);
        tft.drawFloat(battery_voltage, 2, TFT_WIDTH / 2 , BATTERY_POS_Y + 5, 2);
        tft.drawString("V", TFT_WIDTH / 2 + 35, BATTERY_POS_Y + 5, 2);

        digitalWrite(ADC_EN, 0);  // deactive voltage divider to prevent battery discharge
    }
}

void showGauge(const char* text, uint32_t pos_y, uint16_t color) {
  tft.fillRoundRect(0, pos_y, GAUGE_WIDTH, GAUGE_HIGH, 4, color);
  tft.fillRoundRect(GAUGE_FRAME_W, pos_y + GAUGE_FRAME_W, \
        GAUGE_WIDTH - 2 * GAUGE_FRAME_W, GAUGE_HIGH - 2 * GAUGE_FRAME_W, 3, COLOR_BACKGROUND);
  tft.setTextColor(color, COLOR_BACKGROUND);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(text, 10, pos_y + 5, 2);
}

void updateGauge(float value, int32_t pos_y, uint16_t color) {
  tft.fillRect(GAUGE_FRAME_W, pos_y + GAUGE_FRAME_W + 18, \
        GAUGE_WIDTH - 2 * GAUGE_FRAME_W, GAUGE_HIGH - 2 * GAUGE_FRAME_W - 20, COLOR_BACKGROUND);
  tft.setTextColor(color, COLOR_BACKGROUND);
  tft.setTextDatum(TC_DATUM);
  tft.drawFloat(value, 2, TFT_WIDTH / 2 , pos_y + 25, 4);
}

void myBtn1Callback(int16_t repeats, uint16_t flags) {
  // printf("Button 1 Press Callback : %d | %04x\n", repeats, flags);
}

void myBtn2Callback(int16_t repeats, uint16_t flags) {
  // printf("Button 2 Press Callback : %d | %04x\n", repeats, flags);
}

void setup(void) {
  // Ein-/Ausgänge initialisieren
  pinMode(ADC_EN, OUTPUT);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, 1);

  Wire.begin();

  printf("===============================================================\n");
  printf(PROJECT_NAME);
  printf("\n\n");

  ESP_LOGI(TAG, PROJECT_NAME);

  printf("Run on Core   : %d @ %d MHz\n", xPortGetCoreID(), ESP.getCpuFreqMHz());
  printf("Flash size    : %d\n", ESP.getFlashChipSize());
  printf("Sketch size   : %d ; used : %d\n", ESP.getFreeSketchSpace(), ESP.getSketchSize());
  printf("RAM size      : %d ; free : %d\n", ESP.getHeapSize(), ESP.getFreeHeap());
  if (psramFound()) {
    printf("PSRAM         : %d \n", ESP.getPsramSize());
  } else {
    printf("PSRAM         : not available\n");
  }

  // internal ADC Calibrierung for Battery Voltage
  esp_adc_cal_characteristics_t adc_chars;
  esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);    //Check type of calibration value used to characterize ADC
  if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
    printf("eFuse Vref    : %u mV\n", adc_chars.vref);
    vref = adc_chars.vref;
  } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
    printf("Two Point --> coeff_a:%umV coeff_b:%umV\n", adc_chars.coeff_a, adc_chars.coeff_b);
  } else {
    printf("Default Vref  : 1100mV\n");
  }

  // Sensor init
  int i = 0;
  while (i < 10) {
    sensor_ready = aht20.begin();
    if (sensor_ready)
      break;
    i++;
    delay(1000);
  }
  if (sensor_ready) 
    printf("Sensor AHT2x  : OK\n");
  else
    printf("Sensor AHT2x  : not connected or fail to load calibration coefficient\n");

  //Wire.setClock(400000); //experimental I2C speed! 400KHz, default 100KHz

  // Display initialisieren
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  
  // Only font numbers 2,4,6,7 are valid. Font 6 only contains characters [space] 0 1 2 3 4 5 6 7 8 9 : . a p m
  // Font 7 is a 7 segment font and only contains characters [space] 0 1 2 3 4 5 6 7 8 9 : .
  tft.drawCentreString(PROJECT_NAME, TFT_WIDTH / 2, 1, 4);

  /*
  tft.setTextColor(TFT_DARKGREEN, TFT_BLACK);
  tft.setCursor(1,25);
  tft.printf("Core       : %d MHz\n", ESP.getCpuFreqMHz());
  tft.setCursor(1,35);
  tft.printf("Flash size : %d MB\n", ESP.getFlashChipSize() >> 20);
  tft.setCursor(1,45);
  tft.printf("RAM free   : %d kB\n", ESP.getFreeHeap() >> 10);
  tft.setCursor(1,55);
  if (psramFound()) {
    tft.printf("PSRAM      : %d MB\n", ESP.getPsramSize() >> 20);
  } else {
    tft.printf("PSRAM not available\n");
  }
  */

  // Hintergrund für Batteriespannung zeichnen
  tft.fillRoundRect(7, BATTERY_POS_Y, TFT_WIDTH - 14, BATTERY_HIGH, 3, TFT_SKYBLUE);
  tft.fillRoundRect(10, BATTERY_POS_Y + 3, TFT_WIDTH - 20, BATTERY_HIGH - 6, 3, COLOR_BACKGROUND);

  showGauge("Temperatur (°C)", GAUGE1_POS_Y, TFT_GREENYELLOW);
  showGauge("Luftfeucht (%)", GAUGE2_POS_Y, TFT_ORANGE);
  // showGauge("-----", GAUGE3_POS_Y, TFT_PINK);

  // Button init
  myinputs.onButtonPress(myBtn1Callback, BUTTON_1, AsyncInput::PRESS);
  myinputs.onButtonPress(myBtn2Callback, BUTTON_2, AsyncInput::PRESS);
  if (!myinputs.begin()) {
    printf("Button-Task   : ERROR - Task not created\n");
  } else {
    printf("Button-Task   : OK\n");
  }

  // Display Refresh Zeit initialisieren
  targetTime = millis() + 100;
}

void loop() {
  if ( targetTime < millis() ) {
    targetTime = millis() + MESSURE_INTERVAL;

    showBattery();

    temperature = aht20.readTemperature();
    humidity = aht20.readHumidity();
//    printf("Temperatur : %3.2f °C\n", temperature);
//    printf("Luftfeuchte : %2.1f %%\n", humidity);

    if (temperature == AHTXX_ERROR || humidity == AHTXX_ERROR) { //AHTXX_ERROR = 255, library returns 255 if error occurs
      printStatus(); //print temperature command status

      if ( aht20.softReset() == true ) 
        printf("reset success\n"); //as the last chance to make it alive
      else
        printf("reset failed\n");
    }

    updateGauge(temperature, GAUGE1_POS_Y, TFT_GREENYELLOW);
    updateGauge(humidity, GAUGE2_POS_Y, TFT_ORANGE);
//    updateGauge(power_mW, GAUGE3_POS_Y, TFT_PINK);
  }
}

/**************************************************************************
* printStatus()
*   Print last command status
**************************************************************************/
void printStatus() {
  switch (aht20.getStatus())
  {
    case AHTXX_NO_ERROR:
      printf("no error\n");
      break;

    case AHTXX_BUSY_ERROR:
      printf("sensor busy, increase polling time\n");
      break;

    case AHTXX_ACK_ERROR:
      printf("sensor didn't return ACK, not connected, broken, long wires (reduce speed), bus locked by slave (increase stretch limit)\n");
      break;

    case AHTXX_DATA_ERROR:
      printf("received data smaller than expected, not connected, broken, long wires (reduce speed), bus locked by slave (increase stretch limit)\n");
      break;

    case AHTXX_CRC8_ERROR:
      printf("computed CRC8 not match received CRC8, this feature supported only by AHT2x sensors\n");
      break;

    default:
      printf("unknown status\n");
      break;
  }
}