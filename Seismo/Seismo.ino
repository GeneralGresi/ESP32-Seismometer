#include <WiFi.h>
#include "credentials.h"
#include <InfluxDbClient.h>


#define WIFI_TIMEOUT_DEF 30
#define PERIOD_READ_MICROSECS 100                //Reading period 
#define PERIOD_WRITE 100                //WRITING period 


#define DEVICE "Sensor01"

#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"


#define inputPinX1000 36
#define inputPinX100 39
#define inputPinX10 34


InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);

Point seismo("Seismometer");


TaskHandle_t Task1;

unsigned long lastReadTime;
unsigned long lastWriteTime;


unsigned long lastTimeSyncTime;
void syncToNTP() {
  if ((unsigned long)(millis() - lastTimeSyncTime) > 3600000) { //Sync every hour
    timeSync(TZ_INFO, "0.at.pool.ntp.org", "1.at.pool.ntp.org");
    lastTimeSyncTime = millis();
  }
}


void software_Reset() // Restarts program from beginning but does not reset the peripherals and registers
{
  Serial.println("Software reset");
  delay(1000);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);


  Serial.println("Connecting to Wi-Fi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int wifi_loops = 0;
  int wifi_timeout = WIFI_TIMEOUT_DEF;
  while (WiFi.status() != WL_CONNECTED) {
    wifi_loops++;
    Serial.print(".");
    delay(500);
    if (wifi_loops > wifi_timeout) software_Reset();
  }
  Serial.println();
  Serial.println("Wi-Fi Connected");
  timeSync(TZ_INFO, "0.at.pool.ntp.org", "1.at.pool.ntp.org");

  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());


  seismo.addTag("device", DEVICE);
  
  
  delay(50);

  client.setWriteOptions(WriteOptions().writePrecision(WritePrecision::US));
  client.setWriteOptions(WriteOptions().batchSize(1000));
  client.setHTTPOptions(HTTPOptions().connectionReuse(true));
  
  lastReadTime=micros();
  lastWriteTime = lastTimeSyncTime = millis();

  xTaskCreatePinnedToCore(
      PostToInflux, /* Function to implement the task */
      "PostToInflux", /* Name of the task */
      10000,  /* Stack size in words */
      NULL,  /* Task input parameter */
      0,  /* Priority of the task */
      &Task1,  /* Task handle. */
      1); /* Core where the task should run */
}

void PostToInflux( void * parameter) {
  while (true) {
      if ((unsigned long)(millis() - lastWriteTime) > PERIOD_WRITE) {
        client.flushBuffer();
        lastWriteTime = millis();
      }
  }
}


int readInputs() {
  int x1000 = analogRead(inputPinX1000);
  if (x1000 == 1023) {
    int x100 = analogRead(inputPinX100);
    if (x100 == 1023) {
      int x10 = analogRead(inputPinX10);
      return x10 * 100;
    } else {
      return x100 * 10;
    }
  } else {
    return x1000;
  }
}



void loop() {
  syncToNTP();

  if ((unsigned long)(micros() - lastReadTime) > PERIOD_READ_MICROSECS) {
    int inputValue = readInputs();
    seismo.clearFields();
    seismo.addField("reading", inputValue);
    client.writePoint(seismo);
    lastReadTime = micros();
  }
}
