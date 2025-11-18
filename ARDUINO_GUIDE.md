# 아두이노 Pub/Sub 클라이언트 사용 가이드

## 초기화 과정

### 1. setup() 함수에서
```cpp
void setup() {
  Serial.begin(9600);  // 라즈베리파이와 동일한 보드레이트
  
  // 시리얼 버퍼 초기화 (쓰레기 데이터 제거)
  while(Serial.available() > 0) {
    Serial.read();
  }
  delay(100);
  
  // 구독할 토픽 설정
  subscribe("sensor/temperature");
  subscribe("sensor/humidity");
  subscribe("command/led");
  
  pinMode(LED_BUILTIN, OUTPUT);
}
```

## 토픽 구독

### 방법 1: 코드에서 직접 구독
```cpp
void setup() {
  // 최대 5개까지 구독 가능
  subscribe("sensor/temperature");
  subscribe("command/led");
  subscribe("arduino/config");
}
```

### 방법 2: 메시지 핸들러에서 확인
`handlePublish()` 함수에서 토픽별로 처리:
```cpp
void handlePublish(uint16_t msgId, byte qos, String topic, byte* payload, int len) {
  if (topic == "command/led") {
    // LED 제어
    if (payload[0]) {
      digitalWrite(LED_BUILTIN, HIGH);
    } else {
      digitalWrite(LED_BUILTIN, LOW);
    }
  }
  else if (topic == "sensor/temperature") {
    // 온도 데이터 처리
  }
}
```

## 데이터 전송 방법

### 1. 바이트 배열 전송
```cpp
byte data[] = {0x01, 0x02, 0x03};
publish("arduino/data", data, 3);
```

### 2. 문자열 전송
```cpp
String message = "Hello from Arduino";
publish("arduino/message", message);
```

### 3. 센서 데이터 전송
```cpp
// 온도 센서 값
int temp = analogRead(A0);
byte tempData[2];
tempData[0] = (temp >> 8) & 0xFF;
tempData[1] = temp & 0xFF;
publish("sensor/temperature", tempData, 2);

// 또는 문자열로
String tempStr = String(temp) + "C";
publish("sensor/temperature", tempStr);
```

### 4. 주기적 전송
```cpp
void loop() {
  processMessages();  // 수신 처리 (필수)
  
  static unsigned long lastPublish = 0;
  if (millis() - lastPublish > 5000) {  // 5초마다
    publish("arduino/heartbeat", "alive");
    lastPublish = millis();
  }
  
  delay(100);
}
```

## 메시지 수신 처리

### handlePublish() 함수 커스터마이징
```cpp
void handlePublish(uint16_t msgId, byte qos, String topic, byte* payload, int len) {
  // 구독 여부 확인 (자동)
  if (!isSubscribed(topic)) {
    return;
  }
  
  // QoS 1이면 자동으로 ACK 전송
  if (qos == AT_LEAST_ONCE) {
    sendAck(msgId);
  }
  
  // ===== 여기에 토픽별 처리 로직 추가 =====
  
  if (topic == "command/led") {
    if (len > 0 && payload[0]) {
      digitalWrite(LED_BUILTIN, HIGH);
    } else {
      digitalWrite(LED_BUILTIN, LOW);
    }
  }
  else if (topic == "command/motor") {
    // 모터 제어
    int speed = payload[0];
    analogWrite(9, speed);
  }
  else if (topic == "config/interval") {
    // 설정 변경
    uint16_t interval = (payload[0] << 8) | payload[1];
    // interval 사용
  }
}
```

## 전체 흐름

```
[아두이노 시작]
    ↓
1. Serial.begin(9600)
    ↓
2. subscribe("토픽1", "토픽2", ...)
    ↓
3. loop() 시작
    ↓
4. processMessages() - 수신 메시지 처리
    │  ├─ 메시지 파싱
    │  ├─ CRC 검증
    │  ├─ 구독 여부 확인
    │  ├─ ACK 전송 (QoS 1)
    │  └─ handlePublish() 호출
    ↓
5. publish() - 메시지 전송
    │  ├─ 메시지 직렬화
    │  ├─ CRC 계산
    │  └─ Serial.write()
    ↓
6. delay(100)
    ↓
7. loop() 반복
```

## 라즈베리파이 ↔ 아두이노 통신 예제

### 라즈베리파이에서
```cpp
broker.subscribe("arduino/status", [](const UARTMessage& msg) {
    cout << "아두이노 상태: " << string(msg.payload.begin(), msg.payload.end()) << endl;
});

broker.publish("command/led", {1});  // LED ON
```

### 아두이노에서
```cpp
// 상태 전송
publish("arduino/status", "running");

// LED 명령 수신
if (topic == "command/led") {
    digitalWrite(LED_BUILTIN, payload[0]);
}
```

## 주의사항

1. **보드레이트 일치**: 라즈베리파이와 동일하게 9600
2. **loop()에서 processMessages() 필수 호출**: 수신 처리
3. **최대 5개 토픽 구독 가능**: 배열 크기 제한
4. **QoS 1 자동 처리**: ACK는 자동으로 전송됨
5. **버퍼 크기**: 최대 512바이트 메시지 지원

## 테스트 방법

1. 아두이노에 코드 업로드
2. TX/RX 핀을 라즈베리파이에 연결
3. 라즈베리파이에서 `sudo ./uart_pubsub` 실행
4. 아두이노 시리얼 모니터로 로그 확인
5. 양방향 통신 확인
