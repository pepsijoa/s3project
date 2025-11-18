#ifndef UART_MESSAGE_H
#define UART_MESSAGE_H

#include <cstdint>
#include <string>
#include <vector>

// 메시지 타입
enum class MessageType : uint8_t {
    PUBLISH = 0x01,     // 메시지 발행
    SUBSCRIBE = 0x02,   // 토픽 구독
    ACK = 0x03,         // 수신 확인 (QoS 1)
    PING = 0x04,        // 연결 확인
    PONG = 0x05         // Ping 응답
};

// QoS 레벨
enum class QoS : uint8_t {
    AT_MOST_ONCE = 0,   // QoS 0: 최대 1회 전송
    AT_LEAST_ONCE = 1,  // QoS 1: 최소 1회 전송 (ACK 필요)
};

// UART 메시지 구조
// [START(1)] [TYPE(1)] [MSG_ID(2)] [QOS(1)] [TOPIC_LEN(1)] [TOPIC(var)] [PAYLOAD_LEN(2)] [PAYLOAD(var)] [CRC(2)] [END(1)]
struct UARTMessage {
    static const uint8_t START_BYTE = 0x7E;  // 시작 바이트 [START(1)]
    
    
    MessageType type;
    uint16_t message_id;    // ACK 매칭용
    QoS qos;
    std::string topic;      
    std::vector<uint8_t> payload;  
    
    UARTMessage() : type(MessageType::PUBLISH), message_id(0), qos(QoS::AT_LEAST_ONCE) {}
    
    // 메시지를 바이트 배열로 직렬화
    std::vector<uint8_t> serialize() const; // [PAYLOAD(var)]
    
    // 바이트 배열에서 메시지 역직렬화
    static bool deserialize(const uint8_t* data, size_t len, UARTMessage& msg); 
    
    static uint16_t calculateCRC(const uint8_t* data, size_t len); // [CRC(2)]

    static const uint8_t END_BYTE = 0x7F;    // 종료 바이트 [END(1)]
};

#endif // UART_MESSAGE_H
