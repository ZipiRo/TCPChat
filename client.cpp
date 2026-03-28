#include <iostream>
#include <thread>

#include <Client.h>
#include <ServerSettings.h>

void ProcessPacket(Packet &packet)
{
    PacketReader reader(packet);
    
    int type = reader.Read<int>();

    switch (PacketType(type))
    {
    case PACKET_WELLCOME:
    {
        int id = reader.Read<int>();

        std::cout << "You are player " << id << '\n';

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
        Packet packet(PACKET_JOIN);
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