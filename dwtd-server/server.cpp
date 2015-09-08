#include "RakPeerInterface.h"
#include "MessageIdentifiers.h"
#include "BitStream.h"
#include "RakNetTypes.h"  // MessageID
#include "packetids.h"
#include <stdio.h>
#include <string.h>

#define PORT 8081
#define MAX_CLIENTS 25

enum PlayerState {
    NONEXISTENT, EXISTENCE_ACKNOWLEDGED,
    AWAITING_CLIENT, ABOUT_TO_PLAY, PLAYING
};

struct PlayerStatus {
    PlayerState state;
    RakNet::SystemIndex index;
    RakNet::SystemIndex playing_with_index;
    RakNet::SystemAddress addr;
    uint32_t color;

    bool was_killed;
    bool thinks_other_was_killed;
    int kill_streak;
};

void register_kill(RakNet::RakPeerInterface* peer, PlayerStatus* killer, PlayerStatus* killed) {
    killer->was_killed = false;
    killer->thinks_other_was_killed = false;
    killer->kill_streak += 1;

    killed->was_killed = false;
    killed->thinks_other_was_killed = false;
    killed->kill_streak = 0;

    bool boss_mode = killer->kill_streak >= 5;

    printf("Player %i killed player %i! Streak: %i\n", killer->index, killed->index, killer->kill_streak);

    RakNet::BitStream resp2killer;
    resp2killer.Write((RakNet::MessageID)ID_KILL_HAPPENED);
    resp2killer.Write0();
    boss_mode ? resp2killer.Write1() : resp2killer.Write0();

    RakNet::BitStream resp2killed;
    resp2killed.Write((RakNet::MessageID)ID_KILL_HAPPENED);
    resp2killed.Write1();
    boss_mode ? resp2killed.Write1() : resp2killed.Write0();

    peer->Send(&resp2killer, HIGH_PRIORITY, RELIABLE_ORDERED, 0, killer->addr, false);
    peer->Send(&resp2killed, HIGH_PRIORITY, RELIABLE_ORDERED, 0, killed->addr, false);
}

