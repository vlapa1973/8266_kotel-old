/*
    Система контроля температуры для управления котлом
    * СЕРВЕР * = vlapa = 20221127 - 20230417
    v.016

    test2
*/
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#define ONE_WIRE_BUS 2
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <PubSubClient.h>

//-----------------------------------
const char ssid[] = "link";
// const char ssid[] = "MikroTik-2-ext";
const char pass[] = "dkfgf#*12091997";

// const char ssid[] = "TP-Link_77C4";
// const char pass[] = "14697272";

// const char *mqtt_server = "94.103.87.97";
const char *mqtt_server = "178.20.46.157";
const uint16_t mqtt_port = 1883;
const char *mqtt_client = "GasBoilerServer_Villa";
const char *mqtt_client2 = "GasBoiler_Villa";
// const char *mqtt_client = "GasBoilerServer_Maxx";
// const char *mqtt_client2 = "GasBoiler_Maxx";
const char *mqtt_user = "mqtt";
const char *mqtt_pass = "qwe#*1243";

const char *inTopicTempRoom = "/Temp_Room";
const char *inTopicTempDS18b20 = "/Room_ds18b20";
const char *inTopicReboot = "/Reboot";
const char *outTopicIP = "/IP_Boiler";
const char *outTopicHeating = "/Heating";
const char *outTopicTempDS18b20 = "/Kotel_ds18b20";
const char *outTopicUpTime = "/UpTime";

const uint8_t sensorNumRoom = 99; //  номер датчика температуры комнаты
const uint8_t pinReley = 0;
const uint8_t pinDS18b20 = 2;

float temperRoom = 0.0;
float dataTempBase = 23.0;  //  температура в комнате по умолчанию
float dataTempKotel = 60.0; //  температура теплоносителя по умолчанию
float temperRoom_ds18b20 = 0.0;
float temperBoiler_ds18b20 = 0.0;
uint32_t millisOld = 0;
uint32_t pauseTimeTemp = 30000;
float gisteresis = 0.1;
bool conditionHeater = true;
float k = 0.1; // коэффициент фильтрации, 0.0-1.0

WiFiClient espClient;
PubSubClient client(espClient);

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

//-----------------------------------
inline bool mqtt_subscribe(PubSubClient &client, const String &topic)
{
  Serial.print("Subscribing to: ");
  Serial.println(topic);
  return client.subscribe(topic.c_str());
}

//-----------------------------------
inline bool mqtt_publish(PubSubClient &client, const String &topic, const String &value)
{
  Serial.print("Publishing topic ");
  Serial.print(topic);
  Serial.print(" = ");
  Serial.println(value);
  return client.publish(topic.c_str(), value.c_str());
}

//-----------------------------------
void mqttDataOut()
{
  String topic = "/";
  topic += mqtt_client2;
  topic += outTopicIP;
  mqtt_publish(client, topic, WiFi.localIP().toString());

  topic = "/";
  topic += mqtt_client2;
  topic += outTopicUpTime;

  uint32_t timer = millis() / 1000;
  uint32_t timeSec = (timer % 3600) % 60;
  uint32_t timeMin = (timer % 3600) / 60;
  uint32_t timeHour = timer / 3600 % 24;
  uint32_t timeDay = timer / 60 / 60 / 24;

  String s = "";
  s += timeDay;
  s += ":";
  if (timeHour < 10)
    s += "0";
  s += timeHour;
  s += ":";
  if (timeMin < 10)
    s += "0";
  s += timeMin;
  s += ":";
  if (timeSec < 10)
    s += "0";
  s += timeSec;
  mqtt_publish(client, topic, s);

  topic = "/";
  topic += mqtt_client2;
  topic += outTopicHeating;
  mqtt_publish(client, topic, (!conditionHeater) ? "OFF" : "ON");

  topic = "/";
  topic += mqtt_client2;
  topic += inTopicTempRoom;
  mqtt_publish(client, topic, (String)temperRoom);

  topic = "/";
  topic += mqtt_client2;
  topic += outTopicTempDS18b20;
  mqtt_publish(client, topic, (String)temperBoiler_ds18b20);

  topic = "/";
  topic += mqtt_client2;
  topic += inTopicReboot;
  mqtt_publish(client, topic, "0");
}

