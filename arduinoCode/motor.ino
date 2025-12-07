#include <WiFi.h>
#include <WebServer.h>
#include "soc/mcpwm_reg.h"
#include "soc/mcpwm_struct.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
#include "driver/mcpwm.h"

#include <HardwareSerial.h>

// ==========================================
// 1. Wi-Fi 설정 
// ==========================================
const char* ssid = "GOODMILK";      // 와이파이 이름
const char* password = "kTw2530!"; // 와이파이 비밀번호

// ==========================================
// 2. 핀 및 상수 정의 (제공해주신 코드 기반)
// ==========================================
#define PIN_PWMA 15
#define PIN_AIN1 12
#define PIN_AIN2 13
#define PIN_STBY 14

#define RXD2 16
#define TXD2 17

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


// ==========================================
// 3. 전역 변수 및 서버 객체
// ==========================================
WebServer server(80);
float currentSpeed = 0.0; // 현재 속도 (0.0 ~ 100.0)
bool currentDirection = true; // 방향 (true: 정회전, false: 역회전)

const uint8_t START_BYTE = 0x7E;
const uint8_t END_BYTE = 0x7F;

uint16_t nextMessageId = 1;

uint8_t recvBuffer[1024]; // ESP32의 넉넉한 RAM 활용
int recvBufferLen = 0;

String subscribedTopics[10];
int subscribedCount = 0;

unsigned long lastPublish = 0;

uint8_t txBuffer[256];
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
void handleSpeedUp() {
  currentSpeed += 10.0;
  if (currentSpeed > 100.0) currentSpeed = 100.0;
  
  setMotor(currentSpeed, currentDirection);
  Serial.println("Speed UP: " + String(currentSpeed));
  server.send(200, "text/plain", String((int)currentSpeed));
}

// 속도 감소 요청 처리
void handleSpeedDown() {
  currentSpeed -= 10.0;
  if (currentSpeed < 0.0) currentSpeed = 0.0;
  
  setMotor(currentSpeed, currentDirection);
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


// ========== CRC16-CCITT ==========
// 함수 원형 선언
uint16_t calculateCRC16(uint8_t* data, int len);

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

  uint8_t* buffer = txbuffer;
  
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
  Serial2.write(buffer, pos);
}

void publish(String topic, String payload) {
  publish(topic, (uint8_t*)payload.c_str(), payload.length());
}

// ========== 메시지 수신 처리 ==========

void handlePublish(uint16_t msgId, uint8_t qos, String topic, uint8_t* payload, int len);
void handleAck(uint16_t msgId);
void sendAck(uint16_t msgId);

void processMessages() {
  // Serial2에서 데이터 읽기
  while (Serial2.available() > 0 && recvBufferLen < 1024) {
    recvBuffer[recvBufferLen++] = Serial2.read();
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
  
  if (topic == "command/led") {
    if (len > 0) {
      if (payload[0] == '1' || payload[0] == 1) {
        digitalWrite(2, HIGH); // ESP32 Builtin LED
        Serial.println("[CMD] LED ON");
      } else {
        digitalWrite(2, LOW);
        Serial.println("[CMD] LED OFF");
      }
    }
  } else {
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
  
  Serial2.write(buffer, pos);
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
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  // 내장 LED 핀 모드 설정
  pinMode(2, OUTPUT); 


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
  subscribe("command/led");


}

void loop() {
  server.handleClient(); // 클라이언트 요청 처리

  // 메시지 수신 처리 (Non-blocking)
  processMessages();
}