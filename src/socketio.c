
#include "wrapper.h"
#include "s2c.h"
#include "socketio.h"
#include "log.h"
#include "util.h"
#ifdef COMPRESSION
#include "zlib.h"
#endif

static readPacketVars_t readPacketVars = {.pktbytes = -1};

readPacketVars_t *readValues() { return (readPacketVars_t *)&readPacketVars; }
uint8_t readAllowed() { return !readPacketVars.failed && readPacketVars.bufferpos < readPacketVars.pktsize; }
uint8_t readFailed() { return readPacketVars.failed; }
void readReject()
{
  readPacketVars.failed = 1;
  readPacketVars.pktbytes = 0;
  if (readPacketVars.player) readPacketVars.player->remove_player_event = 1;
}
void readStart(player_t *player) { readPacketVars.player = player; }
uint8_t readPeekByte()
{
  if (readPacketVars.failed || readPacketVars.bufferpos >= readPacketVars.pktsize ||
      readPacketVars.bufferpos >= sizeof(readPacketVars.buffer))
  {
    readReject();
    return 0;
  }
  return readPacketVars.buffer[readPacketVars.bufferpos++];
}
uint8_t readByte()
{
  if (!readPacketVars.pktbytes) { readReject(); return 0; }
  readPacketVars.pktbytes--;
  return readPeekByte();
}
void readBuffer(char *buffer, size_t size)
{
  if (!buffer || readPacketVars.failed || readPacketVars.bufferpos > readPacketVars.pktsize ||
      readPacketVars.pktsize > sizeof(readPacketVars.buffer) ||
      size > readPacketVars.pktsize - readPacketVars.bufferpos || size > readPacketVars.pktbytes)
  {
    readReject();
    return;
  }
  memcpy(buffer, readPacketVars.buffer + readPacketVars.bufferpos, size);
  readPacketVars.bufferpos += size;
  readPacketVars.pktbytes -= size;
}
int16_t readShort()
{
  int16_t ret = 0;
  ret |= readByte() << 8;
  ret |= readByte();
  return ret;
}
double readDouble()
{
  uint64_t c = 0;
  double v;
  readBuffer((char *)&c, sizeof(uint64_t));
#if (ENDIAN)
  c = __builtin_bswap64(c);
#endif
  memcpy(&v, &c, sizeof(double));
  return v;
}
float readFloat()
{
  uint32_t c = 0;
  float v;
  readBuffer((char *)&c, sizeof(uint32_t));
#if (ENDIAN)
  c = __builtin_bswap32(c);
#endif
  memcpy(&v, &c, sizeof(float));
  return v;
}
int64_t readLong()
{
  int64_t c = 0;
  readBuffer((char *)&c, sizeof(int64_t));
#if (ENDIAN)
  c = __builtin_bswap64(c);
#endif
  return c;
}
int32_t readVarInt()
{
  uint32_t value = 0;
  for (unsigned i = 0; i < VARINT_MAX; i++)
  {
    uint8_t byte = readByte();
    if (readFailed()) return 0;
    if (i == 4 && (byte & 0xf0)) { readReject(); return 0; }
    value |= (uint32_t)(byte & 0x7f) << (7 * i);
    if (!(byte & 0x80)) return (int32_t)value;
  }
  readReject();
  return 0;
}
void readPosition(int32_t *x, int32_t *y, int32_t *z)
{
  uint64_t pos = 0;
  readBuffer((char *)&pos, sizeof(uint64_t));
#if (ENDIAN)
  pos = __builtin_bswap64(pos);
#endif
  *x = (pos >> 38) & 0x3FFFFFF;
  *z = (pos >> 12) & 0x3FFFFFF;
  *y = pos & 0xFFF;

  if (*x >= 1 << 25)
  {
    *x -= 1 << 26;
  }
  if (*y >= 1 << 11)
  {
    *y -= 1 << 12;
  }
  if (*z >= 1 << 25)
  {
    *z -= 1 << 26;
  }
}

void readString(char *data, size_t maxlen)
{
  int32_t length = readVarInt();
  if (!data || maxlen == 0 || readFailed() || length < 0 || (size_t)length >= maxlen)
  {
    if (data && maxlen) data[0] = 0;
    readReject();
    return;
  }
  readBuffer(data, (size_t)length);
  if (!readFailed()) data[length] = 0;
}

// Writing utils
static sendPacketVars_t sendPacketVars;
static void sendFail(void)
{
  sendPacketVars.failed = 1;
  if (sendPacketVars.player) sendPacketVars.player->remove_player_event = 1;
}

