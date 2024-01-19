#include <WiFi.h>
#include "credentials.h"
#include <InfluxDbClient.h>
#include "time.h"
#include <esp_task_wdt.h>
#include <WebServer.h>
#include <ElegantOTA.h>
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include <CircularBuffer.h>

#define fwversion "20240116_1658"

#define CUTOFFFREQUENCY 35.0 //in hertz
#define WIFI_RECONNECT_TIMEOUT_S 30
#define TIME_SYNC_INTERVAL_S 300
#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"

const char* esp_hostname = "ESP32_Seismometer01";
#define DEVICE "Sensor01"


#define inputPinX1000 36
#define inputPinX100 39
#define inputPinX10 34



typedef struct{
   unsigned long long timestamp;
   int value;
}Data;

CircularBuffer<Data,4000> pointBuffer;


float offsetX10 = -75.0;
float offsetX100 = -75.0;
float offsetX1000 = -75.0;

float filteredX10 = 0;
float filteredX100 = 0;
float filteredX1000 = 0;

float PERIOD_READ_US;

unsigned long lastReadTime;
unsigned long lastWriteTime;
unsigned long lastTimeSyncTime;

float alpha; //for lowpass;

TaskHandle_t Task1;
TaskHandle_t Task2;
xQueueHandle xQueue;

esp_adc_cal_characteristics_t adc1_chars;

InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);
WebServer server(80);

void syncToNTP() {
  if ((unsigned long)(millis() - lastTimeSyncTime) > TIME_SYNC_INTERVAL_S * 1000) {
    timeSync(TZ_INFO, "0.at.pool.ntp.org", "1.at.pool.ntp.org");
    lastTimeSyncTime = millis();
  }
}



void otaSetup() {

  server.on("/", []() {
    server.send(200, "text/plain", "Device: " + String(esp_hostname) + ", Firmware Version: " + String(fwversion) + ", Wifi Signal: " + String(WiFi.RSSI()) + "\n" + 
    "Sample Interval: " + String(PERIOD_READ_US) + ", CUTOFFFREQUENCY: " + String(CUTOFFFREQUENCY) + "\n");
  });
  
  ElegantOTA.setAuth(otaUser, otaPass);
  ElegantOTA.onEnd([](bool success) {
    if (success) {
      Serial.println("OTA update completed successfully.");
      // Add success handling here.
    } else {
      Serial.println("OTA update failed.");
      // Add failure handling here.
    }
  });
  ElegantOTA.begin(&server);
  server.begin();
}

unsigned long lastWifiConnectionAttempt = -WIFI_RECONNECT_TIMEOUT_S * 1000; //don't delay first connection attempt
bool printedWifiInfo = false;
bool ntpSynced = false;
void setupWifi() {
  if (WiFi.status()!= WL_CONNECTED) {
    if ((unsigned long)(millis() - lastWifiConnectionAttempt) > WIFI_RECONNECT_TIMEOUT_S * 1000) {
      printedWifiInfo = false;
      Serial.print("Connecting to ");
      Serial.println(WIFI_SSID);
      
      WiFi.mode(WIFI_STA);
      WiFi.hostname(esp_hostname);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
      Serial.println();
  
      lastWifiConnectionAttempt = millis();
    }
  } else {
    if (!printedWifiInfo){
      Serial.println("Wi-Fi Connected");
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP());
      timeSync(TZ_INFO, "0.at.pool.ntp.org", "1.at.pool.ntp.org");
      ntpSynced = true;
      printedWifiInfo = true;
    }
  }
}

