// Exercises production native.c with mocked game services; no live game/save access.
#include "../native.c"
#include <stdlib.h>
enum {ACTIVE=1,COINS,ADD,SETCOINS,SETSTOCK,SAVE,MODE,DIALOG,ENTRIES,COUNT,ITEM,COST,CANFEED,FERTILIZE,TREE_SIZE};
#define M(n) ((void*)(uintptr_t)(n))
static unsigned char app[0x240],board[0x400],user[0x100],profile[0x180],purchases[0x120],garden[0x80],cursor[0x100],challenge[0x100],data[0x80],entries[0x40],entry[3][0x80];
static Shared telem;static int modeValue=50,costs[3]={75,100,100},dialogs,canFeed=1,height=59,saves,feeds,failStock;
static unsigned char truth=1; static int frameCalls; static void frame_original(void *appObj,void *mi){(void)mi;if(appObj!=app)exit(99);frameCalls++;}
static void put(void *p,int off,int n){memcpy((char*)p+off,&n,4);}static void ptr(void*p,int off,void*v){memcpy((char*)p+off,&v,8);}
static void* unbox(void *v){return v;}static void* cls(void *v){(void)v;return M(99);}static const char* cname(void*v){(void)v;return "List`1";}
static void* find_method(void*k,const char*n,int argc){(void)k;(void)argc;return !strcmp(n,"get_Count")?M(COUNT):!strcmp(n,"get_Item")?M(ITEM):0;}
static void* run(void*m,void*obj,void**args,void**exception){
 static int count=3;
 switch((uintptr_t)m){
 case ACTIVE:return profile;case COINS:return profile+0x50;
 case ADD:put(profile,0x50,get_int(profile,0x50)+*(int*)args[0]);return 0;
 case SETCOINS:put(profile,0x50,*(int*)args[0]);return 0;
 case SETSTOCK:if(failStock){failStock=0;*exception=M(1);return 0;}put(purchases,0x20+*(int*)args[0]*4,*(int*)args[1]);return 0;
 case SAVE:saves++;return &truth;case MODE:return &modeValue;case DIALOG:return &dialogs;case ENTRIES:return entries;case COUNT:return &count;
 case ITEM:return entry[*(int*)args[0]];
 case COST:if(obj||args[1]!=user){*exception=M(1);return 0;}for(int i=0;i<3;i++)if(args[0]==entry[i])return &costs[i];*exception=M(1);return 0;
 case CANFEED:return &canFeed;
 case FERTILIZE:feeds++;put(purchases,0x90,get_int(purchases,0x90)-1);return 0;
 case TREE_SIZE:return &height;default:*exception=M(1);return 0;
 }
}
static void setup(int coins,int food){
 memset(app,0,sizeof(app));memset(board,0,sizeof(board));memset(profile,0,sizeof(profile));memset(purchases,0,sizeof(purchases));memset(garden,0,sizeof(garden));memset(cursor,0,sizeof(cursor));memset(challenge,0,sizeof(challenge));memset(&telem,0,sizeof(telem));
 appUser=0x70;appGarden=0x108;appData=0x68;appScene=0xe0;boardPaused=0x118;boardCursor=0xa8;cursorType=0x7c;gardenInStore=0x40;profilePurchases=0x78;profileCoins=0x50;storeEntryItem=0x48;
 ptr(app,appUser,user);ptr(app,appGarden,garden);ptr(app,appData,data);ptr(board,boardCursor,cursor);ptr(profile,profilePurchases,purchases);put(app,appScene,3);put(profile,profileCoins,coins);put(purchases,0x18,30);
 put(purchases,0x20+27*4,1);put(purchases,0x90,food);put(entry[0],0x48,14);put(entry[1],0x48,15);put(entry[2],0x48,28);
 appBoard=0x100;boardChallenge=0x110;ptr(app,appBoard,board);ptr(board,boardChallenge,challenge);originalActiveUpdate=frame_original;frameCalls=0;shared=&telem;shared->status=1;shared->flags=480;quitting=0;gardenBusy=0;nextSupplyCheck=nextTreeFeed=0;modeValue=50;dialogs=0;canFeed=1;height=59;saves=feeds=failStock=0;
 activeProfile=M(ACTIVE);userGetCoins=M(COINS);userAddCoins=M(ADD);userSetCoins=M(SETCOINS);userSetPurchases=M(SETSTOCK);userSaveDetails=M(SAVE);appGameMode=M(MODE);appDialogCount=M(DIALOG);getStoreEntries=M(ENTRIES);storeEntryCost=M(COST);treeCanFeed=M(CANFEED);treeFertilize=M(FERTILIZE);treeGetSize=M(TREE_SIZE);
 object_unbox=unbox;object_get_class=cls;class_get_name=cname;class_get_method_from_name=find_method;runtime_invoke=run;
}
#define CHECK(v,n) do{if(!(v)){fprintf(stderr,"FAIL: %s\n",n);return 1;}}while(0)
int main(void){
 setup(1000,1000);garden_automation(app,board,challenge,50);
 CHECK(get_int(profile,0x50)==725&&supply_stock(profile,14)==1005&&supply_stock(profile,15)==1005&&supply_stock(profile,28)==1000&&telem.treeFeeds==1&&saves==3,"native prices, 5/5/1 pack quantities, one animated feed");
 garden_automation(app,board,challenge,50);CHECK(saves==3&&feeds==1,"throttle repeated frames");
 nextSupplyCheck=nextTreeFeed=0;garden_automation(app,board,challenge,50);CHECK(saves==4&&telem.treeFoodPurchases==2&&get_int(profile,0x50)==625,"only depleted tree food repurchased");
 setup(0,1000);garden_automation(app,board,challenge,50);CHECK(saves==0&&feeds==0&&get_int(profile,0x50)==0&&telem.supplyInsufficient==3,"insufficient funds do not grant supplies or feed");
 setup(100,1000);shared->flags=128|256;garden_automation(app,board,challenge,50);CHECK(get_int(profile,0x50)==0&&feeds==1&&saves==1,"exact balance buys one dose");
 setup(1000,1002);shared->flags=256;garden_automation(app,board,challenge,50);CHECK(feeds==1&&saves==0&&get_int(profile,0x50)==1000&&supply_stock(profile,28)==1001,"feed existing stock without buying");
 setup(1000,1000);shared->flags=128;garden_automation(app,board,challenge,50);CHECK(saves==1&&feeds==0&&supply_stock(profile,28)==1001,"buy-only switch independent");
 setup(1000,1002);put(board,boardPaused,1);garden_automation(app,board,challenge,50);CHECK(!feeds&&!saves,"paused board excluded");
 setup(1000,1002);garden[gardenInStore]=1;garden_automation(app,board,challenge,50);CHECK(!feeds&&!saves,"store excluded");
 setup(1000,1002);dialogs=1;garden_automation(app,board,challenge,50);CHECK(!feeds&&!saves,"dialog excluded");
 setup(1000,1002);modeValue=43;garden_automation(app,board,0,43);CHECK(!feeds&&saves==2,"garden replenishes but never feeds tree");
 setup(1000,1002);modeValue=0;garden_automation(app,board,challenge,50);CHECK(!feeds&&!saves,"combat excluded");
 setup(1000,1002);put(cursor,cursorType,10);garden_automation(app,board,challenge,50);CHECK(!feeds,"manual tool not cancelled");
 setup(1000,1002);canFeed=0;garden_automation(app,board,challenge,50);CHECK(!feeds,"native animation gate respected");
 setup(1000,1000);failStock=1;int result=buy_supply_if_empty(app,user,profile,28,1,&telem.treeFoodPurchases);CHECK(!result&&get_int(profile,0x50)==1000&&supply_stock(profile,28)==1000&&!telem.treeFoodPurchases,"failed transaction rolled back");
 setup(1000,1002);shared->flags=0;garden_automation(app,board,challenge,50);CHECK(!feeds&&!saves,"all switches disabled");
 setup(1000,1002);quitting=1;garden_automation(app,board,challenge,50);CHECK(!feeds&&!saves,"quit gate respected");
 setup(1000,1002);height=2147483000;garden_automation(app,board,challenge,50);CHECK(!feeds,"signed height overflow prevented");
 setup(1000,1002);active_update(app,0);CHECK(frameCalls==1&&feeds==1&&saves==2,"activity frame callback preserves game update and feeds tree"); setup(1000,1002);modeValue=43;active_update(app,0);CHECK(frameCalls==1&&feeds==0&&saves==2,"activity frame callback routes garden without tree feeding"); puts("20 garden automation scenarios passed; no game process or save files accessed.");return 0;
}