int main(int argc, char** argv) {
    PlayerStatus players[100];
    uint32_t colors[] = {
        0xFFFF9900, 0xFF00FF00, 0xFF007BFF, 0xFFFF5EB4, 0xFFFF6100,
        0xFF6F8927, 0xFF323987, 0xFF70374A, 0xFF375B37, 0xFF0000FF
    };
    // So that we don't loop through the entire list.
    int highest_index = 0;

    RakNet::RakPeerInterface* peer = RakNet::RakPeerInterface::GetInstance();
    peer->SetTimeoutTime(30000,RakNet::UNASSIGNED_SYSTEM_ADDRESS);

    RakNet::Packet* packet;

    {
        RakNet::SocketDescriptor descriptor(PORT, "45.63.16.189");
        peer->Startup(MAX_CLIENTS, &descriptor, 1);
    }
    peer->SetMaximumIncomingConnections(MAX_CLIENTS);

    printf("up we go! on port %i\n", PORT);

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
                players[packet->systemAddress.systemIndex].addr = packet->systemAddress;
                players[packet->systemAddress.systemIndex].index = packet->systemAddress.systemIndex;
                players[packet->systemAddress.systemIndex].was_killed = false;
                players[packet->systemAddress.systemIndex].thinks_other_was_killed = false;
                players[packet->systemAddress.systemIndex].kill_streak = 0;
                if (packet->systemAddress.systemIndex > highest_index)
                    highest_index = packet->systemAddress.systemIndex;
                break;
            case ID_NO_FREE_INCOMING_CONNECTIONS:
                printf("The server is full.\n");
                break;
            case ID_DISCONNECTION_NOTIFICATION:
                printf("Player %i has disconnected.\n", packet->systemAddress.systemIndex);
                break;
            case ID_CONNECTION_LOST:
                printf("Player %i lost the connection.\n", packet->systemAddress.systemIndex);
                players[packet->systemAddress.systemIndex].state = NONEXISTENT;
                break;

            // Sent by a player planning to host a game.
            case ID_NEW_HOST: {
                uint32_t color = colors[rand() % (sizeof(colors) / sizeof(uint32_t))];
                printf("Dis guy want a color! %s index: %i, sending %x\n", packet->systemAddress.ToString(true, ':'), packet->systemAddress.systemIndex, color);

                RakNet::BitStream color_resp;

                color_resp.Write((RakNet::MessageID)ID_ASSIGNED_COLOR);
                color_resp.Write(color);
                color_resp.Write(packet->systemAddress.systemIndex);

                peer->Send(&color_resp, HIGH_PRIORITY, RELIABLE_ORDERED, 0, packet->systemAddress, false);

                players[packet->systemAddress.systemIndex].color = color;
                players[packet->systemAddress.systemIndex].state = AWAITING_CLIENT;
            } break;

            // Sent by a player who needs the host list so they know who they can join.
            case ID_QUERY_HOSTS: {
                RakNet::BitStream resp;
                resp.Write((RakNet::MessageID)ID_HOST_LIST);

                int indices[100];
                uint32_t size = 0;

                for (int i = 0; i <= highest_index; i++) {
                    if (players[i].state == AWAITING_CLIENT) {
                        indices[size] = i;
                        size += 1;
                    }
                }

                resp.Write(size);

                for (int i = 0; i < size; i++) {
                    PlayerStatus* player = &players[indices[i]];
                    resp.Write(player->index);
                    resp.Write(player->color);
                }

                printf("Sending host list of %i players to player %i\n", size, packet->systemAddress.systemIndex);

                peer->Send(&resp, HIGH_PRIORITY, RELIABLE_ORDERED, 0, packet->systemAddress, false);
            } break;

            // Sent by a player who just decided to join a host's game.
            case ID_JOIN_GAME: {
                RakNet::BitStream req(packet->data, packet->length, false);
                req.IgnoreBytes(sizeof(RakNet::MessageID));

                RakNet::SystemIndex requested_index;
                if (!req.Read((char*)&requested_index, sizeof(RakNet::SystemIndex))) {
                    printf("Something went wrong reading player %i's requested host to join!\n", packet->systemAddress.systemIndex);
                    break;
                }
                requested_index = ntohs(requested_index);

                players[requested_index].state = ABOUT_TO_PLAY;
                players[requested_index].playing_with_index = packet->systemAddress.systemIndex;
                players[packet->systemAddress.systemIndex].state = ABOUT_TO_PLAY;
                players[packet->systemAddress.systemIndex].playing_with_index = requested_index;

                // We send HOST_STILL_GOOD because the host may have disconnected by the time someone attempts to join them.
                RakNet::BitStream resp;
                resp.Write((RakNet::MessageID)ID_HOST_STILL_GOOD);

                peer->Send(&resp, HIGH_PRIORITY, RELIABLE_ORDERED, 0, players[requested_index].addr, false);

                printf("Player %i joined player %i\n", packet->systemAddress.systemIndex, requested_index);

            } break;

            // Comes from hosts after receiving ID_HOST_STILL_GOOD to indicate that the game shall start.
            case ID_READY_TO_ROLL: {
                PlayerStatus* player1 = &players[packet->systemAddress.systemIndex];
                PlayerStatus* player2 = &players[player1->playing_with_index];

                RakNet::BitStream resp;
                resp.Write((RakNet::MessageID)ID_READY_TO_ROLL);

                player1->state = PLAYING;
                player2->state = PLAYING;

                peer->Send(&resp, HIGH_PRIORITY, RELIABLE_ORDERED, 0, player1->addr, false);
                resp.ResetReadPointer();
                peer->Send(&resp, HIGH_PRIORITY, RELIABLE_ORDERED, 0, player2->addr, false);

                printf("Ready to roll!\n");
            } break;

            case ID_SEND_INPUT: {
                PlayerStatus* player1 = &players[packet->systemAddress.systemIndex];
                PlayerStatus* player2 = &players[player1->playing_with_index];

                RakNet::BitStream req(packet->data, packet->length, false);
                req.IgnoreBytes(sizeof(RakNet::MessageID));

                bool p1_was_killed = req.ReadBit();
                bool p2_was_killed = req.ReadBit();

                bool p1_suicide = req.ReadBit();
                bool p2_suicide = req.ReadBit();

                if (p1_suicide) {
                    printf("Player 1 suicided from their own perspective.\n");
                    register_kill(peer, player2, player1);
                    break;
                }
                if (p2_suicide) {
                    printf("Player 2 suicided from player 1's perspective.\n");
                    register_kill(peer, player1, player2);
                    break;
                }

                player1->was_killed              = player1->was_killed              || p1_was_killed;
                player1->thinks_other_was_killed = player1->thinks_other_was_killed || p2_was_killed;

                RakNet::uint24_t p1_action = 0;
                RakNet::uint24_t p1_cell_x = 0;
                RakNet::uint24_t p1_cell_y = 0;
                bool update_pos = req.ReadBit();

                if (update_pos) {
                    req.Read(p1_action);
                    req.Read(p1_cell_x);
                    req.Read(p1_cell_y);
                }

                if (player1->was_killed && player2->thinks_other_was_killed) {
                    // Player 1 was killed
                    register_kill(peer, player2, player1);
                }
                else if (player2->was_killed && player1->thinks_other_was_killed) {
                    // Player 2 was killed
                    register_kill(peer, player1, player2);
                }

                if (update_pos) {
                    // JUST MOVEMENT
                    RakNet::BitStream resp;
                    resp.Write((RakNet::MessageID)ID_RECEIVE_INPUT);
                    resp.Write(p1_action);
                    resp.Write(p1_cell_x);
                    resp.Write(p1_cell_y);
                    peer->Send(&resp, HIGH_PRIORITY, RELIABLE_ORDERED, 0, player2->addr, false);
                }
            } break;

            default:
                printf("Message with identifier %i has arrived.\n", packet->data[0]);
                break;
            }
        }
    }

    return 0;
}
