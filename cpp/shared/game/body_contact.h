#pragma once
#include "shared/game/config.h"
#include "shared/core/world.h"

namespace flix {
inline double squareMobHalfExtent(const World& world, Entity entity) {
    const auto* type=world.tryGet<MobType>(entity);
    const auto* body=world.tryGet<Body>(entity);
    if(!type || !body || type->configIndex>=content().mobCount()) return 0;
    const auto& config=content().mob(type->configIndex);
    // square's SVG spans 8..92 plus its 3-unit outline in a 100-unit box.
    return config.id=="square" ? body->radius*config.visualScale*.90 : 0;
}
inline Vec2 contactRotate(Vec2 p,double angle) {
    const double c=std::cos(angle),s=std::sin(angle);
    return {p.x*c-p.y*s,p.x*s+p.y*c};
}
struct BodyContactResult { Vec2 normal; double depth=0; };
inline BodyContactResult circleSquareContact(Vec2 square,double half,double angle,
                                            Vec2 circle,double radius) {
    const Vec2 p=contactRotate(circle-square,-angle);
    const Vec2 nearest{clamp(p.x,-half,half),clamp(p.y,-half,half)};
    const Vec2 delta=p-nearest; const double distance=delta.length();
    if(distance>1e-9) return {contactRotate(delta*(1.0/distance),angle),radius-distance};
    const double x=half-std::abs(p.x),y=half-std::abs(p.y);
    const Vec2 direction=x<y ? Vec2{p.x<0?-1.0:1.0,0} : Vec2{0,p.y<0?-1.0:1.0};
    return {contactRotate(direction,angle),radius+std::min(x,y)};
}
inline BodyContactResult shapedBodyContact(Vec2 a,double radiusA,double halfA,double angleA,
                                           Vec2 b,double radiusB,double halfB,double angleB,double gap=0) {
    if(halfA>0 && halfB<=0) return circleSquareContact(a,halfA,angleA,b,radiusB+gap);
    if(halfB>0 && halfA<=0) {
        auto result=circleSquareContact(b,halfB,angleB,a,radiusA+gap);
        result.normal=result.normal*-1.0; return result;
    }
    const Vec2 delta=b-a;
    if(halfA>0 && halfB>0) {
        const Vec2 axesA[]={contactRotate({1,0},angleA),contactRotate({0,1},angleA)};
        const Vec2 axesB[]={contactRotate({1,0},angleB),contactRotate({0,1},angleB)};
        const auto dot=[](Vec2 x,Vec2 y){return x.x*y.x+x.y*y.y;};
        BodyContactResult result{{1,0},1e30};
        for(auto axis:{axesA[0],axesA[1],axesB[0],axesB[1]}) {
            const double extentA=halfA*(std::abs(dot(axis,axesA[0]))+std::abs(dot(axis,axesA[1])));
            const double extentB=halfB*(std::abs(dot(axis,axesB[0]))+std::abs(dot(axis,axesB[1])));
            const double distance=dot(delta,axis),depth=extentA+extentB+gap-std::abs(distance);
            if(depth<result.depth) result={axis*(distance<0?-1.0:1.0),depth};
        }
        return result;
    }
    const double distance=delta.length();
    return {distance>1e-9?delta*(1.0/distance):Vec2{1,0},radiusA+radiusB+gap-distance};
}
inline bool segmentTouchesSquare(Vec2 from,Vec2 to,Vec2 centre,double half,double angle,double radius) {
    from=contactRotate(from-centre,-angle); to=contactRotate(to-centre,-angle);
    const Vec2 d=to-from;
    double enter=0,leave=1;
    const auto slab=[&](double p,double v) {
        if(std::abs(v)<1e-12) return std::abs(p)<=half;
        double a=(-half-p)/v,b=(half-p)/v; if(a>b) std::swap(a,b);
        enter=std::max(enter,a); leave=std::min(leave,b); return enter<=leave;
    };
    if(slab(from.x,d.x) && slab(from.y,d.y)) return true;
    const auto pointBox=[&](Vec2 p) {
        const Vec2 v{std::max(0.0,std::abs(p.x)-half),std::max(0.0,std::abs(p.y)-half)};
        return v.lengthSq();
    };
    double distance=std::min(pointBox(from),pointBox(to));
    for(double x:{-half,half}) for(double y:{-half,half}) {
        const Vec2 corner{x,y},offset=corner-from;
        const double t=d.lengthSq()>1e-12?clamp((offset.x*d.x+offset.y*d.y)/d.lengthSq(),0.0,1.0):0;
        distance=std::min(distance,(corner-from-d*t).lengthSq());
    }
    return distance<=radius*radius;
}
}
