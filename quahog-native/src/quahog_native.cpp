// Experimental native world portal. Author: Randyadr.
// Built with SDK 1.5; newer player/event functions are optional at runtime.
// No executable patch, detour or hard-coded function addresses.
#include "sms_mod.h"
#include "world_overrides.hpp"
#include "auto_boot.hpp"
#include "api_compat.hpp"
#include "profile.hpp"
#include "portal_draw.hpp"
#include <chrono>
static std::unique_ptr<qworld::Overrides> world_overrides;
#ifdef QUAHOG_TEST
extern qworld::Host quahog_test_host();
#endif
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

// Only base API functions are required. Extensions are size/version checked.
SMS_MOD_EXTERN_C SMS_MOD_EXPORT uint32_t sms_mod_api_version(){return 1u<<16;}
SMS_MOD_EXTERN_C SMS_MOD_EXPORT uint32_t sms_mod_api_prerelease(){return 0;}
static const SmsApi* api=nullptr;
static SmsMod* self=nullptr;
static int target_area=-1,target_episode=-1,current_area=-1,current_episode=-1;
static int return_episode=0,arrival=0,key_enter=0,key_return=0,key_rescue=0;
static float cooldown=2.0f,warning_time=0;
static bool ready=false,started=false;
static std::filesystem::path diagnostic_path;
static void diagnostic(SmsMod* mod,const SmsApi* host,int level,const std::string& message){
    if(!qapi::header(host) || host->major!=1 || !SMS_API_HAS(host,core))return;
    if(SMS_API_HAS(host->core,log))host->core->log(mod,level,"%s",message.c_str());
    // Logging must never hide the original load failure.
    try{
        if(diagnostic_path.empty() && SMS_API_HAS(host->core,data_dir)){
            const char* data=host->core->data_dir(mod);
            if(data && *data)diagnostic_path=std::filesystem::u8path(data)/"startup.log";
        }
        if(!diagnostic_path.empty()){
            std::filesystem::create_directories(diagnostic_path.parent_path());
            std::ofstream out(diagnostic_path,std::ios::app);out<<message<<'\n';
        }
    }catch(const std::exception&){}
}
static int refuse(SmsMod* mod,const SmsApi* host,int code,const std::string& reason){
    std::string message=std::string(mod_title)+" 0.4.4 - host API "+qapi::version(host)+"\n"+reason;
    diagnostic(mod,host,SMS_LOG_ERROR,message);
    if(!diagnostic_path.empty())message+="\n\nDetails: "+diagnostic_path.u8string();
#if defined(_WIN32) && !defined(QUAHOG_TEST)
    qboot::show_error(message);
#endif
    return code;
}