void sendSwitchToGlobalBuffer()
{
  if (sendPacketVars.globalbuffer == NULL)
  {
    sendPacketVars.globalbuffersize = MEM_CHUNK_SIZE;
    if ((sendPacketVars.globalbuffer = U_malloc(sendPacketVars.globalbuffersize)) == NULL)
    {
      printl(LOG_ERROR, "malloc failed globalbuffer\n");
      sendFail();
      return;
    }
  }
  sendPacketVars.global_buffer_active = 1;
}
void sendRevertFromGlobalBuffer()
{
  sendPacketVars.global_buffer_active = 0;
}

void sendSwitchToLocalBuffer(char *buf, size_t maxlen)
{
  sendPacketVars.localbuffer = buf;
  sendPacketVars.localbuffersize = maxlen;
  sendPacketVars.localbuffer_active = 1;
}
size_t sendRevertFromLocalBuffer()
{
  size_t len = sendPacketVars.localbufferindex;
  sendPacketVars.localbuffer = 0;
  sendPacketVars.localbuffersize = 0;
  sendPacketVars.localbuffer_active = 0;
  sendPacketVars.localbufferindex = 0;
  return len;
}
static void send_main_byte(uint8_t byte)
{
  if (sendPacketVars.failed) return;
  if (sendPacketVars.bufferindex >= sendPacketVars.buffersize)
  {
    uint8_t *buffer = NULL;
    // allocate the required memory
    size_t new_size = sendPacketVars.buffersize + MEM_CHUNK_SIZE;
    buffer = U_realloc(sendPacketVars.buffer, new_size);
    if (buffer == NULL)
    {
      printl(LOG_ERROR, "memory allocation failed buffer!\n");
      sendFail();
      return;
    }
    // printl(LOG_INFO,"Buffer size: %ld\n", sendPacketVars.buffersize);
    sendPacketVars.buffer = buffer;
    sendPacketVars.buffersize = new_size;
  }
  sendPacketVars.buffer[sendPacketVars.bufferindex++] = byte;
}
static void send_raw_byte(uint8_t b)
{
  if (sendPacketVars.failed) return;
  if (sendPacketVars.global_buffer_active)
  {
    if (sendPacketVars.globalbufferindex >= sendPacketVars.globalbuffersize)
    {
      uint8_t *buffer = NULL;
      // allocate the required memory
      size_t new_size = sendPacketVars.globalbuffersize + MEM_CHUNK_SIZE;
      buffer = U_realloc(sendPacketVars.globalbuffer, new_size);
      if (buffer == NULL)
      {
        printl(LOG_ERROR, "Memory allocation failed globalbuffersize!\n");
        sendFail();
        return;
      }
      // printl(LOG_INFO,"Buffer size: %ld %ld\n", sendPacketVars.globalbuffersize,sendPacketVars.globalbufferindex);
      sendPacketVars.globalbuffer = buffer;
      sendPacketVars.globalbuffersize = new_size;
    }
    sendPacketVars.globalbuffer[sendPacketVars.globalbufferindex++] = b;
  }
  else
  {
    send_main_byte(b);
  }
}

size_t sendGetGlobalBufferSpaceRemaining() { return sendPacketVars.globalbuffersize - sendPacketVars.globalbufferindex; }

