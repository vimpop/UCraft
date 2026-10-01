
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wrapper.h"
#include "c2s.h"
#include "config.h"
#include "player.h"
#include "s2c.h"
#include "socketio.h"
#include "util.h"
#include "log.h"
#include "enums.h"
#include "encryption.h"
#include "https.h"

int server_fd = -1;
size_t main_tick = 0;

static void c2sHandler(readPacketVars_t *readPacketValue)
{
    player_t *currentPlayer = readPacketValue->player;
    while (readAllowed())
    {
        if (currentPlayer->remove_player_event)
        {
            return;
        }
        readPacketValue->pktbytes = -1;
        readPacketValue->pktbytes = readVarInt();
        if (readPacketValue->pktbytes > readPacketValue->pktsize || readPacketValue->pktbytes > READBUFSIZE)
        {
            printl(LOG_WARN, "c2sHandler overflow! %ld/%ld\n", readPacketValue->pktbytes, readPacketValue->pktsize);
            currentPlayer->remove_player_event = 1;
            return;
        }
        if (currentPlayer->compression_flag)
        {
            uint8_t compsize = readVarInt();
            if (compsize > 0)
            {
                printl(LOG_WARN, "UNIMPLEMENTED inbound compressed packet size:%d\n", compsize);
                if (readPacketValue->pktbytes)
                {
                    while (readPacketValue->pktbytes)
                    {
                        // printf("%02x ", readByte());
                        readByte();
                    }
                }
                continue;
            }
        }
        uint8_t cmd = readByte();
        switch (currentPlayer->handshake_status)
        {
        case 0:
            if (cmd == 0)
            {
                int32_t protocol_version = readVarInt();
                if (protocol_version != PROTOCOL_VERSION && protocol_version != -1)
                {
                    printl(LOG_WARN, "client not supported! version:%d\n", protocol_version);
                    currentPlayer->remove_player_event = 1;
                }
                uint8_t buf[255];
                readString((char *)buf, 255);
                readShort();
                currentPlayer->handshake_status = readVarInt();
            }
            break;
        case 1: // status state
            switch (cmd)
            {
            case 0x00: // Request
                currentPlayer->playerlist_event = 1;
                break;
            case 0x01:
                readBuffer(currentPlayer->ping_payload, sizeof(currentPlayer->ping_payload));
                currentPlayer->ping_event = 1;
                break;
            default:
                break;
            }
            break;
        case 2: // login state
            switch (cmd)
            {
            case 0:
                if (playerGetActiveCount() >= MAX_PLAYERS)
                {
                    strncpy((char *)currentPlayer->disconnect_reason, "Server full!", sizeof(((player_t *)0)->disconnect_reason));
                    currentPlayer->remove_player_event = 1;
                    continue;
                }
                readString((char *)currentPlayer->name, sizeof(((player_t *)0)->name));
                // check the player name
                if (playerCheckName(currentPlayer))
                {
                    strncpy((char *)currentPlayer->name, "stinky_player", sizeof(((player_t *)0)->name));
                    strncpy((char *)currentPlayer->disconnect_reason, "Invalid player name!", sizeof(((player_t *)0)->disconnect_reason));
                    currentPlayer->remove_player_event = 1;
                    continue;
                }
                // check if any other players exist with the same name
                if (playerCheckDuplicate(currentPlayer))
                {
                    strncpy((char *)currentPlayer->name, "stinky_player", sizeof(((player_t *)0)->name));
                    strncpy((char *)currentPlayer->disconnect_reason, "Player is already in game!", sizeof(((player_t *)0)->disconnect_reason));
                    currentPlayer->remove_player_event = 1;
                    continue;
                }
                printl(LOG_INFO, "Player login name: %s\n", currentPlayer->name);
#ifdef ONLINE_MODE
                currentPlayer->encryption_event = 1;
#else
                currentPlayer->login_event = 1;
#endif /*ONLINE_MODE*/
                break;
            case 1: // encryption response
#ifdef ONLINE_MODE
                LoginC2S_encryption_response();
#ifdef ONLINE_MODE_AUTH
                if (currentPlayer->encryption_verified)
                {
                    if (httpsGetData()->currentPlayer == NULL)
                    {
                        httpsGetPlayerInfo(currentPlayer);
                    }
                    else
                    {
                        currentPlayer->https_wait_event = 1;
                    }
                }
#else
                currentPlayer->login_event = 1;
#endif /*ONLINE_MODE_AUTH*/
                currentPlayer->encryption_recv_event = 1;
#endif /*ONLINE_MODE*/
                break;
            case 3: // Login Acknowledged
                currentPlayer->configuration_event = 1;
                currentPlayer->handshake_status = 3;
            default:
                break;
            }
            break;
        case 3: // config state
            if (cmd < C2S_CONFIGURATION_MAPPING_LEN)
            {
                void (**packet_handler)(player_t *) = c2s_configuration_26_1_2;
                if (packet_handler[cmd] != NULL)
                {
                    if (*packet_handler[cmd] != NULL)
                    {
                        (*packet_handler[cmd])(currentPlayer);
                    }
                }
            }
            break;
        case 4: // play state
            if (cmd < C2S_PLAY_MAPPING_LEN)
            {
                void (**packet_handler)(player_t *) = c2s_play_26_1_2;
                if (packet_handler[cmd] != NULL)
                {
                    if (*packet_handler[cmd] != NULL)
                    {
                        (*packet_handler[cmd])(currentPlayer);
                    }
                }
            }
            break;
        default:
            // invalid state
            printl(LOG_WARN, "Player(%d) invalid state! %d\n", currentPlayer->id, currentPlayer->handshake_status);
            currentPlayer->remove_player_event = 1;
            break;
        }
        // empty the packet buffer
        if (readPacketValue->pktbytes)
        {
            //  printl(LOG_INFO,"discarding packet %d with %d bytes remaining\n", cmd,
            // readPacketValue->pktbytes);
            while (readPacketValue->pktbytes)
            {
                // printf("%02x ", readByte());
                readByte();
            }
        }
    }
}

