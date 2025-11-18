/*
#include "uart_comm.h"
#include <iostream>
#include <unistd.h>

using namespace std;

int main() {
    UARTComm uart;
    
    // UART 초기화 (9600 보드레이트)
    if (!uart.init(9600)) {
        return 1;
    }
    
    // 전송할 10바이트 데이터
    unsigned char send_data[10] = {0x01, 0x02, 0x03, 0x04, 0x05, 
                                    0x06, 0x07, 0x08, 0x09, 0x0A};
    
    // 데이터 전송
    cout << "\n아두이노로 데이터 전송..." << endl;
    uart.sendData(send_data, 10);
    
    // 잠시 대기
    sleep(1);
    
    // 데이터 수신 (아두이노로부터 응답 받기)
    cout << "\n아두이노로부터 데이터 수신 대기..." << endl;
    unsigned char recv_buffer[100];
    int received = uart.receiveData(recv_buffer, 100);
    
    if (received > 0) {
        cout << "수신 성공!" << endl;
    } else {
        cout << "수신된 데이터 없음" << endl;
    }
    
    return 0;
}
*/
