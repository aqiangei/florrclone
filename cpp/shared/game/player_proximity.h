#pragma once
#include "shared/game/spatial.h"
#include "shared/game/terrain.h"
#include <vector>

namespace flix {
// Same circular activation rule, with a coarse grid instead of scanning every
// player for every mob. Rebuilt from the current positions each simulation pass.
class PlayerProximityIndex {
public:
    explicit PlayerProximityIndex(double radius) : radius_(radius), grid_(radius) {}
    void rebuild(const std::vector<RealmPoint>& players) {
        players_ = players;
        grid_.clear();
        if(players_.size()<16)return;
        for(std::size_t i=0;i<players_.size();++i)
            grid_.insert(static_cast<Entity>(i+1),players_[i].realm,players_[i].position);
    }
    bool near(Vec2 point,Realm realm) const {
        if(players_.empty())return true;
        const double squared=radius_*radius_;
        if(players_.size()<16){
            for(const auto& p:players_)if(p.realm==realm&&distanceSq(point,p.position)<=squared)return true;
            return false;
        }
        grid_.query(realm,point,radius_,hits_);
        for(const auto id:hits_){
            const auto& p=players_[static_cast<std::size_t>(id)-1];
            if(distanceSq(point,p.position)<=squared)return true;
        }
        return false;
    }
private:
    double radius_;
    SpatialGrid grid_;
    std::vector<RealmPoint> players_;
    mutable std::vector<Entity> hits_;
};
}
