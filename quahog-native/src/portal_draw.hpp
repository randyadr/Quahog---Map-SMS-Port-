#pragma once
#include "sms_mod.h"
#include <cmath>
#include <algorithm>
namespace qportal {
constexpr float pi=3.14159265358979323846f;
struct Portal {SmsVec3 base={};float yaw=0;bool valid=false;};
inline SmsVec3 point(const Portal& p,float x,float y,float z=0){
    float a=p.yaw*pi/180,c=std::cos(a),s=std::sin(a);
    return {p.base.x+c*x+s*z,p.base.y+y,p.base.z-s*x+c*z};
}
inline bool supported(const SmsApi* a){return SMS_API_HAS(a,draw)&&SMS_API_HAS(a->draw,triangle)&&SMS_API_HAS(a->draw,line)&&SMS_API_HAS(a, camera)&&SMS_API_HAS(a->camera,world_to_canvas);}
// The native SDK exposes projected triangles, not depth-buffered custom meshes.
// These visible, two-sided portals use the same world positions as travel.
inline void draw(const SmsApi* api,const Portal& p,float time,const char* label){
    if(!p.valid||!supported(api))return;
    auto color=[&](unsigned alpha,bool bright){return (bright?0xADF9FF00u:0x22BEDB00u)|alpha;};
    auto project=[&](const SmsVec3& v,float& x,float& y){float d=0;return api->camera->world_to_canvas(&v,&x,&y,&d)&&std::isfinite(x)&&std::isfinite(y)&&d>8;};
    auto tri=[&](SmsVec3 a,SmsVec3 b,SmsVec3 c,SmsColor shade){float ax,ay,bx,by,cx,cy;if(project(a,ax,ay)&&project(b,bx,by)&&project(c,cx,cy))api->draw->triangle(ax,ay,bx,by,cx,cy,shade);};
    auto line=[&](SmsVec3 a,SmsVec3 b,float width,SmsColor shade){float ax,ay,bx,by;if(project(a,ax,ay)&&project(b,bx,by))api->draw->line(ax,ay,bx,by,width,shade);};
    const int segments=64;const float pulse=1+0.025f*std::sin(time*2.5f);
    auto oval=[&](float a,float r,float z=0){return point(p,std::cos(a)*138*r*pulse,205+std::sin(a)*198*r*pulse,z);};
    SmsVec3 center=point(p,0,205);
    for(int i=0;i<segments;++i){
        float a=2*pi*i/segments,b=2*pi*(i+1)/segments;
        float shimmer=.5f+.5f*std::sin(a*5-time*2);
        tri(center,oval(a,.89f),oval(b,.89f),color(70+unsigned(30*shimmer),false));
        tri(oval(a,.89f),oval(a,1),oval(b,1),color(220,true));
        tri(oval(a,.89f),oval(b,1),oval(b,.89f),color(220,true));
        tri(oval(a,1),oval(a,1.14f),oval(b,1.14f),color(35,false));
        tri(oval(a,1),oval(b,1.14f),oval(b,1),color(35,false));
        line(oval(a,.96f,3),oval(b,.96f,3),2.1f,color(255,true));
    }
    for(int arm=0;arm<3;++arm)for(int i=0;i<24;++i){
        float t0=i/24.f,t1=(i+1)/24.f;
        float a=time*1.4f+arm*2*pi/3+t0*4.4f,b=time*1.4f+arm*2*pi/3+t1*4.4f;
        line(oval(a,.12f+.68f*t0,4),oval(b,.12f+.68f*t1,4),1.1f,color(170,true));
    }
    float x,y;if(label&&SMS_API_HAS(api->draw,text)&&project(point(p,0,450),x,y))api->draw->text(x,y,14,color(255,true),SMS_TEXT_CENTER|SMS_TEXT_SHADOW,label);
}
}
