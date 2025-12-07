#include <SoftwareSerial.h>

// ===========================================================
// [아두이노 나노] -> [ESP32] 로 명령을 보내는 코드
// 목적: ESP32의 내장 LED(GPIO 2) 제어
// ===========================================================

// ESP32와 연결된 핀 (RX=2, TX=3)
// 나노 D2 (RX) <---연결---> ESP32 TX2 (17)
// 나노 D3 (TX) <---연결---> ESP32 RX2 (16)
SoftwareSerial espSerial(2, 3);

// --- 프로토콜 상수 ---
const byte START_BYTE = 0x7E;
const byte END_BYTE   = 0x7F;
const byte TYPE_PUB   = 0x01;
const byte QOS_LEVEL  = 0x01; // AT_LEAST_ONCE

// --- 전역 변수 ---
uint16_t msgId = 1;

// CRC16 계산 함수 원형
uint16_t calculateCRC16(byte* data, int len);

void setup() {
  Serial.begin(115200);     // PC와 시리얼 통신 (시리얼 모니터용)
  espSerial.begin(9600);    // ESP32와 통신 (Baudrate 일치 필수)
  
  Serial.println("[Nano] Remote Controller Started.");
  Serial.println("Usage:");
  Serial.println("  Turn ON  -> command/led:1");
  Serial.println("  Turn OFF -> command/led:0");
}

void loop() {
  // 1. 시리얼 모니터(PC)에서 명령 입력 받기
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim(); // 공백 제거
    
    if (input.length() > 0) {
      // 구분자 처리 (':' 또는 '/')
      int splitIndex = input.indexOf(':');
      if (splitIndex == -1) splitIndex = input.indexOf('/');

      if (splitIndex != -1) {
        String topic = input.substring(0, splitIndex);
        String payload = input.substring(splitIndex + 1);
        
        // 입력받은 명령을 ESP32로 전송
        sendPacket(topic, payload);
        
      } else {
        Serial.println("[Error] 형식이 잘못되었습니다. 예: command/led:1");
      }
    }
  }
}

// --- 패킷 전송 함수 (나노 -> ESP32) ---
void sendPacket(String topic, String payload) {
  // 패킷 전체 길이 계산 (헤더11byte + 토픽길이 + 페이로드길이)
  int totalLen = 11 + topic.length() + payload.length();
  
  // 동적 메모리 할당
  byte* buffer = (byte*)malloc(totalLen);
  
  if (buffer == NULL) {
      Serial.println("[Error] 메모리 부족");
      return;
  }

  int pos = 0;

  // 1. 헤더 작성
  buffer[pos++] = START_BYTE;
  buffer[pos++] = TYPE_PUB;
  buffer[pos++] = (msgId >> 8) & 0xFF;
  buffer[pos++] = msgId & 0xFF; 
  msgId++; // 메시지 ID 증가
  buffer[pos++] = QOS_LEVEL;
  
  // 2. 토픽 작성
  buffer[pos++] = (byte)topic.length();
  for (unsigned int i = 0; i < topic.length(); i++) buffer[pos++] = topic[i];

  // 3. 페이로드 작성
  int payLen = payload.length();
  buffer[pos++] = (payLen >> 8) & 0xFF;
  buffer[pos++] = payLen & 0xFF;
  for (unsigned int i = 0; i < (unsigned int)payLen; i++) buffer[pos++] = payload[i];

  // 4. CRC 계산 및 푸터 작성
  uint16_t crc = calculateCRC16(buffer + 1, pos - 1);
  buffer[pos++] = (crc >> 8) & 0xFF;
  buffer[pos++] = crc & 0xFF;
  buffer[pos++] = END_BYTE;

  // 5. ESP32로 실제 전송
  espSerial.write(buffer, pos);
  
  // 6. 디버깅용 출력
  Serial.print("[Sent to ESP32] Topic: ");
  Serial.print(topic);
  Serial.print(" | Payload: ");
  Serial.println(payload);

  free(buffer); // 메모리 해제
}

// CRC16-CCITT 계산 함수
uint16_t calculateCRC16(byte* data, int len) {
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