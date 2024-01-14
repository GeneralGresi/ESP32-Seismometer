#include <LiquidCrystal_I2C.h> 


LiquidCrystal_I2C lcd(0x27, 16, 2);

int count = 0;

unsigned long lastInterrupt;
int debounceTime = 5;


void setup() {
  // put your setup code here, to run once:
  pinMode(2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(2), pin_ISR, FALLING);
  lcd.init(); //initialize the lcd
  lcd.backlight(); //open the backlight 
  lcd.setCursor(0, 0);
  lcd.print("Count: 0");
  lastInterrupt = millis();

}

void loop() {
  // put your main code here, to run repeatedly:
  //lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Count: ");
  lcd.print(count);
  delay(100);
}

void pin_ISR() {
  if (millis() - lastInterrupt > debounceTime) {
    
    count++;
    lastInterrupt = millis();
  }
}
