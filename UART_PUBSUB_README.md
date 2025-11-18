# UART 기반 Pub/Sub 메시징 시스템

## 개요
**인터넷 없이** UART만을 사용하여 MQTT 개념(토픽, pub/sub, QoS)을 구현한 경량 메시징 시스템입니다.

## 특징
- ✅ **인터넷 불필요**: UART 시리얼 통신만 사용
- ✅ **토픽 기반 Pub/Sub**: MQTT처럼 토픽으로 메시지 라우팅
- ✅ **QoS 1 지원**: ACK를 통한 메시지 전달 보장
- ✅ **CRC16 검증**: 데이터 무결성 확인
- ✅ **바이너리 프로토콜**: 효율적인 메시지 직렬화

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

## 아두이노 연동

아두이노도 동일한 프로토콜을 구현하면 통신 가능합니다.
별도의 아두이노 라이브러리 작성 필요.
