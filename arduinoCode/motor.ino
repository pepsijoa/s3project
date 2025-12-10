#include <WiFi.h>
#include <WebServer.h>
#include "soc/mcpwm_reg.h"
#include "soc/mcpwm_struct.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
#include "driver/mcpwm.h"

#include <HardwareSerial.h>


#include <DW1000Ng.hpp>
#include <SPI.h>


// ==========================================
// 1. Wi-Fi 설정 
// ==========================================
const char* ssid = "GOODMILK";      // 와이파이 이름
const char* password = "kTw2530!"; // 와이파이 비밀번호

// ==========================================
// 2. 핀 및 상수 정의 (제공해주신 코드 기반)
// ==========================================

const uint8_t PIN_SCK = 18;
const uint8_t PIN_MISO = 19;
const uint8_t PIN_MOSI = 23;
const uint8_t PIN_SS = 5;
const uint8_t PIN_RST = 16; // 리셋 핀 (사용자 설정)
const uint8_t PIN_IRQ = 4;  // 인터럽트 핀

#define PIN_PWMA 15
#define PIN_AIN1 12
#define PIN_AIN2 13
#define PIN_STBY 14

#define PWM_FREQ 20000
#define PWM_UNIT MCPWM_UNIT_0
#define PWM_TIMER MCPWM_TIMER_0

// 메시지 타입
enum MessageType {
  PUBLISH = 0x01,
  SUBSCRIBE = 0x02,
  ACK = 0x03,
  PING = 0x04,
  PONG = 0x05
};

// QoS 레벨
enum QoS {
  AT_MOST_ONCE = 0,
  AT_LEAST_ONCE = 1,
  EXACTLY_ONCE = 2
};

//DWM RECV/SEND 정보
device_configuration_t DEFAULT_CONFIG = {
    false,
    true,
    true,
    true,
    false,
    SFDMode::STANDARD_SFD,
    Channel::CHANNEL_5,
    DataRate::RATE_850KBPS,
    PulseFrequency::FREQ_16MHZ,
    PreambleLength::LEN_256,
    PreambleCode::CODE_3
};

// FSM: 제어 모드 정의
enum ControlMode {
  HTTP_CONTROL = 0,  // HTTP(웹)에서 제어
  MQTT_CONTROL = 1   // MQTT(DWM)에서 제어
};

WebServer server(80);
float currentSpeed = 0.0; // 현재 속도 (0.0 ~ 100.0)
float targetSpeed = 0.0;
const float acceleration = 2.5; // 속도 변화량

bool currentDirection = true; // 방향 (true: 정회전, false: 역회전)
ControlMode controlMode = HTTP_CONTROL; // 초기 상태: HTTP 제어

const uint8_t START_BYTE = 0x7E;
const uint8_t END_BYTE = 0x7F;

uint16_t nextMessageId = 1;

uint8_t recvBuffer[1024]; // ESP32의 넉넉한 RAM 활용
int recvBufferLen = 0;

String subscribedTopics[10];
int subscribedCount = 0;

unsigned long lastPublish = 0;

uint8_t txBuffer[256];

int16_t numReceived = 0;
String message;


// ========== 함수 원형 선언 ==========
uint16_t calculateCRC16(uint8_t* data, int len);
bool parseMessage(uint8_t* data, int len);
void handlePublish(uint16_t msgId, uint8_t qos, String topic, uint8_t* payload, int len);
void handleAck(uint16_t msgId);
void sendAck(uint16_t msgId);

// ==========================================
// 4. 모터 제어 함수 (제공해주신 코드)
// ==========================================
void initMotor() {
  // GPIO 초기화
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = ((1ULL << PIN_AIN1) | (1ULL << PIN_AIN2) | (1ULL << PIN_STBY));
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);

  // STBY High
  gpio_set_level((gpio_num_t)PIN_STBY, 1);

  // MCPWM 초기화
  mcpwm_gpio_init(PWM_UNIT, MCPWM0A, PIN_PWMA);

  mcpwm_config_t pwm_config;
  pwm_config.frequency = PWM_FREQ * 2;
  pwm_config.cmpr_a = 0;
  pwm_config.cmpr_b = 0;
  pwm_config.counter_mode = MCPWM_UP_DOWN_COUNTER;
  pwm_config.duty_mode = MCPWM_DUTY_MODE_0;
  
  mcpwm_init(PWM_UNIT, PWM_TIMER, &pwm_config);
}