size_t sendGetGlobalBufferIndex() { return sendPacketVars.globalbufferindex; }
void sendclearGlobalBuffer()
{
  sendPacketVars.globalbufferindex = 0;
  // player->global_buffer_start_index = start_index;
  //  free uneeded space if its more than MEM_CHUNK_THRESHOLD chunk sizes
  if (sendPacketVars.globalbuffer != NULL)
  {
    if ((ssize_t)((sendPacketVars.globalbuffersize - sendPacketVars.globalbufferindex) / MEM_CHUNK_SIZE) >= MEM_CHUNK_THRESHOLD)
    {
      uint8_t *buffer = NULL;

      // printl(LOG_INFO, "extra memory can be freed %ld new: %ld\n", (sendPacketVars.globalbuffersize - sendPacketVars.globalbufferindex), sendPacketVars.globalbufferindex);
      buffer = U_realloc(sendPacketVars.globalbuffer, sendPacketVars.globalbufferindex + 1);
      if (buffer == NULL)
      {
        printl(LOG_ERROR, "Memory de/allocation failed!\n");
        sendFail();
        return;
      }
      sendPacketVars.globalbuffer = buffer;
      sendPacketVars.globalbuffersize = sendPacketVars.globalbufferindex + 1;
    }
  }
}
void sendGlobalBuffer(player_t *player)
{
  // send bytes from 0 to start which is not part of this player
  if (player->global_buffer_start_index > 0)
  {
    for (size_t i = 0; i < player->global_buffer_start_index; i++)
    {
      send_main_byte(sendPacketVars.globalbuffer[i]);
    }
  }
  // send bytes from the end till the global buffer end
  for (size_t i = player->global_buffer_end_index; i < sendGetGlobalBufferIndex(); i++)
  {
    send_main_byte(sendPacketVars.globalbuffer[i]);
  }
}
size_t sendData(uint8_t *data, size_t buffersize, int *blocked)
{
  int sock = sendPacketVars.player->fd;
  size_t totalSent = 0;
  if (blocked != NULL)
  {
    *blocked = 0;
  }
  // split the packet in fragments
  while (totalSent < buffersize)
  {
    size_t remaining = buffersize - totalSent;
    size_t blockSize = remaining < MAX_SEND_FRAGMENT_SIZE ? remaining : MAX_SEND_FRAGMENT_SIZE;
    ssize_t r = U_send(sock, (char *)data + totalSent, blockSize, MSG_NOSIGNAL);

    if (r < 0)
    {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        if (blocked != NULL)
        {
          *blocked = 1;
        }
        return totalSent;
      }
      printl(LOG_ERROR, "could not send (%d) code %ld (%p %ld)\n", sock, r, data, totalSent);
      printl(LOG_ERROR, "errno: %s\n", strerror(errno));
      sendFail();
      return totalSent;
    }
    if (r == 0)
    {
      printl(LOG_ERROR, "could not send (%d) code %ld (%p %ld)\n", sock, r, data, totalSent);
      sendFail();
      return totalSent;
    }
    totalSent += r;
  }
  return totalSent;
}
void sendStartPlayer(player_t *player)
{
  sendPacketVars.player = player;
  sendPacketVars.failed = 0;
  if (sendPacketVars.buffer == NULL)
  {
    sendPacketVars.buffersize = MEM_CHUNK_SIZE;
    if ((sendPacketVars.buffer = U_malloc(sendPacketVars.buffersize)) == NULL)
    {
      printl(LOG_ERROR, "malloc failed buffer\n");
      sendFail();
      return;
    }
  }
  sendPacketVars.bufferindex = 0;

  // free uneeded space if its more than MEM_CHUNK_THRESHOLD chunk sizes
  if ((ssize_t)((sendPacketVars.buffersize - sendPacketVars.bufferindex) / MEM_CHUNK_SIZE) >= MEM_CHUNK_THRESHOLD)
  {
    uint8_t *buffer = NULL;

    // printl(LOG_INFO,"extra memory can be freed %ld new: %ld\n", (sendPacketVars.buffersize - sendPacketVars.bufferindex) / MEM_CHUNK_SIZE, sendPacketVars.bufferindex);
    buffer = U_realloc(sendPacketVars.buffer, sendPacketVars.bufferindex + 1);
    if (buffer == NULL)
    {
      printl(LOG_ERROR, "Memory de/allocation failed buffer!\n");
      sendFail();
      return;
    }
    sendPacketVars.buffer = buffer;
    sendPacketVars.buffersize = sendPacketVars.bufferindex + 1;
  }
}