static const SmsVec3 spawn={-157.27223f,8062.334f,-317.7065f};
static const SmsVec3 zero={0,0,0};
static qportal::Portal entrance,exit_gate;
static bool placement_notice=false,return_valid=false;
static int pending_action=0;
static float saved_return_yaw=180;
static SmsVec3 return_position={};
static std::filesystem::path placement_path;
static bool in_world(){return current_area==target_area && current_episode==target_episode;}
static bool finite(const SmsVec3& p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::fabs(p.x)<100000&&std::fabs(p.y)<100000&&std::fabs(p.z)<100000;}
static bool is_near(const SmsVec3& a,const qportal::Portal& p){
    if(!p.valid)return false;
    const float yaw=p.yaw*qportal::pi/180,x=a.x-p.base.x,z=a.z-p.base.z;
    float across=std::cos(yaw)*x-std::sin(yaw)*z,depth=std::sin(yaw)*x+std::cos(yaw)*z;
    return std::fabs(across)<118&&std::fabs(depth)<110&&a.y>p.base.y-90&&a.y<p.base.y+320;
}
static void locate(){
    SmsGameState s={};s.size=sizeof(s);api->game->state(&s);
    current_area=s.area;current_episode=s.episode;
    ready=s.stage_ready && (current_area==1 || (in_world()&&api->game->find_object("QuahogNativeReady")));
}
static void move_to(const SmsVec3& p,float yaw=180){
    api->player->set_position(&p);api->player->set_velocity(&zero);api->player->set_forward_speed(0);
    if(SMS_API_HAS(api->player,set_yaw))api->player->set_yaw(yaw);
    cooldown=3.0f;
}
static SmsVec3 outside(const qportal::Portal& p){return qportal::point(p,0,65,390);}
static void equip_fludd(){
    if(SMS_API_HAS(api->player,set_fludd))api->player->set_fludd(1);
    if(SMS_API_HAS(api->player,set_nozzle))api->player->set_nozzle(4,0);
}
static bool read_placement(const std::filesystem::path& path){
    std::ifstream f(path);qportal::Portal p;std::string extra;
    if(!(f>>p.base.x>>p.base.y>>p.base.z>>p.yaw)||(f>>extra)||!finite(p.base)||!std::isfinite(p.yaw))return false;
    p.valid=true;entrance=p;return true;
}
static void save_placement(){
    if(placement_path.empty())return;
    try{std::filesystem::create_directories(placement_path.parent_path());std::ofstream f(placement_path);f.precision(9);
        f<<entrance.base.x<<' '<<entrance.base.y<<' '<<entrance.base.z<<' '<<entrance.yaw<<'\n';
        if(!f)throw std::runtime_error("Portal position could not be saved.");
    }catch(const std::exception& e){diagnostic(self,api,SMS_LOG_WARN,e.what());warning_time=8;}
}
static void place_here(){
    locate();if(!ready||current_area!=1){warning_time=8;return;}
    SmsPlayer p={};p.size=sizeof(p);if(!api->player->get(&p)||!finite(p.position))return;
    // Stand against the wall, facing it. The portal faces out toward the player.
    entrance={p.position,p.yaw+180,true};save_placement();cooldown=8;placement_notice=true;
}
static void enter(){
    locate();SmsGameState s={};s.size=sizeof(s);api->game->state(&s);
    if(!s.stage_ready||s.paused||cooldown>0)return;
    if(!ready){warning_time=7;return;}
    if(in_world()){move_to(spawn);return;}
    if(current_area!=1)return;
    SmsPlayer p={};p.size=sizeof(p);if(!api->player->get(&p)||!finite(p.position))return;
    return_position=entrance.valid?outside(entrance):p.position;
    saved_return_yaw=entrance.valid?entrance.yaw:p.yaw;return_valid=true;
    return_episode=current_episode;arrival=1;cooldown=5;started=false;
    if(!api->game->warp(target_area,target_episode)){arrival=0;cooldown=1;started=true;}
}
static void leave(){
    locate();SmsGameState s={};s.size=sizeof(s);api->game->state(&s);
    if(!s.stage_ready||s.paused||cooldown>0||!ready)return;
    if(!in_world())return;
    arrival=2;cooldown=5;started=false;
    if(!api->game->warp(1,return_episode)){arrival=0;cooldown=1;started=true;}
}
static void action(SmsMod*,const char* key,void*){
    if(std::strcmp(key,"enter")==0)pending_action=1;
    else if(std::strcmp(key,"return")==0)pending_action=2;
    else if(std::strcmp(key,"place")==0)place_here();
    else if(std::strcmp(key,"rescue")==0)pending_action=3;
}
static void place_arrival(){
    if(in_world()&&ready){move_to(spawn);equip_fludd();}
    else if(ready&&return_valid&&arrival==2&&current_area==1)move_to(return_position,saved_return_yaw);
}
static int event(SmsMod*,SmsEvent* ev,void*){
    if(ev->type==SMS_EV_STAGE_LOADING&&ev->size>=sizeof(SmsEvStageLoading)){
        const auto& e=*static_cast<SmsEvStageLoading*>(ev->data);
        current_area=e.area;current_episode=e.episode;ready=false;started=false;cooldown=3;
    }else if(ev->type==SMS_EV_PLAYER_SPAWN&&ev->size>=sizeof(SmsEvPlayerSpawn)){
        auto& e=*static_cast<SmsEvPlayerSpawn*>(ev->data);
        if(in_world()||(return_valid&&arrival==2&&current_area==1)){
            e.position=in_world()?spawn:return_position;e.yaw=in_world()?180:saved_return_yaw;e.has_position=1;e.kind=SMS_START_WAIT;
        }
    }else if(ev->type==SMS_EV_STAGE_LOADED){
        locate();cooldown=3;place_arrival();
        if(api->minor<4){started=true;arrival=0;}
    }else if(ev->type==SMS_EV_STAGE_START){
        locate();started=true;cooldown=3;place_arrival();arrival=0;
    }else if(ev->type==SMS_EV_STAGE_LEAVING){ready=false;started=false;}
    else if(ev->type==SMS_EV_TICK&&ev->size>=sizeof(SmsEvTick)){
        float dt=static_cast<SmsEvTick*>(ev->data)->dt;
        if(!std::isfinite(dt)||dt<0)return SMS_CONTINUE;
        dt=std::fmin(dt,.25f);
        SmsGameState s={};s.size=sizeof(s);api->game->state(&s);
        if(s.paused||!s.stage_ready)return SMS_CONTINUE;
        cooldown=std::fmax(0.0f,cooldown-dt);warning_time=std::fmax(0.0f,warning_time-dt);
        if(!ready||!started||cooldown>0)return SMS_CONTINUE;
        if(pending_action){int command=pending_action;pending_action=0;if(command==1)enter();else if(command==2)leave();else if(in_world())move_to(spawn);return SMS_CONTINUE;}
        SmsPlayer p={};p.size=sizeof(p);if(!api->player->get(&p))return SMS_CONTINUE;
        if(in_world()){
            if(p.position.y<6500){move_to(spawn);return SMS_CONTINUE;}
            if(is_near(p.position,exit_gate))leave();
        }else if(current_area==1){
            if(is_near(p.position,entrance))enter();
        }
    }else if(ev->type==SMS_EV_KEY&&ev->size>=sizeof(SmsEvKey)){
        auto& e=*static_cast<SmsEvKey*>(ev->data);if(!e.down||e.repeat)return SMS_CONTINUE;
        if(e.scancode==key_enter){enter();return SMS_CANCEL;}
        if(e.scancode==key_return){leave();return SMS_CANCEL;}
        if(e.scancode==key_rescue){if(in_world()&&ready)move_to(spawn);return SMS_CANCEL;}
    }else if(ev->type==SMS_EV_DRAW){
        if(!ready&&warning_time<=0)return SMS_CONTINUE;
        if(ready){
            const auto now=std::chrono::steady_clock::now().time_since_epoch();
            const float time=float(std::fmod(std::chrono::duration<double>(now).count(),4096.0));
            SmsPlayer p={};p.size=sizeof(p);
            auto draw_gate=[&](const qportal::Portal& gate,const char* label){
                if(!gate.valid||!api->player->get(&p))return;
                auto d=SmsVec3{p.position.x-gate.base.x,p.position.y-gate.base.y,p.position.z-gate.base.z};
                if(d.x*d.x+d.y*d.y+d.z*d.z<6000.f*6000.f)qportal::draw(api,gate,time,label);
            };
            if(in_world())draw_gate(exit_gate,"DELFINO PLAZA");
            else if(current_area==1)draw_gate(entrance,"QUAHOG");
        }
        if(SMS_API_HAS(api,draw)&&SMS_API_HAS(api->draw,text)&&SMS_API_HAS(api->draw,rect)&&SMS_API_HAS(api->draw,hud_slot)){
            SmsRect r={};if(!api->draw->hud_slot(self,SMS_ANCHOR_BOTTOM_LEFT,410,27,&r))return SMS_CONTINUE;
            api->draw->rect(r.x,r.y,r.w,r.h,0x142132C0u);
            const char* text=pending_action?"Travel selected. Close F1 to continue.":warning_time>0?"Load Delfino Plaza to use or place this portal.":
                !entrance.valid?"Quahog: F6 enters / F1: Place portal here":
                placement_notice&&cooldown>0?"Portal saved. Step away before it activates.":
                in_world()?"Quahog - F8: return / F10: rescue":"Quahog - walk into portal / F6: enter";
            api->draw->text(r.x+8,r.y+7,13,0xFFFFFFFFu,SMS_TEXT_SHADOW,text);
        }
    }
    return SMS_CONTINUE;
}
SMS_MOD_EXTERN_C SMS_MOD_EXPORT int sms_mod_load(SmsMod* mod,const SmsApi* host){
    diagnostic_path.clear();
    const std::string absent=qapi::missing(host);
    if(!absent.empty())return refuse(mod,host,1,absent);
    if(!qportal::supported(host))return refuse(mod,host,1,"Native portal drawing requires draw.triangle, draw.line and camera.world_to_canvas.");
    api=host;self=mod;
    std::string build="unknown";
    if(SMS_API_HAS(api->core,game_build)){
        const char* name=api->core->game_build();if(name)build=name;
    }
    diagnostic(mod,host,SMS_LOG_INFO,std::string("Starting ")+mod_title+" 0.4.4; host API "+qapi::version(host)+"; game build "+build);
    if(!SMS_API_HAS(api->player,set_yaw) || api->minor<5)
        diagnostic(mod,host,SMS_LOG_INFO,"Using compatible stage-load placement. Newer yaw/FLUDD functions are optional.");
    try {
#ifdef QUAHOG_TEST
        auto native_host=quahog_test_host();
#else
        auto native_host=qworld::resolve_host();
#endif
        const char* mod_dir=api->core->mod_dir(mod);
        if(!mod_dir || !*mod_dir)throw std::runtime_error("The game returned an empty mod folder.");
        auto folder=std::filesystem::u8path(mod_dir);
#if defined(_WIN32) && !defined(QUAHOG_TEST)
        if(SMS_API_HAS(api,net)&&SMS_API_HAS(api->net,role)&&api->net->role()!=SMS_NET_OFF)throw std::runtime_error("This map preview currently supports single-player Play. Disable it before hosting or joining multiplayer.");
        SmsGameState state={};state.size=sizeof(state);api->game->state(&state);
        diagnostic(mod,host,SMS_LOG_INFO,state.eclipse?"Running Eclipse; preparing maps from the active launcher ISO.":"Running Sunshine; preparing maps from the active launcher ISO.");
        api->core->log(mod,SMS_LOG_INFO,"Preparing/loading cached maps from the launcher's active ISO...");
        const char* data_dir=api->core->data_dir(mod);
        if(!data_dir || !*data_dir)throw std::runtime_error("The game returned an empty mod data folder.");
        auto data=std::filesystem::u8path(data_dir);
        qboot::configure_memory(*native_host.memory_size,state.stage_ready!=0,data);
        auto prepared_folder=qboot::prepare_maps(folder,data);
#else
        auto prepared_folder=folder;
#endif
        auto prepared=std::make_unique<qworld::Overrides>(native_host);
        try{prepared->install(prepared_folder);}
        catch(const std::exception& error){
#if defined(_WIN32) && !defined(QUAHOG_TEST)
            if(std::string(error.what()).find("ISO or stage data changed")==std::string::npos)throw;
            prepared.reset();prepared_folder=qboot::prepare_maps(folder,data,true);
            prepared=std::make_unique<qworld::Overrides>(native_host);prepared->install(prepared_folder);
#else
            (void)error;throw;
#endif
        }
        target_area=prepared->area;target_episode=prepared->episode;
        if(target_area==1)throw std::runtime_error("Wrong prepared map profile.");
        entrance={};placement_notice=false;pending_action=0;
        exit_gate=qportal::Portal{{-157.27223f,7995.334f,-892.7065f},0,true};
        const char* stored=api->core->data_dir(mod);
        placement_path=std::filesystem::u8path(stored)/"quahog-portal.txt";
        if(!read_placement(placement_path))read_placement(prepared_folder/"portal-layout.txt");
        return_valid=entrance.valid;
        return_position=entrance.valid?outside(entrance):SmsVec3{0,300,0};saved_return_yaw=entrance.yaw;

        world_overrides=std::move(prepared);
    } catch(const std::exception& e) {
        return refuse(mod,host,2,e.what());
    }
    cooldown=3;warning_time=0;arrival=0;started=false;return_episode=0;locate();
    started=ready; // Supports re-enabling from F1 while a stage is already active.
    for(unsigned int type:{SMS_EV_STAGE_LOADING,SMS_EV_PLAYER_SPAWN,SMS_EV_STAGE_LOADED,SMS_EV_STAGE_START,SMS_EV_STAGE_LEAVING,SMS_EV_TICK,SMS_EV_KEY,SMS_EV_DRAW}){
        if((type==SMS_EV_STAGE_LOADING || type==SMS_EV_PLAYER_SPAWN) && api->minor<5)continue;
        if(type==SMS_EV_STAGE_START && api->minor<4)continue;
        if(api->core->subscribe(mod,type,event,nullptr,0)<=0){
            world_overrides.reset();return refuse(mod,host,4,"Could not register native event "+std::to_string(type)+".");
        }
    }

    key_enter=api->input->bind_key(mod,"enter","F6");
    key_return=api->input->bind_key(mod,"return","F8");
    key_rescue=api->input->bind_key(mod,"rescue","F10");
    for(const char* k:{"enter","return","place","rescue"}){
        SmsOption option={};option.size=sizeof(option);option.key=k;
        option.label=std::strcmp(k,"enter")==0?"Enter Quahog":
            std::strcmp(k,"return")==0?"Return to Plaza":std::strcmp(k,"place")==0?"Place portal here (stand at wall, face wall)":"Rescue to Quahog entrance";
        option.type=SMS_OPTION_ACTION;option.on_change=action;api->config->add_option(mod,&option);
    }
    diagnostic(mod,host,SMS_LOG_INFO,std::string(mod_title)+" 0.4.4 loaded. Visible portals enabled. Target "+std::to_string(target_area)+"/"+std::to_string(target_episode));
    return 0;
}
SMS_MOD_EXTERN_C SMS_MOD_EXPORT void sms_mod_unload(SmsMod*){world_overrides.reset();api=nullptr;self=nullptr;ready=false;started=false;diagnostic_path.clear();entrance={};placement_path.clear();}
