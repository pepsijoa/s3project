# UART 기반 Pub/Sub 시스템 - 전체 실행 순서

## 시스템 개요

```
┌─────────────────┐         UART (GPIO 14/15)        ┌─────────────────┐
│  라즈베리파이    │ ◄────────────────────────────► │    아두이노      │
│  (Publisher +   │    토픽 기반 메시지 교환          │   (Subscriber +  │
│   Subscriber)   │    QoS 1, CRC16 검증            │    Publisher)    │
└─────────────────┘                                 └─────────────────┘
```

## 1. 하드웨어 연결

### 라즈베리파이 ↔ 아두이노
```
라즈베리파이          아두이노
GPIO 14 (TX)  ----->  RX (Pin 0)
GPIO 15 (RX)  <-----  TX (Pin 1)
GND           ----->  GND
```

⚠️ **주의**: 레벨 시프터 권장 (라즈베리파이 3.3V, 아두이노 5V)

---

## 2. 라즈베리파이 설정

### 2.1 UART 활성화
```bash
sudo raspi-config
```
- Interface Options → Serial Port
- "로그인 셸 사용" → **No**
- "시리얼 포트 하드웨어" → **Yes**
- 재부팅

### 2.2 시리얼 콘솔 비활성화
```bash
sudo systemctl stop serial-getty@ttyS0.service
sudo systemctl disable serial-getty@ttyS0.service
```

### 2.3 빌드
```bash
cd /home/kkw/s3_project/build
cmake ..
make
```

---

## 3. 아두이노 설정

### 3.1 코드 업로드
1. `arduino_pubsub.ino` 파일을 아두이노 IDE로 열기
2. 보드 선택 (예: Arduino Uno)
3. 포트 선택
4. 업로드

### 3.2 구독 토픽 설정 (setup 함수)
```cpp
void setup() {
  Serial.begin(9600);
  
  // 구독할 토픽 설정 (최대 5개)
  subscribe("sensor/temperature");
  subscribe("sensor/humidity");
  subscribe("command/led");
  
  pinMode(LED_BUILTIN, OUTPUT);
}
```

### 3.3 TX/RX 연결
⚠️ 업로드 **후에** TX/RX 핀을 라즈베리파이에 연결

---

## 4. 실행 순서

### Step 1: 아두이노 시작
```
[아두이노 전원 ON]
  ↓
Serial.begin(9600) - UART 초기화
  ↓
subscribe() 호출 - 토픽 등록
  ↓
loop() 시작
  ↓
processMessages() 대기 - 수신 대기 모드
```

### Step 2: 라즈베리파이 시작
```bash
cd /home/kkw/s3_project/build
sudo ./uart_pubsub
```

```
[프로그램 시작]
  ↓
uart.init(9600) - UART 초기화
  ↓
broker.subscribe() - 토픽 구독 등록
  ├─ "sensor/temperature"
  ├─ "sensor/humidity"
  └─ "command/led"
  ↓
loop 시작
  ├─ processMessages() - 수신 처리
  └─ publish() - 메시지 발행
```

---

## 5. 메시지 송수신 흐름

### 5.1 라즈베리파이 → 아두이노 (Publish)

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

### 5.2 아두이노 → 라즈베리파이 (Publish)

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

## 6. 실행 후 동작

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

## 7. 전체 타임라인

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

## 8. QoS 1 동작 과정

```
[발신측]
  1. publish() 호출
  2. message_id 생성
  3. 메시지 전송
  4. pending_acks[message_id] 저장
  5. ACK 대기...
  
[수신측]
  1. 메시지 수신
  2. CRC 검증
  3. handlePublish() 실행
  4. sendAck(message_id)
  
[발신측]
  1. ACK 수신
  2. pending_acks[message_id] 제거
  3. 전송 완료 ✓
```

---

## 9. 주요 함수 호출 순서

### 라즈베리파이
```cpp
main()
  └─ broker.init(9600)
      └─ uart.init()
          └─ open("/dev/serial0")
          └─ tcsetattr() // UART 설정
  
  └─ broker.subscribe("topic", callback)
      └─ subscribers[topic].push_back(callback)
  
  └─ while(running)
      ├─ broker.processMessages()
      │   └─ uart.receiveData()
      │   └─ find START/END
      │   └─ UARTMessage::deserialize()
      │   └─ handleMessage()
      │       └─ sendAck() (QoS 1)
      │       └─ callback(msg)
      │
      └─ broker.publish("topic", data)
          └─ UARTMessage::serialize()
          └─ uart.sendData()
          └─ pending_acks[id] 저장
```

### 아두이노
```cpp
setup()
  └─ Serial.begin(9600)
  └─ subscribe("topic")
      └─ subscribedTopics[] 저장

loop()
  ├─ processMessages()
  │   └─ Serial.read()
  │   └─ find START/END
  │   └─ parseMessage()
  │   └─ handlePublish()
  │       └─ isSubscribed() 확인
  │       └─ sendAck() (QoS 1)
  │       └─ 토픽별 처리
  │
  └─ publish("topic", data)
      └─ 메시지 직렬화
      └─ calculateCRC16()
      └─ Serial.write()
```

---

## 10. 테스트 시나리오

### 시나리오 1: LED 제어
1. 라즈베리파이: `broker.publish("command/led", {1})`
2. 아두이노: 메시지 수신 → LED ON
3. 아두이노: ACK 전송
4. 라즈베리파이: ACK 수신 확인

### 시나리오 2: 센서 데이터 전송
1. 아두이노: `publish("sensor/temp", "23C")`
2. 라즈베리파이: 메시지 수신 → 콜백 실행
3. 라즈베리파이: ACK 전송
4. 아두이노: ACK 수신 확인

### 시나리오 3: 양방향 통신
1. 라즈베리파이 → 아두이노: 명령 전송
2. 아두이노: 명령 실행
3. 아두이노 → 라즈베리파이: 상태 보고
4. 라즈베리파이: 상태 확인

---

## 11. 문제 해결

### UART 장치를 열 수 없음
```bash
sudo systemctl stop serial-getty@ttyS0.service
sudo systemctl disable serial-getty@ttyS0.service
```

### 메시지 수신 안 됨
- TX/RX 핀 연결 확인 (크로스 연결)
- GND 공통 연결 확인
- 보드레이트 일치 확인 (9600)

### CRC 오류
- 케이블 품질 확인
- 노이즈 차폐 확인
- 전원 안정성 확인

### ACK 타임아웃
- `processMessages()` 주기 확인
- 구독 토픽 일치 확인
- 시리얼 버퍼 오버플로우 확인

---

## 12. 종료

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

### 아두이노
- 전원 OFF 또는 리셋
- loop()는 계속 실행됨

---

## 요약

1. **하드웨어 연결**: TX ↔ RX, GND 공통
2. **라즈베리파이 설정**: UART 활성화, 시리얼 콘솔 비활성화
3. **빌드**: `cmake .. && make`
4. **실행**: `sudo ./uart_pubsub`
5. **통신**: 토픽 기반 pub/sub, QoS 1, CRC 검증
6. **종료**: Ctrl+C

**인터넷 불필요, MQTT 브로커 불필요, 순수 UART 통신만 사용!**
