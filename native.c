// PVZQoL2: native animated pickup + garden-only cabbage appearance.
// No zombie hooks, health reads, graphics hooks or overlay windows.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "MinHook.h"

#define MAGIC 0x32515A50u
#define MAP_SIZE 4096
typedef struct {
    uint32_t magic,version; volatile LONG flags,status;
    uint64_t heartbeat;
    volatile LONG starts,completes,sunStarts,moneyStarts,sunCompletes,moneyCompletes;
    volatile LONG errors,animationFrames; uint32_t minCoinAge;
    volatile LONG gardenRequests,gardenOverrides;
    volatile LONG rakePurchases,rakeSpent,rakeInsufficient;
    volatile LONG fertilizerPurchases,bugSprayPurchases,treeFoodPurchases,supplySpent,supplyInsufficient;
    volatile LONG treeFeeds,treeHeight;
} Shared;
_Static_assert(offsetof(Shared,animationFrames)==52,"animation layout");
_Static_assert(offsetof(Shared,minCoinAge)==56,"delay layout");
_Static_assert(offsetof(Shared,gardenRequests)==60,"garden telemetry layout");
_Static_assert(offsetof(Shared,gardenOverrides)==64,"garden telemetry layout");
_Static_assert(offsetof(Shared,rakePurchases)==68,"rake telemetry layout");
_Static_assert(offsetof(Shared,fertilizerPurchases)==80,"supply telemetry layout");
_Static_assert(offsetof(Shared,treeFeeds)==100,"tree telemetry layout");
static Shared *shared;
static HANDLE mapHandle;
static volatile LONG quitting;
static void *coinCollect,*coinClass,*appGameMode;
static int cDead,cType,cAge,cCollecting,definitionSeed,definitionEaster;
static DWORD gardenContext=TLS_OUT_OF_INDEXES;
static void* (*originalCreatePlant)(void*,int,void*,unsigned char,unsigned char,unsigned char,unsigned char,void*);
static void* (*originalGetPrefab)(void*,unsigned char,unsigned char,unsigned char,unsigned char,void*);
static void (*originalCoinUpdate)(void*,void*);
static void (*originalCoinScore)(void*,void*);
static void (*originalQuit)(void*,void*);
static void (*originalPlaceRake)(void*,void*);
static int boardApp,boardRows,appUser,profilePurchases,profileCoins;
static void *activeProfile,*userGetCoins,*userAddCoins,*userSetCoins,*userSetPurchases,*userSaveDetails,*boardGetRake,*rakeCost;
static void *isVersus,*isVases,*isWhack,*isBoss,*isIZombie,*isSquirrel;
static volatile LONG buyingRake;
static void (*originalActiveUpdate)(void*,void*);
static void *treeCanFeed,*treeFertilize,*treeGetSize,*getStoreEntries,*storeEntryCost,*appDialogCount;
static int appBoard,boardChallenge,gardenInStore;
static int appGarden,appData,appScene,boardPaused,boardCursor,cursorType,storeEntryItem;
static ULONGLONG nextSupplyCheck,nextTreeFeed;
static volatile LONG gardenBusy;

static void* (*domain_get)(void);
static void** (*domain_get_assemblies)(void*,size_t*);
static void* (*assembly_get_image)(void*);
static size_t (*image_get_class_count)(void*);
static void* (*image_get_class)(void*,size_t);
static const char* (*class_get_name)(void*);
static const char* (*class_get_namespace)(void*);
static void* (*class_get_field_from_name)(void*,const char*);
static size_t (*field_get_offset)(void*);
static void* (*class_get_method_from_name)(void*,const char*,int);
static void* (*runtime_invoke)(void*,void*,void**,void**);
static void* (*object_unbox)(void*);
static void* (*object_get_class)(void*);
static void* (*thread_attach)(void*);
static void (*thread_detach)(void*);

