#include "https.h"
#ifdef ONLINE_MODE_AUTH
#include <errno.h>
#include "encryption.h"
#include "log.h"
#include "util.h"
#include "mbedtls/platform.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/pk.h"
#include "mbedtls/rsa.h"
#include "mbedtls/sha1.h"
#include "mbedtls/ssl.h"
#include "mbedtls/debug.h"
#include "lwjson/lwjson.h"
#include "wrapper.h"

static httpsData_t httpsData = {.net = {.fd = -1}, .connection_closed = 1};
static struct sockaddr_in auth_server;

#ifdef MBEDTLS_DEBUG_C
static void sslDebug(void *ctx, int level, const char *file, int line, const char *str)
{
    printl(LOG_INFO, "%s:%04d: %s", file, line, str);
}
#endif /*MBEDTLS_DEBUG_C*/

static void httpsClose()
{
    if (httpsData.connection_closed)
    {
        return;
    }

    // Best-effort nonblocking closure; never spin on WANT_WRITE.
    mbedtls_ssl_close_notify(&httpsData.ssl);
    if (httpsData.net.fd >= 0)
    {
        U_shutdown(httpsData.net.fd, SHUT_RDWR);
        U_close(httpsData.net.fd);
        httpsData.net.fd = -1;
    }
    mbedtls_ssl_free(&httpsData.ssl);
    mbedtls_ssl_config_free(&httpsData.conf);
    httpsData.connection_closed = 1;
}
// TODO: Add checks if the socket is nonblocking or not as the implementation for net_would_block is missing
int mbedtls_net_recv(void *ctx, unsigned char *buf, size_t len)
{
    int ret;
    int fd = ((mbedtls_net_context *)ctx)->fd;
    ret = (int)U_recv(fd, buf, len, MSG_NOSIGNAL);
    if (ret < 0)
    {
        if (errno == EPIPE || errno == ECONNRESET)
        {
            return (MBEDTLS_ERR_NET_CONN_RESET);
        }

        if (errno == EINTR)
        {
            return (MBEDTLS_ERR_SSL_WANT_READ);
        }

        return (MBEDTLS_ERR_SSL_WANT_READ);
    }
    return ret;
}
int mbedtls_net_send(void *ctx, const unsigned char *buf, size_t len)
{
    int ret;
    int fd = ((mbedtls_net_context *)ctx)->fd;
    ret = (int)U_send(fd, buf, len, MSG_NOSIGNAL);
    if (ret < 0)
    {
        if (errno == EPIPE || errno == ECONNRESET)
        {
            return (MBEDTLS_ERR_NET_CONN_RESET);
        }

        if (errno == EINTR)
        {
            return (MBEDTLS_ERR_SSL_WANT_WRITE);
        }
        return (MBEDTLS_ERR_SSL_WANT_WRITE);
    }
    return ret;
}

httpsData_t *httpsGetData()
{
    return &httpsData;
}

