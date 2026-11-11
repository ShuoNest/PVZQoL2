// Offline checks: all game calls are mocked; no game process or saves are opened.
#include "../native.c"
#include <stdlib.h>

enum {PROFILE=1,COINS,ADD_COINS,SET_COINS,SET_STOCK,SAVE,GET_RAKE,COST,MODE,VERSUS,VASES,WHACK,BOSS,IZOMBIE,SQUIRREL};
#define METHOD(x) ((void*)(uintptr_t)(x))
static unsigned char fakeBoard[0x400],fakeApp[0x200],fakeUser[0x100],fakeProfile[0x100],fakePurchases[0x100],fakeRows[0x100];
static Shared fakeShared;
static int fakeMode,fakeCost,hasRake,allowPlace,failStockOnce,originalCalls;
static unsigned char no=0,yes=1;
static void put_int(void *obj,int offset,int value){memcpy((char*)obj+offset,&value,4);}
static void put_ptr(void *obj,int offset,void *value){memcpy((char*)obj+offset,&value,8);}
static void* fake_unbox(void *obj){return obj;}
static void* fake_invoke(void *methodInfo,void *obj,void **args,void **exception){
    uintptr_t id=(uintptr_t)methodInfo;
    if(id<=SAVE&&obj!=fakeUser){*exception=(void*)1;return 0;}
    switch(id){
        case PROFILE:return fakeProfile;
        case COINS:return fakeProfile+0x50;
        case ADD_COINS:put_int(fakeProfile,0x50,get_int(fakeProfile,0x50)+*(int*)args[0]);return 0;
        case SET_COINS:put_int(fakeProfile,0x50,*(int*)args[0]);return 0;
        case SET_STOCK:
            if(*(int*)args[0]!=24){*exception=(void*)1;return 0;}
            if(failStockOnce){failStockOnce=0;*exception=(void*)1;return 0;}
            put_int(fakePurchases,0x80,*(int*)args[1]);return 0;
        case SAVE:return &yes;
        case GET_RAKE:return hasRake?(void*)1:0;
        case COST:if(obj||*(int*)args[0]!=24){*exception=(void*)1;return 0;}return &fakeCost;
        case MODE:return &fakeMode;
        case VERSUS:case VASES:case WHACK:case BOSS:case IZOMBIE:case SQUIRREL:return &no;
        default:*exception=(void*)1;return 0;
    }
}
static void fake_original_place(void *board,void *mi){
    (void)mi;if(board!=fakeBoard)exit(90);originalCalls++;
    int stock=get_int(fakePurchases,0x80);
    if(allowPlace&&stock>0)put_int(fakePurchases,0x80,stock-1);
}
static void setup(int coins,int stock){
    memset(fakeBoard,0,sizeof(fakeBoard));memset(fakeApp,0,sizeof(fakeApp));memset(fakeUser,0,sizeof(fakeUser));
    memset(fakeProfile,0,sizeof(fakeProfile));memset(fakePurchases,0,sizeof(fakePurchases));memset(fakeRows,0,sizeof(fakeRows));memset(&fakeShared,0,sizeof(fakeShared));
    boardApp=0x310;boardRows=0x158;appUser=0x70;profilePurchases=0x78;profileCoins=0x50;
    put_ptr(fakeBoard,boardApp,fakeApp);put_ptr(fakeBoard,boardRows,fakeRows);put_ptr(fakeApp,appUser,fakeUser);put_ptr(fakeProfile,profilePurchases,fakePurchases);
    put_int(fakePurchases,0x18,30);put_int(fakePurchases,0x80,stock);put_int(fakeProfile,0x50,coins);put_int(fakeRows,0x18,6);
    for(int i=0;i<5;i++)put_int(fakeRows,0x20+i*4,1);
    fakeMode=0;fakeCost=20;hasRake=0;allowPlace=1;failStockOnce=0;originalCalls=0;buyingRake=0;quitting=0;
    shared=&fakeShared;shared->status=1;shared->flags=27;
    activeProfile=METHOD(PROFILE);userGetCoins=METHOD(COINS);userAddCoins=METHOD(ADD_COINS);userSetCoins=METHOD(SET_COINS);userSetPurchases=METHOD(SET_STOCK);userSaveDetails=METHOD(SAVE);
    boardGetRake=METHOD(GET_RAKE);rakeCost=METHOD(COST);appGameMode=METHOD(MODE);isVersus=METHOD(VERSUS);isVases=METHOD(VASES);isWhack=METHOD(WHACK);isBoss=METHOD(BOSS);isIZombie=METHOD(IZOMBIE);isSquirrel=METHOD(SQUIRREL);
    object_unbox=fake_unbox;runtime_invoke=fake_invoke;originalPlaceRake=fake_original_place;
}
#define CHECK(test,name) do{if(!(test)){fprintf(stderr,"FAIL: %s\n",name);return 1;}}while(0)
int main(void){
    setup(100,0);place_rake(fakeBoard,0);
    CHECK(get_int(fakeProfile,0x50)==80&&get_int(fakePurchases,0x80)==2&&shared->rakePurchases==1&&shared->rakeSpent==20&&originalCalls==1,"buy one pack then consume one use");
    place_rake(fakeBoard,0);place_rake(fakeBoard,0);place_rake(fakeBoard,0);
    CHECK(get_int(fakeProfile,0x50)==60&&get_int(fakePurchases,0x80)==2&&shared->rakePurchases==2&&shared->rakeSpent==40,"no repeat purchase until next zero-stock level");
    setup(19,0);place_rake(fakeBoard,0);
    CHECK(get_int(fakeProfile,0x50)==19&&get_int(fakePurchases,0x80)==0&&shared->rakePurchases==0&&shared->rakeInsufficient==1,"insufficient coins do not change state");
    setup(100,3);place_rake(fakeBoard,0);
    CHECK(get_int(fakeProfile,0x50)==100&&get_int(fakePurchases,0x80)==2&&shared->rakePurchases==0,"existing uses consumed normally without payment");
    setup(100,0);shared->flags=11;place_rake(fakeBoard,0);
    CHECK(get_int(fakeProfile,0x50)==100&&get_int(fakePurchases,0x80)==0&&shared->rakePurchases==0,"feature disabled");
    setup(100,0);fakeMode=43;allowPlace=0;place_rake(fakeBoard,0);
    CHECK(get_int(fakeProfile,0x50)==100&&shared->rakePurchases==0,"no purchase in Zen Garden");
    setup(100,0);hasRake=1;allowPlace=0;place_rake(fakeBoard,0);
    CHECK(get_int(fakeProfile,0x50)==100&&shared->rakePurchases==0,"no purchase if a rake already exists");
    setup(100,0);failStockOnce=1;place_rake(fakeBoard,0);
    CHECK(get_int(fakeProfile,0x50)==100&&get_int(fakePurchases,0x80)==0&&shared->rakePurchases==0&&shared->errors>0,"failed stock update restores previous coins and stock");
    setup(100,0);allowPlace=0;for(int i=0;i<5;i++)put_int(fakeRows,0x20+i*4,0);place_rake(fakeBoard,0);
    CHECK(get_int(fakeProfile,0x50)==100&&shared->rakePurchases==0,"no purchase with no eligible land rows");
    setup(20,0);place_rake(fakeBoard,0);
    CHECK(get_int(fakeProfile,0x50)==0&&get_int(fakePurchases,0x80)==2&&shared->rakePurchases==1,"exact balance allowed");
    puts("10 offline rake purchase checks passed. No game process or save files accessed.");return 0;
}