void setTargetSpeed(float speed, ControlMode requiredMode) {
  if (controlMode == requiredMode) {
    targetSpeed = speed;
    Serial.printf("[Speed] Set to %.1f (Mode: %s)\n", speed, 
                  requiredMode == HTTP_CONTROL ? "HTTP" : "MQTT");
  } else {
    Serial.printf("[Speed] IGNORED - Current mode: %s, Request from: %s\n",
                  controlMode == HTTP_CONTROL ? "HTTP" : "MQTT",
                  requiredMode == HTTP_CONTROL ? "HTTP" : "MQTT");
  }
}

void updateMotorSpeed(bool direction)
{
  if (abs(currentSpeed - targetSpeed) > 0.1) {
    if (currentSpeed < targetSpeed) {
      currentSpeed += acceleration; // 가속
      if (currentSpeed > targetSpeed) currentSpeed = targetSpeed;
    } else {
      currentSpeed -= acceleration; // 감속
      if (currentSpeed < targetSpeed) currentSpeed = targetSpeed;
    }
    setMotor(abs(currentSpeed), direction); 
  }
}

void setMotor(float speed, bool direction) {
  if (direction) { 
    gpio_set_level((gpio_num_t)PIN_AIN1, 1); 
    gpio_set_level((gpio_num_t)PIN_AIN2, 0); 
  } else {         
    gpio_set_level((gpio_num_t)PIN_AIN1, 0); 
    gpio_set_level((gpio_num_t)PIN_AIN2, 1); 
  }
  mcpwm_set_duty(PWM_UNIT, PWM_TIMER, MCPWM_OPR_A, speed); 
  mcpwm_set_duty_type(PWM_UNIT, PWM_TIMER, MCPWM_OPR_A, MCPWM_DUTY_MODE_0);
}

// ==========================================
// 5. 웹 서버 핸들러 함수
// ==========================================

// 메인 페이지 HTML 제공
void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>";
  html += "body { font-family: sans-serif; text-align: center; margin-top: 50px; background-color: #222; color: white; }";
  html += "h1 { margin-bottom: 30px; }";
  html += ".btn { padding: 20px 40px; font-size: 24px; margin: 10px; border: none; border-radius: 10px; color: white; cursor: pointer; width: 80%; max-width: 300px; touch-action: manipulation; }";
  html += ".btn-up { background-color: #4CAF50; }"; // 초록색
  html += ".btn-down { background-color: #f44336; }"; // 빨간색
  html += ".btn:active { transform: scale(0.95); opacity: 0.8; }";
  html += "#speed-display { font-size: 30px; margin: 20px; color: #ffeb3b; }";
  html += "</style></head><body>";
  
  html += "<h1>ESP32 Motor Control</h1>";
  html += "<p>Current Speed: <span id='speed-display'>" + String((int)currentSpeed) + "</span>%</p>";
  html += "<button class='btn btn-up' onclick=\"changeSpeed('up')\">Speed UP (+10)</button><br>";
  html += "<button class='btn btn-down' onclick=\"changeSpeed('down')\">Speed DOWN (-10)</button>";

  // JavaScript (비동기 통신)
  html += "<script>";
  html += "function changeSpeed(action) {";
  html += "  fetch('/' + action).then(response => response.text()).then(data => {";
  html += "    document.getElementById('speed-display').innerText = data;";
  html += "  });";
  html += "}";
  html += "</script>";
  
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// 속도 증가 요청 처리
void handleSpeedUp() 
{
  if (controlMode != HTTP_CONTROL) {
    server.send(403, "text/plain", "MQTT Control Mode Active");
    Serial.println("[HTTP] Speed UP BLOCKED - MQTT mode active");
    return;
  }
  
  currentSpeed += 10.0;
  if (currentSpeed > 100.0) currentSpeed = 100.0;
  
  setTargetSpeed(currentSpeed, HTTP_CONTROL);
  Serial.println("Speed UP: " + String(currentSpeed));
  server.send(200, "text/plain", String((int)currentSpeed));
}