//-----------------------------------
void reconnect()
{
  uint8_t countMQTT = 10;
  while (!client.connected())
  {
    if (client.connect(mqtt_client, mqtt_user, mqtt_pass))
    {
      Serial.println("MQTT - Ok!");

      String topic = "/";
      topic += mqtt_client2;
      topic += inTopicTempRoom;
      mqtt_subscribe(client, topic);

      topic = "/";
      topic += mqtt_client2;
      topic += inTopicTempDS18b20;
      mqtt_subscribe(client, topic);

      topic = "/";
      topic += mqtt_client2;
      topic += inTopicReboot;
      mqtt_subscribe(client, topic);
    }
    else
    {
      Serial.print("MQTT - FALSE !\t");
      Serial.println(countMQTT);
      delay(1000);
      if (countMQTT)
      {
        --countMQTT;
      }
      else
      {
        Serial.println("ESP - reboot !");
        ESP.restart();
      }
    }
  }
}

//-----------------------------------
void setupWiFi()
{
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println("\n\nSetup WiFi: ");

  WiFi.mode(WIFI_STA);
  uint8_t countWiFi = 20;
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(500);
    if (countWiFi)
    {
      --countWiFi;
    }
    else
    {
      ESP.restart();
    }
  }

  digitalWrite(LED_BUILTIN, HIGH);

  //-----------------------------------
  // индикация IP
  Serial.print("\nWiFi connected !\n");
  Serial.println(WiFi.localIP());

  // индикация силы сигнала
  int16_t dBm = WiFi.RSSI();
  Serial.print("RSSI dBm = ");
  Serial.println(dBm);
  int16_t quality = 2 * (dBm + 100);
  if (quality > 100)
    quality = 100;
  Serial.print("RSSI % = ");
  Serial.println(quality);
  Serial.println();
}

//-----------------------------------
void releWork()
{
  if ((temperRoom - gisteresis) > temperRoom_ds18b20)
  {
    conditionHeater = true;
    digitalWrite(pinReley, conditionHeater);
  }
  else if ((temperRoom + gisteresis) < temperRoom_ds18b20)
  {
    conditionHeater = false;
    digitalWrite(pinReley, conditionHeater);
  }
}

//-----------------------------------
void mqtt_callback(char *topic, byte *payload, unsigned int length)
{
  // for (uint8_t i = 0; i < length; i++)
  // {
  //   Serial.print((char)payload[i]);
  // }
  // Serial.println();
  String temp = "";

  char *topicBody = topic + strlen(mqtt_client2) + 1;
  if (!strncmp(topicBody, inTopicReboot, strlen(inTopicReboot)))
  {
    for (uint8_t i = 0; i < length; i++)
      temp += (char)payload[i];
    if (temp.toInt())
    {
      ESP.restart();
      Serial.println("REBOOT !");
    }
  }
  if (!strncmp(topicBody, inTopicTempRoom, strlen(inTopicTempRoom)))
  {
    for (uint8_t i = 0; i < length; i++)
      temp += (char)payload[i];
    temperRoom = temp.toFloat();

    byte raw[4];
    (float &)raw = temperRoom;
    for (byte i = 0; i < 4; i++)
      EEPROM.write(0 + i, raw[i]);
    EEPROM.commit();

    Serial.print("TempRoom: ");
    Serial.print(temperRoom);
    Serial.print("\t");
    Serial.println(millis() / 1000);
    releWork();
  }
  if (!strncmp(topicBody, inTopicTempDS18b20, strlen(inTopicTempDS18b20)))
  {
    for (uint8_t i = 0; i < length; i++)
      temp += (char)payload[i];
    temperRoom_ds18b20 = temp.toFloat();

    Serial.print("TempDs18b20: ");
    Serial.println(temperRoom_ds18b20);
  }
}

