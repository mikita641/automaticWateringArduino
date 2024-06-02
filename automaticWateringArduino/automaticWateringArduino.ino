// Подключаем библиотеки
#include <WiFi.h>
#include <AsyncUDP.h>
#include <ESPmDNS.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

#define NBOARDS 3
#define SOIL_MOISTURE_SEMSOR 25
#define MOTOR_PIN 13
#define MAX_VAL 80
#define MIN_VAL 20

struct multidata {
  /* Номер платы (необходим для быстрого доступа по индексу
    в массиве структур) */
  uint8_t num;
  // Текущие millis платы
  unsigned long millis;
  /* В структуру можно добавлять элементы
    например, ip-адрес текущей платы:*/
  IPAddress boardIP;
  // или показания датчика:
  uint16_t sensor;
};

multidata data[NBOARDS] {0};
const char* master_host = "esp32master";
const char* slave_host = "esp32slave";
const char* SSID = "asus_18";
const char* PASSWORD = "10146859";
int status = WL_IDLE_STATUS;
const uint16_t PORT = 49152;
const unsigned int NUM = 0;
const unsigned int NUMSLAVE = 1;

AsyncUDP udp;

void parsePacketMaster(AsyncUDPPacket packet)
{
  // Преобразуем указатель на данные к указателю на структуру
  const multidata* tmp = (multidata*)packet.data();

  // Вычисляем размер данных (ожидаем получить размер в один элемент структур)
  const size_t len = packet.length() / sizeof(data[0]);

  // Если указатель на данные не равен нулю и размер данных больше нуля...
  if (tmp != nullptr && len > 0) {

    // Записываем порядковый номер платы
    data[tmp->num].num = tmp->num;
    // Записываем текущие millis платы
    data[tmp->num].millis = tmp->millis;
    // Записываем IP адрес
    data[tmp->num].boardIP = tmp->boardIP;
    // Записываем показания датчика
    data[tmp->num].sensor = tmp->sensor;

    // Отправляем данные всех плат побайтово
    packet.write((uint8_t*)data, sizeof(data));
  }
}

void parsePacketSlave(AsyncUDPPacket packet)
{
  const multidata* tmp = (multidata*)packet.data();

  // Вычисляем размер данных
  const size_t len = packet.length() / sizeof(data[0]);

  // Если адрес данных не равен нулю и размер данных больше нуля...
  if (tmp != nullptr && len > 0) {

    // Проходим по элементам массива
    for (size_t i = 0; i < len; i++) {

      // Если это не ESP на которой выполняется этот скетч
      if (i != NUMSLAVE) {
        // Обновляем данные массива структур
        data[i].num = tmp[i].num;
        data[i].millis = tmp[i].millis;
        data[i].boardIP = tmp[i].boardIP;
        data[i].sensor = tmp[i].sensor;
      }
    }
  }
}

void wifiEsp32Master() {
  // Записываем адрес текущей платы в элемент структуры
  data[NUM].boardIP = WiFi.softAPIP();

  if (!MDNS.begin(master_host)) {
    Serial.println(data[NUM].boardIP);
  }

  // Инициируем сервер
  if (udp.listen(PORT)) {

    // вызываем callback функцию при получении пакета
    udp.onPacket(parsePacketMaster);
  }
  Serial.println("Сервер запущен.");
}

void wifiEsp32Slave() {
  data[NUMSLAVE].num = NUMSLAVE;
  Serial.print("Подключаем к WiFi");
  while ( status != WL_CONNECTED) {
    Serial.print("Attempting to connect to WEP network, SSID: ");
    Serial.println(SSID);
    Serial.println(status);
    status = WiFi.begin(SSID, PASSWORD);

    // wait 10 seconds for connection:
    delay(10000);
  }
  Serial.println();

  // Записываем адрес текущей платы в элемент структуры
  data[NUMSLAVE].boardIP = WiFi.localIP();

  // Инициируем mDNS с именем "esp32slave" + номер платы
  if (!MDNS.begin(String(slave_host + NUMSLAVE).c_str())) {
    Serial.println("не получилось инициировать mDNS");
  }

  // Узнаём IP адрес платы с UDP сервером
  IPAddress server = MDNS.queryHost(master_host);

  // Если удалось подключиться по UDP
  if (udp.connect(server, PORT)) {

    Serial.println("UDP подключён");

    // вызываем callback функцию при получении пакета
    udp.onPacket(parsePacketSlave);
  }
}
/*
   true - ESP32 в реживе master
   false - ESP32 в реживе slave
*/
void wifiClient(bool clientMode) {
  switch (clientMode) {
    case true:
      wifiEsp32Master();
      break;
    case false:
      wifiEsp32Slave();
      break;
  };
}

void SendingData(bool mode) {
  switch (mode) {
    case false:
      udp.broadcastTo((uint8_t*)&data[NUM], sizeof(data[0]), PORT); // Отправляем данные этой платы побайтово
      break;
    case true:
      break;
  }
}

void timerServer() {
  // Записываем текущие millis в элемент массива, соответствующий данной плате
  data[NUM].millis = millis();
  // Записываем показания датчика (для демонстрации это просто millis / 10)
  data[NUM].sensor = millis() / 10;
}

void testConnected() {
  for (size_t i = 0; i < NBOARDS; i++) {
    Serial.print("IP адрес платы: ");
    Serial.print(data[i].boardIP);
    Serial.print(", порядковый номер: ");
    Serial.print(data[i].num);
    Serial.print(", текущие millis: ");
    Serial.print(data[i].millis);
    Serial.print(", значение датчика: ");
    Serial.print(data[i].sensor);
    Serial.print("; ");
    Serial.println();
  }
  Serial.println("----------------------------");

  delay(1000);
}

int data_in_percentage(int sensor, bool sensor_mode) {
  int val = analogRead(sensor);
  switch (sensor_mode) {
    case true:
      val = map(val, 0, 1023, 0, 100);
      break;
    case false:
      val = map(val, 1023, 0, 0, 100);
      break;
  };
  return val;
}

void motor_mode(int sensor, int maxVal, int minVal, int motorPin, int sensor_mode) {
  if (data_in_percentage(sensor, sensor_mode) >= maxVal) {
    digitalWrite(motorPin, 0);
  }
  if (data_in_percentage(sensor, sensor_mode) <= minVal) {
    digitalWrite(motorPin, 1);
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.softAP(SSID, PASSWORD); // Инициируем WiFi
  wifiClient(false);
  pinMode(MOTOR_PIN, OUTPUT);
}

void loop()
{
  timerServer();
  SendingData(false);
  testConnected();
  motor_mode(SOIL_MOISTURE_SEMSOR, MAX_VAL, MIN_VAL, MOTOR_PIN, true);
}
