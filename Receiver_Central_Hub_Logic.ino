// FINAL RECEIVER CODE - INTEGRATING RF, FUSION, GPS (Hardware Serial), and GSM (Software Serial)

#include <RH_ASK.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>

// ==== PIN DEFINITIONS ====
#define RF_RX_PIN 11      // RF Receiver Data Pin (Internal to RH_ASK)
#define GSM_RX_PIN 6      // UNO Pin connected to GSM TX pin (GSM sends data to UNO)
#define GSM_TX_PIN 7      // UNO Pin connected to GSM RX pin (UNO sends data to GSM)
#define BUZZER_PIN 8      // Pin for Local Alarm Buzzer

// --- POWER AND SAFETY REMINDER ---
// WARNING: Connect UNO TX (D7) to GSM RX (GSM) via a 10k/20k Voltage Divider!

// ==== ALERT RECIPIENTS (MUST BE UPDATED) ====
#define FARMER_PHONE "+91xxxxxxxx" 
#define FIRE_STATION_PHONE "+91xxxxxxxx"

// ==== TIMING & FUSION THRESHOLDS ====
#define ALERT_COOLDOWN_MS 300000UL // 5 minutes cooldown before sending a repeat SMS
#define GAS_THRESHOLD 350         // Gas Analog Read (0-1023)
#define FLAME_THRESHOLD 1         // Digital Read

// ==== RF DRIVER ====
// Using custom pin 11 for receive, 2000 baud
RH_ASK rf_driver(2000, RF_RX_PIN);

// ==== GSM SERIAL ====
// Using SoftwareSerial for GSM on D6/D7 to free up hardware serial for GPS
SoftwareSerial gsmSerial(GSM_RX_PIN, GSM_TX_PIN);

// ==== GPS USING HARDWARE SERIAL ====
// GPS TX must be connected to UNO RX (D0)
// GPS RX must be connected to UNO TX (D1)
TinyGPSPlus gps;

// ==== PACKET STRUCT (MUST MATCH TRANSMITTER) ====
struct NodePacket {
  // Node A (Original Node)
  uint8_t flameA;
  int16_t gasA;
  float tempA;
  float humA;
  // Node B (New Node 2)
  uint8_t flameB;
  int16_t gasB;
  float tempB;
  float humB;
  // Node C (New Node 3 - Missing DHT)
  uint8_t flameC;
  int16_t gasC;
  float tempC;
  float humC;
};

NodePacket packet;

// FIRE STATE TRACKING
unsigned long lastAlertA = 0, lastAlertB = 0, lastAlertC = 0;
bool fireA = false, fireB = false, fireC = false;

// ======================================================
// GSM HELPERS
// ======================================================
String readGSM(unsigned long timeout = 350) {
  String r = "";
  unsigned long start = millis();
  while (millis() - start < timeout) {
    while (gsmSerial.available()) r += (char)gsmSerial.read();
  }
  return r;
}

String parseCREG(String s) {
  // Parses AT+CREG? response to human-readable status
  if (s.indexOf("0,1") != -1 || s.indexOf("0,5") != -1) return "Registered"; // 1=Home, 5=Roaming
  if (s.indexOf("0,2") != -1) return "Searching...";
  if (s.indexOf("0,3") != -1) return "Denied";
  return "Not Registered/Fail";
}

void printGSMStatus() {
  Serial.println("\n--- GSM Status ---");

  // Check Network Registration
  gsmSerial.println("AT+CREG?");
  delay(120);
  Serial.print("Network: ");
  Serial.println(parseCREG(readGSM()));

  // Check Signal Quality
  gsmSerial.println("AT+CSQ");
  delay(120);
  String csq = readGSM();
  int pos = csq.indexOf("+CSQ:");
  String lvl = "No Signal";

  if (pos != -1) {
    int rssi = csq.substring(pos + 5).toInt();
    if (rssi > 2 && rssi <= 10) lvl = "Weak";
    else if (rssi > 10 && rssi <= 18) lvl = "OK";
    else if (rssi > 18) lvl = "Excellent";
  }
  Serial.print("Signal: ");
  Serial.println(lvl);
}


// ======================================================
// GPS USING HARDWARE SERIAL (D0/D1)
// ======================================================
String getGPS(bool &validOut) {
  // Feeds raw serial data into the TinyGPS++ parser
  while (Serial.available()) gps.encode(Serial.read());

  if (gps.location.isValid()) {
    validOut = true;
    char buf[40];
    // Formats the Lat/Lon string for the SMS alert
    snprintf(buf, sizeof(buf), "%.6f,%.6f",
              gps.location.lat(), gps.location.lng());
    return String(buf);
  }
  validOut = false;
  return "INVALID (No Lock)";
}


// ======================================================
// SMS SENDING
// ======================================================
void sendSMS(const char* number, String body) {
  // Ensure basic connection is alive
  gsmSerial.println("AT");
  delay(150); readGSM();

  // Set to Text mode
  gsmSerial.println("AT+CMGF=1");
  delay(150); readGSM();

  // Send the recipient number
  gsmSerial.print("AT+CMGS=\"");
  gsmSerial.print(number);
  gsmSerial.println("\"");
  delay(250);

  // Send the body of the message
  gsmSerial.print(body);
  gsmSerial.write(26); // ASCII code for Ctrl+Z (terminates SMS)
  Serial.println("SMS command sent.");
  delay(5000); // Wait for transmission complete
}