//-----------------------------------
// бегущее среднее
float expRunningAverage(float newVal)
{
  static float filVal = 0;
  filVal += (newVal - filVal) * k;
  return filVal;
}

// медиана на 3 значения со своим буфером
float medianRoom(float newValRoom)
{
  static float bufRoom[3] = {0, 0, 0};
  if (!(bufRoom[0] + bufRoom[1] + bufRoom[2]))
    bufRoom[0] = bufRoom[1] = bufRoom[2] = newValRoom;
  static byte countRoom = 0;
  bufRoom[countRoom] = newValRoom;
  if (++countRoom >= 3)
    countRoom = 0;
  float dataRoom = (max(bufRoom[0], bufRoom[1]) == max(bufRoom[1], bufRoom[2]))
                       ? max(bufRoom[0], bufRoom[2])
                       : max(bufRoom[1], min(bufRoom[0], bufRoom[2]));
  // return expRunningAverage(data);
  return dataRoom;
}

float medianBoiler(float newValBoiler)
{
  static float bufBoiler[3] = {0, 0, 0};
  if (!(bufBoiler[0] + bufBoiler[1] + bufBoiler[2]))
    bufBoiler[0] = bufBoiler[1] = bufBoiler[2] = newValBoiler;
  static byte countBoiler = 0;
  bufBoiler[countBoiler] = newValBoiler;
  if (++countBoiler >= 3)
    countBoiler = 0;
  float dataBoiler = (max(bufBoiler[0], bufBoiler[1]) == max(bufBoiler[1], bufBoiler[2]))
                         ? max(bufBoiler[0], bufBoiler[2])
                         : max(bufBoiler[1], min(bufBoiler[0], bufBoiler[2]));
  // return expRunningAverage(data);
  return dataBoiler;
}

//-----------------------------------
void setup()
{
  Serial.begin(9600);
  Serial.setTimeout(100);
  pinMode(pinReley, OUTPUT);
  digitalWrite(pinReley, LOW);
  pinMode(pinDS18b20, OUTPUT);
  digitalWrite(pinDS18b20, HIGH);

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqtt_callback);

  setupWiFi();
  EEPROM.begin(4);
  byte raw[4];
  for (byte i = 0; i < 4; i++)
    raw[i] = EEPROM.read(0 + i);
  float &num = (float &)raw;
  (num >= 10.0 && num <= 30.0) ? temperRoom = num : temperRoom = dataTempBase;

  // setupWiFi();
  // reconnect();
  // mqttDataOut();

  server.begin();
  httpUpdater.setup(&server);

  // millisOld = millis() - pauseTimeTemp;
}

//-----------------------------------
void loop()
{
  if (WiFi.status() != WL_CONNECTED)
    setupWiFi();

  if (!client.connected())
  {
    reconnect();
    mqttDataOut();
  }

  //-----------------------------------
  if (Serial.available())
  {
    String bufString = Serial.readString(); // читаем как строку
    if (bufString.substring(0, 2).toFloat() == sensorNumRoom)
    {
      temperRoom_ds18b20 = medianRoom(bufString.substring(3, 8).toFloat());

      String topic = "/";
      topic += mqtt_client2;
      topic += inTopicTempDS18b20;
      mqtt_publish(client, topic, (String)temperRoom_ds18b20);

      millisOld = millis();
    }
  }
  //---------------------------------------------
  if (millis() >= millisOld + pauseTimeTemp)
  {
    sensors.requestTemperatures();
    temperBoiler_ds18b20 = medianBoiler(sensors.getTempCByIndex(0));

    if (dataTempKotel <= temperBoiler_ds18b20)
    {
      conditionHeater = true;
      digitalWrite(pinReley, conditionHeater);
    }
    else
    {
      releWork();
    }

    mqttDataOut();
    millisOld = millis();
  }

  //---------------------------------------------
  server.handleClient();
  client.loop();
  delay(1);
}