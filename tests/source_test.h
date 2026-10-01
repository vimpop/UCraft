#ifndef SOURCE_TEST_H
#define SOURCE_TEST_H
#include "socketio.h"
#include "UCraft.h"
#include "blocks.h"
#include "storage.h"
#include "world.h"
#include "c2s.h"
#include "s2c.h"
#include "https.h"
#include <limits.h>
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)
extern player_t p;
extern unsigned char wire[262144];
extern size_t wire_len, send_limit;
extern int send_error, fail_alloc, close_count, ssl_close_count;
extern const unsigned char *tls_input;
extern size_t tls_len, tls_pos, tls_fragment;
extern int tls_result;
void fixture(void);
void finish(void);
void input(const unsigned char *data, size_t len);
size_t decode(const unsigned char *data, size_t len, size_t *offset);
void test_readers(void);
void test_reader_bounds(void);
void test_writers(void);
void test_s2c(void);
void test_queues(void);
void test_writer_failures(void);
void test_handlers(void);
void test_inventory_capacity(void);
void test_block_churn(void);
void test_allocations(void);
void test_players(void);
void test_world(void);
void test_digest(void);
void test_auth(void);
void test_auth_errors(void);
void test_utilities(void);
#endif
