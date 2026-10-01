#include "wrapper.h"
#include "player.h"
#include "https.h"
#include "log.h"

static player_t *playerListHead = NULL;          // connected players list
static playerd_t *playerDisconnectedHead = NULL; // disconnected players list

static fd_set masterset;

fd_set *playerGetSet() { return &masterset; }
void playerPushDisconnected(int32_t playerid, char *playername, size_t len)
{
  if (playername == NULL)
  {
    return;
  }
  playerd_t *element = U_calloc(1, sizeof(playerd_t));
  if (element == NULL)
  {
    return;
  }
  char *name = U_calloc(1, len);
  if (name == NULL)
  {
    U_free(element);
    return;
  }
  strncpy(name, playername, len);
  element->name = name;
  element->id = playerid;
  element->next = playerDisconnectedHead;
  playerDisconnectedHead = element;
}
playerd_t *playerPopDisconnected()
{
  if (playerDisconnectedHead == NULL)
  {
    return NULL;
  }
  playerd_t *temp = playerDisconnectedHead;
  playerDisconnectedHead = playerDisconnectedHead->next;
  return temp;
}
player_t *playerGetHead() { return playerListHead; }
size_t playerGetCount()
{
  size_t currentPlayers = 0;
  player_t *player = playerListHead;
  while (player != NULL)
  {
    currentPlayers++;
    player = player->next;
  }
  return currentPlayers;
}
size_t playerGetActiveCount()
{
  size_t currentActivePlayers = 0;
  player_t *player = playerListHead;
  while (player != NULL)
  {
    if (player->active)
    {
      currentActivePlayers++;
    }
    player = player->next;
  }
  return currentActivePlayers;
}
size_t playerGetInGameCount()
{
  size_t currentReadyToPlay = 0;
  player_t *player = playerListHead;
  while (player != NULL)
  {
    if (player->ready_to_play)
    {
      currentReadyToPlay++;
    }
    player = player->next;
  }
  return currentReadyToPlay;
}
player_t *playerGetId(int32_t player_id)
{
  player_t *player = playerListHead;
  while (player != NULL)
  {
    if (player->id == player_id)
    {
      return player;
    }
    player = player->next;
  }
  return NULL;
}
player_t *playerAdd(uint32_t player_fd)
{
  player_t *player = U_calloc(1, sizeof(player_t));
  if (player == NULL)
  {
    return NULL;
  }
  player->fd = player_fd;
  FD_SET(player_fd, &masterset);
  player->id = player_fd + PLAYER_BASE;
  player->next = playerListHead;
  playerListHead = player;
  return player;
}
int playerCheckName(player_t *player)
{
  size_t len = strnlen(player->name, sizeof(player->name));
  if (len == 0 || len > 16) return 1;
  for (size_t i = 0; i < len; i++)
  {
    unsigned char c = (unsigned char)player->name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_')) return 1;
  }
  return 0;
}
int playerCheckDuplicate(player_t *player)
{
  player_t *current = playerListHead;
  while (current != NULL)
  {
    if (current != player && strncmp(current->name, player->name, sizeof(((player_t *)0)->name)) == 0)
    {
      return 1;
    }
    current = current->next;
  }
  return 0;
}
uint8_t playerRemove(player_t *player)
{
  if (playerListHead == NULL)
  {
    return 1;
  }
  if (player == NULL)
  {
    return 1;
  }
#ifdef ONLINE_MODE
  mbedtls_aes_free(&player->aes_ctx);
#endif /*ONLINE_MODE*/
#ifdef ONLINE_MODE_AUTH
  if (strnlen(player->name, sizeof(((player_t *)0)->name)) > 0)
  {
    httpsFreePlayer(player);
  }
  if (player->texture_value)
  {
    U_free(player->texture_value);
  }
  if (player->texture_signature)
  {
    U_free(player->texture_signature);
  }
#endif /*ONLINE_MODE_AUTH*/
  U_shutdown(player->fd, SHUT_RDWR);
  U_close(player->fd);
  while (player->out_head)
  {
    out_packet_t *pkt = player->out_head;
    player->out_head = pkt->next;
    U_free(pkt->data);
    U_free(pkt);
  }
  player->out_tail = NULL;
  FD_CLR(player->fd, &masterset);
  gamePlayerLeft(player);
  if (player->active)
  {
    playerPushDisconnected(player->id, player->name, sizeof(((player_t *)0)->name));

    printl(LOG_INFO, "player %s has been removed from the game\n", player->name);
  }
  if (playerListHead == player)
  {
    playerListHead = player->next;
    U_free(player);
    return 0;
  }
  player_t *current = playerListHead;
  while (current != NULL && current->next != player)
  {
    current = current->next;
  }
  if (current == NULL)
  {
    return 1;
  }
  current->next = player->next;
  U_free(player);
  return 0;
}
void playerCleanup()
{
  for (player_t *player = playerGetHead(); player != NULL;)
  {
    player_t *temp = player->next;
    playerRemove(player);
    if (playerGetHead() == NULL)
    {
      break;
    }
    player = temp;
  }
  for (playerd_t *disconnected = playerPopDisconnected(); disconnected != NULL; disconnected = playerPopDisconnected())
  {
    U_free(disconnected->name);
    U_free(disconnected);
  }
}
