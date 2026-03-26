#pragma once 

#include <string>

#include <winsock2.h>

struct Packet
{   
    int type;
    std::string data;

    template <typename T>
    void Write(const T& data)
    {
        this->data.append((const char*)&data, sizeof(T));
    }
};

class PacketReader
{
private:
    std::string data;
    int offset;

public:
    PacketReader(const std::string &data) 
    {
        this->data = data;
        offset = 0;
    }

    template <typename T>
    T Read()
    {
        T value;
        memcpy(&value, data.data() + offset, sizeof(T));
        offset += sizeof(T);
        return value;
    }

    void Reset()
    {
        offset = 0;
    }
};

bool SendData(const SOCKET &socket, const void* data, int size)
{
    const char* buffer = (const char*)data;

    int total_sent = 0;
    while (total_sent < size)
    {
        int sent = send(socket, buffer + total_sent, size - total_sent, 0);

        if(sent == SOCKET_ERROR)
            return false;

        total_sent += sent;
    }
    
    return true;
}

bool ReceiveData(const SOCKET &socket, void* data, int size)
{
    char* buffer = (char*)data;
    int total_received = 0;

    while (total_received < size)
    {
        int received = recv(socket, buffer + total_received, size - total_received, 0);

        if(received <= 0)
            return false;

        total_received += received;
    }
    
    return true;
}