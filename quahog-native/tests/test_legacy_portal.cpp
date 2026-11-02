// This host uses the ACTUAL older SDK structs, in a separate translation unit
// from the plugin built with 1.5. This catches access beyond the old ABI.
#if LEGACY_MINOR == 4
#include "compat_sdk/sms_mod_1_4.h"
#else
#include "compat_sdk/sms_mod_1_3.h"
#endif
#include "../src/world_overrides.hpp"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
struct SmsMod{};
extern "C" int sms_mod_load(SmsMod*,const SmsApi*);
extern "C" void sms_mod_unload(SmsMod*);
static SmsMod mod;
static SmsGameState state={};
static SmsPlayer player={};
static std::map<unsigned,SmsEventFn> callbacks;
static std::string folder,messages;
static int destination=-1,episode=-1;
static uint32_t memory_size=1024u*1024*1024;
static std::map<std::string,qworld::Bytes> disc;
static void* read_stage(const char* path,size_t* n){auto i=disc.find(path);if(i==disc.end())return nullptr;*n=i->second.size();void* p=std::malloc(*n);std::memcpy(p,i->second.data(),*n);return p;}
static int replace_stage(const char* path,const void* data,uint32_t n){disc[path]=qworld::Bytes(static_cast<const unsigned char*>(data),static_cast<const unsigned char*>(data)+n);return 1;}
qworld::Host quahog_test_host(){return {read_stage,replace_stage,std::free,&memory_size};}
static void logger(SmsMod*,int,const char* format,...){char b[4096];va_list args;va_start(args,format);std::vsnprintf(b,sizeof(b),format,args);va_end(args);messages+=b;}
static int subscribe(SmsMod*,unsigned type,SmsEventFn f,void*,int){assert(type<=unsigned(LEGACY_MINOR==4?30:29));callbacks[type]=f;return int(type);}
static const char* mod_folder(SmsMod*){return folder.c_str();}
static void get_state(SmsGameState* out){*out=state;}
static SmsObject find(const char* name){return std::strcmp(name,"QuahogNativeReady")==0?&mod:nullptr;}
static int warp(int area,int ep){destination=area;episode=ep;return 1;}
static int get_player(SmsPlayer* out){*out=player;return 1;}
static int position(const SmsVec3* p){player.position=*p;return 1;}
static int velocity(const SmsVec3* p){player.velocity=*p;return 1;}
static int speed(float v){player.forward_speed=v;return 1;}
static int bind(SmsMod*,const char*,const char* key){return std::atoi(key+1);}
static int option(SmsMod*,const SmsOption*){return 1;}
static void triangle(float,float,float,float,float,float,SmsColor){}
static void line(float,float,float,float,float,SmsColor){}
static int project(const SmsVec3*,float* x,float* y,float* d){*x=100;*y=100;*d=1000;return 1;}
static void emit(unsigned type,void* data=nullptr,unsigned size=0){auto i=callbacks.find(type);if(i!=callbacks.end()){SmsEvent e={type,size,data,0,nullptr};i->second(&mod,&e,nullptr);}}
static void tick(){SmsEvTick t={};t.dt=0.25f;for(int i=0;i<16;++i)emit(SMS_EV_TICK,&t,sizeof(t));}
static void key(int k){SmsEvKey e={};e.down=1;e.scancode=k;emit(SMS_EV_KEY,&e,sizeof(e));}
static void load(int area,int ep){
    state.area=area;state.episode=ep;state.stage_ready=1;
    player.position={0,0,0};emit(SMS_EV_STAGE_LOADED);
    // Safe even before the opening ends; no pre-spawn hook exists here.
    if(area==20)assert(player.position.y>8000);
#if LEGACY_MINOR == 4
    emit(SMS_EV_STAGE_START);
#endif
}
int main(int argc,char** argv){
    assert(argc==2);folder=argv[1];std::filesystem::create_directories(folder);
    qworld::Bytes before(64,'b'),after(64,'a');std::memcpy(after.data(),"RARC",4);
    std::ofstream manifest(std::filesystem::path(folder)/"overrides.txt");manifest<<"QUAHOG_NATIVE_OVERRIDES_2\n20 0\n";
    for(const char* n:{"world"}){
        std::string path="data/scene/"+std::string(n)+".szs",file=std::string(n)+".arc";disc["/"+path]=before;
        std::ofstream out(std::filesystem::path(folder)/file,std::ios::binary);out.write(reinterpret_cast<const char*>(after.data()),after.size());
        manifest<<path<<" "<<file<<" "<<std::hex<<qworld::hash(before.data(),before.size())<<" "<<qworld::hash(after.data(),after.size())<<" "<<std::dec<<before.size()<<"\n";
    }
    manifest.close();
    std::ofstream(std::filesystem::path(folder)/"portal-layout.txt")<<"1000 20 2000 180\n";
    SmsCoreApi core={};core.size=sizeof(core);core.log=logger;core.subscribe=subscribe;core.mod_dir=mod_folder;core.data_dir=mod_folder;
    SmsGameApi game={};game.size=sizeof(game);game.state=get_state;game.find_object=find;game.warp=warp;
    SmsPlayerApi p={};p.size=sizeof(p);p.get=get_player;p.set_position=position;p.set_velocity=velocity;p.set_forward_speed=speed;
    SmsInputApi input={};input.size=sizeof(input);input.bind_key=bind;
    SmsConfigApi config={};config.size=sizeof(config);config.add_option=option;
    SmsApi api={};api.size=sizeof(api);api.major=1;api.minor=LEGACY_MINOR;api.core=&core;api.game=&game;api.player=&p;api.input=&input;api.config=&config;
    SmsDrawApi draw={};draw.size=sizeof(draw);draw.triangle=triangle;draw.line=line;api.draw=&draw;
    SmsCameraApi camera={};camera.size=sizeof(camera);camera.world_to_canvas=project;api.camera=&camera;
    state.size=sizeof(state);state.area=1;state.episode=3;state.stage_ready=1;
    player.size=sizeof(player);player.valid=1;
    for(int eclipse:{0,1}){
        state.eclipse=eclipse;
        assert(sms_mod_load(&mod,&api)==0);
        assert(disc["/data/scene/world.szs"]==after);
        load(1,3);tick();key(6);assert(destination==20 && episode==0);
        load(20,0);tick();player.position.y=6000;tick();assert(player.position.y>8000);
        tick();key(8);assert(destination==1 && episode==3);
        load(1,3);assert(player.position.z==1610 && player.position.y==85);
        tick();player.position={1000,20,2000};tick();assert(destination==20);
        load(20,0);tick();player.position={-157.27223f,7995.334f,-892.7065f};tick();assert(destination==1);
        sms_mod_unload(&mod);callbacks.clear();
        assert(disc["/data/scene/world.szs"]==before);
        state.area=1;state.episode=3;
    }
    assert(messages.find("Using compatible stage-load placement")!=std::string::npos);
    std::printf("Actual SDK 1.%d host tests passed: Sunshine/Eclipse state, no new events or player functions, portal, return placement, rescue, unload.\n",LEGACY_MINOR);
}
