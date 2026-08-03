/*
 * Overvoltage And Undervoltage Protection System - Arduino Nano Version
 *
 * Reference article:
 * https://circuitdiagrams.in/overvoltage-and-undervoltage-protection-system/
 *
 * Required libraries:
 *  - EmonLib            https://github.com/openenergymonitor/EmonLib
 *  - LiquidCrystal_I2C  https://github.com/fdebrabander/Arduino-LiquidCrystal-I2C-library
 *
 * Connections:
 *  - ZMPT101B (or 9V AC transformer + voltage divider) OUT -> A0
 *  - Relay Module IN  -> D10
 *  - Buzzer (+)       -> D9
 *  - LED anode        -> D8 (via 220 ohm resistor)
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EmonLib.h>  // EmonLib for voltage measurement

const int relay = 10;      // Relay connected to pin 10
const int buzzer = 9;      // Buzzer connected to pin 9
const int warningLED = 8;  // LED connected to pin 8

EnergyMonitor emon;
float current_Volts;
unsigned long printPeriod = 1000;
unsigned long previousMillis = 0;
LiquidCrystal_I2C lcd(0x27, 20, 4);  // I2C LCD: 20x4

void setup() {
  lcd.init();
  lcd.backlight();

  pinMode(relay, OUTPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(warningLED, OUTPUT);

  digitalWrite(buzzer, LOW);
  digitalWrite(warningLED, LOW);

  emon.voltage(A0, 234.0, 1.7);  // Calibration: adjust if needed

  lcd.setCursor(0, 0);
  lcd.print("Voltage Monitor");
  delay(2000);
  lcd.setCursor(0, 1);
  lcd.print("Under:180V Over:240V");
  delay(2000);
  lcd.clear();
}

void loop() {
  emon.calcVI(20, 2000);       // Sample voltage
  current_Volts = emon.Vrms;   // Read RMS voltage

  if (millis() - previousMillis >= printPeriod) {
    previousMillis = millis();

    // Display voltage
    lcd.setCursor(0, 0);
    lcd.print("Voltage: ");
    lcd.print(current_Volts, 1);
    lcd.print(" V     ");

    // Voltage range checks
    if (current_Volts < 180) {
      lcd.setCursor(0, 1);
      lcd.print("Status: Under Volt ");
      digitalWrite(relay, LOW);
      digitalWrite(buzzer, HIGH);
      digitalWrite(warningLED, HIGH);
    } else if (current_Volts > 240) {
      lcd.setCursor(0, 1);
      lcd.print("Status: Over Volt  ");
      digitalWrite(relay, LOW);
      digitalWrite(buzzer, HIGH);
      digitalWrite(warningLED, HIGH);
    } else {
      lcd.setCursor(0, 1);
      lcd.print("Status: Normal     ");
      digitalWrite(relay, HIGH);
      digitalWrite(buzzer, LOW);
      digitalWrite(warningLED, LOW);
    }
  }
}