static void sendQueueData(const uint8_t *data, size_t len)
{
  if (len == 0)
  {
    return;
  }
  if (sendPacketVars.player == NULL)
  {
    return;
  }
  out_packet_t *pkt = U_malloc(sizeof(out_packet_t));
  if (pkt == NULL)
  {
    printl(LOG_ERROR, "Memory allocation failed packet queue!\n");
    sendFail();
    return;
  }
  pkt->data = U_malloc(len);
  if (pkt->data == NULL)
  {
    printl(LOG_ERROR, "Memory allocation failed packet data!\n");
    U_free(pkt);
    sendFail();
    return;
  }
  memcpy(pkt->data, data, len);
  pkt->len = len;
  pkt->sent = 0;
  pkt->next = NULL;
  if (sendPacketVars.player->out_tail)
  {
    sendPacketVars.player->out_tail->next = pkt;
  }
  else
  {
    sendPacketVars.player->out_head = pkt;
  }
  sendPacketVars.player->out_tail = pkt;
}
void sendDispatch()
{
  if (sendPacketVars.failed) return;
  // send the data
  if (sendPacketVars.bufferindex != 0)
  {
    if (sendPacketVars.player->out_head != NULL)
    {
      sendFlush(sendPacketVars.player);
    }
#ifdef ONLINE_MODE
    if (sendPacketVars.player->encryption_recv_event)
    {
      int ret = mbedtls_aes_crypt_cfb8(&sendPacketVars.player->aes_ctx, MBEDTLS_AES_ENCRYPT, sendPacketVars.bufferindex, sendPacketVars.player->iv_encrypt, sendPacketVars.buffer, sendPacketVars.buffer);
      if (ret != 0)
      {
        printl(LOG_ERROR, "aes encrypt failed %d\n", ret);
        return;
      }
    }
#endif /*ONLINE_MODE*/
    if (sendPacketVars.player->out_head != NULL)
    {
      sendQueueData(sendPacketVars.buffer, sendPacketVars.bufferindex);
      sendPacketVars.bufferindex = 0;
      return;
    }
    int blocked = 0;
    size_t sent = sendData(sendPacketVars.buffer, sendPacketVars.bufferindex, &blocked);
    if (blocked && sent < sendPacketVars.bufferindex)
    {
      sendQueueData(sendPacketVars.buffer + sent, sendPacketVars.bufferindex - sent);
    }
    sendPacketVars.bufferindex = 0;
  }
}
void sendFlush(player_t *player)
{
  if (player == NULL)
  {
    return;
  }
  while (player->out_head)
  {
    out_packet_t *pkt = player->out_head;
    if (pkt->sent >= pkt->len)
    {
      player->out_head = pkt->next;
      if (player->out_head == NULL)
      {
        player->out_tail = NULL;
      }
      U_free(pkt->data);
      U_free(pkt);
      continue;
    }
    size_t remaining = pkt->len - pkt->sent;
    size_t blockSize = remaining < MAX_SEND_FRAGMENT_SIZE ? remaining : MAX_SEND_FRAGMENT_SIZE;
    ssize_t r = U_send(player->fd, (char *)pkt->data + pkt->sent, blockSize, MSG_NOSIGNAL);
    if (r < 0)
    {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        return;
      }
      printl(LOG_ERROR, "could not send (%d) code %ld (%p %ld)\n", player->fd, r, pkt->data, pkt->sent);
      printl(LOG_ERROR, "errno: %s\n", strerror(errno));
      player->remove_player_event = 1;
      return;
    }
    if (r == 0)
    {
      printl(LOG_ERROR, "could not send (%d) code %ld (%p %ld)\n", player->fd, r, pkt->data, pkt->sent);
      player->remove_player_event = 1;
      return;
    }
    pkt->sent += (size_t)r;
    if (pkt->sent == pkt->len)
    {
      player->out_head = pkt->next;
      if (player->out_head == NULL)
      {
        player->out_tail = NULL;
      }
      U_free(pkt->data);
      U_free(pkt);
    }
  }
}
uint8_t sendAllowed() { return 1; }
void sendStart()
{
  if (sendPacketVars.packetbuffer == NULL)
  {
    sendPacketVars.packetsize = MEM_CHUNK_SIZE;
    if ((sendPacketVars.packetbuffer = U_malloc(sendPacketVars.packetsize)) == NULL)
    {
      printl(LOG_ERROR, "U_malloc failed packetbuffer\n");
      sendFail();
      return;
    }
  }
  sendPacketVars.packetindex = 0;
  sendPacketVars.packet_prefixed_start = 0;
  sendPacketVars.packet_prefixed_end = 0;
  sendPacketVars.packet_prefixed_active = 0;
}
void sendByte(uint8_t b)
{
  if (sendPacketVars.failed) return;

  if (sendPacketVars.localbuffer_active)
  {
    if (sendPacketVars.localbufferindex >= sendPacketVars.localbuffersize)
    {
      printl(LOG_ERROR, "local buffer no more space! %ld/%ld\n", sendPacketVars.localbufferindex, sendPacketVars.localbuffersize);
      sendFail();
      return;
    }
    if (sendPacketVars.localbuffer == NULL)
    {
      printl(LOG_ERROR, "local buffer NULL\n");
      sendFail();
      return;
    }
    sendPacketVars.localbuffer[sendPacketVars.localbufferindex++] = b;
  }
  else
  {
    if (sendPacketVars.packetindex >= sendPacketVars.packetsize)
    {
      uint8_t *buffer = NULL;
      // allocate the required memory
      size_t new_size = sendPacketVars.packetsize + MEM_CHUNK_SIZE;
      buffer = U_realloc(sendPacketVars.packetbuffer, new_size);
      if (buffer == NULL)
      {
        printl(LOG_ERROR, "Memory allocation failed!\n");
        sendFail();
        return;
      }
      // printl(LOG_INFO,"Buffer size: %ld\n", sendPacketVars.packetsize);
      sendPacketVars.packetbuffer = buffer;
      sendPacketVars.packetsize = new_size;
    }
    sendPacketVars.packetbuffer[sendPacketVars.packetindex++] = b;
  }
}
// TODO: Make it handle multiple contexts for the same packet window (from sendStart till sendDone if needed)
void sendPrefixedStart()
{
  if (sendPacketVars.localbuffer_active)
  {
    printl(LOG_ERROR, "prefixed segment not supported in local buffer\n");
    sendFail();
    return;
  }
  if (sendPacketVars.packet_prefixed_active)
  {
    printl(LOG_ERROR, "prefixed segment already active\n");
    sendFail();
    return;
  }
  sendPacketVars.packet_prefixed_start = sendPacketVars.packetindex;
  sendPacketVars.packet_prefixed_active = 1;
}
void sendPrefixedEnd()
{
  if (sendPacketVars.localbuffer_active)
  {
    printl(LOG_ERROR, "prefixed segment not supported in local buffer\n");
    sendFail();
    return;
  }
  if (!sendPacketVars.packet_prefixed_active)
  {
    printl(LOG_ERROR, "prefixed segment start not set\n");
    sendFail();
    return;
  }
  sendPacketVars.packet_prefixed_end = sendPacketVars.packetindex;
}

