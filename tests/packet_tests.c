#include "source_test.h"

void test_readers(void)
{
    fixture();
    const unsigned char ints[][5] = {{0}, {127}, {128,1}, {255,255,255,255,7}, {255,255,255,255,15}, {128,128,128,128,8}};
    const size_t sizes[] = {1,1,2,5,5,5};
    const int32_t values[] = {0,127,128,INT32_MAX,-1,INT32_MIN};
    for (size_t i=0;i<6;i++) { input(ints[i], sizes[i]); CHECK(readVarInt()==values[i]); CHECK(!p.remove_player_event); }
    const unsigned char position[] = {255,255,255,255,255,255,255,255};
    int32_t x,y,z;
    input(position,8); readPosition(&x,&y,&z); CHECK(x==-1 && y==-1 && z==-1);
    const unsigned char numbers[] = {255,254, 63,128,0,0, 63,240,0,0,0,0,0,0};
    input(numbers,sizeof(numbers)); CHECK(readShort()==-2); CHECK(readFloat()==1.0f); CHECK(readDouble()==1.0);
    finish();
}
void test_reader_bounds(void)
{
    fixture();
    struct { char text[4]; unsigned char guard; } dest = {{0}, 0xa5};
    const unsigned char text[] = {4,'a','b','c','d'};
    input(text,sizeof(text)); readString(dest.text,sizeof(dest.text));
    CHECK(dest.guard==0xa5 && p.remove_player_event);
    const unsigned char valid[] = {3,'a','b','c'};
    input(valid,sizeof(valid)); readString(dest.text,sizeof(dest.text));
    CHECK(!strcmp(dest.text,"abc") && !p.remove_player_event);
    input(valid,sizeof(valid)); readString(NULL,0); CHECK(p.remove_player_event);
    const unsigned char negative[] = {255,255,255,255,15};
    input(negative,5); readString(dest.text,sizeof(dest.text)); CHECK(p.remove_player_event);
    const unsigned char overlong[] = {128,128,128,128,128};
    input(overlong,5); readVarInt(); CHECK(p.remove_player_event);
    const unsigned char overflow[] = {255,255,255,255,31};
    input(overflow,5); readVarInt(); CHECK(p.remove_player_event);
    input(valid,1); readString(dest.text,sizeof(dest.text)); CHECK(p.remove_player_event);
    input(valid,0); readByte(); CHECK(p.remove_player_event);
    input(valid,1); readValues()->bufferpos=READBUFSIZE; readPeekByte(); CHECK(p.remove_player_event);
    finish();
}
void test_writers(void)
{
    fixture();
    sendStartPlayer(&p); sendStart();
    sendVarInt(-1); sendShort(-2); sendInt(0x12345678); sendPosition(-1,-1,-1);
    sendFloat(1.0f); sendDouble(1.0); sendString("abc",3);
    sendDone(); sendDispatch();
    const unsigned char expected[] = {35,255,255,255,255,15,255,254,18,52,86,120,
        255,255,255,255,255,255,255,255,63,128,0,0,63,240,0,0,0,0,0,0,3,'a','b','c'};
    CHECK(wire_len==sizeof(expected) && !memcmp(wire,expected,wire_len));
    wire_len=0; sendStart(); sendByte(42); sendPrefixedStart(); sendByte(7); sendByte(8); sendPrefixedEnd(); sendByte(9); sendDone(); sendDispatch();
    const unsigned char prefixed[]={5,42,2,7,8,9};
    CHECK(wire_len==sizeof(prefixed) && !memcmp(wire,prefixed,wire_len));
    wire_len=0; PlayS2Cblockchangeack(&p,300); sendDispatch();
    CHECK(wire_len==4 && wire[0]==3 && wire[1]==S2C_PLAY_BLOCK_CHANGED_ACK && wire[2]==172 && wire[3]==2);
    /* Capture actual zlib streams for independent decompression by Python. */
    const int lengths[] = {COMPRESSION_THRESHOLD-1,COMPRESSION_THRESHOLD,COMPRESSION_THRESHOLD+1};
    for(size_t i=0;i<3;i++) {
        wire_len=0; p.compression_flag=1; sendStart();
        for(int j=0;j<lengths[i];j++) sendByte((uint8_t)(j%251));
        sendDone(); sendDispatch();
        size_t at=0; size_t length=decode(wire,wire_len,&at); CHECK(length==wire_len-at);
        size_t expanded=decode(wire,wire_len,&at);
        CHECK(expanded==(i==0 ? 0 : (size_t)lengths[i]));
        char name[40]; snprintf(name,sizeof(name),"compression-%d.bin",lengths[i]);
        FILE *f=fopen(name,"wb"); CHECK(f); CHECK(fwrite(wire,1,wire_len,f)==wire_len); fclose(f);
    }
    finish();
}
static size_t packet_body(unsigned id)
{
    sendDispatch();
    size_t at=0, length=decode(wire,wire_len,&at);
    CHECK(length==wire_len-at && decode(wire,wire_len,&at)==id);
    return at;
}
void test_s2c(void)
{
    fixture(); sendStartPlayer(&p);
    storage_t inventory={0};
    inventory.inventory_slots[45]=(inventory_slots_t){.count=4,.item_id=300};
    PlayS2Ccontainersetcontent(&p,&inventory);
    size_t at=packet_body(S2C_PLAY_CONTAINER_SET_CONTENT);
    CHECK(decode(wire,wire_len,&at)==0 && decode(wire,wire_len,&at)==0);
    CHECK(decode(wire,wire_len,&at)==INVENTORY_SIZE);
    for(int i=0;i<45;i++) CHECK(decode(wire,wire_len,&at)==0);
    CHECK(decode(wire,wire_len,&at)==4 && decode(wire,wire_len,&at)==300);
    CHECK(decode(wire,wire_len,&at)==0 && decode(wire,wire_len,&at)==0);
    CHECK(decode(wire,wire_len,&at)==0 && at==wire_len);
    wire_len=0; PlayS2Ccontainersetslot(&p,1,44,3,300);
    at=packet_body(S2C_PLAY_CONTAINER_SET_SLOT);
    const unsigned char slot[]={1,0,0,44,3,172,2,0,0};
    CHECK(wire_len-at==sizeof(slot) && !memcmp(wire+at,slot,sizeof(slot)));
    wire_len=0; PlayS2Csettime(24000,1); at=packet_body(S2C_PLAY_SET_TIME);
    const unsigned char time[]={0,0,0,0,0,0,93,192,1};
    CHECK(wire_len-at==sizeof(time) && !memcmp(wire+at,time,sizeof(time)));
    wire_len=0; PlayS2Cblock(MINECRAFT_DIRT,-1,-1,-1); at=packet_body(S2C_PLAY_BLOCK_UPDATE);
    for(int i=0;i<8;i++) CHECK(wire[at++]==255);
    CHECK(decode(wire,wire_len,&at)==MINECRAFT_DIRT && at==wire_len);
    wire_len=0; LoginS2Csuccess(&p); at=packet_body(2);
    CHECK(wire[at+6]==0x30 && wire[at+8]==0x80); at+=16;
    CHECK(decode(wire,wire_len,&at)==5 && !memcmp(wire+at,"Alice",5)); at+=5;
    CHECK(decode(wire,wire_len,&at)==0 && at==wire_len);
    wire_len=0; LoginS2Ccompression(&p); at=packet_body(3);
    CHECK(decode(wire,wire_len,&at)==COMPRESSION_THRESHOLD && at==wire_len && p.compression_flag);
    p.compression_flag=0;
    wire_len=0; ConfigurationS2Cregistry(); sendDispatch(); at=0;
    size_t packets=0;
    while(at<wire_len) {
        size_t length=decode(wire,wire_len,&at), end=at+length;
        CHECK(end<=wire_len && decode(wire,end,&at)==S2C_CONFIGURATION_REGISTRY_DATA);
        size_t name=decode(wire,end,&at); CHECK(name && name<=end-at); at+=name;
        size_t count=decode(wire,end,&at); CHECK(count>0);
        for(size_t i=0;i<count;i++) {
            name=decode(wire,end,&at); CHECK(name && name<=end-at); at+=name;
            CHECK(at<end && wire[at++]==0); /* known-pack entries omit NBT */
        }
        CHECK(at==end); packets++;
    }
    CHECK(packets>=10 && !p.remove_player_event);
    finish();
}
void test_queues(void)
{
    fixture(); sendStartPlayer(&p); sendStart(); sendString("abcd",4); sendDone();
    send_limit=2; sendDispatch(); CHECK(p.out_head && p.out_head->len==4);
    sendStart(); sendByte(9); sendDone(); sendDispatch(); CHECK(p.out_head->next);
    send_limit=sizeof(wire); sendFlush(&p);
    const unsigned char expected[]={5,4,'a','b','c','d',1,9};
    CHECK(wire_len==sizeof(expected) && !memcmp(wire,expected,wire_len)); CHECK(!p.out_head && !p.out_tail);
    wire_len=0; send_error=EINTR; sendStart(); sendByte(17); sendDone(); sendDispatch();
    CHECK(!p.remove_player_event && wire_len==2 && wire[1]==17);
    send_limit=0; sendStart(); sendByte(20); sendDone(); sendDispatch();
    send_error=EINTR; send_limit=sizeof(wire); sendFlush(&p); CHECK(!p.remove_player_event && !p.out_head);
    send_error=EPIPE; sendStart(); sendByte(4); sendDone(); sendDispatch(); CHECK(p.remove_player_event);
    finish();
}
void test_writer_failures(void)
{
    fixture(); sendStartPlayer(&p); sendStart();
    for(int i=0;i<MEM_CHUNK_SIZE;i++) sendByte(1);
    fail_alloc=1; sendByte(2); CHECK(p.remove_player_event);
    for(int i=0;i<20;i++) sendByte(3); /* must not write past the old allocation */
    sendDone(); sendDispatch();
    socketioCleanup(); socketioCleanup(); p.remove_player_event=0;
    wire_len=0; sendStartPlayer(&p); sendStart(); sendByte(5); sendDone(); sendDispatch();
    CHECK(wire_len==2 && wire[1]==5);
    char tiny[1]; sendSwitchToLocalBuffer(tiny,1); sendByte(1); sendByte(2); CHECK(p.remove_player_event); sendRevertFromLocalBuffer();
    finish();
    for (int failure=1; failure<=16; failure++) {
        fixture(); p.compression_flag=1; sendStartPlayer(&p); sendStart();
        for (int i=0;i<COMPRESSION_THRESHOLD+1;i++) sendByte((uint8_t)(i%251));
        fail_alloc=failure; sendDone(); sendDispatch();
        finish();
    }
}

