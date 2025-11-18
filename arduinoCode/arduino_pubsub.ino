/*
 * UART 기반 Pub/Sub 시스템 - 아두이노 클라이언트
 * 라즈베리파이와 동일한 프로토콜 사용
 */

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
const byte START_BYTE = 0x7E;
const byte END_BYTE = 0x7F;

// 메시지 ID 관리
uint16_t nextMessageId = 1;

// 수신 버퍼
byte recvBuffer[512];
int recvBufferLen = 0;

// 구독 토픽 관리 (최대 5개)
String subscribedTopics[5];
int subscribedCount = 0;

void setup() {
  Serial.begin(9600);
  
  // 시리얼 버퍼 초기화
  while(Serial.available() > 0) {
    Serial.read();
  }
  
  delay(100);
  
  // 토픽 구독 설정
  subscribe("sensor/temperature");
  subscribe("sensor/humidity");
  subscribe("command/led");
  
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  // 메시지 수신 처리
  processMessages();
  
  // 5초마다 상태 메시지 발행
  static unsigned long lastPublish = 0;
  if (millis() - lastPublish > 5000) {
    // 아두이노 상태 전송
    byte status[] = {0x01};  // 정상 동작
    publish("arduino/status", status, 1);
    
    delay(100);
    
    // 센서 데이터 전송 (예제)
    String tempStr = "Arduino: 23C";
    publish("sensor/temperature", tempStr);
    
    lastPublish = millis();
  }
  
  delay(100);
}

// ========== 구독 관리 ==========

