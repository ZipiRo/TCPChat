#pragma once

#include <thread>
#include <mutex>
#include <queue>
#include <string>
#include <atomic>
#include <condition_variable>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <Packet.h>
#include <Logger.h>

class Client;
void ReceiveThread(Client &client);
void SendThread(Client &client);

class Client
{
private:
    int ID;
    int max_data_size;

    friend void ReceiveThread(Client &client);
    friend void ProcessPackets(Client &client);

public:
    std::atomic<bool> connected;

    SOCKET network_socket;

    std::queue<std::string> recv_queue;
    std::mutex recv_mutex;
    std::condition_variable recv_cv;

    std::queue<std::string> send_queue;
    std::mutex send_mutex;
    std::condition_variable send_cv;

    Client(int max_data_size) 
    {
        ID = 0;
        connected = false;
        this->max_data_size = max_data_size;
    }

    bool Connect(const char* server_ip, int server_port)
    {   
        WSADATA wsdata;
        WSAStartup(MAKEWORD(2, 2), &wsdata);

        network_socket = socket(AF_INET, SOCK_STREAM, 0);

        sockaddr_in server = {};
        server.sin_family = AF_INET;
        server.sin_port = htons(server_port);

        inet_pton(AF_INET, server_ip, &server.sin_addr);

        if(connect(network_socket, (sockaddr *)&server, sizeof(server)) == SOCKET_ERROR)
        {
            closesocket(network_socket);
            WSACleanup();
            
            return false;
        }

        connected = true;
        std::thread(ReceiveThread, std::ref(*this)).detach();
        std::thread(SendThread, std::ref(*this)).detach();

        return true;
    }

    void Disconnect()
    {
        connected = false; 
        send_cv.notify_all();
        recv_cv.notify_all();
    }

    void SendData(const std::string &data)
    {
        std::lock_guard<std::mutex> lock(send_mutex);
        send_queue.push(data);
        send_cv.notify_one();
    }

    int GetID() { return ID; }

    void Close()
    {
        connected = false;
        send_cv.notify_all();
        recv_cv.notify_all();
        shutdown(network_socket, SD_BOTH);
        closesocket(network_socket);
        WSACleanup();
    }
};

void SendThread(Client &client)
{
    std::unique_lock<std::mutex> lock(client.send_mutex);

    while(client.connected)
    {
        client.send_cv.wait(lock, [&client] {
            return !client.send_queue.empty() || !client.connected;
        });

        while(!client.send_queue.empty())
        {
            std::string buffer = client.send_queue.front();
            client.send_queue.pop();
            
            lock.unlock();

            if(client.connected && client.network_socket != INVALID_SOCKET)
            {
                DebugLog("Sending data to server, data size: " + std::to_string(buffer.size()));
                SendData(client.network_socket, buffer.data(), buffer.size());
            }

            lock.lock();
        }
    }
}

void ReceiveThread(Client &client)
{
    std::string buffer;
    while (client.connected)
    {
        SOCKET &network_socket = client.network_socket;

        int packet_size = 0;
        if(!ReceiveData(network_socket, &packet_size, sizeof(int)))
        {
            DebugLog("Server has disconected (header fail)");
            client.Disconnect();
            break;            
        }

        if(packet_size <= 0 || packet_size > client.max_data_size)
        {
            DebugLog("Invalid packet size from server");
            client.Disconnect();
            break;
        }

        buffer.clear();
        buffer.resize(packet_size);

        if(!ReceiveData(network_socket, buffer.data(), packet_size))
        {
            DebugLog("Server has disconected (payload fail)");
            client.Disconnect();
            break;            
        }

        {
            std::lock_guard<std::mutex> lock(client.recv_mutex);
            client.recv_queue.push(buffer);
        } 

       client.recv_cv.notify_one();
    }
}