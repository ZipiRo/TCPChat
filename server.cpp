#include <iostream>
#include <thread>

#include <Logger.h>
#include <Server.h>
#include <ServerSettings.h>

void ProcessPackets(Server &server)
{
    while (server.running)
    {
        std::unique_lock<std::mutex> lock(server.recv_mutex);

        server.recv_cv.wait(lock, [&server] {
            return !server.recv_queue.empty() || !server.running;
        });

        while (!server.recv_queue.empty())
        {
            ClientPacket packet = server.recv_queue.front();
            server.recv_queue.pop();

            lock.unlock();

            DebugLog("Client " + std::to_string(packet.client_index) + " says: " + packet.buffer.data());

            lock.lock();
        }
    }
}

int main()
{
    Server server(MAX_CLIENTS, MAX_DATA_SIZE);

    server.Start(100);

    std::thread(ProcessPackets, std::ref(server)).detach();

    while (server.running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    server.Close();
    
    return 0;
}