void subscribe(String topic) {
  if (subscribedCount < 5) {
    subscribedTopics[subscribedCount++] = topic;
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

void publish(String topic, byte* payload, int payloadLen) {
  uint16_t msgId = nextMessageId++;
  if (nextMessageId == 0) nextMessageId = 1;
  
  // 메시지 직렬화
  int bufferSize = 11 + topic.length() + payloadLen;
  byte* buffer = (byte*)malloc(bufferSize);
  int pos = 0;
  
  // START
  buffer[pos++] = START_BYTE;
  
  // TYPE
  buffer[pos++] = PUBLISH;
  
  // MESSAGE_ID (Big Endian)
  buffer[pos++] = (msgId >> 8) & 0xFF;
  buffer[pos++] = msgId & 0xFF;
  
  // QoS
  buffer[pos++] = AT_LEAST_ONCE;
  
  // TOPIC_LEN
  buffer[pos++] = topic.length();
  
  // TOPIC
  for (int i = 0; i < topic.length(); i++) {
    buffer[pos++] = topic[i];
  }
  
  // PAYLOAD_LEN (Big Endian)
  buffer[pos++] = (payloadLen >> 8) & 0xFF;
  buffer[pos++] = payloadLen & 0xFF;
  
  // PAYLOAD
  for (int i = 0; i < payloadLen; i++) {
    buffer[pos++] = payload[i];
  }
  
  // CRC16 계산 (START 제외)
  uint16_t crc = calculateCRC16(buffer + 1, pos - 1);
  buffer[pos++] = (crc >> 8) & 0xFF;
  buffer[pos++] = crc & 0xFF;
  
  // END
  buffer[pos++] = END_BYTE;
  
  // 전송
  Serial.write(buffer, pos);
  
  free(buffer);
}

void publish(String topic, String payload) {
  publish(topic, (byte*)payload.c_str(), payload.length());
}

// ========== 메시지 수신 처리 ==========

void processMessages() {
  // UART 데이터 읽기
  while (Serial.available() > 0 && recvBufferLen < 512) {
    recvBuffer[recvBufferLen++] = Serial.read();
  }
  
  // START 바이트 찾기
  while (recvBufferLen > 0) {
    int startPos = -1;
    for (int i = 0; i < recvBufferLen; i++) {
      if (recvBuffer[i] == START_BYTE) {
        startPos = i;
        break;
      }
    }
    
    if (startPos == -1) {
      recvBufferLen = 0;  // START 없음
      break;
    }
    
    // START 이전 데이터 제거
    if (startPos > 0) {
      memmove(recvBuffer, recvBuffer + startPos, recvBufferLen - startPos);
      recvBufferLen -= startPos;
    }
    
    // 최소 크기 확인
    if (recvBufferLen < 11) {
      break;  // 더 많은 데이터 필요
    }
    
    // END 바이트 찾기
    int endPos = -1;
    for (int i = 1; i < recvBufferLen; i++) {
      if (recvBuffer[i] == END_BYTE) {
        endPos = i;
        break;
      }
    }
    
    if (endPos == -1) {
      if (recvBufferLen > 400) {
        recvBufferLen = 0;  // 버퍼 오버플로우 방지
      }
      break;  // 더 많은 데이터 필요
    }
    
    // 메시지 파싱
    if (parseMessage(recvBuffer, endPos + 1)) {
      // 성공
    } else {
      // 실패
    }
    
    // 처리한 메시지 제거
    int remainLen = recvBufferLen - (endPos + 1);
    if (remainLen > 0) {
      memmove(recvBuffer, recvBuffer + endPos + 1, remainLen);
    }
    recvBufferLen = remainLen;
  }
}

bool parseMessage(byte* data, int len) {
  int pos = 1;  // START 건너뛰기
  
  // TYPE
  byte msgType = data[pos++];
  
  // MESSAGE_ID
  uint16_t msgId = (data[pos] << 8) | data[pos + 1];
  pos += 2;
  
  // QoS
  byte qos = data[pos++];
  
  // TOPIC_LEN
  byte topicLen = data[pos++];
  
  if (pos + topicLen + 2 > len) return false;
  
  // TOPIC
  String topic = "";
  for (int i = 0; i < topicLen; i++) {
    topic += (char)data[pos++];
  }
  
  // PAYLOAD_LEN
  uint16_t payloadLen = (data[pos] << 8) | data[pos + 1];
  pos += 2;
  
  if (pos + payloadLen + 3 > len) return false;
  
  // PAYLOAD
  byte* payload = data + pos;
  pos += payloadLen;
  
  // CRC 확인
  uint16_t receivedCRC = (data[pos] << 8) | data[pos + 1];
  uint16_t calculatedCRC = calculateCRC16(data + 1, pos - 1);
  pos += 2;
  
  if (receivedCRC != calculatedCRC) {
    return false;  // CRC 불일치
  }
  
  // END 확인
  if (data[pos] != END_BYTE) {
    return false;
  }
  
  // 메시지 타입 처리
  if (msgType == PUBLISH) {
    handlePublish(msgId, qos, topic, payload, payloadLen);
  } else if (msgType == ACK) {
    handleAck(msgId);
  }
  
  return true;
}

void handlePublish(uint16_t msgId, byte qos, String topic, byte* payload, int len) {
  // 구독 여부 확인
  if (!isSubscribed(topic)) {
    return;
  }
  
  // QoS 1이면 ACK 전송
  if (qos == AT_LEAST_ONCE) {
    sendAck(msgId);
  }
  
  // 메시지 처리
  if (topic == "sensor/temperature") {
    // 온도 데이터 수신 (디버그 출력 비활성화)
    // Serial.print("[Arduino] 온도 데이터 수신: ");
    // for (int i = 0; i < len; i++) {
    //   Serial.print(payload[i]);
    //   Serial.print(" ");
    // }
    // Serial.println();
  }
  else if (topic == "sensor/humidity") {
    // 습도 데이터 수신 (디버그 출력 비활성화)
    // String data = "";
    // for (int i = 0; i < len; i++) {
    //   data += (char)payload[i];
    // }
    // Serial.print("[Arduino] 습도 데이터 수신: ");
    // Serial.println(data);
  }
  else if (topic == "command/led") {
    // LED 제어
    if (len > 0) {
      if (payload[0]) {
        digitalWrite(LED_BUILTIN, HIGH);
        // Serial.println("[Arduino] LED ON");
      } else {
        digitalWrite(LED_BUILTIN, LOW);
        // Serial.println("[Arduino] LED OFF");
      }
    }
  }
}

void handleAck(uint16_t msgId) {
  // ACK 수신 (디버그 출력 비활성화)
  // Serial.print("[Arduino] ACK 수신: 메시지 ID ");
  // Serial.println(msgId);
}

void sendAck(uint16_t msgId) {
  byte buffer[11];
  int pos = 0;
  
  buffer[pos++] = START_BYTE;
  buffer[pos++] = ACK;
  buffer[pos++] = (msgId >> 8) & 0xFF;
  buffer[pos++] = msgId & 0xFF;
  buffer[pos++] = AT_MOST_ONCE;
  buffer[pos++] = 0;  // topic len = 0
  buffer[pos++] = 0;  // payload len high
  buffer[pos++] = 0;  // payload len low
  
  uint16_t crc = calculateCRC16(buffer + 1, pos - 1);
  buffer[pos++] = (crc >> 8) & 0xFF;
  buffer[pos++] = crc & 0xFF;
  buffer[pos++] = END_BYTE;
  
  Serial.write(buffer, pos);
}

// ========== CRC16-CCITT ==========

uint16_t calculateCRC16(byte* data, int len) {
  uint16_t crc = 0xFFFF;
  
  for (int i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int j = 0; j < 8; j++) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc = crc << 1;
      }
    }
  }
  
  return crc;
}
