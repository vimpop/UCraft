#include "source_test.h"
#include "util.h"

player_t p;
unsigned char wire[262144];
size_t wire_len, send_limit;
int send_error, fail_alloc, close_count, ssl_close_count;
const unsigned char *tls_input;
size_t tls_len, tls_pos, tls_fragment;
int tls_result;

static int allocation_fails(void) { return fail_alloc > 0 && --fail_alloc == 0; }
void *test_malloc(size_t n) { return allocation_fails() ? NULL : malloc(n); }
void *test_calloc(size_t n, size_t s) { return allocation_fails() ? NULL : calloc(n, s); }
void *test_realloc(void *p0, size_t n) { return allocation_fails() ? NULL : realloc(p0, n); }
ssize_t test_send(int fd, const void *data, size_t n, int flags)
{
    if (send_error) { errno = send_error; send_error = 0; return -1; }
    if (!send_limit) { errno = EAGAIN; return -1; }
    if (n > send_limit) n = send_limit;
    CHECK(wire_len + n <= sizeof(wire));
    memcpy(wire + wire_len, data, n);
    wire_len += n;
    send_limit -= n;
    return (ssize_t)n;
}
int test_close(int fd) { CHECK(fd >= 0); close_count++; return 0; }
int test_shutdown(int fd, int how) { CHECK(fd >= 0); return 0; }
int test_ssl_read(mbedtls_ssl_context *ctx, unsigned char *data, size_t n)
{
    if (tls_result) return tls_result;
    size_t available = tls_len - tls_pos;
    if (n > available) n = available;
    if (n > tls_fragment) n = tls_fragment;
    memcpy(data, tls_input + tls_pos, n);
    tls_pos += n;
    return (int)n;
}
int test_ssl_write(mbedtls_ssl_context *ctx, const unsigned char *data, size_t n)
{ return tls_result ? tls_result : (int)(n < tls_fragment ? n : tls_fragment); }
int test_ssl_close(mbedtls_ssl_context *ctx) { ssl_close_count++; return 0; }

void fixture(void)
{
    memset(&p, 0, sizeof(p));
    strcpy(p.name, "Alice");
    p.fd = 10;
    wire_len = 0;
    send_limit = sizeof(wire);
    send_error = fail_alloc = close_count = ssl_close_count = 0;
    tls_input = (const unsigned char *)"";
    tls_len = tls_pos = 0;
    tls_fragment = 4096;
    tls_result = 0;
    main_tick = 1;
    gamePreload();
}
void finish(void)
{
    fail_alloc = 0;
    while (p.out_head) {
        out_packet_t *next = p.out_head->next;
        free(p.out_head->data);
        free(p.out_head);
        p.out_head = next;
    }
    gamePlayerLeft(&p);
    playerCleanup();
    gameCleanup();
    socketioCleanup();
    httpsCleanup();
    free(p.texture_value);
    free(p.texture_signature);
}
void input(const unsigned char *data, size_t len)
{
    readPacketVars_t *r = readValues();
    memset(r, 0, sizeof(*r));
    CHECK(len <= sizeof(r->buffer));
    memcpy(r->buffer, data, len);
    r->pktbytes = r->pktsize = len;
    readStart(&p);
    p.remove_player_event = 0;
}
size_t decode(const unsigned char *data, size_t len, size_t *offset)
{
    size_t value = 0;
    for (unsigned i = 0; i < 5; i++) {
        CHECK(*offset < len);
        unsigned char byte = data[(*offset)++];
        value |= (size_t)(byte & 127) << (7 * i);
        if (!(byte & 128)) return value;
    }
    CHECK(0);
    return 0;
}
void test_utilities(void)
{
    fixture();
    hexDump(NULL, NULL, 0);
    hexDump("empty", NULL, -1);
    unsigned char data[] = {0, 'A', 255};
    hexDump("sample", data, sizeof(data));
    finish();
}
int main(int argc, char **argv)
{
    const struct { const char *name; void (*run)(void); } cases[] = {
        {"readers",test_readers}, {"reader_bounds",test_reader_bounds},
        {"writers",test_writers}, {"s2c",test_s2c}, {"queues",test_queues}, {"writer_failures",test_writer_failures},
        {"handlers",test_handlers}, {"inventory_capacity",test_inventory_capacity},
        {"block_churn",test_block_churn}, {"allocations",test_allocations},
        {"players",test_players}, {"world",test_world}, {"digest",test_digest},
        {"auth",test_auth}, {"auth_errors",test_auth_errors}, {"utilities",test_utilities}
    };
    CHECK(argc == 2);
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++)
        if (!strcmp(argv[1], cases[i].name)) { cases[i].run(); puts("PASS"); return 0; }
    return 1;
}
