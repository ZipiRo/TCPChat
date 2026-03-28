#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <string>
#include <atomic>
#include <condition_variable>
#include <functional>

#include <winsock2.h>

#include <Packet.h>
#include <Logger.h>

struct ClientPacket
{
    int client_index;
    Packet packet;
};

class Server;
void ServerThread(Server &);
void ProcessThread(Server &);
void ClientHandler(Server &, int);
void SendThread(Server &);

class Server
{
private:
    int max_clients;
    int max_data_size;

    std::thread server_thread;
    std::thread send_thread;
    std::thread process_thread;
    std::vector<std::thread> client_therads;

    std::function<void(const ClientPacket&)> packet_handler;
    
    friend void ServerThread(Server &);
    friend void ProcessThread(Server &);
    friend void SendThread(Server&);
    friend void ClientHandler(Server &, int);

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

    Server() {}

    Server(int max_clients, int max_data_size)
    {
        Init(max_clients, max_data_size);
    }

    void Init(int max_clients, int max_data_size)
    {
        running = false;
        clients_connected = 0;
        client_socket.resize(max_clients);
        client_connected.resize(max_clients, false);
        client_therads.resize(max_clients);
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

        send_thread = std::thread(SendThread, std::ref(*this));
        server_thread = std::thread(ServerThread, std::ref(*this));
        process_thread = std::thread(ProcessThread, std::ref(*this));

        return true;
    }

    void SendClientPacket(const Packet &packet, int client_index)
    {
        ClientPacket client_packet;
        client_packet.client_index = client_index;
        client_packet.packet = packet;

        std::lock_guard<std::mutex> lock(send_mutex);
        send_queue.push(client_packet);

        send_cv.notify_one();
    }

    void BroadCast(const Packet &packet)
    {
        std::lock_guard<std::mutex> lock(send_mutex);

        for(int client_index = 0; client_index < max_clients; client_index++)
        {
            if(client_connected[client_index])
            {
                ClientPacket client_packet;
                client_packet.client_index = client_index;
                client_packet.packet = packet;

                send_queue.push(client_packet);
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

        if(socket != INVALID_SOCKET)
        {
            shutdown(socket, SD_BOTH);
            closesocket(socket);
        }

        DebugLog("Client " + std::to_string(client_index) + " disconnected");
    }

    void SetPacketHandler(std::function<void (const ClientPacket&)> handler)
    {
        packet_handler = handler;
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

        if(process_thread.joinable())
            process_thread.join();

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
            ClientPacket client_packet = server.send_queue.front();
            Packet packet = client_packet.packet;
            server.send_queue.pop();

            lock.unlock();

            SOCKET socket;

            {
                std::lock_guard<std::mutex> lock1(server.mutex);
                socket = server.client_socket[client_packet.client_index];
            }

            if(socket != INVALID_SOCKET)
            {
                int packet_size = packet.Size();
                SendData(socket, &packet_size, sizeof(int));
                SendData(socket, packet.StringData().data(), packet_size);
            }

            lock.lock();
        }
    }
}

void ClientHandler(Server &server, int client_index)
{
    ClientPacket client_packet = {};
    client_packet.client_index = client_index;

    while (true)
    {
        SOCKET client_socket;
        bool connected;
        
        {
            std::lock_guard<std::mutex> lock(server.mutex);
            connected = server.client_connected[client_index];
            client_socket = server.client_socket[client_index];
        }

        if(!connected)
            break;
        
        int packet_size = 0;
        if(!ReceiveData(client_socket, &packet_size, sizeof(int)))
        {
            DebugLog("Client " + std::to_string(client_index) + " had some socket errors");
            server.DisconnectClient(client_index);
            break;            
        }

        if(packet_size <= 0 || packet_size > server.max_data_size)
        {
            DebugLog("Invalid packet size from client " + std::to_string(client_index));
            server.DisconnectClient(client_index);
            break;
        }

        Packet packet;
        packet.StringData().resize(packet_size);

        if(!ReceiveData(client_socket, packet.StringData().data(), packet_size))
        {
            DebugLog("Client " + std::to_string(client_index) + " had some socket errors");
            server.DisconnectClient(client_index);
            break;            
        }

        DebugLog("Client " + std::to_string(client_index) + " packet size " + std::to_string(packet_size));

        client_packet.packet = packet;

        {
            std::lock_guard<std::mutex> lock(server.recv_mutex);
            server.recv_queue.push(client_packet);
        } 

       server.recv_cv.notify_one();
    }
}

void ProcessThread(Server &server)
{
    while (server.running)
    {
        std::unique_lock<std::mutex> lock(server.recv_mutex);

        server.recv_cv.wait(lock, [&server] {
            return !server.recv_queue.empty() || !server.running;
        });

        while (!server.recv_queue.empty())
        {
            ClientPacket client_packet = server.recv_queue.front();
            server.recv_queue.pop();

            lock.unlock();

            if(server.packet_handler)
                server.packet_handler(client_packet);

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

                    if(server.client_therads[i].joinable())
                        server.client_therads[i].join();

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