void sendPlayPacketHeader(size_t id)
{
  if (id < S2C_PLAY_MAPPING_LEN)
  {
    sendByte(id);
    return;
  }
  printl(LOG_ERROR, "Incorrect PLAY packet id ID:%ld player: %d\n", id, sendPacketVars.player->id);
  sendFail();
}
void sendConfigurationPacketHeader(size_t id)
{
  if (id < S2C_CONFIGURATION_MAPPING_LEN)
  {
    sendByte(id);
    return;
  }
  printl(LOG_ERROR, "Incorrect CONFIG packet id ID:%ld player: %d\n", id, sendPacketVars.player->id);
  sendFail();
}
void sendBuffer(const char *buf, size_t len)
{
  for (size_t i = 0; i < len; i++)
  {
    sendByte(buf[i]);
  }
}
void sendInt(int32_t v)
{
  int32_t c;
#if (ENDIAN)
  c = __builtin_bswap32(v);
#else
  c = v;
#endif
  sendBuffer((char *)&c, sizeof(int32_t));
}
void sendShort(int16_t v)
{
  uint16_t c;
#if (ENDIAN)
  c = __builtin_bswap16(v);
#else
  c = v;
#endif
  sendBuffer((char *)&c, sizeof(int16_t));
}
void sendLong(int64_t v)
{
  int64_t c = 0;
#if (ENDIAN)
  c = __builtin_bswap64(v);
#else
  c = v;
#endif
  sendBuffer((char *)&c, sizeof(int64_t));
}
void sendDouble(double v)
{
  uint64_t c = 0;
  memcpy(&c, &v, sizeof(uint64_t));
#if (ENDIAN)
  c = __builtin_bswap64(c);
#endif
  sendBuffer((char *)&c, sizeof(uint64_t));
}
void sendFloat(float v)
{
  uint32_t c = 0;
  memcpy(&c, &v, sizeof(uint32_t));
#if (ENDIAN)
  c = __builtin_bswap32(c);
#endif
  sendBuffer((char *)&c, sizeof(uint32_t));
}
static void send_raw_data(char *dat, size_t len)
{
  for (size_t i = 0; i < len; i++)
  {
    send_raw_byte(dat[i]);
  }
}
static size_t send_raw_varint(int32_t v)
{
  size_t i;
  for (i = 0; i < VARINT_MAX; i++)
  {
    if ((v & ~0x7F) == 0)
    {
      send_raw_byte(v);
      break;
    }

    send_raw_byte((v & 0x7F) | 0x80);
    v = (uint32_t)v >> 7;
  }
  return i;
}
static size_t get_varint_len(int32_t v)
{
  uint32_t u = (uint32_t)v;

  if ((u & ~0x7Fu) == 0)
    return 1;
  if ((u & ~0x3FFFu) == 0)
    return 2;
  if ((u & ~0x1FFFFFu) == 0)
    return 3;
  if ((u & ~0x0FFFFFFFu) == 0)
    return 4;
  return 5;
}