int httpsConnect(player_t *currentPlayer, const char *hostname, const char *port)
{
    if (currentPlayer == NULL)
    {
        printl(LOG_ERROR, "how??? currentPlayer is NULL\n");
        return 1;
    }
    if (httpsData.currentPlayer != NULL)
    {
        printl(LOG_ERROR, "in use\n");
        return 1;
    }
    httpsData.currentPlayer = currentPlayer;
    httpsData.connection_closed = 0;
    int ret;
    mbedtls_ssl_init(&httpsData.ssl);
    mbedtls_ssl_config_init(&httpsData.conf);
    // connect to the server manually
    httpsData.net.fd = U_socket(AF_INET, SOCK_STREAM, 0);
    if (httpsData.net.fd < 0)
    {
        printl(LOG_ERROR, "U_socket returned %d\n", httpsData.net.fd);
        return 1;
    }
    ret = U_setsocknonblock(httpsData.net.fd);
    if (ret < 0)
    {
        printl(LOG_ERROR, "U_setsocknonblock returned %d\n", ret);
        return 1;
    }

    auth_server.sin_family = AF_INET;
    auth_server.sin_port = htons(atoi(port));
    if (auth_server.sin_addr.s_addr == 0)
    {
        struct hostent *hostinfo = U_gethostbyname(hostname);
        if (hostinfo == NULL)
        {
            printl(LOG_ERROR, "U_gethostbyname Failed\r\n");
            return 1;
        }
        auth_server.sin_addr = *((struct in_addr *)hostinfo->h_addr);
    }
    ret = U_connect(httpsData.net.fd, (struct sockaddr *)&auth_server, sizeof(auth_server));
    if (ret != 0 && errno != 0 && errno != EINPROGRESS)
    {
        printl(LOG_ERROR, "U_connect returned %d errno: %s\n", ret, strerror(errno));
        return 1;
    }
    ret = mbedtls_ssl_config_defaults(&httpsData.conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0)
    {
        printl(LOG_ERROR, "mbedtls_ssl_config_defaults returned %d\n", ret);
        return 1;
    }
    // TODO:is a certificate required? ofc not bro we're just a client, jk will fix it later
    mbedtls_ssl_conf_authmode(&httpsData.conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&httpsData.conf, mbedtls_ctr_drbg_random, &encryptionGetData()->ctr_drbg);

    ret = mbedtls_ssl_setup(&httpsData.ssl, &httpsData.conf);
    if (ret != 0)
    {
        printl(LOG_ERROR, "mbedtls_ssl_setup returned %d\n", ret);
        return 1;
    }
    ret = mbedtls_ssl_set_hostname(&httpsData.ssl, hostname);
    if (ret != 0)
    {
        printl(LOG_ERROR, "mbedtls_ssl_set_hostname returned %d\n", ret);
        return 1;
    }
#ifdef MBEDTLS_DEBUG_C
    mbedtls_ssl_conf_dbg(&httpsData.conf, sslDebug, NULL);
    mbedtls_debug_set_threshold(4);
#endif /*MBEDTLS_DEBUG_C*/
    mbedtls_ssl_set_bio(&httpsData.ssl, &httpsData.net, mbedtls_net_send, mbedtls_net_recv, NULL);
    return 0;
}
void httpsGetPlayerInfo(player_t *currentPlayer)
{
    if (currentPlayer == NULL)
    {
        printl(LOG_ERROR, "how??? currentPlayer is NULL\n");
        return;
    }
    if (httpsData.currentPlayer != NULL)
    {
        printl(LOG_ERROR, "in use\n");
        return;
    }
    // generate the login hash
    uint8_t hash[20];
    uint8_t publickey[256];
    uint8_t server_hash[45];
    mbedtls_sha1_context sha1;
    mbedtls_sha1_init(&sha1);
    mbedtls_sha1_starts(&sha1);
    memset(hash, 0, sizeof(hash));
    mbedtls_sha1_update(&sha1, hash, 20);
    mbedtls_sha1_update(&sha1, currentPlayer->iv_encrypt, 16);
    int ret = mbedtls_pk_write_pubkey_der(&encryptionGetData()->key, publickey, sizeof(publickey));
    if (ret < 0)
    {
        mbedtls_sha1_free(&sha1);
        printl(LOG_ERROR, "mbedtls_pk_write_pubkey_der returned %d\n", ret);
        currentPlayer->remove_player_event = 1;
        return;
    }
    mbedtls_sha1_update(&sha1, &publickey[sizeof(publickey) - ret], ret);
    mbedtls_sha1_finish(&sha1, hash);
    mbedtls_sha1_free(&sha1);
    encryptionHexDigest(server_hash, hash, sizeof(server_hash));
    ret = httpsConnect(currentPlayer, AUTH_HOST, AUTH_HOST_PORT);
    if (ret != 0)
    {
        strncpy((char *)currentPlayer->disconnect_reason, "Internal server error!", sizeof(((player_t *)0)->disconnect_reason));
        printl(LOG_ERROR, "httpsConnect returned %d\n", ret);
        httpsFreePlayer(currentPlayer);
        currentPlayer->remove_player_event = 1;
        return;
    }
    // form the request
    httpsGetData()->len = snprintf(httpsGetData()->buffer, sizeof(((httpsData_t *)0)->buffer), "GET /session/minecraft/hasJoined?username=%s&serverId=%s HTTP/1.1\r\nHost: %s\r\nAccept: application/json\r\nContent-Type: application/json\r\n\r\n", currentPlayer->name, server_hash, AUTH_HOST);
    // set the rts flag and dispatch it whenever it can be sent
    currentPlayer->https_rts_event = 1;
}
static int header_equals(const char *a, size_t n, const char *b)
{
    if (n != strlen(b)) return 0;
    for (size_t i = 0; i < n; i++)
    {
        unsigned char c = (unsigned char)a[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (unsigned char)b[i]) return 0;
    }
    return 1;
}

