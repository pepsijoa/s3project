#include "uart_broker.h"
#include <iostream>
#include <algorithm>

using namespace std;

UARTBroker::UARTBroker() : next_message_id(1) {
    recv_buffer.reserve(1024);
}

UARTBroker::~UARTBroker() {
}

bool UARTBroker::init(int baudrate) {
    return uart.init(baudrate);
}

uint16_t UARTBroker::generateMessageId() {
    lock_guard<mutex> lock(broker_mutex);
    uint16_t id = next_message_id++;
    if (next_message_id == 0) {
        next_message_id = 1;  // 0은 예약
    }
    return id;
}

void UARTBroker::subscribe(const string& topic, function<void(const UARTMessage&)> callback) {
    lock_guard<mutex> lock(broker_mutex);
    subscribers[topic].push_back(callback);
    cout << "[BROKER] 토픽 구독: " << topic << endl;
}

bool UARTBroker::publish(const string& topic, const vector<uint8_t>& payload) {
    UARTMessage msg;
    msg.type = MessageType::PUBLISH;
    msg.message_id = generateMessageId();
    msg.qos = QoS::AT_LEAST_ONCE;
    msg.topic = topic;
    msg.payload = payload;
    
    vector<uint8_t> data = msg.serialize();
    
    cout << "[BROKER] 메시지 발행: " << topic 
         << " (ID: " << msg.message_id 
         << ", 크기: " << payload.size() << " bytes)" << endl;
    
    // UART로 전송
    bool success = uart.sendData(data.data(), data.size());
    
    if (success && msg.qos == QoS::AT_LEAST_ONCE) {
        lock_guard<mutex> lock(broker_mutex);
        pending_acks[msg.message_id] = msg;  // ACK 대기 목록에 추가
    }
    
    return success;
}

bool UARTBroker::publish(const string& topic, const string& payload) {
    vector<uint8_t> data(payload.begin(), payload.end());
    return publish(topic, data);
}

bool UARTBroker::sendAck(uint16_t message_id) {
    UARTMessage ack;
    ack.type = MessageType::ACK;
    ack.message_id = message_id;
    ack.qos = QoS::AT_MOST_ONCE;
    ack.topic = "";
    ack.payload.clear();
    
    vector<uint8_t> data = ack.serialize();
    
    cout << "[BROKER] ACK 전송: 메시지 ID " << message_id << endl;
    
    return uart.sendData(data.data(), data.size());
}

void UARTBroker::handleMessage(const UARTMessage& msg) {
    switch (msg.type) {
        case MessageType::PUBLISH: {
            cout << "[BROKER] 메시지 수신: " << msg.topic 
                 << " (ID: " << msg.message_id 
                 << ", 크기: " << msg.payload.size() << " bytes)" << endl;
            
            // QoS 1이면 ACK 전송
            if (msg.qos == QoS::AT_LEAST_ONCE) {
                sendAck(msg.message_id);
            }
            
            // 구독자에게 메시지 전달
            lock_guard<mutex> lock(broker_mutex);
            auto it = subscribers.find(msg.topic);
            if (it != subscribers.end()) {
                for (auto& callback : it->second) {
                    callback(msg);
                }
            } else {
                cout << "[BROKER] 구독자 없음: " << msg.topic << endl;
            }
            break;
        }
        
        case MessageType::ACK: {
            cout << "[BROKER] ACK 수신: 메시지 ID " << msg.message_id << endl;
            
            lock_guard<mutex> lock(broker_mutex);
            pending_acks.erase(msg.message_id);
            break;
        }
        
        case MessageType::PING: {
            cout << "[BROKER] PING 수신" << endl;
            // PONG 응답 (구현 생략)
            break;
        }
        
        default:
            cout << "[BROKER] 알 수 없는 메시지 타입" << endl;
            break;
    }
}

void UARTBroker::processMessages() {
    unsigned char buffer[512];
    int received = uart.receiveData(buffer, sizeof(buffer));
    
    if (received > 0) {
        // 수신 버퍼에 추가
        recv_buffer.insert(recv_buffer.end(), buffer, buffer + received);
        
        // START 바이트 찾기
        while (true) {
            auto start_it = find(recv_buffer.begin(), recv_buffer.end(), UARTMessage::START_BYTE);
            
            if (start_it == recv_buffer.end()) {
                recv_buffer.clear();  // START 바이트 없음
                break;
            }
            
            // START 이전 데이터 제거
            if (start_it != recv_buffer.begin()) {
                recv_buffer.erase(recv_buffer.begin(), start_it);
            }
            
            // 최소 크기 확인
            if (recv_buffer.size() < 11) {
                break;  // 더 많은 데이터 필요
            }
            
            // END 바이트 찾기
            auto end_it = find(recv_buffer.begin() + 1, recv_buffer.end(), UARTMessage::END_BYTE);
            
            if (end_it == recv_buffer.end()) {
                // END 바이트 없음, 더 많은 데이터 필요
                if (recv_buffer.size() > 512) {
                    recv_buffer.clear();  // 버퍼 오버플로우 방지
                }
                break;
            }
            
            // 완전한 메시지 발견
            size_t msg_len = distance(recv_buffer.begin(), end_it) + 1;
            
            UARTMessage msg;
            if (UARTMessage::deserialize(recv_buffer.data(), msg_len, msg)) {
                handleMessage(msg);
            } else {
                cerr << "[BROKER] 메시지 파싱 실패 (CRC 오류 또는 손상된 데이터)" << endl;
            }
            
            // 처리한 메시지 제거
            recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + msg_len);
        }
    }
}