// 속도 감소 요청 처리
void handleSpeedDown() {
  if (controlMode != HTTP_CONTROL) {
    server.send(403, "text/plain", "MQTT Control Mode Active");
    Serial.println("[HTTP] Speed DOWN BLOCKED - MQTT mode active");
    return;
  }
  
  currentSpeed -= 10.0;
  if (currentSpeed < 0.0) currentSpeed = 0.0;
  
  setTargetSpeed(currentSpeed, HTTP_CONTROL);
  Serial.println("Speed DOWN: " + String(currentSpeed));
  server.send(200, "text/plain", String((int)currentSpeed));
}

// ==========================================
// mqtt source
// ========== 구독 관리 ==========

void subscribe(String topic) {
  if (subscribedCount < 10) {
    subscribedTopics[subscribedCount++] = topic;
    Serial.printf("[Subscribe] Added: %s\n", topic.c_str());
  }
}

bool isSubscribed(String topic) {
  for (int i = 0; i < subscribedCount; i++) {
    if (subscribedTopics[i] == topic) {
      return true;
    }
  }
  return false;
}


// ========== 메시지 발행 ==========

void publish(String topic, uint8_t* payload, int payloadLen) {
  uint16_t msgId = nextMessageId++;
  if (nextMessageId == 0) nextMessageId = 1;
  
  // 메시지 직렬화
  int bufferSize = 11 + topic.length() + payloadLen;
  if(bufferSize > sizeof(txBuffer))
  {
    Serial.println("[Error] Tx Buffer overflow");
    return;
  }

  uint8_t* buffer = txBuffer;
  
  if (buffer == NULL) {
    Serial.println("[Error] Memory allocation failed");
    return;
  }

  int pos = 0;
  
  buffer[pos++] = START_BYTE;
  buffer[pos++] = PUBLISH;
  buffer[pos++] = (msgId >> 8) & 0xFF;
  buffer[pos++] = msgId & 0xFF;
  buffer[pos++] = AT_LEAST_ONCE;
  buffer[pos++] = (uint8_t)topic.length();
  
  for (int i = 0; i < topic.length(); i++) {
    buffer[pos++] = (uint8_t)topic[i];
  }
  
  // Payload Len (2bytes)
  buffer[pos++] = (payloadLen >> 8) & 0xFF;
  buffer[pos++] = payloadLen & 0xFF;
  
  for (int i = 0; i < payloadLen; i++) {
    buffer[pos++] = payload[i];
  }
  
  uint16_t crc = calculateCRC16(buffer + 1, pos - 1);
  buffer[pos++] = (crc >> 8) & 0xFF;
  buffer[pos++] = crc & 0xFF;
  
  buffer[pos++] = END_BYTE;
  
  // Serial2로 전송
  //Serial2.write(buffer, pos);
}

void publish(String topic, String payload) {
  publish(topic, (uint8_t*)payload.c_str(), payload.length());
}

// ========== 메시지 수신 처리 ==========


