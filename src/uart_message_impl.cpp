#include "uart_message.h"
#include <cstring>

using namespace std;

// static const 멤버 변수 정의
const uint8_t UARTMessage::START_BYTE;
const uint8_t UARTMessage::END_BYTE;

// CRC16-CCITT 계산
uint16_t UARTMessage::calculateCRC(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc = crc << 1;
        }
    }
    return crc;
}

// 메시지 직렬화
vector<uint8_t> UARTMessage::serialize() const {
    vector<uint8_t> buffer;
    
    // START 바이트
    buffer.push_back(START_BYTE);
    
    // TYPE
    buffer.push_back(static_cast<uint8_t>(type));
    
    // MESSAGE_ID (2바이트, Big Endian)
    buffer.push_back((message_id >> 8) & 0xFF);
    buffer.push_back(message_id & 0xFF);
    
    // QoS
    buffer.push_back(static_cast<uint8_t>(qos));
    
    // TOPIC_LEN (1바이트, 최대 255)
    uint8_t topic_len = topic.length();
    buffer.push_back(topic_len);
    
    // TOPIC
    for (char c : topic) {
        buffer.push_back(static_cast<uint8_t>(c));
    }
    
    // PAYLOAD_LEN (2바이트, Big Endian)
    uint16_t payload_len = payload.size();
    buffer.push_back((payload_len >> 8) & 0xFF);
    buffer.push_back(payload_len & 0xFF);
    
    // PAYLOAD
    for (uint8_t b : payload) {
        buffer.push_back(b);
    }
    
    // CRC 계산 (START 제외, TYPE부터 PAYLOAD까지)
    uint16_t crc = calculateCRC(buffer.data() + 1, buffer.size() - 1);
    buffer.push_back((crc >> 8) & 0xFF);
    buffer.push_back(crc & 0xFF);
    
    // END 바이트
    buffer.push_back(END_BYTE);
    
    return buffer;
}

// 메시지 역직렬화
bool UARTMessage::deserialize(const uint8_t* data, size_t len, UARTMessage& msg) {
    if (len < 11) {  // 최소 크기: START(1) + TYPE(1) + MSG_ID(2) + QOS(1) + TOPIC_LEN(1) + PAYLOAD_LEN(2) + CRC(2) + END(1)
        return false;
    }
    
    size_t pos = 0;
    
    // START 바이트 확인
    if (data[pos++] != START_BYTE) {
        return false;
    }
    
    // TYPE
    msg.type = static_cast<MessageType>(data[pos++]);
    
    // MESSAGE_ID
    msg.message_id = (static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1];
    pos += 2;
    
    // QoS
    msg.qos = static_cast<QoS>(data[pos++]);
    
    // TOPIC_LEN
    uint8_t topic_len = data[pos++];
    
    if (pos + topic_len + 2 > len) {  // topic + payload_len
        return false;
    }
    
    // TOPIC
    msg.topic = string(reinterpret_cast<const char*>(data + pos), topic_len);
    pos += topic_len;
    
    // PAYLOAD_LEN
    uint16_t payload_len = (static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1];
    pos += 2;
    
    if (pos + payload_len + 3 > len) {  // payload + crc + end
        return false;
    }
    
    // PAYLOAD
    msg.payload.clear();
    msg.payload.insert(msg.payload.end(), data + pos, data + pos + payload_len);
    pos += payload_len;
    
    // CRC 확인
    uint16_t received_crc = (static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1];
    uint16_t calculated_crc = calculateCRC(data + 1, pos - 1);  // START 제외
    pos += 2;
    
    if (received_crc != calculated_crc) {
        return false;  // CRC 불일치
    }
    
    // END 바이트 확인
    if (data[pos] != END_BYTE) {
        return false;
    }
    
    return true;
}
