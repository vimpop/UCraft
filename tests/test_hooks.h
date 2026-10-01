#ifndef UCRAFT_TEST_HOOKS_H
#define UCRAFT_TEST_HOOKS_H
#include "config.h"
#include <stddef.h>
#ifdef ONLINE_MODE
#include <mbedtls/ssl.h>
int test_ssl_read(mbedtls_ssl_context *, unsigned char *, size_t);
int test_ssl_write(mbedtls_ssl_context *, const unsigned char *, size_t);
int test_ssl_close(mbedtls_ssl_context *);
#define mbedtls_ssl_read test_ssl_read
#define mbedtls_ssl_write test_ssl_write
#define mbedtls_ssl_close_notify test_ssl_close
#endif
void *test_malloc(size_t);
void *test_calloc(size_t, size_t);
void *test_realloc(void *, size_t);
ssize_t test_send(int, const void *, size_t, int);
int test_close(int);
int test_shutdown(int, int);
#define U_malloc test_malloc
#define U_calloc test_calloc
#define U_realloc test_realloc
#define U_send test_send
#define U_close test_close
#define U_shutdown test_shutdown
#endif