// ======================================================
// PRINT NODE READINGS & STATUS
// ======================================================
void printNodes() {
  bool ok;
  String gpsStr = getGPS(ok);
  
  Serial.println("---------------------------------------------------");
  Serial.print("GPS: "); Serial.print(gpsStr);
  Serial.print(" | Satellites: "); Serial.println(gps.satellites.value());
  Serial.println("---------------------------------------------------");

  Serial.print("Node A (Original) | Status: "); 
  Serial.print(fireA ? "FIRE" : "OK");
  Serial.print(" | F/G/T: "); Serial.print(packet.flameA);
  Serial.print("/"); Serial.print(packet.gasA);
  Serial.print("/"); Serial.println(packet.tempA);

  Serial.print("Node B (New 2) | Status: "); 
  Serial.print(fireB ? "FIRE" : "OK");
  Serial.print(" | F/G/T: "); Serial.print(packet.flameB);
  Serial.print("/"); Serial.print(packet.gasB);
  Serial.print("/"); Serial.println(packet.tempB);

  Serial.print("Node C (New 3) | Status: "); 
  Serial.print(fireC ? "FIRE" : "OK");
  Serial.print(" | F/G/T: "); Serial.print(packet.flameC);
  Serial.print("/"); Serial.print(packet.gasC);
  
  // Handling missing DHT for Node C
  if (packet.tempC > -999) {
    Serial.print("/"); Serial.print(packet.tempC);
  } else {
    Serial.print(" / Temp UNAVAILABLE");
  }
  Serial.println();
}


// ======================================================
// FIRE ALERT LOGIC
// ======================================================
void sendFireAlert(char node, String gpsMsg) {
  String msg = "🔥 URGENT FIRE: Node ";
  msg += node;
  msg += " at JIIT Field.";

  // Append Location Data
  if (gpsMsg != "INVALID (No Lock)") {
    msg += "\nGPS: " + gpsMsg;
    msg += "\nMap Link: https://maps.google.com/?q=" + gpsMsg;
  } else {
    msg += "\nWARNING: GPS is NOT fixed. Location unknown.";
  }
  
  sendSMS(FARMER_PHONE, msg);
  sendSMS(FIRE_STATION_PHONE, msg);
  Serial.println("--- SMS Sent to Farmer and Fire Station ---");
}

// Function to check one node's fusion logic
void checkSingleNode(uint8_t flame, int16_t gas, float temp, 
                     unsigned long &lastAlert, bool &fireStatus, char nodeChar) {
  
  unsigned long now = millis();
  
  // Fusion Logic: Must have FLAME AND (HIGH GAS OR HIGH TEMP)
  bool fireDetected = (flame >= FLAME_THRESHOLD) && 
                      (gas >= GAS_THRESHOLD || temp >= 40.0);

  // Ignore data if missing (Node C only)
  if (temp < -998) { // If Temp is -999 (unavailable)
      fireDetected = (flame >= FLAME_THRESHOLD) && (gas >= GAS_THRESHOLD);
  }

  // --- TRIGGER ALERT ---
  if (fireDetected && !fireStatus) {
    Serial.print("\n!!! --- REAL FIRE DETECTED at Node ");
    Serial.print(nodeChar);
    Serial.println(" --- !!!");
    
    // Buzzer ON
    digitalWrite(BUZZER_PIN, HIGH);
    
    // SMS Cooldown Check
    if (now - lastAlert > ALERT_COOLDOWN_MS) {
      bool ok;
      String gpsMsg = getGPS(ok);
      sendFireAlert(nodeChar, gpsMsg);
      lastAlert = now;
    }
    fireStatus = true;
  }
  // --- CLEAR ALERT ---
  else if (!fireDetected && fireStatus) {
    // Clear only if condition is safe AND cooldown is passed
    Serial.print("\n--- ALERT CLEARED for Node ");
    Serial.print(nodeChar);
    Serial.println(" ---");
    fireStatus = false;
    // Keep buzzer on if another node is still firing
  }
}

// Main function called by RF receive
void checkFireAlert() {
  // Node A Check
  checkSingleNode(packet.flameA, packet.gasA, packet.tempA, lastAlertA, fireA, 'A');
  
  // Node B Check
  checkSingleNode(packet.flameB, packet.gasB, packet.tempB, lastAlertB, fireB, 'B');
  
  // Node C Check
  checkSingleNode(packet.flameC, packet.gasC, packet.tempC, lastAlertC, fireC, 'C');

  // Buzzer Control: Turn off only if ALL nodes are clear
  if (!fireA && !fireB && !fireC)
    digitalWrite(BUZZER_PIN, LOW);
}


// ======================================================
// RF RECEIVE LOOP
// ======================================================
void checkRF() {
  uint8_t buf[sizeof(NodePacket)];
  uint8_t len = sizeof(NodePacket);

  if (rf_driver.recv(buf, &len)) {
    if (len == sizeof(NodePacket)) {
      memcpy(&packet, buf, sizeof(NodePacket));

      // Check for fire and update status immediately after receiving
      checkFireAlert();
      
      // Print status/readings
      printNodes();
      printGSMStatus();
      Serial.println("\n---------------------------------------------------");
    }
  }
}


// ======================================================
// SETUP (Runs once)
// ======================================================
void setup() {
  // Use Hardware Serial for GPS (D0/D1)
  Serial.begin(9600); 
  // Use Software Serial for GSM (D6/D7)
  gsmSerial.begin(9600);
  
  rf_driver.init();

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.println("RECEIVER INITIALIZED: Monitoring 3 Nodes. Good luck!");
}


// ======================================================
// MAIN LOOP
// ======================================================
void loop() {
  // The primary loop focuses on continuously checking the RF link
  checkRF();
}
