#include <WiFi.h>
#include "credentials.h"
#include <InfluxDbClient.h>
#include "time.h"
#include <esp_task_wdt.h>


#define WIFI_TIMEOUT_DEF 30
#define PERIOD_READ_MICROSECS 200                //Reading period 
#define PERIOD_WRITE 10                //WRITING period 


#define DEVICE "Sensor01"

#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"


#define inputPinX1000 36
#define inputPinX100 39
#define inputPinX10 34

typedef struct{
   unsigned long long timestamp;
   int value;
}Data;




unsigned long lastReadTime;
unsigned long lastWriteTime;
unsigned long lastTimeSyncTime;
  




TaskHandle_t Task1;
TaskHandle_t Task2;
xQueueHandle xQueue;

InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);


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
  delay(50);

  
  xQueue = xQueueCreate(100, sizeof(Point));

 
  client.setHTTPOptions(HTTPOptions().connectionReuse(true));
  client.setWriteOptions(WriteOptions().writePrecision(WritePrecision::US).batchSize(100).useServerTimestamp(false));
  
  lastReadTime=micros();
  lastTimeSyncTime = lastWriteTime = millis();

  esp_task_wdt_init(10, true); 
  
  
  xTaskCreatePinnedToCore(
      dataToQueue, /* Function to implement the task */
      "ReadDataToQueue", /* Name of the task */
      10000,  /* Stack size in words */
      NULL,  /* Task input parameter */
      0,  /* Priority of the task */
      &Task1,  /* Task handle. */
      0); /* Core where the task should run */
      
  xTaskCreatePinnedToCore(
      postToInflux, /* Function to implement the task */
      "postToInflux", /* Name of the task */
      10000,  /* Stack size in words */
      NULL,  /* Task input parameter */
      2,  /* Priority of the task */
      &Task2,  /* Task handle. */
      1); /* Core where the task should run */

}


void dataToQueue( void * parameter) {
  esp_task_wdt_add(NULL);
  const TickType_t xTicksToWait = pdMS_TO_TICKS(100);
  BaseType_t xStatus;
  Data dataPoint;
  while (true) {
      esp_task_wdt_reset();
      if ((unsigned long)(micros() - lastReadTime) >= PERIOD_READ_MICROSECS) {
        lastReadTime = micros();
        dataPoint.timestamp = getMicros();
        int inputValue = readInputs();
        dataPoint.value = inputValue;
        xStatus = xQueueSendToBack( xQueue, &dataPoint, xTicksToWait );  
    }
  }
}


unsigned long long getMicros() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return ((unsigned long long)((tv.tv_sec * 1000000LL) + tv.tv_usec));
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



void postToInflux(void * parameter) {
  esp_task_wdt_add(NULL);
  const TickType_t xTicksToWait = pdMS_TO_TICKS(100);
  BaseType_t xStatus;
  Point point("Seismometer");
  point.addTag("device", DEVICE);
  while(true) {
    esp_task_wdt_reset();
    syncToNTP();
    Data dataPoint;
    xStatus = xQueueReceive( xQueue, &dataPoint, xTicksToWait );
    if(xStatus == pdPASS) {
      point.clearFields();
      point.addField("reading", dataPoint.value);
      point.setTime(dataPoint.timestamp);
      //Serial.println(dataPoint.timestamp + ": " + dataPoint.value);
      client.writePoint(point);
    }
    if ((unsigned long)(millis() - lastWriteTime) > PERIOD_WRITE) {
      //client.flushBuffer();
      lastWriteTime = millis();
    }
  }
}

void loop() {
  vTaskDelete(NULL);
}
