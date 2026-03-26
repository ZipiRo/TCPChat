#include <iostream>
#include <thread>

#include <Client.h>
#include <ServerSettings.h>

void ProcessPacket(const Packet &packet)
{

}

int main()
{   
    Logger::SetWriteType(LOGGER_WRITE_NONE);
    Client client(MAX_DATA_SIZE);

    client.SetPacketHandler(ProcessPacket);
    client.Connect("127.0.0.1", 100);

    while(client.connected) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    client.Disconnect();

    return 0;
}