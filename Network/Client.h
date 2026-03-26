#pragma once

#include <thread>
#include <mutex>
#include <queue>
#include <string>
#include <atomic>
#include <condition_variable>
#include <functional>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <Packet.h>
#include <Logger.h>

class Client;
void ReceiveThread(Client &client);
void SendThread(Client &client);
void ProcessThread(Client &client);

class Client
{
private:
    std::string server_ip;
    int server_port;
    int max_data_size;

    std::thread receive_thread;
    std::thread send_thread;
    std::thread process_thread;

    std::function<void(const Packet &packet)> packet_handler;

    friend void ReceiveThread(Client &client);
    friend void ProcessThread(Client &client);
    friend void SendThread(Client &client);

public:
    std::atomic<bool> connected;

    SOCKET network_socket;
    std::mutex mutex;

    std::queue<Packet> recv_queue;
    std::mutex recv_mutex;
    std::condition_variable recv_cv;

    std::queue<Packet> send_queue;
    std::mutex send_mutex;
    std::condition_variable send_cv;

    Client(int max_data_size) 
    {
        connected = false;
        this->max_data_size = max_data_size;
    }

    bool Connect(const char* server_ip, int server_port)
    {   
        this->server_ip = server_ip;
        this->server_port = server_port;

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

        receive_thread = std::thread(ReceiveThread, std::ref(*this));
        send_thread = std::thread(SendThread, std::ref(*this));
        process_thread = std::thread(ProcessThread, std::ref(*this));

        connected = true;

        return true;
    }

    void SetPacketHandler(std::function<void(const Packet &packet)> handler)
    {
        packet_handler = handler;
    }

    void SendPacket(const Packet &packet)
    {
        std::lock_guard<std::mutex> lock(send_mutex);
        send_queue.push(packet);
        send_cv.notify_one();
    }

    void Disconnect()
    {
        std::lock_guard<std::mutex> lock(mutex);
        connected = false;
        send_cv.notify_all();
        recv_cv.notify_all();

        shutdown(network_socket, SD_BOTH);
        closesocket(network_socket);

        if(receive_thread.joinable())
            receive_thread.join();

        if(send_thread.joinable())
            send_thread.join();

        if(process_thread.joinable())
            process_thread.join();

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
            Packet packet = client.send_queue.front();
            client.send_queue.pop();
            
            lock.unlock();

            if(client.connected && client.network_socket != INVALID_SOCKET)
            {
                DebugLog("Sending data to server, data size: " + std::to_string(packet.data.size()));

                int packet_size = packet.data.size();

                SendData(client.network_socket, &packet_size, sizeof(int));
                SendData(client.network_socket, &packet.type, sizeof(int));
                SendData(client.network_socket, packet.data.data(), packet_size);
            }

            lock.lock();
        }
    }
}

void ReceiveThread(Client &client)
{
    Packet packet = {};
    while (client.connected)
    {
        SOCKET &network_socket = client.network_socket;

        int packet_size = 0;
        if(!ReceiveData(network_socket, &packet_size, sizeof(int)))
        {
            DebugLog("Server has disconected (packet size fail)");
            client.Disconnect();
            break;            
        }

        if(packet_size <= 0 || packet_size > client.max_data_size)
        {
            DebugLog("Invalid packet size from server");
            client.Disconnect();
            break;
        }

        int packet_type = -1;
        if(!ReceiveData(network_socket, &packet_type, sizeof(int)))
        {
            DebugLog("Server has disconected (packet type fail)");
            client.Disconnect();
            break;            
        }

        packet.data.clear();
        packet.data.resize(packet_size);
        packet.type = packet_type;

        if(!ReceiveData(network_socket, packet.data.data(), packet_size))
        {
            DebugLog("Server has disconected (payload fail)");
            client.Disconnect();
            break;            
        }

        {
            std::lock_guard<std::mutex> lock(client.recv_mutex);
            client.recv_queue.push(packet);
        } 

       client.recv_cv.notify_one();
    }
}

void ProcessThread(Client &client)
{
    while (client.connected)
    {
        std::unique_lock<std::mutex> lock(client.recv_mutex);

        client.recv_cv.wait(lock, [&client] {
            return !client.recv_queue.empty() || !client.connected;
        });

        while (!client.recv_queue.empty())
        {
            Packet packet = client.recv_queue.front();
            client.recv_queue.pop();

            lock.unlock();

            if(client.packet_handler)
                client.packet_handler(packet);

            lock.lock();
        }
    }
}   