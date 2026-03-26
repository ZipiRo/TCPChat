#include <iostream>
#include <thread>

#include <Server.h>
#include <ServerSettings.h>

Server server;

bool ValidClient(int protocol, int handshake)
{
    if(protocol != PROTOCOL_VERSION) return false;
    if(handshake != MAGIC_HANDSHAKE) return false;

    return true;
}

void ProcessClientPacket(const ClientPacket &client_packet)
{
    int client_index = client_packet.client_index;
    Packet packet = client_packet.packet;

    PacketReader reader(packet.data);
    
    switch (packet.type)
    {
    case PACKET_JOIN:
    {
        int protocol = reader.Read<int>();
        int handshake = reader.Read<int>();

        if(!ValidClient(protocol, handshake))
        {
            DebugLog("Client is not valid");
        }
        else 
        {
            DebugLog("Client is valid");

            Packet packet;
            packet.type = PACKET_WELLCOME;
            server.SendClientPacket(packet, client_index);
        }

        break;
    }
    default:
        break;
    }    
}

int main()
{
    Logger::SetWriteType(LOGGER_WRITE_CONSOLE);
    
    server.Init(MAX_CLIENTS, MAX_DATA_SIZE);
    server.SetPacketHandler(ProcessClientPacket);
    server.Start(100);

    while (server.running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    server.Close();
    
    return 0;
}