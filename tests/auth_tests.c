#include "source_test.h"

void test_digest(void)
{
    fixture();
    unsigned char src[20]={0},dest[45];
    encryptionHexDigest(dest,src,sizeof(dest)); CHECK(!strcmp((char *)dest,"0"));
    src[19]=1; encryptionHexDigest(dest,src,sizeof(dest)); CHECK(!strcmp((char *)dest,"1"));
    memset(src,255,sizeof(src)); encryptionHexDigest(dest,src,sizeof(dest)); CHECK(!strcmp((char *)dest,"-1"));
    memset(src,0,sizeof(src)); src[0]=128; encryptionHexDigest(dest,src,sizeof(dest));
    CHECK(!strcmp((char *)dest,"-8000000000000000000000000000000000000000"));
    const unsigned char invalid[]={0}; input(invalid,1); LoginC2S_encryption_response(); CHECK(p.remove_player_event && !p.encryption_verified);
    CHECK(encryptionBegin()==0);
    encryptionData_t *crypto=encryptionGetData();
    unsigned char packet[260], secret[16], token[4]={1,2,3,4};
    for(size_t i=0;i<sizeof(secret);i++) secret[i]=(unsigned char)i;
    memcpy(p.verify_token,token,sizeof(token));
    packet[0]=packet[130]=128; packet[1]=packet[131]=1;
    CHECK(!mbedtls_rsa_rsaes_pkcs1_v15_encrypt(mbedtls_pk_rsa(crypto->key),
        mbedtls_ctr_drbg_random,&crypto->ctr_drbg,sizeof(secret),secret,packet+2));
    CHECK(!mbedtls_rsa_rsaes_pkcs1_v15_encrypt(mbedtls_pk_rsa(crypto->key),
        mbedtls_ctr_drbg_random,&crypto->ctr_drbg,sizeof(token),token,packet+132));
    input(packet,sizeof(packet)); LoginC2S_encryption_response();
    CHECK(p.encryption_verified && !p.remove_player_event && !memcmp(p.iv_encrypt,secret,16));
    mbedtls_aes_free(&p.aes_ctx); p.encryption_verified=0;
    p.verify_token[0]^=1;
    input(packet,sizeof(packet)); LoginC2S_encryption_response();
    CHECK(p.remove_player_event && !p.encryption_verified);
    input(packet,sizeof(packet)-1); LoginC2S_encryption_response();
    CHECK(p.remove_player_event && !p.encryption_verified);
    encryptionCleanup();
    finish();
}
static void response(const char *text, size_t fragment)
{
    httpsData_t *h=httpsGetData(); memset(h,0,sizeof(*h)); h->currentPlayer=&p; h->net.fd=21;
    tls_input=(const unsigned char *)text; tls_len=strlen(text); tls_pos=0; tls_fragment=fragment; tls_result=0;
    p.remove_player_event=0; p.login_event=0; ssl_close_count=0;
}
void test_auth(void)
{
    fixture();
    const char *body="{\"name\":\"Alice\",\"properties\":[]}";
    char http[256]; snprintf(http,sizeof(http),"HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n%s",strlen(body),body);
    response(http,7);
    for(int i=0;i<100;i++) {
        int more=httpsRtr(&p);
        if(!more) break;
        CHECK(!ssl_close_count); /* incomplete body must retain the TLS connection */
        CHECK(i<99);
    }
    CHECK(p.login_event && !p.remove_player_event && ssl_close_count==1);
    httpsFreePlayer(&p); CHECK(!httpsGetData()->currentPlayer && ssl_close_count==1);
    body="{\"properties\":[{\"signature\":\"sig\",\"value\":\"skin\",\"name\":\"textures\"}],\"name\":\"Alice\"}";
    snprintf(http,sizeof(http),"HTTP/1.1 200 OK\r\ntRaNsFeR-EnCoDiNg: chunked\r\n\r\n%zx\r\n%s\r\n0\r\n\r\n",strlen(body),body);
    response(http,1);
    for(size_t i=0; httpsRtr(&p); i++) CHECK(i<sizeof(http));
    CHECK(p.login_event && !p.remove_player_event && ssl_close_count==1);
    CHECK(p.texture_value_len==4 && !strcmp(p.texture_value,"skin"));
    CHECK(p.texture_signature_len==3 && !strcmp(p.texture_signature,"sig"));
    httpsFreePlayer(&p);
    response("",3);
    httpsGetData()->len=7;
    while(httpsRts(&p)) CHECK(!p.remove_player_event);
    CHECK(p.https_rtr_event && httpsGetData()->offset==0);
    httpsFreePlayer(&p);
    finish();
}
void test_auth_errors(void)
{
    fixture();
    const char *cases[]={
        "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 500 Error\r\nContent-Length: 2\r\n\r\n{}",
        "HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 999999\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n}{",
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\n{}",
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nTransfer-Encoding: chunked\r\n\r\n{}",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nz\r\n{}\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}"
    };
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        response(cases[i],4096);
        for(int step=0;step<3 && !p.remove_player_event;step++) httpsRtr(&p);
        CHECK(p.remove_player_event && !p.login_event);
        httpsFreePlayer(&p);
    }
    response("",4096); tls_result=MBEDTLS_ERR_SSL_WANT_READ;
    for(int i=0;i<AUTH_TIMEOUT+3;i++) httpsRtr(&p);
    CHECK(p.remove_player_event && !p.login_event); httpsFreePlayer(&p);
    response("",4096); tls_result=MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    CHECK(!httpsRtr(&p) && p.remove_player_event); httpsFreePlayer(&p);
    const char *invalid_json[]={"{\"name\":42,\"properties\":[]}",
        "{\"name\":\"\",\"properties\":[]}", "{\"name\":\"Alice\",\"properties\":{}}"};
    for(size_t i=0;i<sizeof(invalid_json)/sizeof(invalid_json[0]);i++) {
        char http[256]; snprintf(http,sizeof(http),"HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n%s",strlen(invalid_json[i]),invalid_json[i]);
        response(http,4096); CHECK(!httpsRtr(&p) && p.remove_player_event && !p.login_event);
        httpsFreePlayer(&p);
    }
    response("",0); httpsGetData()->len=1;
    CHECK(!httpsRts(&p) && p.remove_player_event); httpsFreePlayer(&p);
    response("",1); httpsGetData()->len=1; tls_result=MBEDTLS_ERR_SSL_WANT_WRITE;
    for(int i=0;i<AUTH_TIMEOUT+3;i++) httpsRts(&p);
    CHECK(p.remove_player_event); httpsFreePlayer(&p);
    finish();
}
