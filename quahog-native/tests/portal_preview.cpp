// Preview of the actual native portal draw calls, with a small orthographic camera.
#include "../src/portal_draw.hpp"
#include <cstdio>
static float panel_x=0,panel_y=0,angle=0;
static void style(SmsColor c){std::printf("fill=\"#%06x\" fill-opacity=\"%.3f\"",c>>8,(c&255)/255.f);}
static int project(const SmsVec3* p,float* x,float* y,float* depth){*x=panel_x+(p->x*std::cos(angle)+p->z*std::sin(angle))*.70f;*y=panel_y-p->y*.70f;*depth=1000;return 1;}
static void triangle(float ax,float ay,float bx,float by,float cx,float cy,SmsColor c){std::printf("<polygon points=\"%.2f,%.2f %.2f,%.2f %.2f,%.2f\" ",ax,ay,bx,by,cx,cy);style(c);std::puts("/>");}
static void line(float ax,float ay,float bx,float by,float width,SmsColor c){std::printf("<path d=\"M%.2f %.2f L%.2f %.2f\" stroke=\"#%06x\" stroke-opacity=\"%.3f\" stroke-width=\"%.2f\" fill=\"none\"/>\n",ax,ay,bx,by,c>>8,(c&255)/255.f,width);}
int main(){
    SmsDrawApi draw={};draw.size=sizeof(draw);draw.triangle=triangle;draw.line=line;
    SmsCameraApi camera={};camera.size=sizeof(camera);camera.world_to_canvas=project;
    SmsApi api={};api.size=sizeof(api);api.draw=&draw;api.camera=&camera;
    std::puts("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1200\" height=\"500\" viewBox=\"0 0 1200 500\"><rect width=\"1200\" height=\"500\" fill=\"#121d28\"/><g font-family=\"sans-serif\" fill=\"#e3edf5\"><text x=\"40\" y=\"42\" font-size=\"25\">Visible portal renderer · v0.4.4</text><text x=\"40\" y=\"70\" font-size=\"15\" fill=\"#91a8bc\">Actual DLL drawing code with an offline camera — not an in-game screenshot</text></g>");
    for(int i=0;i<2;++i){panel_x=400+400*i;panel_y=415;angle=i%2?.65f:0;
        std::printf("<text x=\"%.1f\" y=\"115\" text-anchor=\"middle\" font-family=\"sans-serif\" font-size=\"16\" fill=\"#dbe7ef\">%s · %s</text>\n",panel_x,"Quahog",i%2?"angled":"front");
        qportal::draw(&api,{{0,0,0},0,true},1.25f,nullptr);
    }
    std::puts("<text x=\"40\" y=\"472\" font-family=\"sans-serif\" font-size=\"15\" fill=\"#91a8bc\">Animated rings and swirls · two-sided drawing · portal visuals and entry triggers share the same position</text></svg>");
}
