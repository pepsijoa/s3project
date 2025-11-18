#ifndef UART_BROKER_H
#define UART_BROKER_H

#include "uart_comm.h"
#include "uart_message.h"
#include <map>
#include <vector>
#include <functional>
#include <mutex>

// 구독 정보
struct Subscription {
    std::string topic;
    std::function<void(const UARTMessage&)> callback;
};

// UART 기반 경량 메시지 브로커
class UARTBroker {
private:
    UARTComm uart;
    uint16_t next_message_id;
    std::map<std::string, std::vector<std::function<void(const UARTMessage&)>>> subscribers;
    std::map<uint16_t, UARTMessage> pending_acks;  // QoS 1 대기 중인 메시지
    std::mutex broker_mutex;
    
    // 수신 버퍼
    std::vector<uint8_t> recv_buffer;
    
    // ACK 전송
    bool sendAck(uint16_t message_id);
    
    // 메시지 처리
    void handleMessage(const UARTMessage& msg);
    
public:
    UARTBroker();
    ~UARTBroker();
    
    // 초기화
    bool init(int baudrate = 9600);
    
    // 토픽 구독
    void subscribe(const std::string& topic, std::function<void(const UARTMessage&)> callback);
    
    // 메시지 발행 (QoS 1)
    bool publish(const std::string& topic, const std::vector<uint8_t>& payload);
    bool publish(const std::string& topic, const std::string& payload);
    
    // 수신 메시지 처리 (논블로킹)
    void processMessages();
    
    // 메시지 ID 생성
    uint16_t generateMessageId();
};

#endif // UART_BROKER_H
