#include "uart_broker.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>

using namespace std;

bool running = true;

void signal_handler(int signum) {
    cout << "\n프로그램 종료 신호 수신..." << endl;
    running = false;
}

int main() {
    signal(SIGINT, signal_handler);
    
    UARTBroker broker;
    
    // UART 초기화
    if (!broker.init(9600)) {
        cerr << "UART 초기화 실패" << endl;
        return 1;
    }
    
    cout << "\n=== UART 기반 Pub/Sub 시스템 ===" << endl;
    cout << "QoS 1 지원, 인터넷 불필요" << endl;
    cout << "Ctrl+C로 종료하세요.\n" << endl;
    
    // 토픽 구독 예제
    broker.subscribe("sensor/temperature", [](const UARTMessage& msg) {
        cout << "\n[콜백] 온도 센서 데이터 수신: ";
        for (uint8_t b : msg.payload) {
            cout << b << " ";
        }
        cout << endl;
    });
    
    broker.subscribe("sensor/humidity", [](const UARTMessage& msg) {
        cout << "\n[콜백] 습도 센서 데이터 수신: ";
        string data(msg.payload.begin(), msg.payload.end());
        cout << data << endl;
    });
    
    broker.subscribe("command/led", [](const UARTMessage& msg) {
        cout << "\n[콜백] LED 제어 명령 수신: ";
        if (!msg.payload.empty()) {
            cout << (msg.payload[0] ? "ON" : "OFF") << endl;
        }
    });
    
    // 테스트 메시지 발행
    int counter = 0;
    
    while (running) {
        // 수신 메시지 처리
        broker.processMessages();
        
        // 5초마다 테스트 메시지 발행
        if (counter % 50 == 0) {
            // 온도 데이터 발행
            vector<uint8_t> temp_data = {25, 30, 28};  // 예: 온도 값들
            broker.publish("sensor/temperature", temp_data);
            
            this_thread::sleep_for(chrono::milliseconds(200));
            
            // 습도 데이터 발행
            broker.publish("sensor/humidity", "65%");
            
            this_thread::sleep_for(chrono::milliseconds(200));
            
            // LED 명령 발행
            vector<uint8_t> led_cmd = {1};  // LED ON
            broker.publish("command/led", led_cmd);
        }
        
        counter++;
        this_thread::sleep_for(chrono::milliseconds(100));
    }
    
    cout << "프로그램 종료" << endl;
    return 0;
}
