#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <string>
#include <atomic>
#include <condition_variable>

#include <winsock2.h>

#include <Packet.h>
#include <Logger.h>

struct ClientPacket
{
    int client_index;
    std::string buffer;
};

class Server;
void ServerThread(Server &);
void SendThread(Server &);
void ClientHandler(Server &, int);

class Server
{
private:
    int max_clients;
    int max_data_size;
    
    friend void ServerThread(Server &);
    friend void ProcessPackets(Server &);
    friend void ClientHandler(Server &, int);

    std::thread server_thread;
    std::thread send_thread;
    std::vector<std::thread> client_therads;

public:
    std::atomic<bool> running;
    int clients_connected;
    std::vector<bool> client_connected;

    SOCKET listen_socket;
    std::vector<SOCKET> client_socket;
    std::mutex mutex;

    std::queue<ClientPacket> recv_queue;
    std::mutex recv_mutex;
    std::condition_variable recv_cv;

    std::queue<ClientPacket> send_queue;
    std::mutex send_mutex;
    std::condition_variable send_cv;

    Server(int max_clients, int max_data_size)
    {
        running = false;
        clients_connected = 0;
        client_socket.resize(max_clients);
        client_connected.resize(max_clients, false);
        this->max_clients = max_clients;
        this->max_data_size = max_data_size;
    }

    bool Start(int server_port)
    {
        WSAData wsdata;
        WSAStartup(MAKEWORD(2, 2), &wsdata);

        listen_socket = socket(AF_INET, SOCK_STREAM, 0);

        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_port = htons(server_port);
        address.sin_addr.S_un.S_addr = INADDR_ANY;

        bind(listen_socket, (sockaddr *)&address, sizeof(address));

        if(listen_socket == SOCKET_ERROR)
        {
            closesocket(listen_socket);
            WSACleanup();
            return false;            
        }

        listen(listen_socket, max_clients);
        running = true;

        server_thread = std::thread(ServerThread, std::ref(*this));
        send_thread = std::thread(SendThread, std::ref(*this));

        return true;
    }

    void SendPacket(const ClientPacket &packet)
    {
        std::lock_guard<std::mutex> lock(send_mutex);
        send_queue.push(packet);

        send_cv.notify_one();
    }

    void BroadCast(const std::string &buffer)
    {
        std::lock_guard<std::mutex> lock(send_mutex);

        for(int client_index = 0; client_index < max_clients; client_index++)
        {
            if(client_connected[client_index])
            {
                ClientPacket packet;
                packet.client_index = client_index;
                packet.buffer = buffer;

                send_queue.push(packet);
            }
        }

        send_cv.notify_one();
    }

    void DisconnectClient(int client_index)
    {
        SOCKET socket;   
        
        {
            std::lock_guard<std::mutex> lock(mutex);

            if(!client_connected[client_index])
                return;

            socket = client_socket[client_index];

            client_connected[client_index] = false;
            client_socket[client_index] = INVALID_SOCKET;
            clients_connected--;

        }

        closesocket(socket);
        DebugLog("Client " + std::to_string(client_index) + " disconnected");
    }

    void Close()
    {
        std::lock_guard<std::mutex> lock(mutex);
        running = false;
    
        send_cv.notify_all();
        recv_cv.notify_all();

        shutdown(listen_socket, SD_BOTH);
        closesocket(listen_socket);
        
        if(server_thread.joinable())
            server_thread.join();

        if(send_thread.joinable())
            send_thread.join();

        for(int client_index = 0; client_index < max_clients; client_index++)
            if(client_connected[client_index])
            {
                shutdown(client_socket[client_index], SD_BOTH);
                closesocket(client_socket[client_index]);
             
                std::thread &thread = client_therads[client_index]; 
                if(thread.joinable())
                    thread.join();
            }
        
        WSACleanup();
    }
};

void ClientHandler(Server &server, int client_index)
{
    ClientPacket packet = {};
    packet.client_index = client_index;

    while (true)
    {
        {
            std::lock_guard<std::mutex> lock(server.mutex);
            if(!server.client_connected[client_index])
                break;
        }

        SOCKET client_socket;
        
        {
            std::lock_guard<std::mutex> lock(server.mutex);
            client_socket = server.client_socket[client_index];
        }

        int packet_size = 0;
        if(!ReceiveData(client_socket, &packet_size, sizeof(int)))
        {
            DebugLog("Client " + std::to_string(packet.client_index) + " had a header fail");
            server.DisconnectClient(client_index);
            break;            
        }

        if(packet_size <= 0 || packet_size > server.max_data_size)
        {
            DebugLog("Invalid packet size from client " + std::to_string(client_index));
            server.DisconnectClient(client_index);
            break;
        }

        DebugLog("Client " + std::to_string(client_index) + " packet size " + std::to_string(packet_size));

        packet.buffer.clear();
        packet.buffer.resize(packet_size);

        if(!ReceiveData(client_socket, packet.buffer.data(), packet_size))
        {
            DebugLog("Client " + std::to_string(packet.client_index) + " had a payload fail");
            server.DisconnectClient(client_index);
            break;            
        }

        {
            std::lock_guard<std::mutex> lock1(server.recv_mutex);
            server.recv_queue.push(packet);
        } 

       server.recv_cv.notify_one();
    }
}

void SendThread(Server &server)
{
    std::unique_lock<std::mutex> lock(server.send_mutex);

    while(server.running)
    {
        server.send_cv.wait(lock, [&server] {
            return !server.send_queue.empty() || !server.running;
        });

        while(!server.send_queue.empty())
        {
            ClientPacket packet = server.send_queue.front();
            server.send_queue.pop();

            lock.unlock();

            SOCKET socket;

            {
                std::lock_guard<std::mutex> lock1(server.mutex);
                socket = server.client_socket[packet.client_index];
            }

            if(socket != INVALID_SOCKET)
                SendData(socket, packet.buffer.data(), packet.buffer.size());

            lock.lock();
        }
    }
}

void ServerThread(Server &server)
{
    while(server.running)
    {
        SOCKET new_client = accept(server.listen_socket, 0, 0);

        if(new_client == INVALID_SOCKET) 
            continue;

        int client_index = -1;
        
        {
            std::lock_guard<std::mutex> lock(server.mutex);

            if(server.clients_connected < server.max_clients)
            {
                for(int i = 0; i < server.max_clients; i++)
                {
                    if(server.client_connected[i]) continue;

                    client_index = i;

                    server.client_socket[i] = new_client;
                    server.client_connected[i] = true;
                    server.clients_connected++;
                    break;
                }
            }
        }

        if(client_index == -1)
        {
            closesocket(new_client);
            DebugLog("Server full, client rejected");
            continue;
        }

        DebugLog("Client " + std::to_string(client_index) + " has connected");

        server.client_therads.emplace_back(ClientHandler, std::ref(server), client_index);
    }        
}