#include "RakPeerInterface.h"
#include "MessageIdentifiers.h"
#include "BitStream.h"
#include "RakNetTypes.h"  // MessageID
#include "packetids.h"
#include <stdio.h>
#include <string.h>

#define PORT 3999
#define MAX_CLIENTS 25

enum PlayerState {
    NONEXISTENT, EXISTENCE_ACKNOWLEDGED,
    AWAITING_CLIENT, PLAYING
};

struct PlayerStatus {
    PlayerState state;
    RakNet::SystemIndex index;
};

int main(int argc, char** argv) {
    PlayerStatus players[100];
    uint32_t colors[] = {
        0xFFFF9900, 0xFF00FF00, 0xFF007BFF, 0xFFFF5EB4, 0xFFFF6100,
        0xFF6F8927, 0xFF323987, 0xFF70374A, 0xFF375B37, 0xFF0000FF
    };

	RakNet::RakPeerInterface* peer = RakNet::RakPeerInterface::GetInstance();
	peer->SetTimeoutTime(30000,RakNet::UNASSIGNED_SYSTEM_ADDRESS);

    RakNet::Packet* packet;

    {
        RakNet::SocketDescriptor descriptor(PORT, NULL);
        peer->Startup(MAX_CLIENTS, &descriptor, 1);
    }
    peer->SetMaximumIncomingConnections(MAX_CLIENTS);

    printf("up we go!\n");

    while (true) {
        for (packet = peer->Receive(); packet; peer->DeallocatePacket(packet), packet = peer->Receive()) {
            switch (packet->data[0]) {
            case ID_REMOTE_DISCONNECTION_NOTIFICATION:
                printf("Another client has disconnected.\n");
                break;
            case ID_REMOTE_CONNECTION_LOST:
                printf("Another client has lost the connection.\n");
                break;
            case ID_REMOTE_NEW_INCOMING_CONNECTION:
                printf("Another client has connected.\n");
                break;
            case ID_CONNECTION_REQUEST_ACCEPTED:
                printf("Our connection request has been accepted.\n");
                break;					
            case ID_NEW_INCOMING_CONNECTION:
                printf("A connection is incoming. Index: %i\n", packet->systemAddress.systemIndex);
                players[packet->systemAddress.systemIndex].state = EXISTENCE_ACKNOWLEDGED;
                break;
            case ID_NO_FREE_INCOMING_CONNECTIONS:
                printf("The server is full.\n");
                break;
            case ID_DISCONNECTION_NOTIFICATION:
                printf("A client has disconnected.\n");
                break;
            case ID_CONNECTION_LOST:
                printf("A client lost the connection.\n");
                players[packet->systemAddress.systemIndex].state = NONEXISTENT;
                break;

            case ID_NEW_HOST: {
                uint32_t color = colors[rand() % (sizeof(colors) / sizeof(uint32_t))];
                printf("Dis guy want a color! %s index: %i, sending %x\n", packet->systemAddress.ToString(true, ':'), packet->systemAddress.systemIndex, color);

                RakNet::BitStream color_resp;

                color_resp.Write((RakNet::MessageID)ID_ASSIGNED_COLOR);
                color_resp.Write(color);
                color_resp.Write(packet->systemAddress.systemIndex);

                peer->Send(&color_resp, HIGH_PRIORITY, RELIABLE_ORDERED, 0, packet->systemAddress, false);

                players[packet->systemAddress.systemIndex].index = packet->systemAddress.systemIndex;
                players[packet->systemAddress.systemIndex].state = AWAITING_CLIENT;
            } break;

            default:
                printf("Message with identifier %i has arrived.\n", packet->data[0]);
                break;
            }
        }
    }

    return 0;
}