#ifndef _SOCKETIO_H
#define _SOCKETIO_H
#include "config.h"
#include "player.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define VARINT_MAX 5

struct readPacketVars_t
{
  uint8_t buffer[READBUFSIZE];
  size_t bufferpos;
  // current packet context
  size_t pktsize;
  size_t pktbytes;
  uint8_t failed;
  // player object
  player_t *player;
};
typedef struct readPacketVars_t readPacketVars_t;

struct sendPacketVars_t
{
  //'main' or final buffer for the client
  uint8_t *buffer;
  size_t buffersize;
  size_t bufferindex;
  // current packet for the player
  uint8_t *packetbuffer;
  size_t packetsize;
  size_t packetindex;
  // prefixed
  size_t packet_prefixed_start;
  size_t packet_prefixed_end;
  // global buffer for all the clients
  uint8_t *globalbuffer;
  size_t globalbuffersize;
  size_t globalbufferindex;
  // local buffer
  char *localbuffer;
  size_t localbuffersize;
  size_t localbufferindex;
  uint8_t failed;
  // Flags
  uint8_t packet_prefixed_active : 1;
  uint8_t global_buffer_active : 1;
  uint8_t localbuffer_active : 1;
  // player object
  player_t *player;
};
typedef struct sendPacketVars_t sendPacketVars_t;
readPacketVars_t *readValues();
uint8_t readAllowed();
uint8_t readFailed();
void readReject();
void readStart(player_t *player);
uint8_t readPeekByte();
uint8_t readByte();
void readBuffer(char *buffer, size_t size);
int16_t readShort();
double readDouble();
float readFloat();
int64_t readLong();
int32_t readVarInt();
void readPosition(int32_t *x, int32_t *y, int32_t *z);
void readString(char *data, size_t maxlen);
// writing utils, global context, should be copied into the main buffer
void sendSwitchToGlobalBuffer();
void sendRevertFromGlobalBuffer();
void sendSwitchToLocalBuffer(char *buf, size_t maxlen);
size_t sendRevertFromLocalBuffer();
size_t sendGetGlobalBufferSpaceRemaining();
size_t sendGetGlobalBufferIndex();
void sendclearGlobalBuffer();
void sendGlobalBuffer(player_t *player);

// writing utils, one player context only.
size_t sendData(uint8_t *data, size_t packetsize, int *blocked);
void sendStartPlayer(player_t *player);
void sendDispatch();
void sendFlush(player_t *player);
uint8_t sendAllowed();
void sendStart();
void sendByte(uint8_t b);
void sendPrefixedStart();
void sendPrefixedEnd();
void sendPlayPacketHeader(size_t id);
void sendConfigurationPacketHeader(size_t id);
void sendBuffer(const char *buf, size_t len);
void sendInt(int32_t v);
void sendShort(int16_t v);
void sendLong(int64_t v);
void sendDouble(double v);
void sendFloat(float v);
void sendDone();
void sendVarInt(int32_t value);
void sendPosition(int32_t x, int32_t y, int32_t z);
void sendString(const char *str, size_t len);
void sendFormattedString(const char *str, size_t len);
void sendUUID(uint16_t seed);
void sendUUIDString(uint16_t seed);
void socketioCleanup();
void socketioLog();
#endif