// 0: incomplete, -1: invalid, 1: complete. Buffer always has a spare terminator.
static int http_body(size_t *body_offset, size_t *body_length)
{
    char *buffer = httpsData.buffer;
    char *end = strstr(buffer, "\r\n\r\n");
    if (!end) return 0;
    char *line = strstr(buffer, "\r\n");
    if (!line || line - buffer < 12 ||
        (strncmp(buffer, "HTTP/1.1 200 ", 13) && strncmp(buffer, "HTTP/1.0 200 ", 13))) return -1;
    size_t length = 0;
    int has_length = 0, chunked = 0;
    for (line += 2; line < end; )
    {
        char *next = strstr(line, "\r\n");
        char *colon = memchr(line, ':', (size_t)(next - line));
        if (!colon) return -1;
        char *value = colon + 1;
        while (value < next && (*value == ' ' || *value == '\t')) value++;
        char *value_end = next;
        while (value_end > value && (value_end[-1] == ' ' || value_end[-1] == '\t')) value_end--;
        if (header_equals(line, (size_t)(colon - line), "content-length"))
        {
            if (has_length || value == value_end) return -1;
            has_length = 1;
            for (; value < value_end; value++)
            {
                if (*value < '0' || *value > '9') return -1;
                length = length * 10 + (size_t)(*value - '0');
                if (length >= sizeof(httpsData.buffer)) return -1;
            }
        }
        else if (header_equals(line, (size_t)(colon - line), "transfer-encoding"))
        {
            if (chunked || !header_equals(value, (size_t)(value_end-value), "chunked")) return -1;
            chunked = 1;
        }
        line = next + 2;
    }
    size_t start = (size_t)(end - buffer) + 4;
    if (has_length)
    {
        if (chunked || length >= sizeof(httpsData.buffer) - start) return -1;
        if (length > httpsData.offset - start) return 0;
        *body_offset = start; *body_length = length;
        return 1;
    }
    if (!chunked) return -1;
    // Validate the entire chunked body before compacting it in place.
    for (int pass = 0; pass < 2; pass++)
    {
        size_t cursor = start, out = start;
        for (;;)
        {
            char *size_end = strstr(buffer + cursor, "\r\n");
            if (!size_end) return 0;
            size_t size = 0;
            if (size_end == buffer + cursor) return -1;
            for (char *digit = buffer + cursor; digit < size_end; digit++)
            {
                unsigned char c = (unsigned char)*digit;
                unsigned value;
                if (c >= '0' && c <= '9') value = c - '0';
                else if (c >= 'a' && c <= 'f') value = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') value = c - 'A' + 10;
                else return -1;
                size = size * 16 + value;
                if (size >= sizeof(httpsData.buffer)) return -1;
            }
            cursor = (size_t)(size_end - buffer) + 2;
            if (size + 2 > httpsData.offset - cursor) return 0;
            if (buffer[cursor + size] != '\r' || buffer[cursor + size + 1] != '\n') return -1;
            if (!size) { *body_offset = start; *body_length = out - start; break; }
            if (pass) memmove(buffer + out, buffer + cursor, size);
            out += size; cursor += size + 2;
        }
    }
    return 1;
}

