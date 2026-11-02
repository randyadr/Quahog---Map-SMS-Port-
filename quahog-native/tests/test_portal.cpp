// Public-API mock: exercise the actual plugin's event handlers, not a copy.
#include "sms_mod.h"
#include "../src/profile.hpp"
#include "../src/world_overrides.hpp"
#include <cassert>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <map>
#include <cstdio>
struct SmsMod{};
extern "C" int sms_mod_load(SmsMod*,const SmsApi*);
extern "C" void sms_mod_unload(SmsMod*);
static std::map<unsigned,SmsEventFn> callbacks;
static std::string messages;
static unsigned fail_event=0;
static SmsGameState state={};static SmsPlayer player={};static bool markers=true;
static int requested_area=-1,requested_episode=-1,warps=0;static SmsMod mod;
static void emit(unsigned type,void* data=nullptr,unsigned size=0){SmsEvent e={type,size,data,0,nullptr};auto it=callbacks.find(type);if(it!=callbacks.end())it->second(&mod,&e,nullptr);}
static int subscribe(SmsMod*,unsigned type,SmsEventFn fn,void*,int){if(type==fail_event)return 0;callbacks[type]=fn;return int(type);}
static void logger(SmsMod*,int,const char* format,...){char line[4096];va_list args;va_start(args,format);std::vsnprintf(line,sizeof(line),format,args);va_end(args);messages+=line;messages+='\n';}
static void game_state(SmsGameState* out){*out=state;}
static std::string folder;
static const char* mod_folder(SmsMod*){return folder.c_str();}
static uint32_t mem_size=64u*1024*1024;
static std::map<std::string,qworld::Bytes> disc;
static void* read_stage(const char* path,size_t* n){auto i=disc.find(path);if(i==disc.end())return nullptr;*n=i->second.size();void* p=std::malloc(*n);std::memcpy(p,i->second.data(),*n);return p;}
static int replace_stage(const char* path,const void* data,uint32_t n){disc[path]=qworld::Bytes(static_cast<const unsigned char*>(data),static_cast<const unsigned char*>(data)+n);return 1;}
qworld::Host quahog_test_host(){return {read_stage,replace_stage,std::free,&mem_size};}

static SmsObject find(const char* name){return markers && std::strcmp(name,"QuahogNativeReady")==0?&mod:nullptr;}
static int warp(int a,int e){++warps;requested_area=a;requested_episode=e;return 1;}
static int get_player(SmsPlayer* out){*out=player;return player.valid;}
static int position(const SmsVec3* p){player.position=*p;return 1;}
static int velocity(const SmsVec3* v){player.velocity=*v;return 1;}
static int speed(float v){player.forward_speed=v;return 1;}
static int yaw(float v){player.yaw=v;return 1;}
static int fludd(int v){player.has_fludd=v;return 1;}
static int nozzle(int v,int){player.nozzle=v;return 1;}
static int bind(SmsMod*,const char*,const char* key){return std::atoi(key+1);}
static std::map<std::string,SmsOption> options;
static int option(SmsMod*,const SmsOption* o){options[o->key]=*o;return 1;}
static int triangles=0,lines=0;
static void triangle(float,float,float,float,float,float,SmsColor){++triangles;}
static void line(float,float,float,float,float,SmsColor){++lines;}
static int project(const SmsVec3* p,float* x,float* y,float* depth){*x=p->x*.1f;*y=p->y*.1f;*depth=1000;return 1;}
static void action(const char* name){auto o=options.at(name);o.on_change(&mod,name,nullptr);}

