#include <iostream>
#include <thread>

#include <Client.h>
#include <ServerSettings.h>

void ProcessPacket(const Packet &packet)
{
    switch (packet.type)
    {
    case PACKET_WELLCOME:
    {
        PacketReader reader(packet.data);

        std::cout << "CONNECTION SUCCESFULL!";

        break;
    }
    default:
        break;
    }    
}

int main()
{   
    Logger::SetWriteType(LOGGER_WRITE_NONE);
    Client client(MAX_DATA_SIZE);

    client.SetPacketHandler(ProcessPacket);
    client.Connect("127.0.0.1", 100);

    if(client.connected)
    {
        Packet packet;
        packet.type = PACKET_JOIN;
        packet.Write(PROTOCOL_VERSION);
        packet.Write(MAGIC_HANDSHAKE);

        client.SendPacket(packet);
    }

    while(client.connected) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    client.Disconnect();

    return 0;
}