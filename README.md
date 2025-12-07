# UART 기반 Pub/Sub 메시징 시스템

## 개요
일단은 UART를 통해 MQTT 프로토콜 구현함.


전체 타임라인을 확인하면, 실행 순서 파악 가능.

motor.ino 코드의 processMessages()의 시작 지점의 Serial2 대신에 DWM으로 받아온 recvBuffer 데이터로 바꾸면 될 것 같음.
보내는 것도 마찬가지로, 
publish()안에
// Serial2로 전송
  Serial2.write(buffer, pos);

  해당 부분을 변경하면 될 것 같음.

## 프로토콜 구조

### 메시지 포맷
```
[START] [TYPE] [MSG_ID] [QOS] [TOPIC_LEN] [TOPIC] [PAYLOAD_LEN] [PAYLOAD] [CRC] [END]
  1B      1B      2B      1B       1B       변수        2B         변수      2B    1B
```

- **START**: `0x7E` (시작 바이트)
- **TYPE**: 메시지 타입 (PUBLISH=0x01, SUBSCRIBE=0x02, ACK=0x03, PING=0x04, PONG=0x05)
- **MSG_ID**: 메시지 ID (2바이트, Big Endian)
- **QOS**: 0=최대1회, 1=최소1회, 2=정확히1회
- **TOPIC_LEN**: 토픽 길이 (1바이트, 최대 255)
- **TOPIC**: 토픽 문자열
- **PAYLOAD_LEN**: 페이로드 길이 (2바이트, Big Endian)
- **PAYLOAD**: 실제 데이터
- **CRC**: CRC16-CCITT 체크섬
- **END**: `0x7F` (종료 바이트)

## 사용 예제

### 기본 사용법

```cpp
UARTBroker broker;
broker.init(9600);

// 토픽 구독
broker.subscribe("sensor/temperature", [](const UARTMessage& msg) {
    cout << "온도 데이터: ";
    for (uint8_t b : msg.payload) {
        cout << (int)b << " ";
    }
    cout << endl;
});

// 메시지 발행
vector<uint8_t> data = {25, 30, 28};
broker.publish("sensor/temperature", data);

// 메시지 처리 루프
while (running) {
    broker.processMessages();
    this_thread::sleep_for(chrono::milliseconds(100));
}
```

### 지원하는 메시지 타입

1. **PUBLISH**: 토픽에 메시지 발행
2. **ACK**: QoS 1 메시지 수신 확인
3. **SUBSCRIBE**: 토픽 구독 (로컬에서만 동작)
4. **PING/PONG**: 연결 확인 (예약)

## 빌드 및 실행

```bash
cd /home/kkw/s3_project/build
cmake ..
make
sudo ./uart_pubsub
```

## 시스템 구성

### 라즈베리파이 (양쪽 모두)
```
라즈베리파이 A          라즈베리파이 B
    GPIO 14 (TX) -----> GPIO 15 (RX)
    GPIO 15 (RX) <----- GPIO 14 (TX)
    GND          <----> GND
```

### 동작 흐름

1. **메시지 발행 (Publish)**
   - 애플리케이션이 `publish()` 호출
   - 메시지 직렬화 (토픽 + 페이로드 + CRC)
   - UART로 전송
   - QoS 1이면 ACK 대기

2. **메시지 수신**
   - UART로 바이트 수신
   - START/END 바이트로 프레임 검출
   - CRC 검증
   - 역직렬화
   - QoS 1이면 ACK 전송
   - 구독자 콜백 호출

## 토픽 예제

```cpp
// 센서 데이터
broker.subscribe("sensor/temperature", callback);
broker.subscribe("sensor/humidity", callback);
broker.subscribe("sensor/pressure", callback);

// 제어 명령
broker.subscribe("command/led", callback);
broker.subscribe("command/motor", callback);

// 상태 정보
broker.subscribe("status/battery", callback);
broker.subscribe("status/error", callback);
```

## QoS 1 동작 방식

1. Publish 측: 메시지 전송 → ACK 대기 목록에 추가
2. Subscribe 측: 메시지 수신 → CRC 확인 → ACK 전송
3. Publish 측: ACK 수신 → 대기 목록에서 제거

## 주의사항

- 토픽 이름은 최대 255자
- 페이로드는 최대 65535바이트
- QoS 2는 현재 미구현
- 단방향/양방향 통신 모두 지원
- CRC 실패 시 메시지 자동 폐기

## 1. 메시지 송수신 흐름

### 1.1 라즈베리파이 → 아두이노 (Publish)