static void send_uncompressed()
{
  size_t packet_len = sendPacketVars.packetindex;
  size_t segment_len = 0;
  if (sendPacketVars.packet_prefixed_active)
  {
    if (sendPacketVars.packet_prefixed_end < sendPacketVars.packet_prefixed_start || sendPacketVars.packet_prefixed_end > sendPacketVars.packetindex)
    {
      printl(LOG_ERROR, "prefixed segment range invalid\n");
      sendFail();
      sendPacketVars.packet_prefixed_active = 0;
      return;
    }
    segment_len = sendPacketVars.packet_prefixed_end - sendPacketVars.packet_prefixed_start;
    packet_len += get_varint_len((int32_t)segment_len);
  }
  // construct the packet header
  if (sendPacketVars.player->compression_flag)
  {
    packet_len++;
  }
  send_raw_varint(packet_len);
  // varint for the 'Data Length' field
  if (sendPacketVars.player->compression_flag)
  {
    send_raw_varint(0);
  }
  // send the marked buffer with its size
  if (sendPacketVars.packet_prefixed_active)
  {
    // send the data prior to the marker
    send_raw_data((char *)sendPacketVars.packetbuffer, sendPacketVars.packet_prefixed_start);
    // send the buffer length prefix
    send_raw_varint((int32_t)segment_len);
    // send the marked buffer
    send_raw_data((char *)&sendPacketVars.packetbuffer[sendPacketVars.packet_prefixed_start], segment_len);
    // send the remaining packet
    send_raw_data((char *)&sendPacketVars.packetbuffer[sendPacketVars.packet_prefixed_end], sendPacketVars.packetindex - sendPacketVars.packet_prefixed_end);
    sendPacketVars.packet_prefixed_active = 0;
  }
  else
  {
    send_raw_data((char *)sendPacketVars.packetbuffer, sendPacketVars.packetindex);
  }
}
#ifdef COMPRESSION

static size_t varint_buffer(unsigned char buffer[5], int32_t v)
{
  if (buffer == NULL)
  {
    return 0;
  }
  size_t i;
  for (i = 0; i < VARINT_MAX; i++)
  {
    if ((v & ~0x7F) == 0)
    {
      *buffer++ = (unsigned char)v;
      return i + 1;
    }
    *buffer++ = (unsigned char)((v & 0x7F) | 0x80);
    v = (uint32_t)v >> 7;
  }
  return VARINT_MAX;
}

static void *my_zalloc(void *opaque, uInt items, uInt size)
{
  (void)opaque;
  return U_calloc(items, size);
}

static void my_zfree(void *opaque, voidpf address)
{
  (void)opaque;
  U_free(address);
}

static int push_raw_compressed(z_streamp strm, unsigned char *data, size_t size, int flag)
{
  unsigned char buffer[512];
  int rc;
  strm->next_in = data;
  strm->avail_in = size;
  strm->next_out = buffer;
  strm->avail_out = sizeof(buffer);
  for (;;)
  {
    rc = deflate(strm, flag);
    if (sizeof(buffer) - strm->avail_out > 0)
    {
      send_raw_data((char *)buffer, sizeof(buffer) - strm->avail_out);
      if (sendPacketVars.failed) return Z_MEM_ERROR;
    }

    if (rc == Z_STREAM_END)
    {
      return Z_OK;
    }
    if ((rc != Z_OK) && (rc != Z_BUF_ERROR))
    {
      return rc;
    }

    if (strm->avail_out == 0)
    {
      strm->next_out = buffer;
      strm->avail_out = sizeof(buffer);
      continue;
    }

    if (flag == Z_FINISH)
    {
      if (rc == Z_BUF_ERROR)
      {
        return rc;
      }
      strm->next_out = buffer;
      strm->avail_out = sizeof(buffer);
      continue;
    }

    if (strm->avail_in == 0)
    {
      return Z_OK;
    }
  }
  return Z_OK;
}

