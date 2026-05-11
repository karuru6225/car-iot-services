#include <Arduino.h>
#define RELAY_0_PIN 11
#define RELAY_1_PIN 13
#define RELAY_2_PIN 15

void setup()
{
  Serial.begin(115200);
  pinMode(RELAY_0_PIN, OUTPUT);
  pinMode(RELAY_1_PIN, OUTPUT);
  pinMode(RELAY_2_PIN, OUTPUT);
  delay(1000);
  Serial.println("blank");
}

static long int lastTime = 0;

void loop()
{
  if (millis() - lastTime >= 1000)
  {
    Serial.println("blank loop");
    lastTime = millis();
    digitalWrite(RELAY_0_PIN, digitalRead(RELAY_0_PIN) == HIGH ? LOW : HIGH);
    digitalWrite(RELAY_1_PIN, digitalRead(RELAY_1_PIN) == HIGH ? LOW : HIGH);
    digitalWrite(RELAY_2_PIN, digitalRead(RELAY_2_PIN) == HIGH ? LOW : HIGH);
  }
  delay(1000);
  Serial.println("blank loop");
}