void processMessages() {
  // Serial2에서 데이터 읽기
  // while (Serial2.available() > 0 && recvBufferLen < 1024) {
  //   recvBuffer[recvBufferLen++] = Serial2.read();
  // }

  DW1000Ng::startReceive();
  while(!DW1000Ng::isReceiveDone()) {
    delay(1); // Watchdog Timer 리셋을 위해 필수
  }
  
  DW1000Ng::clearReceiveStatus();
  numReceived++;
  DW1000Ng::getReceivedData(message);

  // String message를 recvBuffer에 추가
  int msgLen = message.length();
  if (recvBufferLen + msgLen <= sizeof(recvBuffer)) {
    memcpy(recvBuffer + recvBufferLen, message.c_str(), msgLen);
    recvBufferLen += msgLen;
  } else {
    Serial.println("[Error] Recv Buffer Overflow");
  }

  while (recvBufferLen > 0) {
    int startPos = -1;
    for (int i = 0; i < recvBufferLen; i++) {
      if (recvBuffer[i] == START_BYTE) {
        startPos = i;
        break;
      }
    }
    
    if (startPos == -1) {
      recvBufferLen = 0;
      break;
    }
    
    if (startPos > 0) {
      memmove(recvBuffer, recvBuffer + startPos, recvBufferLen - startPos);
      recvBufferLen -= startPos;
    }
    
    if (recvBufferLen < 11) break;
    
    int endPos = -1;
    for (int i = 1; i < recvBufferLen; i++) {
      if (recvBuffer[i] == END_BYTE) {
        endPos = i;
        break;
      }
    }
    
    if (endPos == -1) {
      if (recvBufferLen > 1000) recvBufferLen = 0; // 버퍼 리셋
      break;
    }
    
    parseMessage(recvBuffer, endPos + 1);
    
    
    int remainLen = recvBufferLen - (endPos + 1);
    if (remainLen > 0) {
      memmove(recvBuffer, recvBuffer + endPos + 1, remainLen);
    }
    recvBufferLen = remainLen;
  }
}

bool parseMessage(uint8_t* data, int len) {
  int pos = 1; 
  
  uint8_t msgType = data[pos++];
  
  uint16_t msgId = (data[pos] << 8) | data[pos + 1];
  pos += 2;
  
  uint8_t qos = data[pos++];
  uint8_t topicLen = data[pos++];
  
  if (pos + topicLen + 2 > len) return false;
  
  String topic = "";
  for (int i = 0; i < topicLen; i++) {
    topic += (char)data[pos++];
  }
  
  uint16_t payloadLen = (data[pos] << 8) | data[pos + 1];
  pos += 2;
  
  if (pos + payloadLen + 3 > len) return false;
  
  uint8_t* payload = data + pos;
  pos += payloadLen;
  
  uint16_t receivedCRC = (data[pos] << 8) | data[pos + 1];
  uint16_t calculatedCRC = calculateCRC16(data + 1, pos - 1);
  
  if (receivedCRC != calculatedCRC) {
    Serial.println("[Error] CRC mismatch");
    return false; 
  }
  
  if (msgType == PUBLISH) {
    handlePublish(msgId, qos, topic, payload, payloadLen);
  } else if (msgType == ACK) {
    handleAck(msgId);
  }
  
  return true;
}

void handlePublish(uint16_t msgId, uint8_t qos, String topic, uint8_t* payload, int len) {
  if (!isSubscribed(topic)) return;
  
  if (qos == AT_LEAST_ONCE) {
    sendAck(msgId);
  }
  
  if (topic == "command/controlled") {
    if (len > 0) {
      if (payload[0] == '1' || payload[0] == 1) {
        controlMode = MQTT_CONTROL;
        Serial.println("[FSM] Mode: MQTT_CONTROL (HTTP BLOCKED)");
      } else {
        controlMode = HTTP_CONTROL;
        Serial.println("[FSM] Mode: HTTP_CONTROL (HTTP ENABLED)");
      }
    }
  }
  else if (topic == "command/led") {
    if (len > 0) {
      if (payload[0] == '1' || payload[0] == 1) {
        digitalWrite(2, HIGH); // ESP32 Builtin LED
        Serial.println("[CMD] LED ON");
      } else {
        digitalWrite(2, LOW);
        Serial.println("[CMD] LED OFF");
      }
    }
  }
  else if (topic == "command/motor") {
    if (len > 0) {
      // payload를 null-terminated string으로 변환
      char buffer[32];
      int copyLen = (len < 31) ? len : 31;
      memcpy(buffer, payload, copyLen);
      buffer[copyLen] = '\0';
      
      float speed = atof(buffer); // 문자열을 float으로 변환
      
      // 속도 범위 검증 (0.0 ~ 100.0)
      if (speed < 0.0) speed = 0.0;
      if (speed > 100.0) speed = 100.0;
      
      setTargetSpeed(speed, MQTT_CONTROL);
    }
  }
  else {
    // 디버깅용: 수신된 페이로드 출력
    Serial.printf("[Recv] Topic: %s, Payload Len: %d\n", topic.c_str(), len);
  }
}

