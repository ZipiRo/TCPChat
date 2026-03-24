#include <iostream>
#include <thread>

#include <Logger.h>
#include <Client.h>
#include <ServerSettings.h>

void ProcessPackets(Client &client)
{
    while (client.connected)
    {
        std::unique_lock<std::mutex> lock(client.recv_mutex);

        client.recv_cv.wait(lock, [&client] {
            return !client.recv_queue.empty() || !client.connected;
        });

        while (!client.recv_queue.empty())
        {
            std::string buffer = client.recv_queue.front();
            client.recv_queue.pop();

            lock.unlock();

            DebugLog("Server says: " + buffer);

            lock.lock();
        }
    }
}   

int main()
{   
    Client client(MAX_DATA_SIZE);

    client.Connect("127.0.0.1", 100);

    std::thread(ProcessPackets, std::ref(client)).detach();

    while(client.connected) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    client.Close();

    return 0;
}