static void s2cHandler()
{
    for (player_t *currentPlayer = playerGetHead(); currentPlayer != NULL; currentPlayer = currentPlayer->next)
    {
        if (currentPlayer == NULL)
        {
            break;
        }
        sendStartPlayer(currentPlayer);
        if (currentPlayer->ping_event)
        {
            StatusS2Cpong(currentPlayer);
            currentPlayer->remove_player_event = 1;
            currentPlayer->ping_event = 0;
        }
        if (currentPlayer->playerlist_event)
        {
            StatusS2Cresponse(currentPlayer);
            currentPlayer->playerlist_event = 0;
        }
#ifdef ONLINE_MODE_AUTH
        if (currentPlayer->https_wait_event)
        {
            if (httpsGetData()->currentPlayer == NULL)
            {
                httpsGetPlayerInfo(currentPlayer);
                currentPlayer->https_wait_event = 0;
            }
        }
        if (currentPlayer->https_rtr_event)
        {
            currentPlayer->https_rtr_event = httpsRtr(currentPlayer);
        }
        if (currentPlayer->https_rts_event)
        {
            currentPlayer->https_rts_event = httpsRts(currentPlayer);
        }
#endif /*ONLINE_MODE_AUTH*/
        if (currentPlayer->spawn_event)
        {
            PlayS2Clogin(currentPlayer);
            PlayS2Cheartbeat(currentPlayer);
            PlayS2Ctablist(currentPlayer, TABLIST_ACTION_ADDPLAYER | TABLIST_ACTION_LISTED | TABLIST_ACTION_LATENCY, currentPlayer->id);
            PlayS2Centitydata(currentPlayer, PLAYER_SKIN_PARTS_FLAGS, ENTITY_DATA_BYTE, currentPlayer->skin_parts); // enable from cape to hat
            // show player to other clients
            for (player_t *p = playerGetHead(); p != NULL; p = p->next)
            {
                if (p != currentPlayer && p->active)
                {
                    PlayS2Ctablist(p, TABLIST_ACTION_ADDPLAYER | TABLIST_ACTION_LISTED, p->id);
                    PlayS2Cspawnentity(p, ENTITY_METADATA_TYPE_PLAYER);
                    PlayS2Centitydata(p, PLAYER_SKIN_PARTS_FLAGS, ENTITY_DATA_BYTE, p->skin_parts); // enable from cape to hat
                    PlayS2Cteleport(p, p->x, p->y, p->z);
                }
            }
            gamePlayerSpawned(currentPlayer);
            currentPlayer->logged_on = 1;
            currentPlayer->spawn_event = 0;
        }
        if (currentPlayer->configuration_known_packs_ack_event)
        {
            ConfigurationS2Cregistry();
            ConfigurationS2Cupdatetags();
            ConfigurationS2Cready();
            currentPlayer->global_buffer_start_index = sendGetGlobalBufferIndex();
            currentPlayer->ingame = 1;
            currentPlayer->configuration_known_packs_ack_event = 0;
        }
        if (currentPlayer->configuration_event)
        {
            ConfigurationS2Cfeatures();
            ConfigurationS2Cknownpacks();
            currentPlayer->configuration_event = 0;
        }
#ifdef ONLINE_MODE
        if (currentPlayer->encryption_event)
        {
            LoginS2Cencryptionrequest(currentPlayer);
            currentPlayer->encryption_event = 0;
        }
#endif /*ONLINE_MODE*/
        if (currentPlayer->login_event)
        {
#ifdef COMPRESSION
            LoginS2Ccompression(currentPlayer);
#endif /*COMPRESSION*/
            LoginS2Csuccess(currentPlayer);
#ifdef ONLINE_MODE_AUTH
            httpsFreePlayer(currentPlayer);
#endif /*ONLINE_MODE_AUTH*/
            currentPlayer->login_event = 0;
        }
        if (currentPlayer->ingame && currentPlayer->active)
        {
            // printl(LOG_INFO,"player %d ingame\n",currentPlayer->player_id);
            sendRevertFromGlobalBuffer();
            if ((main_tick % 9000) == 0)
            {
                if (currentPlayer->heartbeat == 0)
                {
                    strncpy((char *)currentPlayer->disconnect_reason, "Timed out", sizeof(((player_t *)0)->disconnect_reason));
                    currentPlayer->remove_player_event = 1;
                }
                PlayS2Cheartbeat(currentPlayer);
                currentPlayer->heartbeat = 0;
            }
            if (currentPlayer->teleport)
            {
                // not a huge fan of this since the client does not respect the teleport packet in the local context
                PlayS2Cpositionrotation(currentPlayer, currentPlayer->x, currentPlayer->y, currentPlayer->z);
                currentPlayer->teleport = 0;
            }
            if (currentPlayer->ability)
            {
                if (currentPlayer->ability & ABILILTIES_CLEAR)
                {
                    PlayS2Cplayerabilities(currentPlayer, 0);
                }
                else
                {
                    PlayS2Cplayerabilities(currentPlayer, currentPlayer->ability & 0xf);
                }
                currentPlayer->ability = 0;
            }
            gamePlayerLocalTick(currentPlayer);
            sendGlobalBuffer(currentPlayer);
        }
        // disconnect message case {SHOULD BE THE LAST ONE BEFORE DISPATCHING THE PACKET}
        if (currentPlayer->remove_player_event)
        {
            if (strnlen(currentPlayer->disconnect_reason, sizeof(((player_t *)0)->disconnect_reason)))
            {
                switch (currentPlayer->handshake_status)
                {
                case 1: // status state
                    break;
                case 2: // login state
                    LoginS2Cdisconnect(currentPlayer, currentPlayer->disconnect_reason);
                    break;
                case 3: // config state
                    ConfigurationS2Cdisconnect(currentPlayer, currentPlayer->disconnect_reason);
                    break;
                case 4: // play state
                    PlayS2Cdisconnect(currentPlayer, currentPlayer->disconnect_reason);
                    break;
                default:
                    printl(LOG_WARN, "player %s is in an invalid handshake status upon disconnection!\n", currentPlayer->name);
                }
            }
        }
        sendDispatch();
        sendFlush(currentPlayer);
        currentPlayer->global_buffer_start_index = 0;
        currentPlayer->global_buffer_end_index = 0;
    }
    sendclearGlobalBuffer();

    for (player_t *currentPlayer = playerGetHead(); currentPlayer != NULL; currentPlayer = currentPlayer->next)
    {
        if (currentPlayer->ingame)
        {
            sendStartPlayer(currentPlayer);
            // Global context, send to all clients
            sendSwitchToGlobalBuffer();
            currentPlayer->global_buffer_start_index = sendGetGlobalBufferIndex();
            // add current player to the the tablist as well
            if (currentPlayer->logged_on)
            {
                PlayS2Ctablist(currentPlayer, TABLIST_ACTION_ADDPLAYER | TABLIST_ACTION_LISTED, currentPlayer->id);
                PlayS2Cspawnentity(currentPlayer, ENTITY_METADATA_TYPE_PLAYER);
                PlayS2Centitydata(currentPlayer, PLAYER_SKIN_PARTS_FLAGS, ENTITY_DATA_BYTE, currentPlayer->skin_parts); // enable from cape to hat
                currentPlayer->send_chat_login_event = 1;
                currentPlayer->ready_to_play = 1;
                currentPlayer->logged_on = 0;
            }
            if (currentPlayer->position_event)
            {
                if (currentPlayer->yaw > 180)
                {
                    currentPlayer->yaw -= 360;
                }
                else if (currentPlayer->yaw < -180)
                {
                    currentPlayer->yaw += 360;
                }
                currentPlayer->nyaw = (uint8_t)((int8_t)(currentPlayer->yaw * (float)(256.00F / 360.00F)));
                currentPlayer->npitch = (uint8_t)((int8_t)(currentPlayer->pitch * (float)(256.00F / 360.00F)));
                // check if the player has moved since the last packet
                if (currentPlayer->x != currentPlayer->px || currentPlayer->y != currentPlayer->py || currentPlayer->z != currentPlayer->pz)
                {
                    PlayS2Cteleport(currentPlayer, currentPlayer->x, currentPlayer->y, currentPlayer->z);
                    PlayS2Cheadrotation(currentPlayer);
                    // set the chunk that the player is in
                    currentPlayer->chunk_x = (int32_t)floor(currentPlayer->x / 16.00);
                    currentPlayer->chunk_z = (int32_t)floor(currentPlayer->z / 16.00);
                    // set the previous value to current
                    currentPlayer->px = currentPlayer->x;
                    currentPlayer->py = currentPlayer->y;
                    currentPlayer->pz = currentPlayer->z;
                    currentPlayer->pyaw = currentPlayer->yaw;
                    currentPlayer->ppitch = currentPlayer->pitch;
                }
                if (currentPlayer->pyaw != currentPlayer->yaw || currentPlayer->pitch != currentPlayer->ppitch)
                {
                    PlayS2Crotation(currentPlayer);
                    PlayS2Cheadrotation(currentPlayer);
                    // set the previous value to current
                    currentPlayer->pyaw = currentPlayer->yaw;
                    currentPlayer->pitch = currentPlayer->ppitch;
                }
                currentPlayer->position_event = 0;
            }
            if (currentPlayer->swing_arm_event)
            {
                switch (currentPlayer->swing_arm_animation)
                {
                case 0: // main hand
                    PlayS2Centityanimation(currentPlayer, 0);
                    break;
                case 1: // off hand
                    PlayS2Centityanimation(currentPlayer, 3);
                    break;
                default:
                    break;
                }
                currentPlayer->swing_arm_event = 0;
            }
            if (currentPlayer->entity_action_event)
            {
                if (currentPlayer->entity_action_id & 0x20)
                {
                    PlayS2Centitydata(currentPlayer, ENTITY_POSE, ENTITY_DATA_POSE, STATE_SNEAKING);
                    currentPlayer->sneaking = 1;
                }
                else
                {
                    PlayS2Centitydata(currentPlayer, ENTITY_POSE, ENTITY_DATA_POSE, STATE_STANDING);
                    currentPlayer->sneaking = 0;
                }
                currentPlayer->entity_action_event = 0;
            }
            gamePlayerGlobalTickOthers(currentPlayer);
            currentPlayer->global_buffer_end_index = sendGetGlobalBufferIndex();
            // Code below will brodcast to current player and the rest of the players while above will be for other players only
            if (currentPlayer->chat_event)
            {
                if (currentPlayer->chat_ptr)
                {
                    if (chat_inuse)
                    {
                        PlayS2Csysmessage(currentPlayer->chat_ptr, currentPlayer->chat_len);
                        printl(LOG_INFO, "%s\n", currentPlayer->chat_ptr);
                    }
                    chat_inuse = 0;
                }
                currentPlayer->chat_event = 0;
            }
            if (currentPlayer->send_chat_login_event)
            {
                char join_msg[64];
                snprintf(join_msg, sizeof(join_msg), "§e%s has joined the game", currentPlayer->name);
                PlayS2Csysmessage(join_msg, strnlen(join_msg, sizeof(join_msg)));
                currentPlayer->send_chat_login_event = 0;
            }
            if (currentPlayer->settings_changed_event)
            {
                PlayS2Centitydata(currentPlayer, PLAYER_SKIN_PARTS_FLAGS, ENTITY_DATA_BYTE, currentPlayer->skin_parts);
                currentPlayer->settings_changed_event = 0;
            }
            gamePlayerGlobalTick(currentPlayer);
            sendRevertFromGlobalBuffer();
        }
    }
    sendSwitchToGlobalBuffer();
    // special case for player disconnections
    for (playerd_t *disconnected = playerPopDisconnected(); disconnected != NULL; disconnected = playerPopDisconnected())
    {
        if (playerGetActiveCount() > 0)
        {
            char left_msg[64];
            snprintf(left_msg, sizeof(left_msg), "§e%s has left the game", disconnected->name);
            PlayS2Csysmessage(left_msg, strnlen(left_msg, sizeof(left_msg)));
            PlayS2Ctablistremove(disconnected->id);
            PlayS2Centitydestroy(disconnected->id);
        }
        U_free(disconnected->name);
        U_free(disconnected);
    }
    gameGlobalTick();
    sendRevertFromGlobalBuffer();
}
void UCraftCleanup()
{
    printl(LOG_INFO, "Cleaning up!\n");
    // cleanup
    socketioCleanup();
#ifdef ONLINE_MODE
    encryptionCleanup();
#endif /*ONLINE_MODE*/
    playerCleanup();
#ifdef ONLINE_MODE_AUTH
    httpsCleanup();
#endif /*ONLINE_MODE_AUTH*/
    if (server_fd >= 0)
    {
        U_close(server_fd);
    }
    U_wrapperEnd();
}
int UCraftStart(uint8_t *cleanup_flag)
{
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    fd_set set;
    struct timeval timeout;
    int rv;
    int max_sock = 0;
    U_wrapperStart();
    if (cleanup_flag == NULL)
    {
        printl(LOG_ERROR, "Cleanup flag is NULL\n");
        return 0;
    }
#ifdef ONLINE_MODE
    if (encryptionBegin())
    {
        printl(LOG_ERROR, "Failed to begin encryption 'module'\n");
        encryptionCleanup();
        return 1;
    }
#endif /*ONLINE_MODE*/
    if ((server_fd = U_socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printl(LOG_ERROR, "Cannot create socket fd:%d\n", server_fd);
        UCraftCleanup();
        return 1;
    }
    if (U_setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        printl(LOG_ERROR, "setsockopt SOL_SOCKET error fd:%d\n", server_fd);
        UCraftCleanup();
        return 1;
    }
    if (U_setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)))
    {
        printl(LOG_ERROR, "setsockopt IPPROTO_TCP error fd:%d\n", server_fd);
        UCraftCleanup();
        return 1;
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    if (U_bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        printl(LOG_ERROR, "bind error fd:%d\n", server_fd);
        UCraftCleanup();
        return 1;
    }
    if (U_listen(server_fd, MAX_PLAYERS + 1) < 0)
    {
        printl(LOG_ERROR, "listen error fd:%d\n", server_fd);
        UCraftCleanup();
        return 1;
    }
    printl(LOG_INFO, "Listening on *:%d\n", PORT);
    FD_ZERO(playerGetSet());
    FD_SET(server_fd, playerGetSet());
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    max_sock = server_fd;
    printl(LOG_INFO, "UCraft server started!\n");
    printl(LOG_INFO, "Supported client version: %s\n", CLIENT_VERSION);
    gamePreload();
    while (1)
    {
        uint64_t startms = U_millis();
        if (*cleanup_flag)
        {
            break;
        }
        memcpy(&set, playerGetSet(), sizeof(fd_set));
        rv = U_select(max_sock + 1, &set, NULL, NULL, &timeout);
        if (rv == -1)
        {
            printl(LOG_ERROR, "select error fd:%d\n", server_fd);
            break;
        }
        else
        {
            if (FD_ISSET(server_fd, &set))
            {
                int new_socket = U_accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
                // set the socket non blocking
                if (U_setsocknonblock(new_socket) < 0)
                {
                    printl(LOG_ERROR, "setsocknonblock error fd:%d\n", new_socket);
                    break;
                }
                if (U_setsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)))
                {
                    printl(LOG_ERROR, "setsockopt IPPROTO_TCP error fd:%d\n", new_socket);
                    UCraftCleanup();
                    return 1;
                }
                if (new_socket > max_sock)
                {
                    max_sock = new_socket;
                }
                if (!playerAdd(new_socket))
                {
                    printl(LOG_WARN, "player not added\n");
                    FD_CLR(new_socket, playerGetSet());
                    U_shutdown(new_socket, SHUT_RDWR);
                    U_close(new_socket);
                    continue;
                }
                struct sockaddr_in addr;
                socklen_t addr_size = sizeof(addr);
                U_getpeername(new_socket, (struct sockaddr *)&addr, (socklen_t *)&addr_size);
                printl(LOG_INFO, "Incomming connection from IP: %s\n", U_inet_ntoa(addr.sin_addr));
            }
            for (player_t *player = playerGetHead(); player != NULL;)
            {
                if (player->remove_player_event)
                {
                    // printl(LOG_INFO, "removing player due to flag\n");
                    playerRemove(player);
                    player = NULL;
                    if (playerGetHead() == NULL)
                    {
                        break;
                    }
                    continue;
                }
                if (FD_ISSET(player->fd, &set))
                {
                    readPacketVars_t *readPacketValue = readValues();
                    readPacketValue->bufferpos = 0;
                    ssize_t read_size = U_recv(player->fd, readPacketValue->buffer, READBUFSIZE, 0);
                    if (read_size <= 0 || player->fd < 0)
                    {
                        // printl(LOG_WARN,"Connection closed %d fd:%d\n", player->handshake_status,
                        //  player->player_fd);
                        playerRemove(player);
                        player = NULL;
                        if (playerGetHead() == NULL)
                        {
                            break;
                        }
                        continue;
                    }
                    if (read_size > 0)
                    {
                        readPacketValue->pktsize = read_size;
                    }
#ifdef ONLINE_MODE
                    if (player->encryption_recv_event)
                    {
                        int ret = mbedtls_aes_crypt_cfb8(&player->aes_ctx, MBEDTLS_AES_DECRYPT, readPacketValue->pktsize, player->iv_decrypt, readPacketValue->buffer, readPacketValue->buffer);
                        if (ret != 0)
                        {
                            printl(LOG_ERROR, "aes decrypt failed %d\n", ret);
                            player->remove_player_event = 1;
                            continue;
                        }
                    }
#endif /*ONLINE_MODE*/
                    readStart(player);
                    c2sHandler(readPacketValue);
                }
                player = player->next;
            }
        }
        s2cHandler();
        uint64_t endms = U_millis();
        if ((endms - startms) < TICK_TIME_MS)
        {
            U_sleep(TICK_TIME_MS - (endms - startms));
        }
        else
        {
            printl(LOG_WARN, "Server is lagging over %ldms\n", (endms - startms) - TICK_TIME_MS);
        }
        main_tick++;
    }
    gameCleanup();
    UCraftCleanup();
    return 0;
}
