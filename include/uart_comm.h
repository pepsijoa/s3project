#ifndef UART_COMM_H
#define UART_COMM_H

class UARTComm {
private:
    int uart_fd;
    const char* uart_device;
    
public:
    UARTComm();
    ~UARTComm();
    
    bool init(int baudrate = 9600);
    bool sendData(const unsigned char* data, int length);
    int receiveData(unsigned char* buffer, int max_length);
    void close();
};

#endif // UART_COMM_H
