#include <WiFi.h>
#include "credentials.h"
#include <InfluxDbClient.h>


#define WIFI_TIMEOUT_DEF 30
#define PERIOD_READ_MICROSECS 100                //Counting period 
#define WDT_TIMEOUT 10


#define DEVICE "Sensor01"

#define INFLUXDB_TOKEN "<your influx token>"
#define WIFI_SSID "xxx"
#define WIFI_PASSWORD "xxx"

#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"


#define inputPinX1000 36
#define inputPinX100 39
#define inputPinX10 34


InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);

Point seismo("Seismometer");




unsigned long lastWriteTime;

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

  client.setWriteOptions(WriteOptions().writePrecision(WritePrecision::MS));
  client.setWriteOptions(WriteOptions().batchSize(1000));
  client.setHTTPOptions(HTTPOptions().connectionReuse(true));
  
  lastWriteTime=millis();
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
  if (lastCountTime > micros()) {
    lastCountTime = 0; //Reset counter
  }
  if (micros() > lastCountTime + PERIOD_READ_MICROSECS) {
    int value = readInputs();
    seismo.clearFields();
    seismo.addField("reading", value);
    client.writePoint(seismo);
    lastCountTime = micros();
  }
}