static void tick(float seconds){for(int i=0;i<int(seconds*60+1);++i){SmsEvTick t={};t.dt=1.0f/60;emit(SMS_EV_TICK,&t,sizeof(t));}}
static void key(int scancode){SmsEvKey e={};e.down=1;e.scancode=scancode;emit(SMS_EV_KEY,&e,sizeof(e));}
static SmsEvPlayerSpawn load(int area,int episode){
    state.area=area;state.episode=episode;state.stage_ready=1;
    SmsEvStageLoading loading={};loading.area=area;loading.episode=episode;emit(SMS_EV_STAGE_LOADING,&loading,sizeof(loading));
    SmsEvPlayerSpawn spawn={};emit(SMS_EV_PLAYER_SPAWN,&spawn,sizeof(spawn));
    if(spawn.has_position)player.position=spawn.position;
    emit(SMS_EV_STAGE_LOADED);emit(SMS_EV_STAGE_START);return spawn;
}
int main(int argc,char** argv){
    assert(argc==2);folder=argv[1];std::filesystem::create_directories(folder);
    qworld::Bytes before(64,'b'),after(64,'a');std::memcpy(after.data(),"RARC",4);
    std::ofstream manifest(std::filesystem::path(folder)/"overrides.txt");manifest<<"QUAHOG_NATIVE_OVERRIDES_2\n"<<"20 0\n";
    for(const char* name:{"world"}){
        std::string path="data/scene/"+std::string(name)+".szs",file=std::string(name)+".arc";disc["/"+path]=before;
        std::ofstream output(std::filesystem::path(folder)/file,std::ios::binary);output.write(reinterpret_cast<const char*>(after.data()),after.size());
        manifest<<path<<" "<<file<<" "<<std::hex<<qworld::hash(before.data(),before.size())<<" "<<qworld::hash(after.data(),after.size())<<" "<<std::dec<<before.size()<<"\n";
    }
    manifest.close();
    std::ofstream(std::filesystem::path(folder)/"portal-layout.txt")<<"1000 10 2000 180\n";

    SmsCoreApi core={};core.size=sizeof(core);core.log=logger;core.subscribe=subscribe;core.mod_dir=mod_folder;core.data_dir=mod_folder;
    SmsGameApi game={};game.size=sizeof(game);game.state=game_state;game.find_object=find;game.warp=warp;
    SmsPlayerApi p={};p.size=sizeof(p);p.get=get_player;p.set_position=position;p.set_velocity=velocity;p.set_forward_speed=speed;p.set_yaw=yaw;p.set_fludd=fludd;p.set_nozzle=nozzle;
    SmsInputApi input={};input.size=sizeof(input);input.bind_key=bind;
    SmsConfigApi config={};config.size=sizeof(config);config.add_option=option;
    SmsApi api={};api.size=sizeof(api);api.major=1;api.minor=4;api.core=&core;api.game=&game;api.player=&p;api.input=&input;api.config=&config;
    SmsDrawApi draw={};draw.size=sizeof(draw);draw.triangle=triangle;draw.line=line;api.draw=&draw;
    SmsCameraApi camera={};camera.size=sizeof(camera);camera.world_to_canvas=project;api.camera=&camera;
    state.size=sizeof(state);state.area=1;state.episode=3;state.stage_ready=1;player.valid=1;

    assert(sms_mod_load(nullptr,nullptr)==1);
    uint32_t tiny=sizeof(uint32_t);assert(sms_mod_load(&mod,reinterpret_cast<const SmsApi*>(&tiny))==1);
    api.major=2;assert(sms_mod_load(&mod,&api)==1);api.major=1;
    game.find_object=nullptr;assert(sms_mod_load(&mod,&api)==1);game.find_object=find;
    camera.world_to_canvas=nullptr;assert(sms_mod_load(&mod,&api)==1);camera.world_to_canvas=project;
    assert(sms_mod_load(&mod,&api)==2); // insufficient map memory
    mem_size=1024u*1024*1024;api.minor=5;
    assert(sms_mod_load(&mod,&api)==0);
    assert(disc["/data/scene/world.szs"]==after);
    assert(disc.count("/data/scene/plaza.szs")==0);
    load(1,3);tick(4);
    // Place while F1 is paused. Position and direction are persisted.
    state.paused=1;player.position={1100,20,2100};player.yaw=90;action("place");
    tick(10);assert(warps==0);state.paused=0;tick(7);
    assert(warps==0);player.position={1400,20,2100};tick(2);
    emit(SMS_EV_DRAW);assert(triangles>0&&lines>0);

    markers=false;state.paused=1;action("enter");tick(1);assert(warps==0);state.paused=0;tick(.1f);assert(warps==1&&requested_area==20&&requested_episode==0);markers=true;
    key(6);assert(warps==1);
    auto sp=load(20,0);assert(sp.has_position&&sp.position.y>8000);
    tick(4);player.position.y=6000;tick(.1f);assert(player.position.y>8000);
    int drawn=triangles;player.position={-157,8000,-500};emit(SMS_EV_DRAW);assert(triangles>drawn);
    tick(4);key(8);assert(warps==2&&requested_area==1&&requested_episode==3);
    sp=load(1,3);assert(sp.has_position&&sp.position.x<1100&&sp.position.z==2100);
    tick(4);player.position={1100,20,2100};tick(.1f);assert(warps==3);
    load(20,0);tick(4);player.position={-157.27223f,7995.334f,-892.7065f};tick(.1f);assert(warps==4);
    sms_mod_unload(&mod);callbacks.clear();
    assert(disc["/data/scene/world.szs"]==before);
    p.set_yaw=nullptr;p.set_fludd=nullptr;p.set_nozzle=nullptr;
    assert(sms_mod_load(&mod,&api)==0);load(1,3);tick(4);
    // Reload uses the saved custom portal, not the generated default.
    player.position={1100,20,2100};int old_warps=warps;tick(.1f);
    assert(warps==old_warps+1);
    sms_mod_unload(&mod);callbacks.clear();
    fail_event=SMS_EV_TICK;assert(sms_mod_load(&mod,&api)==4);
    assert(disc["/data/scene/world.szs"]==before);
    sms_mod_unload(&mod);callbacks.clear();
    std::printf("%s: travel, render callbacks, saved placement, pause/debounce, profile isolation, return, optional APIs and rollback passed.\n",mod_title);
}