```
[라즈베리파이]
  broker.publish("command/led", {1})
    ↓
  메시지 직렬화
    [START][TYPE][MSG_ID][QOS][TOPIC][PAYLOAD][CRC][END]
    ↓
  UART TX (GPIO 14) 전송
    ↓
    ═══════════════════════════
    ↓
[아두이노]
  Serial.read() - UART RX 수신
    ↓
  recvBuffer에 저장
    ↓
  processMessages() 호출
    ↓
  START/END 바이트 검출
    ↓
  parseMessage() - 역직렬화
    ↓
  CRC 검증 ✓
    ↓
  topic == "command/led" 확인
    ↓
  isSubscribed() 확인 ✓
    ↓
  sendAck(MSG_ID) - ACK 전송 (QoS 1)
    ↓
  handlePublish() 실행
    └─ digitalWrite(LED_BUILTIN, HIGH)
    ↓
    ═══════════════════════════
    ↓
[라즈베리파이]
  ACK 수신
    ↓
  pending_acks에서 제거
    ↓
  "[BROKER] ACK 수신: 메시지 ID XXX"
```

### 1.2 아두이노 → 라즈베리파이 (Publish)

```
[아두이노]
  publish("arduino/status", "OK")
    ↓
  메시지 직렬화
    ↓
  Serial.write() - UART TX 전송
    ↓
    ═══════════════════════════
    ↓
[라즈베리파이]
  uart.receiveData() - UART RX 수신
    ↓
  recv_buffer에 추가
    ↓
  processMessages() 호출
    ↓
  START/END 검출
    ↓
  UARTMessage::deserialize()
    ↓
  CRC 검증 ✓
    ↓
  handleMessage() 실행
    ↓
  topic == "arduino/status" 확인
    ↓
  sendAck(MSG_ID) - ACK 전송 (QoS 1)
    ↓
  구독자 콜백 실행
    └─ [콜백] "OK" 출력
    ↓
    ═══════════════════════════
    ↓
[아두이노]
  ACK 수신
    ↓
  handleAck() 실행
    ↓
  "[Arduino] ACK 수신: 메시지 ID XXX"
```

---

## 2. 실행 후 동작

### 라즈베리파이 출력 예시
```
=== UART 기반 Pub/Sub 시스템 ===
QoS 1 지원, 인터넷 불필요
Ctrl+C로 종료하세요.

UART 초기화 완료 (보드레이트: 9600)
[BROKER] 토픽 구독: sensor/temperature
[BROKER] 토픽 구독: sensor/humidity
[BROKER] 토픽 구독: command/led

[BROKER] 메시지 발행: sensor/temperature (ID: 1, 크기: 3 bytes)
전송된 데이터 (18 bytes): 0x7E 0x01 0x00 0x01 ...

[BROKER] ACK 수신: 메시지 ID 1

[BROKER] 메시지 수신: arduino/status (ID: 5, 크기: 2 bytes)
[콜백] 아두이노 상태: OK
```

### 아두이노 시리얼 모니터 출력
```
[Arduino] 온도 데이터 수신: 25 30 28
[Arduino] ACK 수신: 메시지 ID 1
[Arduino] LED ON
[Arduino] 습도 데이터 수신: 65%
```

---

## 3. 전체 타임라인

```
시간    라즈베리파이                          아두이노
─────────────────────────────────────────────────────────────
0ms     프로그램 시작                         loop() 대기
        UART 초기화                           
        토픽 구독 등록                        
                                              
100ms   publish("sensor/temp", {25,30,28})   
        └─ UART TX ──────────────────────► Serial.read()
                                              parseMessage()
                                              CRC 검증 ✓
                                              handlePublish()
                                              └─ 온도 출력
        ACK 수신 ◄───────────────────────── sendAck()
        
300ms   publish("command/led", {1})
        └─ UART TX ──────────────────────► Serial.read()
                                              LED ON
        ACK 수신 ◄───────────────────────── sendAck()
        
5000ms  processMessages()
        └─ 수신 대기                         publish("arduino/status")
        메시지 수신 ◄────────────────────── Serial.write()
        콜백 실행
        └─ "OK" 출력
        ACK 전송 ───────────────────────► ACK 수신
```

---

## 4. 종료

### 라즈베리파이
```
Ctrl + C 입력
  ↓
signal_handler() 실행
  ↓
running = false
  ↓
loop 종료
  ↓
프로그램 종료
```

---

## 요약

1. **하드웨어 연결**: TX ↔ RX, GND 공통
2. **라즈베리파이 설정**: UART 활성화, 시리얼 콘솔 비활성화
3. **빌드**: `cmake .. && make`
4. **실행**: `sudo ./uart_pubsub`
5. **통신**: 토픽 기반 pub/sub, QoS 1, CRC 검증
6. **종료**: Ctrl+C

**인터넷 불필요, MQTT 브로커 불필요, 순수 UART 통신만 사용!**
