#pragma once

const int MAX_CLIENTS = 2;
const int MAX_DATA_SIZE = 1024 * 1024;

const int MAGIC_HANDSHAKE = 0xAFB41C;
const int PROTOCOL_VERSION = 1;

enum PacketType
{
    PACKET_JOIN,
    PACKET_WELLCOME  
};