static void log_line(const char *text) {
    wchar_t path[MAX_PATH]; HMODULE self=0;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)&log_line,&self);
    GetModuleFileNameW(self,path,MAX_PATH);
    wchar_t *slash=wcsrchr(path,L'\\'); if(!slash) return;
    wcscpy(slash+1,L"native.log");
    FILE *f=_wfopen(path,L"ab"); if(f){fprintf(f,"%llu %s\n",(unsigned long long)GetTickCount64(),text);fclose(f);}
}
static int read_value(void *obj,int off,void *dest,SIZE_T bytes) {
    SIZE_T got=0; if(!obj||off<0) return 0;
    return ReadProcessMemory(GetCurrentProcess(),(char*)obj+off,dest,bytes,&got)&&got==bytes;
}
static int get_int(void *obj,int off){int v=0;read_value(obj,off,&v,4);return v;}
static int get_bool(void *obj,int off){unsigned char v=0;read_value(obj,off,&v,1);return v;}
static void* get_ptr(void *obj,int off){void *v=0;read_value(obj,off,&v,sizeof(v));return v;}
static void* find_class(const char *ns,const char *name){
    size_t n=0;void **assemblies=domain_get_assemblies(domain_get(),&n);
    if(!assemblies||n>1024)return 0;
    for(size_t a=0;a<n;a++){
        void *image=assembly_get_image(assemblies[a]);size_t count=image_get_class_count(image);
        if(count>200000)continue;
        for(size_t i=0;i<count;i++){
            void *k=image_get_class(image,i);const char *kn=class_get_name(k),*kns=class_get_namespace(k);
            if(kn&&!strcmp(kn,name)&&(!ns||(kns&&!strcmp(ns,kns))))return k;
        }
    }return 0;
}
static int field(void *k,const char *name){void *f=k?class_get_field_from_name(k,name):0;return f?(int)field_get_offset(f):-1;}
static void* method(void *k,const char *name,int n){return k?class_get_method_from_name(k,name,n):0;}
static int invoke_checked(void *m,void *obj,void **args,void **result){
    if(!m)return 0;void *exception=0;*result=runtime_invoke(m,obj,args,&exception);
    if(exception){if(shared)InterlockedIncrement(&shared->errors);return 0;}return 1;
}
static void* invoke(void *m,void *obj,void **args){
    void *result=0;return invoke_checked(m,obj,args,&result)?result:0;
}
static int invoke_int(void *m,void *obj,void **args,int *value){
    void *result=0;if(!invoke_checked(m,obj,args,&result)||!result)return 0;
    return read_value(object_unbox(result),0,value,4);
}
static int invoke_bool(void *m,void *obj,int *value){
    void *result=0;if(!invoke_checked(m,obj,0,&result)||!result)return 0;
    unsigned char v=0;if(!read_value(object_unbox(result),0,&v,1))return 0;*value=v!=0;return 1;
}
// Read the same data-backed price used by the current shop UI. Do not use the
// legacy StoreScreen prices, which can differ from the addressable shop data.
static int supply_cost(void *app,void *user,int item,int *cost){
    void *data=get_ptr(app,appData),*entries=0;
    if(!data||!invoke_checked(getStoreEntries,data,0,&entries)||!entries)return 0;
    void *klass=object_get_class(entries);const char *name=class_get_name(klass);
    int array=name&&strstr(name,"[]")!=0,count=0;
    void *getItem=0;
    if(array)count=get_int(entries,0x18);
    else{
        if(!invoke_int(method(klass,"get_Count",0),entries,0,&count))return 0;
        getItem=method(klass,"get_Item",1);if(!getItem)return 0;
    }
    if(count<1||count>256)return 0;
    for(int i=0;i<count;i++){
        void *entry=0;
        if(array)entry=get_ptr(entries,0x20+i*8);
        else{void *args[]={&i};if(!invoke_checked(getItem,entries,args,&entry))return 0;}
        if(!entry||get_int(entry,storeEntryItem)!=item)continue;
        void *args[]={entry,user};
        return invoke_int(storeEntryCost,0,args,cost)&&*cost>0&&*cost<=100000;
    }
    return 0;
}
static int supply_stock(void *profile,int item){
    void *purchases=get_ptr(profile,profilePurchases);
    int length=get_int(purchases,0x18);
    if(item<0||item>=length||length>1024)return -1;
    return get_int(purchases,0x20+item*4);
}
static int buy_supply_if_empty(void *app,void *user,void *profile,int item,int quantity,volatile LONG *counter){
    int stock=supply_stock(profile,item),coins=0,cost=0;
    // Native garden consumables encode ownership as 1000, plus remaining uses.
    if(stock<0||stock>1000)return 0;
    if(!supply_cost(app,user,item,&cost)||!invoke_int(userGetCoins,user,0,&coins))return 0;
    if(coins<cost){InterlockedIncrement(&shared->supplyInsufficient);return 0;}
    int delta=-cost,newStock=1000+quantity;void *result=0;
    void *pay[]={&delta},*set[]={&item,&newStock};
    int paid=invoke_checked(userAddCoins,user,pay,&result);
    int stocked=paid&&invoke_checked(userSetPurchases,user,set,&result);
    if(!stocked||get_int(profile,profileCoins)!=coins-cost||supply_stock(profile,item)!=newStock){
        void *restoreCoins[]={&coins},*restoreStock[]={&item,&stock};
        invoke_checked(userSetCoins,user,restoreCoins,&result);invoke_checked(userSetPurchases,user,restoreStock,&result);
        InterlockedIncrement(&shared->errors);log_line("garden supply purchase failed; previous coins/stock restored");return 0;
    }
    invoke_checked(userSaveDetails,user,0,&result);
    InterlockedIncrement(counter);InterlockedExchangeAdd(&shared->supplySpent,cost);
    char msg[160];snprintf(msg,sizeof(msg),"garden auto-buy: item=%d cost=%d internal uses=%d",item,cost,quantity);log_line(msg);
    return 1;
}
static int garden_ready(void *app,void *board,int expectedMode){
    int mode=0,dialogs=0;
    if(!app||!board||get_bool(board,boardPaused)||get_int(app,appScene)!=3)return 0;
    if(!invoke_int(appGameMode,app,0,&mode)||mode!=expectedMode)return 0;
    void *garden=get_ptr(app,appGarden);
    if(!garden||get_bool(garden,gardenInStore))return 0;
    return invoke_int(appDialogCount,app,0,&dialogs)&&dialogs==0;
}
static void garden_automation(void *app,void *board,void *challenge,int mode){
    if(quitting||!shared||shared->status!=1||!(shared->flags&480))return;
    if(!garden_ready(app,board,mode)||InterlockedCompareExchange(&gardenBusy,1,0)!=0)return;
    void *user=get_ptr(app,appUser),*profile=user?invoke(activeProfile,user,0):0;
    if(!profile)goto done;
    ULONGLONG now=GetTickCount64();LONG flags=shared->flags;
    if(now>=nextSupplyCheck){
        nextSupplyCheck=now+1000;
        if(flags&32)buy_supply_if_empty(app,user,profile,14,5,&shared->fertilizerPurchases);
        if(flags&64)buy_supply_if_empty(app,user,profile,15,5,&shared->bugSprayPurchases);
        if((flags&128)&&supply_stock(profile,27)>0)buy_supply_if_empty(app,user,profile,28,1,&shared->treeFoodPurchases);
    }
    if(mode==50&&challenge){
        int height=0,canFeed=0;
        if(invoke_int(treeGetSize,challenge,0,&height))InterlockedExchange(&shared->treeHeight,height);
        if(!(flags&256)||now<nextTreeFeed||height>=2147483000||supply_stock(profile,28)<=1000)goto done;
        // Never cancel a tool the player is currently holding. Native CanFeed
        // also rejects an active tree-food animation and a growing tree.
        void *cursor=get_ptr(board,boardCursor);
        if(!cursor||get_int(cursor,cursorType)!=0||!invoke_bool(treeCanFeed,challenge,&canFeed)||!canFeed)goto done;
        int before=supply_stock(profile,28);void *result=0;
        nextTreeFeed=now+3000;
        if(invoke_checked(treeFertilize,challenge,0,&result)&&supply_stock(profile,28)==before-1){
            InterlockedIncrement(&shared->treeFeeds);
            // Fertilize spawns the game's normal fertilizer tool and consumes
            // one dose. Its original update later animates and grows the tree.
        }
    }
done:InterlockedExchange(&gardenBusy,0);
}
static void active_update(void *app,void *mi){
    originalActiveUpdate(app,mi);
    if(quitting||!shared||shared->status!=1||!(shared->flags&480))return;
    int mode=0;if(!invoke_int(appGameMode,app,0,&mode)||(mode!=43&&mode!=50))return;
    void *board=get_ptr(app,appBoard);
    garden_automation(app,board,mode==50?get_ptr(board,boardChallenge):0,mode);
}
static int rake_allowed(void *board,void *app){
    void *existing=0;int value=0,mode=0;
    if(!invoke_checked(boardGetRake,board,0,&existing)||existing)return 0;
    if(!invoke_bool(isVersus,app,&value)||value)return 0;
    if(!invoke_bool(isVases,app,&value))return 0;
    if(!value){
        if(!invoke_int(appGameMode,app,0,&mode))return 0;
        if(mode==42||mode==43||mode==50||mode==23||mode==20||mode==24||mode==28)return 0;
        void *excluded[]={isWhack,isBoss,isIZombie,isSquirrel};
        for(int i=0;i<4;i++){if(!invoke_bool(excluded[i],app,&value)||value)return 0;}
    }
    void *rows=get_ptr(board,boardRows);if(!rows||get_int(rows,0x18)<5)return 0;
    for(int i=0;i<5;i++)if(get_int(rows,0x20+i*4)==1)return 1;
    return 0;
}
static void buy_rake_if_empty(void *board){
    void *app=get_ptr(board,boardApp),*user=get_ptr(app,appUser);
    if(!user)return;
    void *profile=invoke(activeProfile,user,0),*purchases=get_ptr(profile,profilePurchases);
    if(!profile||!purchases||get_int(purchases,0x18)<26)return;
    int oldStock=get_int(purchases,0x80);if(oldStock!=0||!rake_allowed(board,app))return;
    int item=24,cost=0,coins=0;void *costArgs[]={&item};
    if(!invoke_int(rakeCost,0,costArgs,&cost)||cost<=0||cost>100000||!invoke_int(userGetCoins,user,0,&coins))return;
    if(coins<cost){InterlockedIncrement(&shared->rakeInsufficient);return;}
    // Stock and coin changes use the same UserService operations as the shop.
    // The current build's native shop replenishes exactly three uses per pack.
    int delta=-cost,quantity=3;void *result=0;
    void *coinArgs[]={&delta},*stockArgs[]={&item,&quantity};
    int paid=invoke_checked(userAddCoins,user,coinArgs,&result);
    int stocked=paid&&invoke_checked(userSetPurchases,user,stockArgs,&result);
    if(!stocked||get_int(profile,profileCoins)!=coins-cost||get_int(purchases,0x80)!=3){
        void *restoreCoins[]={&coins},*restoreStock[]={&item,&oldStock};
        invoke_checked(userSetCoins,user,restoreCoins,&result);
        invoke_checked(userSetPurchases,user,restoreStock,&result);
        InterlockedIncrement(&shared->errors);log_line("rake auto-buy aborted; previous coins/stock restored");return;
    }
    invoke_checked(userSaveDetails,user,0,&result);
    InterlockedIncrement(&shared->rakePurchases);InterlockedExchangeAdd(&shared->rakeSpent,cost);
    char msg[160];snprintf(msg,sizeof(msg),"rake auto-buy: cost=%d internal (%d displayed coins), uses=3",cost,cost*10);log_line(msg);
}
static void place_rake(void *board,void *mi){
    if(!quitting&&shared&&shared->status==1&&(shared->flags&16)&&InterlockedCompareExchange(&buyingRake,1,0)==0){
        buy_rake_if_empty(board);InterlockedExchange(&buyingRake,0);
    }
    originalPlaceRake(board,mi);
}
static void* create_plant(void *app,int seed,void *plant,unsigned char egg,unsigned char december,unsigned char normal,unsigned char retro,void *mi){
    // Every controller creation establishes its own context. Only a live cabbage
    // in Zen Garden mode (43) can request the built-in alternate prefab.
    void *previous=TlsGetValue(gardenContext);int useEgg=0;
    if(!quitting&&shared&&shared->status==1&&(shared->flags&8)&&seed==32&&plant){
        void *boxed=invoke(appGameMode,app,0);
        if(boxed&&*(int*)object_unbox(boxed)==43){useEgg=1;InterlockedIncrement(&shared->gardenRequests);}
    }
    TlsSetValue(gardenContext,(void*)(uintptr_t)useEgg);
    void *result=originalCreatePlant(app,seed,plant,egg,december,normal,retro,mi);
    TlsSetValue(gardenContext,previous);return result;
}
static void* get_prefab(void *definition,unsigned char egg,unsigned char december,unsigned char normal,unsigned char retro,void *mi){
    if(!quitting&&shared&&shared->status==1&&(shared->flags&8)&&TlsGetValue(gardenContext)&&get_int(definition,definitionSeed)==32){
        void *prefab=get_ptr(definition,definitionEaster);
        if(prefab){
            if(InterlockedIncrement(&shared->gardenOverrides)==1)log_line("garden cabbage: selected built-in easter-egg prefab; mode=43 only");
            return prefab;
        }
    }
    return originalGetPrefab(definition,egg,december,normal,retro,mi);
}
static void coin_update(void *coin,void *mi){
    originalCoinUpdate(coin,mi);
    if(quitting||!shared||shared->status!=1)return;
    int type=get_int(coin,cType);
    if(type<1||type>6||get_bool(coin,cDead))return;
    if(get_bool(coin,cCollecting)){InterlockedIncrement(&shared->animationFrames);return;}
    LONG flags=shared->flags;
    if((type<=3&&!(flags&2))||(type>=4&&!(flags&1)))return;
    uint32_t delay=shared->minCoinAge;if(delay>200)delay=200;
    if(get_int(coin,cAge)<(int)delay)return;
    // Same arguments as a normal player click. The game sets the destination,
    // flies the pickup there and calls ScoreCoin only when its animation finishes.
    int player=0,vacuumStyle=0;unsigned char spawnCoins=1;
    void *args[]={&player,&spawnCoins,&vacuumStyle};
    invoke(coinCollect,coin,args);
    if(get_bool(coin,cCollecting)){
        InterlockedIncrement(&shared->starts);
        InterlockedIncrement(type>=4?&shared->sunStarts:&shared->moneyStarts);
    }
    shared->heartbeat=GetTickCount64();
}
static void coin_score(void *coin,void *mi){
    int type=shared&&!quitting?get_int(coin,cType):0;
    originalCoinScore(coin,mi);
    // Telemetry only; scoring remains exclusively the game's original code.
    if(shared&&!quitting&&type>=1&&type<=6){
        InterlockedIncrement(&shared->completes);
        InterlockedIncrement(type>=4?&shared->sunCompletes:&shared->moneyCompletes);
    }
}
static void application_quit(void *app,void *mi){
    InterlockedExchange(&quitting,1);
    if(shared){shared->flags=0;shared->status=2;}
    originalQuit(app,mi);
    // No joining, thread detaching, graphics cleanup or unhooking during exit.
}
static int hook(void *m,void *replacement,void **original){
    void *address=m?*(void**)m:0;
    char msg[160];MH_STATUS status=address?MH_CreateHook(address,replacement,original):MH_ERROR_NOT_EXECUTABLE;
    snprintf(msg,sizeof(msg),"hook address=%p create=%s",address,MH_StatusToString(status));log_line(msg);
    if(status!=MH_OK)return 0;
    status=MH_QueueEnableHook(address);return status==MH_OK;
}
static DWORD WINAPI initialize(void *unused){
    (void)unused;wchar_t name[80];swprintf(name,80,L"Local\\PVZQoL2_%lu",GetCurrentProcessId());
    mapHandle=CreateFileMappingW(INVALID_HANDLE_VALUE,0,PAGE_READWRITE,0,MAP_SIZE,name);
    if(!mapHandle)return 0;shared=(Shared*)MapViewOfFile(mapHandle,FILE_MAP_ALL_ACCESS,0,0,MAP_SIZE);
    if(!shared)return 0;
    if(shared->magic!=MAGIC){memset(shared,0,MAP_SIZE);shared->magic=MAGIC;shared->flags=507;shared->minCoinAge=25;}
    shared->version=5;
    shared->status=0;log_line("initialize: animated pickup and garden appearance; no health overlay");
    HMODULE ga=GetModuleHandleW(L"GameAssembly.dll");if(!ga){shared->status=3;log_line("GameAssembly missing");return 0;}
#define API(n) *(FARPROC*)&n=GetProcAddress(ga,"il2cpp_"#n);if(!n){shared->status=3;log_line("missing API: "#n);return 0;}
    API(domain_get);API(domain_get_assemblies);API(assembly_get_image);API(image_get_class_count);API(image_get_class);
    API(class_get_name);API(class_get_namespace);API(class_get_field_from_name);API(field_get_offset);
    API(class_get_method_from_name);API(runtime_invoke);API(object_unbox);API(object_get_class);API(thread_attach);API(thread_detach);
    void *domain=domain_get();if(!domain){shared->status=3;return 0;}void *thread=thread_attach(domain);
    coinClass=find_class("Reloaded.Gameplay","Coin");
    void *app=find_class(0,"GameplayActivity"),*definition=find_class(0,"PlantDefinition");
    void *board=find_class("Reloaded.Gameplay","Board"),*user=find_class("Reloaded.Services","UserService"),*profile=find_class("Reloaded.Services","UserProfile"),*store=find_class("Reloaded.Gameplay","StoreScreen");
    void *challenge=find_class("Reloaded.Gameplay","Challenge"),*garden=find_class("Reloaded.Gameplay","ZenGarden"),*cursor=find_class("Reloaded.Gameplay","CursorObject");
    void *data=find_class("Reloaded.Services","DataService"),*entry=find_class(0,"StoreEntryData"),*entryModel=find_class("Reloaded.DataModels","StoreEntryModel");
    int failed=0;
#define FIELD(v,k,n) v=field(k,n);if(v<0){failed=1;log_line("missing field: "n);}
    FIELD(cDead,coinClass,"mDead");FIELD(cType,coinClass,"mType");FIELD(cAge,coinClass,"mCoinAge");FIELD(cCollecting,coinClass,"mIsBeingCollected");
    FIELD(definitionSeed,definition,"m_seedType");FIELD(definitionEaster,definition,"m_easterEggGameObject");
    FIELD(boardApp,board,"mApp");FIELD(boardRows,board,"mPlantRow");FIELD(appUser,app,"m_userService");
    FIELD(profilePurchases,profile,"mPurchases");FIELD(profileCoins,profile,"mCoins");
    FIELD(appBoard,app,"m_board");FIELD(boardChallenge,board,"mChallenge");
    FIELD(gardenInStore,garden,"<IsInStore>k__BackingField");FIELD(appGarden,app,"m_zenGarden");FIELD(appData,app,"m_dataService");FIELD(appScene,app,"<GameScene>k__BackingField");
    FIELD(boardPaused,board,"mPaused");FIELD(boardCursor,board,"mCursorObject");FIELD(cursorType,cursor,"mCursorType");FIELD(storeEntryItem,entry,"m_storeItem");
    activeProfile=method(user,"get_ActiveUserProfile",0);userGetCoins=method(user,"GetCoins",0);
    userAddCoins=method(user,"AddCoins",1);userSetCoins=method(user,"SetCoins",1);userSetPurchases=method(user,"SetPurchases",2);userSaveDetails=method(user,"SaveDetails",0);
    boardGetRake=method(board,"GetRake",0);rakeCost=method(store,"GetItemCost",1);
    isVersus=method(app,"IsVersusMode",0);isVases=method(app,"IsScaryPotterLevel",0);isWhack=method(app,"IsWhackAZombieLevel",0);
    isBoss=method(app,"IsFinalBossLevel",0);isIZombie=method(app,"IsIZombieLevel",0);isSquirrel=method(app,"IsSquirrelLevel",0);
    if(!activeProfile||!userGetCoins||!userAddCoins||!userSetCoins||!userSetPurchases||!userSaveDetails||!boardGetRake||!rakeCost||!isVersus||!isVases||!isWhack||!isBoss||!isIZombie||!isSquirrel){failed=1;log_line("rake auto-buy methods missing");}
    appGameMode=method(app,"get_GameMode",0);coinCollect=method(coinClass,"Collect",3);
    treeCanFeed=method(challenge,"TreeOfWisdomCanFeed",0);treeFertilize=method(challenge,"TreeOfWisdomFertilize",0);treeGetSize=method(challenge,"TreeOfWisdomGetSize",0);
    getStoreEntries=method(data,"get_StoreEntryData",0);storeEntryCost=method(entryModel,"GetItemCost",2);appDialogCount=method(app,"GetDialogCount",0);
    if(!treeCanFeed||!treeFertilize||!treeGetSize||!getStoreEntries||!storeEntryCost||!appDialogCount){failed=1;log_line("garden automation methods missing");}
    gardenContext=TlsAlloc();
    if(!appGameMode||!coinCollect||gardenContext==TLS_OUT_OF_INDEXES){failed=1;log_line("pickup/garden context initialization failed");}
    if(!failed&&MH_Initialize()==MH_OK){
        failed=!hook(method(coinClass,"Update",0),(void*)coin_update,(void**)&originalCoinUpdate);
        failed|=!hook(method(coinClass,"ScoreCoin",0),(void*)coin_score,(void**)&originalCoinScore);
        failed|=!hook(method(app,"OnApplicationQuit",0),(void*)application_quit,(void**)&originalQuit);
        failed|=!hook(method(app,"CreatePlantController",6),(void*)create_plant,(void**)&originalCreatePlant);
        failed|=!hook(method(definition,"GetPrefab",4),(void*)get_prefab,(void**)&originalGetPrefab);
        failed|=!hook(method(board,"PlaceRake",0),(void*)place_rake,(void**)&originalPlaceRake);
        // ActiveUpdate is the activity's virtual frame callback. The compiler
        // inlines the legacy TreeOfWisdomUpdate and ZenGardenUpdate bodies.
        failed|=!hook(method(app,"ActiveUpdate",0),(void*)active_update,(void**)&originalActiveUpdate);
        if(!failed&&MH_ApplyQueued()==MH_OK){shared->status=1;log_line("ready: 7 hooks; animated pickup + garden appearance + rake/supplies + native tree feeding");}
        else{MH_Uninitialize();failed=1;}
    }else failed=1;
    if(failed){shared->status=3;log_line("initialization failed; no active feature hooks");}
    thread_detach(thread);return 0;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID reserved){
    (void)reserved;
    if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(module);HANDLE t=CreateThread(0,0,initialize,0,0,0);if(t)CloseHandle(t);}
    else if(reason==DLL_PROCESS_DETACH){InterlockedExchange(&quitting,1);}
    return TRUE;
}
