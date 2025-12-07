#include <HardwareSerial.h>

/* * ESP32 UART 설정
 * UART0: USB 디버깅용 (Serial)
 * UART2: 외부 장치 통신용 (Serial2) - RX: GPIO16, TX: GPIO17 (기본값, 변경 가능)
 */
#define RXD2 16
#define TXD2 17

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

// 프로토콜 상수
const uint8_t START_BYTE = 0x7E;
const uint8_t END_BYTE = 0x7F;

// 메시지 ID 관리
uint16_t nextMessageId = 1;

// 수신 버퍼
uint8_t recvBuffer[1024]; // ESP32의 넉넉한 RAM 활용
int recvBufferLen = 0;

// 구독 토픽 관리 (최대 10개로 확장)
String subscribedTopics[10];
int subscribedCount = 0;

// 타이머 관리 변수
unsigned long lastPublish = 0;

void setup() {
  // 디버깅용 시리얼
  Serial.begin(115200);
  
  // 통신용 시리얼 (UART2 사용)
  // ESP32의 IO MUX 기능을 통해 핀 재할당 가능
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  
  Serial.println("[ESP32] System Started");

  // 토픽 구독 설정
  subscribe("sensor/temperature");
  subscribe("sensor/humidity");
  subscribe("command/led");
  
  // ESP32 내장 LED (보드마다 다를 수 있음, 보통 2번)
  pinMode(2, OUTPUT); 
}

void loop() {
  // 메시지 수신 처리 (Non-blocking)
  processMessages();
  
  // 5초마다 상태 메시지 발행 (Non-blocking)
  unsigned long currentMillis = millis();
  if (currentMillis - lastPublish > 5000) {
    // 상태 전송
    uint8_t status[] = {0x01};  // 정상 동작
    publish("esp32/status", status, 1);
    
    // 센서 데이터 전송 (예제)
    // ESP32는 내부 홀 센서나 온도 센서가 있으나(TRM 31장), 여기서는 더미 데이터 전송
    String tempStr = "ESP32: 25.5C";
    publish("sensor/temperature", tempStr);
    
    lastPublish = currentMillis;
  }
  
  // delay() 제거: ESP32의 멀티태스킹 효율성을 위해 사용하지 않음
}

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
  uint8_t* buffer = (uint8_t*)malloc(bufferSize);
  
  if (!buffer) {
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
  
  free(buffer);
}

void publish(String topic, String payload) {
  publish(topic, (uint8_t*)payload.c_str(), payload.length());
}

// ========== 메시지 수신 처리 ==========

// 함수 원형 선언
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