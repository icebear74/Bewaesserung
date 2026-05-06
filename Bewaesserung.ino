#include <Arduino.h>

#include <Wire.h>
#include <U8g2lib.h>



U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

void setup() {
  u8g2.begin();
}

void loop() {
 u8g2.clearBuffer();
   u8g2.setFont(u8g2_font_ncenB08_tr);	// choose a suitable font

 u8g2.drawStr( 20, 20, "test");
 u8g2.sendBuffer();
delay(1000);
}