int httpsRtr(player_t *currentPlayer)
{
    if (currentPlayer == NULL)
    {
        printl(LOG_ERROR, "how??? currentPlayer is NULL\n");
        return 0;
    }
    if (httpsData.currentPlayer != currentPlayer)
    {
        printl(LOG_ERROR, "how??? currentPlayer is not the same\n");
        return 0;
    }
    if (httpsData.timeout > AUTH_TIMEOUT)
    {
        printl(LOG_ERROR, "httpsRtr timeout\n");
        strncpy((char *)currentPlayer->disconnect_reason, "Authentication Timeout", sizeof(((player_t *)0)->disconnect_reason));
        currentPlayer->remove_player_event = 1;
        return 0;
    }
    if (httpsData.offset >= sizeof(httpsData.buffer) - 1)
    { currentPlayer->remove_player_event = 1; return 0; }
    int ret = mbedtls_ssl_read(&httpsData.ssl, (unsigned char *)httpsData.buffer + httpsData.offset,
                             sizeof(httpsData.buffer) - 1 - httpsData.offset);
    httpsData.timeout++;
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) return 1;
    if (ret <= 0) { currentPlayer->remove_player_event = 1; return 0; }
    httpsData.offset += (size_t)ret;
    httpsData.buffer[httpsData.offset] = '\0';
    httpsData.timeout = 0;
    size_t body_offset, content_length;
    int complete = http_body(&body_offset, &content_length);
    if (!complete) return 1;
    if (complete < 0) { currentPlayer->remove_player_event = 1; return 0; }
    httpsClose();
    httpsData.len = content_length;
    httpsData.offset = body_offset;
    httpsData.buffer[body_offset + content_length] = '\0';
    lwjson_token_t tokens[10];
    lwjson_t lwjson;

    lwjson_init(&lwjson, tokens, LWJSON_ARRAYSIZE(tokens));
    ret = lwjson_parse(&lwjson, &httpsData.buffer[httpsData.offset]);
    if (ret != lwjsonOK)
    {
        printl(LOG_WARN, "lwjson_parse returned %d\n", ret);
        lwjson_free(&lwjson);
        currentPlayer->remove_player_event = 1;
        return 0;
    }
    // parse the json
    const lwjson_token_t *t;
    if ((t = lwjson_find(&lwjson, "name")) == NULL)
    {
        lwjson_free(&lwjson);
        currentPlayer->remove_player_event = 1;
        return 0;
    }
    if (t->type != LWJSON_TYPE_STRING || !t->u.str.token_value_len ||
        t->u.str.token_value_len >= sizeof(currentPlayer->name))
    {
        lwjson_free(&lwjson);
        currentPlayer->remove_player_event = 1;
        return 0;
    }
    memset(currentPlayer->name, 0, sizeof(currentPlayer->name));
    memcpy(currentPlayer->name, t->u.str.token_value, t->u.str.token_value_len);
    // sanity check for the player name
    if (playerCheckName(currentPlayer))
    {
        strncpy((char *)currentPlayer->name, "stinky_player", sizeof(((player_t *)0)->name));
        lwjson_free(&lwjson);
        currentPlayer->remove_player_event = 1;
        return 0;
    }
    // another name sanity check
    if (playerCheckDuplicate(currentPlayer))
    {
        strncpy((char *)currentPlayer->name, "stinky_player", sizeof(((player_t *)0)->name));
        lwjson_free(&lwjson);
        currentPlayer->remove_player_event = 1;
        return 0;
    }
    if ((t = lwjson_find(&lwjson, "properties")) == NULL || t->type != LWJSON_TYPE_ARRAY)
    {
        lwjson_free(&lwjson);
        currentPlayer->remove_player_event = 1;
        return 0;
    }
    for (const lwjson_token_t *obj = lwjson_get_first_child(t); obj; obj = obj->next)
    {
        if (obj->type != LWJSON_TYPE_OBJECT) continue;
        const lwjson_token_t *name = lwjson_find_ex(&lwjson, obj, "name");
        if (!name || name->type != LWJSON_TYPE_STRING || name->u.str.token_value_len != 8 ||
            memcmp(name->u.str.token_value, "textures", 8)) continue;
        const char *keys[] = {"value", "signature"};
        for (size_t i = 0; i < 2; i++)
        {
            const lwjson_token_t *value = lwjson_find_ex(&lwjson, obj, keys[i]);
            if (!value || value->type != LWJSON_TYPE_STRING) continue;
            char **dest = i ? &currentPlayer->texture_signature : &currentPlayer->texture_value;
            size_t *len = i ? &currentPlayer->texture_signature_len : &currentPlayer->texture_value_len;
            if (*dest) continue;
            *dest = U_calloc(1, value->u.str.token_value_len + 1);
            if (!*dest) { lwjson_free(&lwjson); currentPlayer->remove_player_event = 1; return 0; }
            *len = value->u.str.token_value_len;
            memcpy(*dest, value->u.str.token_value, *len);
        }
    }
    lwjson_free(&lwjson);
    // finally let the player join
    currentPlayer->login_event = 1;
    return 0;
}
int httpsRts(player_t *currentPlayer)
{
    if (currentPlayer == NULL)
    {
        printl(LOG_ERROR, "how??? currentPlayer is NULL\n");
        return 0;
    }
    if (httpsData.currentPlayer != currentPlayer)
    {
        printl(LOG_ERROR, "how??? currentPlayer is not the same\n");
        return 0;
    }
    if (httpsData.timeout > AUTH_TIMEOUT)
    {
        printl(LOG_ERROR, "httpsRts timeout\n");
        strncpy((char *)currentPlayer->disconnect_reason, "Authentication Timeout", sizeof(((player_t *)0)->disconnect_reason));
        currentPlayer->remove_player_event = 1;
        return 0;
    }
    int ret = 0;
    if (httpsData.len == 0)
    {
        memset(httpsData.buffer, 0, sizeof(((httpsData_t *)0)->buffer));
        httpsData.offset = 0;
        httpsData.len = 0;
        currentPlayer->https_rtr_event = 1;
        return 0;
    }
    ret = mbedtls_ssl_write(&httpsData.ssl, (unsigned char *)&httpsData.buffer[httpsData.offset], httpsData.len);
    httpsData.timeout++;
    if (ret < 0)
    {
        switch (ret)
        {
        case MBEDTLS_ERR_SSL_WANT_READ:
            ret = mbedtls_ssl_handshake(&httpsData.ssl);
            if (ret < 0)
            {
                if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
                    ret != MBEDTLS_ERR_SSL_WANT_WRITE &&
                    ret != MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS)
                {
                    printl(LOG_WARN, "mbedtls_ssl_handshake returned -0x%x\n", -ret);
                    currentPlayer->remove_player_event = 1;
                    return 0;
                }
            }
            break;
        case MBEDTLS_ERR_SSL_WANT_WRITE:
            break;
        default:
            printl(LOG_WARN, "mbedtls_ssl_write returned %d\n", ret);
            currentPlayer->remove_player_event = 1;
            return 0;
            break;
        }
        return 1;
    }
    if (ret == 0) { currentPlayer->remove_player_event = 1; return 0; }
    httpsData.offset += ret;
    httpsData.len -= ret;
    httpsData.timeout = 0;
    return 1;
}
void httpsFreePlayer(player_t *currentPlayer)
{
    if (currentPlayer == NULL)
    {
        printl(LOG_ERROR, "how??? currentPlayer is NULL\n");
        return;
    }
    if (httpsData.currentPlayer == NULL)
    {
        return;
    }
    if (currentPlayer != httpsData.currentPlayer)
    {
        printl(LOG_ERROR, "how??? currentPlayer is not the same\n");
        return;
    }
    // send SSL/TLS closure notification
    httpsClose();
    memset(&httpsData, 0, sizeof(httpsData_t));
    httpsData.currentPlayer = NULL;
    httpsData.net.fd = -1;
    httpsData.connection_closed = 1;
}
void httpsCleanup()
{
    httpsClose();
    memset(&httpsData, 0, sizeof(httpsData_t));
    httpsData.currentPlayer = NULL;
    httpsData.net.fd = -1;
    httpsData.connection_closed = 1;
}
#endif /*ONLINE_MODE_AUTH*/