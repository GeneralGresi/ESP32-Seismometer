#include <WiFi.h>
#include "credentials.h"
#include <InfluxDbClient.h>
#include "time.h"
#include <esp_task_wdt.h>
#include "driver/adc.h"
#include "esp_adc_cal.h""


#define WIFI_TIMEOUT_DEF 30
#define PERIOD_READ_US 4500
#define PERIOD_READ_US_FULL 4900 //Reading period 
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


esp_adc_cal_characteristics_t adc1_chars;


const int WINDOW_SIZE  = 100;
const int WINDOW_SIZE_OUT  = 5;

int readingsX1000 [WINDOW_SIZE];
int readingsX100 [WINDOW_SIZE];
int readingsX10 [WINDOW_SIZE];
int readIndexX1000  = 0;
int readIndexX100  = 0;
int readIndexX10 = 0;
int totalX1000 = 0;
int totalX100 = 0;
int totalX10 = 0;


int readingsOut [WINDOW_SIZE_OUT];
int readIndexOut = 0;
int totalOut = 0;


unsigned long lastReadTime;
unsigned long lastWriteTime;
unsigned long lastTimeSyncTime;
  


long smoothX1000(int value) {
  totalX1000 = totalX1000 - readingsX1000[readIndexX1000];       // Remove the oldest entry from the sum
  readingsX1000[readIndexX1000] = value;           // Add the newest reading to the window
  totalX1000 = totalX1000 + value;                 // Add the newest reading to the sum
  readIndexX1000 = (readIndexX1000+1) % WINDOW_SIZE;   // Increment the index, and wrap to 0 if it exceeds the window size

  return totalX1000 / WINDOW_SIZE;      // Divide the sum of the window by the window size for the resul
}

long smoothX100(int value) {
  totalX100 = totalX100 - readingsX100[readIndexX100];       // Remove the oldest entry from the sum
  readingsX100[readIndexX100] = value;           // Add the newest reading to the window
  totalX100 = totalX100 + value;                 // Add the newest reading to the sum
  readIndexX100 = (readIndexX100+1) % WINDOW_SIZE;   // Increment the index, and wrap to 0 if it exceeds the window size

  return totalX100 / WINDOW_SIZE;      // Divide the sum of the window by the window size for the resul
}

long smoothX10(int value) {
  totalX10 = totalX10 - readingsX10[readIndexX10];       // Remove the oldest entry from the sum
  readingsX10[readIndexX10] = value;           // Add the newest reading to the window
  totalX10 = totalX10 + value;                 // Add the newest reading to the sum
  readIndexX10 = (readIndexX10+1) % WINDOW_SIZE;   // Increment the index, and wrap to 0 if it exceeds the window size

  return totalX10 / WINDOW_SIZE;      // Divide the sum of the window by the window size for the resul
}

long smoothOutput(int value) {
  totalOut = totalOut - readingsOut[readIndexOut];       // Remove the oldest entry from the sum
  readingsOut[readIndexOut] = value;           // Add the newest reading to the window
  totalOut = totalOut + value;                 // Add the newest reading to the sum
  readIndexOut = (readIndexOut+1) % WINDOW_SIZE_OUT;   // Increment the index, and wrap to 0 if it exceeds the window size

  return totalOut / WINDOW_SIZE_OUT;      // Divide the sum of the window by the window size for the resul
}



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
  Serial.begin(500000);


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


  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 0, &adc1_chars);
  
  xQueue = xQueueCreate(100, sizeof(Point));

 
  client.setHTTPOptions(HTTPOptions().connectionReuse(true));
  client.setWriteOptions(WriteOptions().writePrecision(WritePrecision::MS).batchSize(100).useServerTimestamp(false));
  
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
    Serial.println(micros() - lastReadTime);
    if ((unsigned long)(micros() - lastReadTime) >= PERIOD_READ_US) {
      unsigned long delayLeft = PERIOD_READ_US_FULL - (micros() - lastReadTime);
      if (delayLeft < 5000) {
        delayMicroseconds(delayLeft);
      }
      dataPoint.timestamp = getMillis();
      Serial.println(dataPoint.timestamp);
      int inputValue = readInputs();
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


int readInputs() {
  int curMiddle = map(adc1_get_raw(ADC1_CHANNEL_7), 0 ,4095,-1000,1000);

  int x1000  =  map(adc1_get_raw(ADC1_CHANNEL_0),0,4095,-1000,1000) - curMiddle;
  int x1000avg = smoothX1000(x1000); 
  x1000 -= x1000avg;

  //x1000 *= 20;
  
  int x100   =  map(adc1_get_raw(ADC1_CHANNEL_3),0,4095,-1000,1000) - curMiddle;
  int x100avg = smoothX100(x100);
  x100 -= x100avg;

  //x100 *= 20;
  
  int x10    =  map(adc1_get_raw(ADC1_CHANNEL_6),0,4095,-1000,1000) - curMiddle;
  int x10avg = smoothX10(x10);
  x10 -= x10avg;

  if (abs(x1000) == 1000) {
    if (abs(x100) == 1000) {
      return smoothOutput(x10 * 100);
    } else {
      return smoothOutput(x100 * 10);
    }
  } else {
    return smoothOutput(x1000);
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
  }
}

void loop() {
  esp_task_wdt_reset();
  if (WiFi.status()!= WL_CONNECTED) software_Reset();
  delay(10);
}
