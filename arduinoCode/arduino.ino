byte receivedData[10];
int byteCount = 0;

void setup() {
  Serial.begin(9600);  // 라즈베리파이와 동일한 보드레이트
  
  // 시리얼 버퍼 초기화 - 남아있는 쓰레기 데이터 제거
  while(Serial.available() > 0) {
    Serial.read();
  }
  
  delay(100);  // 안정화 대기
  
  Serial.println("Arduino ready - waiting for data...");
}

void loop() {
  // 데이터 수신 대기
  if (Serial.available() > 0) {
    byte receivedByte = Serial.read();
    
    // 10바이트 버퍼에 저장
    if (byteCount < 10) {
      receivedData[byteCount] = receivedByte;
      byteCount++;
      
      // 10바이트 모두 수신했으면 처리
      if (byteCount == 10) {
        Serial.print("Received 10 bytes: ");
        for (int i = 0; i < 10; i++) {
          Serial.print("0x");
          if (receivedData[i] < 16) Serial.print("0");
          Serial.print(receivedData[i], HEX);
          Serial.print(" ");
        }
        Serial.println();
        
        // 데이터 에코백 (라즈베리파이로 다시 전송)
        Serial.write(receivedData, 10);
        
        // 카운터 리셋
        byteCount = 0;
      }
    }
  }
}