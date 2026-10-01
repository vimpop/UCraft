#include "source_test.h"
#include <limits.h>

void test_inventory_capacity(void)
{
    fixture();
    for(int i=36;i<44;i++) storageInventoryUpdateSlot(&p,(int16_t)i,64,1);
    storageInventoryInsertItem(&p,2,1);
    inventory_slots_t s; storageInventoryGetSlot(&p,44,&s); CHECK(s.item_id==2 && s.count==1);
    storageInventoryUpdateSlot(&p,44,63,2); storageInventoryInsertItem(&p,2,4);
    storageInventoryGetSlot(&p,44,&s); CHECK(s.count==64);
    storageInventoryGetSlot(&p,9,&s); CHECK(s.item_id==2 && s.count==3);
    for(int i=9;i<35;i++) storageInventoryUpdateSlot(&p,(int16_t)i,64,1);
    storageInventoryInsertItem(&p,3,2);
    storageInventoryGetSlot(&p,35,&s); CHECK(s.item_id==3 && s.count==2);
    storageInventoryInsertItem(&p,3,100);
    storageInventoryGetSlot(&p,35,&s); CHECK(s.count==64);
    for(int i=9;i<=44;i++) { storageInventoryGetSlot(&p,(int16_t)i,&s); CHECK(s.count<=64); }
    finish();
}
void test_block_churn(void)
{
    fixture();
    for(int i=0;i<200;i++) {
        int x=i*16, z=-i*16, y=100;
        int original=worldGetBlock(x,y,z);
        CHECK(blocksUpdate(MINECRAFT_DIRT,x,(int16_t)y,z));
        CHECK(blocksGetBlock(x,(int16_t)y,z)==MINECRAFT_DIRT);
        CHECK(blocksUpdate((blocksDefaultState)original,x,(int16_t)y,z));
        CHECK(blocksGetBlock(x,(int16_t)y,z)==original);
    }
    for(int i=0;i<100;i++) CHECK(blocksUpdate(MINECRAFT_DIRT,i*16,100,i*16));
    for(int i=0;i<100;i++) CHECK(blocksGetBlock(i*16,100,i*16)==MINECRAFT_DIRT);
    finish();
}
void test_allocations(void)
{
    for(int n=1;n<=2;n++) {
        fixture(); fail_alloc=n;
        CHECK(!blocksUpdate(MINECRAFT_DIRT,0,100,0));
        finish();
    }
    fixture(); fail_alloc=1; storageInventoryInsertItem(&p,1,1); CHECK(p.remove_player_event);
    finish();
}
void test_players(void)
{
    fixture();
    player_t *a=playerAdd(11), *b=playerAdd(12), *c=playerAdd(13);
    CHECK(a && b && c && playerGetCount()==3);
    strcpy(a->name,"Alice"); strcpy(b->name,"Bob"); strcpy(c->name,"Alice");
    CHECK(playerCheckDuplicate(c)); CHECK(!playerCheckDuplicate(b));
    strcpy(c->name,""); CHECK(playerCheckName(c));
    strcpy(c->name,"valid_Name1"); CHECK(!playerCheckName(c));
    a->active=1; b->ready_to_play=1; CHECK(playerGetActiveCount()==1 && playerGetInGameCount()==1);
    CHECK(playerGetId(b->id)==b); CHECK(!playerRemove(b)); CHECK(playerGetCount()==2);
    CHECK(!playerRemove(a)); CHECK(playerGetCount()==1 && playerGetHead()==c);
    playerCleanup(); playerCleanup(); CHECK(playerGetCount()==0 && !playerPopDisconnected());
    finish();
}
static uint64_t be_number(const unsigned char *bytes, size_t n)
{
    uint64_t value=0; for(size_t i=0;i<n;i++) value=(value<<8)|bytes[i]; return value;
}
void test_world(void)
{
    fixture(); sendStartPlayer(&p);
    unsigned char data[20000], again[20000];
    const int coords[][2]={{0,0},{-1,-1},{1,-1}};
    for(size_t c=0;c<3;c++) {
        int cx=coords[c][0],cz=coords[c][1];
        sendSwitchToLocalBuffer((char *)data,sizeof(data)); worldGenerateChunk(cx,cz,3,7); size_t len=sendRevertFromLocalBuffer();
        sendSwitchToLocalBuffer((char *)again,sizeof(again)); worldGenerateChunk(cx,cz,3,7); size_t len2=sendRevertFromLocalBuffer();
        CHECK(len==len2 && !memcmp(data,again,len));
        size_t at=0;
        for(int section=0;section<24;section++) {
            CHECK(at+5<=len); size_t blocks=(size_t)be_number(data+at,2); at+=4;
            int bits=data[at++]; int palette[16]; size_t num;
            if(bits==0) { palette[0]=(int)decode(data,len,&at); num=1; }
            else { CHECK(bits==4); num=decode(data,len,&at); CHECK(num<=16); for(size_t i=0;i<num;i++) palette[i]=(int)decode(data,len,&at); }
            size_t nonair=0;
            for(int voxel=0;voxel<4096;voxel++) {
                size_t idx=0;
                if(bits) { CHECK(at+2048<=len); uint64_t word=be_number(data+at+(voxel/16)*8,8); idx=(size_t)((word>>((voxel%16)*4))&15); }
                CHECK(idx<num); int state=palette[idx]; if(state!=MINECRAFT_AIR) nonair++;
                int x=voxel%16, z=(voxel/16)%16, y=voxel/256;
                if((x==0 || x==15) && (z==0 || z==15) && (y==0 || y==15))
                    CHECK(state==worldGetBlock(cx*16+x,section*16+y-64,cz*16+z));
            }
            CHECK(nonair==blocks); if(bits) at+=2048;
            CHECK(at<len && data[at++]==0); CHECK(decode(data,len,&at)==0);
        }
        CHECK(at+4==len);
    }
    CHECK(worldGetBlock(0,-65,0)==MINECRAFT_AIR && worldGetBlock(0,320,0)==MINECRAFT_AIR);
    CHECK(worldGetBlock(0,INT_MAX,0)==MINECRAFT_AIR && worldGetBlock(0,INT_MIN,0)==MINECRAFT_AIR);
    sendSwitchToLocalBuffer((char *)data,sizeof(data)); worldGenerateChunk(0,0,0,23); size_t len=sendRevertFromLocalBuffer();
    sendSwitchToLocalBuffer((char *)again,sizeof(again)); worldGenerateChunk(0,0,3,7); size_t len2=sendRevertFromLocalBuffer();
    CHECK(len==len2 && !memcmp(data,again,len));
    finish();
}
