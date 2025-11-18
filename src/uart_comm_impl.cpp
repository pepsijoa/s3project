#include "uart_comm.h"
#include <iostream>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>

using namespace std;

UARTComm::UARTComm() : uart_fd(-1), uart_device("/dev/serial0") {}

UARTComm::~UARTComm() {
    close();
}

bool UARTComm::init(int baudrate) {
    // UART 장치 열기
    uart_fd = open(uart_device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (uart_fd == -1) {
        cerr << "UART 장치를 열 수 없습니다: " << uart_device << endl;
        return false;
    }
    
    // UART 설정
    struct termios options;
    tcgetattr(uart_fd, &options);
    
    // 보드레이트 설정
    speed_t baud;
    switch(baudrate) {
        case 9600: baud = B9600; break;
        case 19200: baud = B19200; break;
        case 38400: baud = B38400; break;
        case 57600: baud = B57600; break;
        case 115200: baud = B115200; break;
        default: baud = B9600;
    }
    
    cfsetispeed(&options, baud);
    cfsetospeed(&options, baud);
    
    // 8N1 설정 (8 data bits, No parity, 1 stop bit)
    options.c_cflag &= ~PARENB;  // No parity
    options.c_cflag &= ~CSTOPB;  // 1 stop bit
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;      // 8 data bits
    options.c_cflag |= CREAD | CLOCAL;  // Enable receiver, ignore modem control lines
    
    // Raw mode 설정
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_oflag &= ~OPOST;
    
    // 읽기 타임아웃 설정
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 10;  // 1초 타임아웃
    
    // 설정 적용
    tcsetattr(uart_fd, TCSANOW, &options);
    tcflush(uart_fd, TCIOFLUSH);
    
    // 버퍼 안정화를 위한 짧은 대기
    usleep(100000);  // 100ms 대기
    
    cout << "UART 초기화 완료 (보드레이트: " << baudrate << ")" << endl;
    return true;
}

bool UARTComm::sendData(const unsigned char* data, int length) {
    if (uart_fd == -1) {
        cerr << "UART가 초기화되지 않았습니다." << endl;
        return false;
    }
    
    // 전송 전 이전 데이터 클리어
    tcflush(uart_fd, TCOFLUSH);
    
    int written = write(uart_fd, data, length);
    if (written < 0) {
        cerr << "데이터 전송 실패" << endl;
        return false;
    }
    
    cout << "전송된 데이터 (" << written << " bytes): ";
    for (int i = 0; i < written; i++) {
        printf("0x%02X ", data[i]);
    }
    cout << endl;
    
    return true;
}

int UARTComm::receiveData(unsigned char* buffer, int max_length) {
    if (uart_fd == -1) {
        cerr << "UART가 초기화되지 않았습니다." << endl;
        return -1;
    }
    
    int bytes_read = read(uart_fd, buffer, max_length);
    
    if (bytes_read > 0) {
        cout << "수신된 데이터 (" << bytes_read << " bytes): ";
        for (int i = 0; i < bytes_read; i++) {
            printf("0x%02X ", buffer[i]);
        }
        cout << endl;
    }
    
    return bytes_read;
}

void UARTComm::close() {
    if (uart_fd != -1) {
        ::close(uart_fd);
        uart_fd = -1;
        cout << "UART 연결 종료" << endl;
    }
}
