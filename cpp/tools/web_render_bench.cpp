// Real browser Canvas/WorldRenderer submission benchmark; not device FPS.
#include "canvas.h"
#include "client/camera.h"
#include "client/render/world_renderer.h"
#include "client/world_view.h"
#include "shared/game/config.h"
#include "shared/game/components.h"
#include <chrono>
#include <cstdio>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
using namespace flix;
int main(){
    std::string error;if(!loadContent("data",error)){std::printf("ERROR %s\n",error.c_str());return 1;}
    constexpr int width=1280,height=900,count=80,frames=45;
    Canvas canvas=Canvas::createVirtual(width,height);Camera camera;camera.setViewport(width,height);camera.userZoom=1;const Vec2 centre{5000,5000};camera.snapTo(centre);
    WorldRenderer renderer;renderer.setContent(&content());renderer.setTerrainCache(false);renderer.options.names=false;renderer.options.healthBars=false;
    WorldView view;view.setRealm(Realm::Overworld);
    RemoteEntity self;self.netId=1;self.kind=net::EntityKind::Player;self.position=self.targetPosition=centre;self.needsSnap=false;view.seedForTest(self);
    for(int i=0;i<count;++i){RemoteEntity web;web.netId=100+i;web.kind=net::EntityKind::Effect;web.typeIndex=static_cast<std::uint16_t>(GroundEffectKind::Web);web.radius=45+(i%3)*8;web.position=web.targetPosition=centre+Vec2{-450.0+(i%10)*100.0,-315.0+(i/10)*90.0};web.angle=i*.173;web.needsSnap=false;view.seedForTest(web);}
    double elapsed[2]={},ops[2]={};
    for(int mode=0;mode<2;++mode){renderer.setWebStampCache(mode!=0);
        for(int frame=-5;frame<frames;++frame){
            const int before=canvasOpsEmitted();const auto start=std::chrono::steady_clock::now();
            renderer.draw(canvas,view,camera,centre,100+frame*.016);
#ifdef __EMSCRIPTEN__
            canvasFlushOps();canvas.present("bench");
#endif
            const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            if(frame>=0){elapsed[mode]+=ms;ops[mode]+=canvasOpsEmitted()-before;}
        }
    }
    char result[512];std::snprintf(result,sizeof(result),"{\"webs\":%d,\"frames\":%d,\"rawMs\":%.5f,\"cachedMs\":%.5f,\"rawOps\":%.2f,\"cachedOps\":%.2f,\"pass\":%s}",count,frames,elapsed[0]/frames,elapsed[1]/frames,ops[0]/frames,ops[1]/frames,ops[1]<ops[0]*.5?"true":"false");
    std::printf("WEB_BENCH %s\n",result);
#ifdef __EMSCRIPTEN__
    EM_ASM({window.__webBenchResult=JSON.parse(UTF8ToString($0));},result);
    return ops[1]<ops[0]*.5?0:2;
#else
    return 0;
#endif
}
