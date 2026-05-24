#include <Wire.h>
#include <Arduino.h>
#include "HT_SSD1306Wire.h"

// Hardware setup for Heltec V3
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);
int pushButton = 45;
bool switchStatus = false; 

void setup() {
  VextON(); // Power the OLED via Heltec MOSFET [cite: 59]
  delay(100);
  display.init();
  
  showSplashScreen();
  delay(3000); // Allow time for seniors to see the credentials
  pinMode(pushButton, INPUT);
}
void loop(){
  switchStatus = digitalRead(pushButton);
  drawDashboard();
  delay(500);
}
void VextON(void)
{
  pinMode(Vext,OUTPUT);
  digitalWrite(Vext, LOW);
}
void showSplashScreen() {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  
  // HPCL Logo Placeholder (Text-based for now)
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 10, "HPCL MUMBAI");
  
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 30, "Substation Monitor");
  
  display.drawHorizontalLine(20, 45, 88);
  
  // Authorship attribution
  display.drawString(64, 50, "Made by Chirag Kotian");
  display.display();
}

// Variable to simulate sensor/switch status from CAN bus


void drawDashboard() {
  display.clear();
  
  // Header Area
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, "DEVICE: LMP-01");
  display.drawHorizontalLine(0, 12, 128);

  // Status Indication
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 20, "SWITCH STATUS");

  // "Pro-ify" UI: Use inverted colors or bold text for the state
  if (switchStatus) {
    display.fillRect(34, 40, 60, 20); // Draw a solid box
    display.setColor(BLACK);          // Invert text color
    display.drawString(64, 42, "HIGH");
    display.setColor(WHITE);          // Reset color
  } else {
    display.drawRect(34, 40, 60, 20); // Draw an empty box
    display.drawString(64, 42, "LOW");
  }
  
  display.display();
}
