#include <DHT.h>

// ---------------- PIN DEFINITIONS ----------------

// Node A
#define DHT_A_PIN A0
#define MQ_A_PIN  A1
#define FLAME_A_PIN 3

// Node B
#define DHT_B_PIN 2
#define MQ_B_PIN  A2
#define FLAME_B_PIN 9

// Node C (DHT optional)
#define DHT_C_PIN 6
#define MQ_C_PIN  A3
#define FLAME_C_PIN 10

// ---------------- SENSOR OBJECTS ----------------
DHT dhtA(DHT_A_PIN, DHT11);
DHT dhtB(DHT_B_PIN, DHT11);
DHT dhtC(DHT_C_PIN, DHT11);     // Node C optional DHT

// ---------------- GAS SCALING ----------------
float baseA_raw = 1;
float baseB_raw = 1;
float baseC_raw = 1;

float scaleA = 1;
float scaleB = 1;
float scaleC = 1;

// ----------------------------------------------------
void setup() {
  Serial.begin(9600);

  pinMode(FLAME_A_PIN, INPUT);
  pinMode(FLAME_B_PIN, INPUT);
  pinMode(FLAME_C_PIN, INPUT);

  dhtA.begin();
  dhtB.begin();
  dhtC.begin();   // Even if not connected, no problem

  Serial.println("Calibrating MQ-2 sensors...");

  baseA_raw = calibrateMQ(MQ_A_PIN);
  baseB_raw = calibrateMQ(MQ_B_PIN);
  baseC_raw = calibrateMQ(MQ_C_PIN);

  float baseAvg = (baseA_raw + baseB_raw + baseC_raw) / 3.0;

  scaleA = baseAvg / baseA_raw;
  scaleB = baseAvg / baseB_raw;
  scaleC = baseAvg / baseC_raw;

  Serial.println("Calibration Done!");
  Serial.println("----------------------------------------------");
}

// ----------------------------------------------------
float calibrateMQ(int pin) {
  long sum = 0;
  for (int i = 0; i < 50; i++) {
    sum += analogRead(pin);
    delay(20);
  }
  return (float)sum / 50.0;
}

// ----------------------------------------------------
void loop() {

  // ----- ACTIVE HIGH FLAME LOGIC -----
  int flameA = (digitalRead(FLAME_A_PIN) == HIGH) ? 1 : 0;
  int flameB = (digitalRead(FLAME_B_PIN) == HIGH) ? 1 : 0;
  int flameC = (digitalRead(FLAME_C_PIN) == HIGH) ? 1 : 0;

  // ----- Gas Sensors -----
  int rawA = analogRead(MQ_A_PIN);
  int rawB = analogRead(MQ_B_PIN);
  int rawC = analogRead(MQ_C_PIN);

  // Apply scaling (normalization)
  int gasA = rawA * scaleA;
  int gasB = rawB * scaleB;
  int gasC = rawC * scaleC;

  if (gasA < 0) gasA = 0;
  if (gasB < 0) gasB = 0;
  if (gasC < 0) gasC = 0;

  // ----- DHT -----
  float tempA = dhtA.readTemperature();
  float humA  = dhtA.readHumidity();

  float tempB = dhtB.readTemperature();
  float humB  = dhtB.readHumidity();

  float tempC = dhtC.readTemperature();
  float humC  = dhtC.readHumidity();

  // ----------- PRINT NODE A --------------
  Serial.println("---------------------------------------------------");
  Serial.print("Node A | Flame:");
  Serial.print(flameA);
  Serial.print(" | Gas:");
  Serial.print(gasA);
  Serial.print(" | Temp:");
  Serial.print(tempA);
  Serial.print(" | Hum:");
  Serial.println(humA);

  // ----------- PRINT NODE B --------------
  Serial.print("Node B | Flame:");
  Serial.print(flameB);
  Serial.print(" | Gas:");
  Serial.print(gasB);
  Serial.print(" | Temp:");
  Serial.print(tempB);
  Serial.print(" | Hum:");
  Serial.println(humB);

  // ----------- PRINT NODE C (DHT optional) --------------
  Serial.print("Node C | Flame:");
  Serial.print(flameC);
  Serial.print(" | Gas:");
  Serial.print(gasC);

  // Print ONLY IF DHT connected (valid readings)
  if (!isnan(tempC) && !isnan(humC)) {
    Serial.print(" | Temp:");
    Serial.print(tempC);
    Serial.print(" | Hum:");
    Serial.print(humC);
  }

  Serial.println();

  delay(800);
}
