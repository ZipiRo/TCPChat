#include <iostream>
#include <thread>

#include <Server.h>
#include <ServerSettings.h>

void ProcessClientPacket(const Packet &packet)
{

}

int main()
{
    Logger::SetWriteType(LOGGER_WRITE_CONSOLE);
    Server server(MAX_CLIENTS, MAX_DATA_SIZE);

    server.SetPacketHandler(ProcessClientPacket);
    server.Start(100);

    while (server.running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    server.Close();
    
    return 0;
}