static void send_compressed()
{
  int rc;
  unsigned char value[5];
  unsigned char *pkt = NULL;
  z_stream strm;
  size_t data_len = sendPacketVars.packetindex;
  size_t segment_len = 0;
  size_t data_len_varint_len;
  size_t len;
  size_t raw_start = sendPacketVars.global_buffer_active ? sendPacketVars.globalbufferindex : sendPacketVars.bufferindex;

  if (sendPacketVars.packet_prefixed_active)
  {
    if (sendPacketVars.packet_prefixed_end < sendPacketVars.packet_prefixed_start || sendPacketVars.packet_prefixed_end > sendPacketVars.packetindex)
    {
      printl(LOG_ERROR, "prefixed segment range invalid\n");
      sendFail();
      sendPacketVars.packet_prefixed_active = 0;
      return;
    }
    segment_len = sendPacketVars.packet_prefixed_end - sendPacketVars.packet_prefixed_start;
    data_len += get_varint_len((int32_t)segment_len);
  }
  data_len_varint_len = get_varint_len((int32_t)data_len);
  memset(&strm, 0, sizeof(strm));
  strm.zalloc = my_zalloc;
  strm.zfree = my_zfree;
  strm.opaque = Z_NULL;
  // windowBits=9, memLevel=1, 9KB
  // 5968+(4×512)+ (256×2)+ (128×4) = 9040 bytes
  rc = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 9, 1, Z_DEFAULT_STRATEGY);
  if (rc != Z_OK)
  {
    printl(LOG_ERROR, "failed to init deflate rc: %d\n", rc);
    sendFail();
    sendPacketVars.packet_prefixed_active = 0;
    return;
  }
  send_raw_varint(-2147483648);       // Packet Length, encode 5 bytes and populate it later
  send_raw_varint((int32_t)data_len); // Data length
  // send the marked buffer with its size
  if (sendPacketVars.packet_prefixed_active)
  {
    // send the data prior to the marker
    rc = push_raw_compressed(&strm, sendPacketVars.packetbuffer, sendPacketVars.packet_prefixed_start, Z_SYNC_FLUSH);
    if (rc != Z_OK) goto compression_error;
    // send the buffer length prefix
    len = varint_buffer(value, (int32_t)segment_len);
    rc = push_raw_compressed(&strm, value, len, Z_SYNC_FLUSH);
    if (rc != Z_OK) goto compression_error;
    // send the marked buffer
    rc = push_raw_compressed(&strm, (unsigned char *)&sendPacketVars.packetbuffer[sendPacketVars.packet_prefixed_start], segment_len, Z_SYNC_FLUSH);
    if (rc != Z_OK) goto compression_error;
    // send the remaining packet
    rc = push_raw_compressed(&strm, (unsigned char *)&sendPacketVars.packetbuffer[sendPacketVars.packet_prefixed_end], sendPacketVars.packetindex - sendPacketVars.packet_prefixed_end, Z_SYNC_FLUSH);
    if (rc != Z_OK) goto compression_error;
    sendPacketVars.packet_prefixed_active = 0;
  }
  else
  {
    rc = push_raw_compressed(&strm, sendPacketVars.packetbuffer, data_len, Z_SYNC_FLUSH);
    if (rc != Z_OK) goto compression_error;
  }
  rc = push_raw_compressed(&strm, Z_NULL, 0, Z_FINISH);
  if (rc != Z_OK) goto compression_error;
  deflateEnd(&strm);
  // i am not a fan of this but this is the only way i know to populate the actual size
  if (sendPacketVars.global_buffer_active)
  {
    pkt = &sendPacketVars.globalbuffer[sendPacketVars.globalbufferindex] - strm.total_out - data_len_varint_len - VARINT_MAX;
    len = varint_buffer(pkt, (int32_t)(data_len_varint_len + strm.total_out));
    memmove(pkt + len, pkt + VARINT_MAX, strm.total_out + data_len_varint_len);
    sendPacketVars.globalbufferindex -= (VARINT_MAX - len);
  }
  else
  {
    pkt = &sendPacketVars.buffer[sendPacketVars.bufferindex] - strm.total_out - data_len_varint_len - VARINT_MAX;
    len = varint_buffer(pkt, (int32_t)(data_len_varint_len + strm.total_out));
    memmove(pkt + len, pkt + VARINT_MAX, strm.total_out + data_len_varint_len);
    sendPacketVars.bufferindex -= (VARINT_MAX - len);
  }
  return;
compression_error:
  deflateEnd(&strm);
  if (sendPacketVars.global_buffer_active) sendPacketVars.globalbufferindex = raw_start;
  else sendPacketVars.bufferindex = raw_start;
  sendPacketVars.packet_prefixed_active = 0;
  sendFail();

}
#endif
void sendDone()
{
  if (sendPacketVars.failed) return;
#ifdef COMPRESSION
  if ((sendPacketVars.packetindex >= COMPRESSION_THRESHOLD) && sendPacketVars.player->compression_flag)
  {
    send_compressed();
  }
  else
  {
    send_uncompressed();
  }
#else
  send_uncompressed();
#endif
  if (sendPacketVars.failed) return;
  // free uneeded space if its more than MEM_CHUNK_THRESHOLD chunk sizes
  if ((ssize_t)((sendPacketVars.packetsize - sendPacketVars.packetindex) / MEM_CHUNK_SIZE) >= MEM_CHUNK_THRESHOLD)
  {
    uint8_t *buffer = NULL;

    // printl(LOG_INFO,"extra memory can be freed %ld new: %ld\n", (sendPacketVars.packetsize - sendPacketVars.packetindex) / MEM_CHUNK_SIZE, sendPacketVars.packetindex);
    buffer = U_realloc(sendPacketVars.packetbuffer, sendPacketVars.packetindex + 1);
    if (buffer == NULL)
    {
      printl(LOG_ERROR, "Memory de/allocation failed packet!\n");
      sendFail();
      return;
    }
    sendPacketVars.packetbuffer = buffer;
    sendPacketVars.packetsize = sendPacketVars.packetindex + 1;
  }
}

