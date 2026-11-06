#include "server/shard_presence.h"
#include "test.h"
#include <iostream>
using namespace flix;using namespace flix::shards;
TEST(shard_presence_ownership_handoff_expiry_and_safe_fields) {
 Presence p;Json state=Json::object();
 auto own=[&](const char* id,const char* shard,const char* boot,int epoch){auto& a=state["accounts"][id];a["phase"]="active";a["shard"]=shard;a["boot"]=boot;a["epoch"]=epoch;};
 auto rows=[](const char* id){Json r=Json::array(),v=Json::object();v["account"]=id;v["name"]="Player";v["level"]=201;v["location"]="Garden";v["password"]="must never be public";r.push(v);return r;};
 Node one{"one","boot1",Role::Game},two{"two","boot2",Role::Game};Json empty=Json::array();
 own("a","one","boot1",1);own("b","two","boot2",1);
 CHECK(p.exchange(state,one,rows("a"),0)["rows"].size()==1);
 auto result=p.exchange(state,two,rows("b"),100);CHECK(result["rows"].size()==2);CHECK(result.dump().find("password")==std::string::npos);
 CHECK(p.exchange(state,two,rows("a"),200)["rows"].size()==1); // Cannot forge other shard; removes own omitted row.
 own("a","two","boot2",2);result=p.exchange(state,one,rows("a"),300);CHECK(result["rows"].size()==0);
 result=p.exchange(state,two,rows("a"),400);CHECK(result["rows"].size()==1);CHECK(result["rows"][0]["shard"].asString()=="two");
 state["accounts"]["a"]["phase"]="offline";CHECK(p.exchange(state,one,empty,500)["rows"].size()==0);
 own("a","two","boot2",3);p.exchange(state,two,rows("a"),600);CHECK(p.exchange(state,one,empty,8599)["rows"].size()==1);CHECK(p.exchange(state,one,empty,8600)["rows"].size()==0);
 own("a","two","boot2",4);p.exchange(state,two,rows("a"),9000);own("a","two","restarted",5);CHECK(p.exchange(state,one,empty,9001)["rows"].size()==0);
 CHECK(!p.exchange(state,Node{"evil","boot",Role::Game},empty,9002)["ok"].asBool());

}

TEST(shard_presence_mob_announcements_are_ephemeral_and_hide_location) {
    Presence presence;
    const Json state = Json::object(), rows = Json::array();
    const Node source{"one", "boot1", Role::Game}, destination{"two", "boot2", Role::Game};
    Json notice = Json::object(), batch = Json::array();
    notice["id"] = "0123456789abcdef0123456789abcdef";
    notice["mob"] = 7;
    notice["rarity"] = rarityIndex(Rarity::Unique);
    notice["realm"] = "private test map";
    notice["x"] = 100;
    batch.push(notice);
    notice["rarity"] = -1;
    batch.push(notice);

    CHECK(presence.exchange(state, source, rows, 0, batch)["notices"].size() == 0);
    presence.exchange(state, source, rows, 100, batch);
    const Json notices = presence.exchange(state, destination, rows, 200)["notices"];
    CHECK(notices.size() == 1);
    CHECK(notices[0]["mob"].asInt() == 7);
    CHECK(notices[0]["rarity"].asInt() == rarityIndex(Rarity::Unique));
    CHECK(notices[0]["realm"].isNull());
    CHECK(notices[0]["x"].isNull());
    CHECK(presence.exchange(state, destination, rows, 30000)["notices"].size() == 0);
}