void test_handlers(void)
{
    fixture();
    p.gamePlayerData.crafting_menu=calloc(INVENTORY_SIZE,sizeof(inventory_slots_t)); CHECK(p.gamePlayerData.crafting_menu);
    const unsigned char bad_slot[]={1,0,0,0,0,0,1,255,255,0};
    input(bad_slot,sizeof(bad_slot)); c2s_play_26_1_2[0x12](&p); CHECK(p.remove_player_event);
    const unsigned char hotbar[]={0,9};
    input(hotbar,2); c2s_play_26_1_2[0x35](&p); CHECK(p.gamePlayerData.inventory_slot==0 && p.remove_player_event);
    const unsigned char short_move[]={0};
    input(short_move,1); c2s_play_26_1_2[0x1e](&p); CHECK(p.remove_player_event && !p.position_event);
    const unsigned char nan_move[]={127,192,0,0,0,0,0,0,0};
    input(nan_move,sizeof(nan_move)); c2s_play_26_1_2[0x20](&p); CHECK(p.remove_player_event && !p.position_event);
    const unsigned char rotation[]={68,52,0,0,67,135,0,0,1}; /* 720 and 270 degrees */
    input(rotation,sizeof(rotation)); c2s_play_26_1_2[0x20](&p);
    CHECK(!p.remove_player_event && p.position_event && p.yaw==0 && p.pitch==-90);
    const unsigned char click[]={0,0,0,0,0,0,1,0,36,1,1,255,255,255,255,15,0,0};
    input(click,sizeof(click)); c2s_play_26_1_2[0x12](&p); CHECK(p.remove_player_event);
    storageInventoryUpdateSlot(&p,5,1,1); storageInventoryUpdateSlot(&p,45,1,2);
    p.gamePlayerData.crafting_menu[10]=(inventory_slots_t){3,3};
    const unsigned char close[]={1};
    input(close,1); c2s_play_26_1_2[0x13](&p);
    CHECK(!p.gamePlayerData.crafting_menu && p.gamePlayerData.full_inventory_update_event);
    inventory_slots_t s; storageInventoryGetSlot(&p,5,&s); CHECK(s.item_id==1);
    storageInventoryGetSlot(&p,45,&s); CHECK(s.item_id==2);
    storageInventoryGetSlot(&p,9,&s); CHECK(s.item_id==3 && s.count==3);
    finish();
}