void sendVarInt(int32_t value)
{
  for (int i = 0; i < VARINT_MAX; i++)
  {
    if ((value & ~0x7F) == 0)
    {
      sendByte(value);
      return;
    }

    sendByte((value & 0x7F) | 0x80);

    value = (uint32_t)value >> 7;
  }
}
void sendPosition(int32_t x, int32_t y, int32_t z)
{
  uint64_t pos = (((uint64_t)x & 0x3FFFFFF) << 38) | (((uint64_t)z & 0x3FFFFFF) << 12) | ((uint64_t)y & 0xFFF);
#if (ENDIAN)
  pos = __builtin_bswap64(pos);
#endif
  sendBuffer((char *)&pos, sizeof(uint64_t));
}
void sendString(const char *str, size_t len)
{
  if (str == NULL)
  {
    printl(LOG_ERROR, "Send string failed! string is null!\n");
    sendFail();
    return;
  }
  if (len == (size_t)(-1))
  {
    len = strnlen(str, MAX_STRING_SIZE);
  }
  if (len > MAX_STRING_SIZE)
  {
    printl(LOG_ERROR, "Send string failed! len(%ld) > %ld\n", len, (size_t)MAX_STRING_SIZE);
    sendFail();
    return;
  }
  sendVarInt(len);
  for (uint32_t i = 0; i < len; i++)
  {
    sendByte(str[i]);
  }
}
void sendFormattedString(const char *str, size_t len)
{
  static const unsigned char NBT_text[] = {
      0x0A, 0x08, 0x00, 0x04, 0x74, 0x65, 0x78, 0x74};
  if (str == NULL)
  {
    printl(LOG_ERROR, "Send formatted string failed! string is null!\n");
    sendFail();
    return;
  }
  if (len == (size_t)(-1))
  {
    len = strnlen(str, MAX_STRING_SIZE);
  }
  if (len > MAX_STRING_SIZE)
  {
    printl(LOG_ERROR, "Send formatted string failed! len(%ld) > %ld\n", len, (size_t)MAX_STRING_SIZE);
    sendFail();
    return;
  }
  sendBuffer((char *)NBT_text, sizeof(NBT_text));
  sendShort(len);
  for (uint32_t i = 0; i < len; i++)
  {
    sendByte(str[i]);
  }
  sendByte(0);
}
// TODO: both of these are not correctly implemented but its random right?
void sendUUID(uint16_t seed)
{
  char stmp[16];
  memset(stmp, 0, sizeof(stmp));
  stmp[6] = 0x30;
  stmp[8] = 0x80;
  stmp[0] = (seed >> 8) & 0xff;
  stmp[1] = (seed) & 0xff;
  sendBuffer(stmp, sizeof(stmp));
}
void sendUUIDString(uint16_t seed)
{
  static char const hexdigits_lower[] = "0123456789abcdef";
  int i;
  char stmp[38];
  for (i = 0; i < 36; i++)
  {
    stmp[i] = '0';
  }
  stmp[36] = 0;
  stmp[8] = '-';
  stmp[13] = '-';
  stmp[18] = '-';
  stmp[23] = '-';
  stmp[14] = '3';
  stmp[19] = '8';
  stmp[0] = hexdigits_lower[((seed) >> 12 & 0xf)];
  stmp[1] = hexdigits_lower[((seed >> 8) & 0xf)];
  stmp[2] = hexdigits_lower[((seed >> 4) & 0xf)];
  stmp[3] = hexdigits_lower[((seed) & 0xf)];
  sendString(stmp, strnlen(stmp, sizeof(stmp)));
}

void socketioCleanup()
{
  U_free(sendPacketVars.buffer);
  U_free(sendPacketVars.packetbuffer);
  U_free(sendPacketVars.globalbuffer);
  memset(&sendPacketVars, 0, sizeof(sendPacketVars));
  memset(&readPacketVars, 0, sizeof(readPacketVars));
}

void socketioLog()
{
  printl(LOG_INFO, "SEND: buffer: %ld packet: %ld globalbuffer: %ld\n", sendPacketVars.buffersize, sendPacketVars.packetsize, sendPacketVars.globalbuffersize);
}