void setup() {
  Serial.begin(500000);
  setupWifi();
  otaSetup();
  float sampleFrequency =  float(CUTOFFFREQUENCY) * 4.0;
  PERIOD_READ_US = ((1.0/float(sampleFrequency)) * 1000.0 * 1000.0); //Reading period microseconds


  float tau = 1.0 / (2.0 * PI * (float)CUTOFFFREQUENCY);
  float readPeriodInSeconds = PERIOD_READ_US / 1000.0 / 1000.0;
  alpha = readPeriodInSeconds / (readPeriodInSeconds + tau); 
  
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 0, &adc1_chars);
  
  xQueue = xQueueCreate(1000, sizeof(Data));

 
  client.setHTTPOptions(HTTPOptions().connectionReuse(true));
  client.setWriteOptions(WriteOptions().writePrecision(WritePrecision::MS).batchSize(50).useServerTimestamp(false));
  
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
    if (!ntpSynced) {
      
      continue;
    }
    if ((unsigned long)(micros() - lastReadTime) >= PERIOD_READ_US * 0.95) {
      unsigned long delayLeft = PERIOD_READ_US - (micros() - lastReadTime);
      if (delayLeft < PERIOD_READ_US) {
        delayMicroseconds(delayLeft);
      }
      dataPoint.timestamp = getMillis();
      int inputValue = int(readInputs());
      lastReadTime = micros();  
      
      //Serial.println(inputValue);
      dataPoint.value = inputValue;
      xStatus = xQueueSendToBack( xQueue, &dataPoint, xTicksToWait );
    }
  }
}


unsigned long long getMillis() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return ((unsigned long long)((tv.tv_sec * 1000LL) + (tv.tv_usec / 1000LL)));
}


float mapfloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}


float mapOutMiddle(float middle, float value) {
  if (value <= middle) {
    return mapfloat(value, -1000.0, middle, -1000.0 , 0.0);
  } else {
    return mapfloat(value, middle, 1000.0, 0.0, 1000.0);
  }
}

float cutOffFilter(float raw, float filteredValue) {
  return (float)alpha * (float)raw + (float)(1.0 - alpha) * (float)filteredValue;
}

float readInputs() {
  float x1000  =  mapfloat(float(adc1_get_raw(ADC1_CHANNEL_0)),0.0,4095.0,-1000.0,1000.0);
  x1000 = cutOffFilter(x1000, filteredX1000);
  filteredX1000 = x1000;
  x1000 = mapOutMiddle(offsetX10,x1000); 


  
  float x100   =  mapfloat(float(adc1_get_raw(ADC1_CHANNEL_3)),0.0,4095.0,-1000.0,1000.0);
  x100 = cutOffFilter(x100, filteredX100);
  filteredX100 = x100;
  x100 = mapOutMiddle(offsetX100,x100); 


  float x10    =  mapfloat(float(adc1_get_raw(ADC1_CHANNEL_6)),0.0,4095.0,-1000.0,1000.0);
  x10 = cutOffFilter(x10, filteredX10);
  filteredX10 = x10;
  x10 = mapOutMiddle(offsetX1000,x10); 

  if (x1000 >= 750 || x1000 <= -750) {
    if (x100 >= 750 || x100 <= -750) {
      return x10*100.0;
    } else {
      return x100*10.0;
    }
  } else {
    return x1000;
  }
}


void postToInflux(void * parameter) {
  esp_task_wdt_add(NULL);
  const TickType_t xTicksToWait = pdMS_TO_TICKS(2);
  BaseType_t xStatus;

  while(true) {
    esp_task_wdt_reset();
    syncToNTP();

    setupWifi();
    otaLoop();
    
    Data dataPoint;
    
    xStatus = xQueueReceive( xQueue, &dataPoint, xTicksToWait );
    while(xStatus == pdPASS) {
      esp_task_wdt_reset();
      pointBuffer.push(dataPoint); //we send it from the queue to a circular buffer, so we can fifo
      xStatus = xQueueReceive( xQueue, &dataPoint, xTicksToWait );
    }
    //Serial.println("Pointbuffer Size: " + String(pointBuffer.size()));

    while (!pointBuffer.isEmpty() && WiFi.status()== WL_CONNECTED) {
      esp_task_wdt_reset();
      Data dataPoint = pointBuffer.first();
      Point seismoPoint("Seismometer");
      seismoPoint.addTag("device", DEVICE);
      seismoPoint.addField("reading", dataPoint.value);
      seismoPoint.setTime(dataPoint.timestamp);

      //Serial.print("Writing: ");
      //Serial.println(seismoPoint.toLineProtocol());
      if (!client.writePoint(seismoPoint)) {
        Serial.print("InfluxDB write failed: ");
        Serial.println(client.getLastErrorMessage());
        break;
      } else {
        pointBuffer.shift(); //only on success shift it.
      }
    }
  }
}

void otaLoop() {
  server.handleClient();
  ElegantOTA.loop();
}


void loop() {
  esp_task_wdt_reset();
  delay(10);
}
