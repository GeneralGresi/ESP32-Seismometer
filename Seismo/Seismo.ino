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

#define PERIOD_READ_US 4500
#define PERIOD_READ_US_FULL 5000 //Reading period 
#define PERIOD_WRITE 10                //WRITING period 
#define CUTOFFFREQUENCY 35 //in hertz

#define WIFI_RECONNECT_TIMEOUT_S 30

const char* esp_hostname = "ESP32_Seismometer01";


#define DEVICE "Sensor01"

#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"


#define inputPinX1000 36
#define inputPinX100 39
#define inputPinX10 34

typedef struct{
   unsigned long long timestamp;
   int value;
}Data;


esp_adc_cal_characteristics_t adc1_chars;


const int WINDOW_SIZE  = 500;

int readingsX1000 [WINDOW_SIZE];
int readingsX100 [WINDOW_SIZE];
int readingsX10 [WINDOW_SIZE];
int readIndexX1000  = 0;
int readIndexX100  = 0;
int readIndexX10 = 0;
int totalX1000 = 0;
int totalX100 = 0;
int totalX10 = 0;


bool averageX1000Settled = false;
bool averageX100Settled = false;
bool averageX10Settled = false;



int filteredX10 = 0;
int filteredX100 = 0;
int filteredX1000 = 0;



unsigned long lastReadTime;
unsigned long lastWriteTime;
unsigned long lastTimeSyncTime;

float alpha; //for lowpass;
  


long avgX1000(int value) {
  totalX1000 = totalX1000 - readingsX1000[readIndexX1000];       // Remove the oldest entry from the sum
  readingsX1000[readIndexX1000] = value;           // Add the newest reading to the window
  totalX1000 = totalX1000 + value;                 // Add the newest reading to the sum
  readIndexX1000 = (readIndexX1000+1) % WINDOW_SIZE;   // Increment the index, and wrap to 0 if it exceeds the window size
  if (readIndexX1000 == 0 ) { // we wen't one full WINDOW_SIZE around
    averageX1000Settled = true;
  }

  return totalX1000 / WINDOW_SIZE;      // Divide the sum of the window by the window size for the resul
}

long avgX100(int value) {
  totalX100 = totalX100 - readingsX100[readIndexX100];       // Remove the oldest entry from the sum
  readingsX100[readIndexX100] = value;           // Add the newest reading to the window
  totalX100 = totalX100 + value;                 // Add the newest reading to the sum
  readIndexX100 = (readIndexX100+1) % WINDOW_SIZE;   // Increment the index, and wrap to 0 if it exceeds the window size
  if (readIndexX100 == 0 ) { // we wen't one full WINDOW_SIZE around
    averageX100Settled = true;
  }

  return totalX100 / WINDOW_SIZE;      // Divide the sum of the window by the window size for the resul
}

long avgX10(int value) {
  totalX10 = totalX10 - readingsX10[readIndexX10];       // Remove the oldest entry from the sum
  readingsX10[readIndexX10] = value;           // Add the newest reading to the window
  totalX10 = totalX10 + value;                 // Add the newest reading to the sum
  readIndexX10 = (readIndexX10+1) % WINDOW_SIZE;   // Increment the index, and wrap to 0 if it exceeds the window size
  if (readIndexX10 == 0 ) { // we wen't one full WINDOW_SIZE around
    averageX10Settled = true;
  }

  return totalX10 / WINDOW_SIZE;      // Divide the sum of the window by the window size for the resul
}


TaskHandle_t Task1;
TaskHandle_t Task2;
xQueueHandle xQueue;

InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);

WebServer server(80);


CircularBuffer<Data,4000> pointBuffer;


void syncToNTP() {
  if ((unsigned long)(millis() - lastTimeSyncTime) > 3600000) { //Sync every hour
    timeSync(TZ_INFO, "0.at.pool.ntp.org", "1.at.pool.ntp.org");
    lastTimeSyncTime = millis();
  }
}



void otaSetup() {

  server.on("/", []() {
    server.send(200, "text/plain", "Device: " + String(esp_hostname) + ", Firmware Version: " + String(fwversion) + ", Wifi Signal: " + String(WiFi.RSSI()));
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

  float tau = 1.0 / (2.0 * PI * (float)CUTOFFFREQUENCY);
  float readPeriodInSeconds = PERIOD_READ_US_FULL / 1000.0 / 1000.0;
  alpha = readPeriodInSeconds / (readPeriodInSeconds + tau); 
  
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 0, &adc1_chars);
  
  xQueue = xQueueCreate(1000, sizeof(Data));

 
  client.setHTTPOptions(HTTPOptions().connectionReuse(true));
  client.setWriteOptions(WriteOptions().writePrecision(WritePrecision::MS).batchSize(200).useServerTimestamp(false));
  
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
    if ((unsigned long)(micros() - lastReadTime) >= PERIOD_READ_US) {
      unsigned long delayLeft = PERIOD_READ_US_FULL - 100 - (micros() - lastReadTime);
      if (delayLeft < PERIOD_READ_US_FULL) {
        delayMicroseconds(delayLeft);
      }
      dataPoint.timestamp = getMillis();
      int inputValue = readInputs();
      if (inputValue == -1) { //invalid value, continue loop
        lastReadTime = micros();  
        continue;
      }
      //Serial.println(inputValue);
      dataPoint.value = inputValue;
      xStatus = xQueueSendToBack( xQueue, &dataPoint, xTicksToWait );
      lastReadTime = micros();  
    }
  }
}


unsigned long long getMillis() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return ((unsigned long long)((tv.tv_sec * 1000LL) + (tv.tv_usec / 1000LL)));
}


int mapOutMiddle(int middle, int value) {
  if (value <= middle) {
    return map(value, -1000, middle, -1000 , 0);
  } else {
    return map(value, middle, 1000, 0, 1000);
  }
}

int cutOffFilter(int raw, int filteredValue) {
  return (float)alpha * (float)raw + (float)(1.0 - alpha) * (float)filteredValue;
}

int readInputs() {
  int x1000  =  map(adc1_get_raw(ADC1_CHANNEL_0),0,4095,-1000,1000);
  
  x1000 = cutOffFilter(x1000, filteredX1000);
  filteredX1000 = x1000;
  x1000 = mapOutMiddle(avgX1000(x1000),x1000); 


  
  int x100   =  map(adc1_get_raw(ADC1_CHANNEL_3),0,4095,-1000,1000);
  x100 = cutOffFilter(x100, filteredX100);
  filteredX100 = x100;
  x100 = mapOutMiddle(avgX100(x100),x100); 


  int x10    =  map(adc1_get_raw(ADC1_CHANNEL_6),0,4095,-1000,1000);
  x10 = cutOffFilter(x10, filteredX10);
  filteredX10 = x10;
  x10 = mapOutMiddle(avgX10(x10),x10); 

  if (!averageX10Settled || !averageX100Settled || !averageX1000Settled) {
    return -1; //if the averages are not yet settled, return an invalid value
  }

  if (x1000 >= 1000 || x1000 <= -1000) {
    if (x100 >= 1000 || x100 <= -1000) {
      return x10*100;
    } else {
      return x100*10;
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