void handleAck(uint16_t msgId) {
  Serial.printf("[ACK] Message ID: %d\n", msgId);
}

void sendAck(uint16_t msgId) {
  uint8_t buffer[11];
  int pos = 0;
  
  buffer[pos++] = START_BYTE;
  buffer[pos++] = ACK;
  buffer[pos++] = (msgId >> 8) & 0xFF;
  buffer[pos++] = msgId & 0xFF;
  buffer[pos++] = AT_MOST_ONCE;
  buffer[pos++] = 0; // topic len
  buffer[pos++] = 0; // payload len high
  buffer[pos++] = 0; // payload len low
  
  uint16_t crc = calculateCRC16(buffer + 1, pos - 1);
  buffer[pos++] = (crc >> 8) & 0xFF;
  buffer[pos++] = crc & 0xFF;
  buffer[pos++] = END_BYTE;
  
  // Serial2.write(buffer, pos);
}

uint16_t calculateCRC16(uint8_t* data, int len) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int j = 0; j < 8; j++) {
      if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
      else crc = crc << 1;
    }
  }
  return crc;
}

void setup() {
  Serial.begin(115200);

  //핀 모드 설정, 5 is Inner LED
  pinMode(5, OUTPUT); 
  pinMode(PIN_IRQ, INPUT); 
  // SPI 및 핀 설정
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_SS);

  // DW1000 초기화
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, LOW);
  delay(10);
  digitalWrite(PIN_RST, HIGH);
  delay(10);

  DW1000Ng::initializeNoInterrupt(PIN_SS);

  DW1000Ng::applyConfiguration(DEFAULT_CONFIG);
  
  DW1000Ng::setDeviceAddress(6); // 수신기 주소
  DW1000Ng::setNetworkId(10);    // 네트워크 ID

  DW1000Ng::setAntennaDelay(16436);
  Serial.println(F("Committed configuration ..."));

  // DWM 확인
  char msg[128];
  DW1000Ng::getPrintableDeviceIdentifier(msg);
  Serial.print("Device ID: "); Serial.println(msg);
  DW1000Ng::getPrintableExtendedUniqueIdentifier(msg);
  Serial.print("Unique ID: "); Serial.println(msg);
  DW1000Ng::getPrintableNetworkIdAndShortAddress(msg);
  Serial.print("Network ID & Device Address: "); Serial.println(msg);
  DW1000Ng::getPrintableDeviceMode(msg);
  Serial.print("Device mode: "); Serial.println(msg);

  ////////////////////////////////////
  // 모터 초기화
  initMotor();
  setMotor(0, true); // 초기 정지 상태

  // Wi-Fi 연결
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP()); // 이 IP 주소를 휴대폰 브라우저에 입력하세요.

  
  server.on("/", handleRoot);
  server.on("/up", handleSpeedUp);
  server.on("/down", handleSpeedDown);

  server.begin();
  Serial.println("Web server started");

  // MQTT 구독
  subscribe("command/controlled");
  subscribe("command/led");
  subscribe("command/motor");


}

void loop() {
  server.handleClient(); // 클라이언트 요청 처리

  // 메시지 수신 처리 (Non-blocking)
  processMessages();

  // 모터 속도 업데이트
  updateMotorSpeed(currentDirection);

}