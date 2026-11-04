#include "test.h"
#include "server_harness.h"
using namespace flix;
using namespace flix::testsupport;
TEST(private_skin_requires_owner_until_explicit_publication) {
    Harness h("skin-privacy"); CHECK(h.ready); if(!h.ready)return;
    NetClient owner, other;
    CHECK(connectClient(h,owner)); owner.requestRegister("author","hunter2!");
    CHECK(h.stepUntil({&owner},[&]{return owner.status()==NetClient::Status::LoggedIn;}));
    CHECK(connectClient(h,other)); other.requestRegister("viewer","hunter2!");
    CHECK(h.stepUntil({&owner,&other},[&]{return other.status()==NetClient::Status::LoggedIn;}));
    SkinShape shape;shape.t=SkinShapeType::Circle;shape.r=20;shape.fill="#e011cc";
    owner.publishSkin("private",{shape});
    CHECK(h.stepUntil({&owner,&other},[&]{return owner.skinCatalog().size()==1;}));
    const auto id=owner.skinCatalog().front().id;
    CHECK(!owner.skinCatalog().front().published);CHECK(other.skinCatalog().empty());
    const auto messages=other.chat().size(); other.publishSavedSkin(id);
    CHECK(h.stepUntil({&owner,&other},[&]{return other.chat().size()>messages;}));
    CHECK(!owner.findSkin(id)->published);
    owner.equipSkin(id);
    CHECK(h.stepUntil({&owner,&other},[&]{return other.findSkin(id)!=nullptr;}));
    CHECK(!other.findSkin(id)->published); // appearance cache, not a public choice
    const auto before=other.chat().size(); other.equipSkin(id);
    CHECK(h.stepUntil({&owner,&other},[&]{return other.chat().size()>before && other.equippedSkinId().empty();}));
    owner.publishSavedSkin(id);
    CHECK(h.stepUntil({&owner,&other},[&]{return other.findSkin(id) && other.findSkin(id)->published;}));
    NetClient offline; CHECK(connectClient(h,offline)); offline.requestRegister("offlinewearer","hunter2!");
    CHECK(h.stepUntil({&owner,&other,&offline},[&]{return offline.status()==NetClient::Status::LoggedIn;}));
    offline.equipSkin(id);offline.requestLeaderboard();
    CHECK(h.stepUntil({&owner,&other,&offline},[&]{return !offline.leaderboardPending();}));
    offline.disconnect();h.step(3,{&owner,&other});
    other.joinGame(1280,720);
    CHECK(h.stepUntil({&owner,&other},[&]{return other.status()==NetClient::Status::Playing;}));
    other.equipSkin(id);
    other.requestLeaderboard(); // ordered roundtrip confirms the equip was processed
    CHECK(h.stepUntil({&owner,&other},[&]{return !other.leaderboardPending();}));
    CHECK_EQ(other.equippedSkinId(),id);
    owner.publishSavedSkin(id,false);
    CHECK(h.stepUntil({&owner,&other},[&]{return other.equippedSkinId().empty() && !owner.findSkin(id)->published;}));
    CHECK_EQ(owner.equippedSkinId(),id);
    const auto* account=h.server.database().findUser("viewer"); CHECK(account);
    CHECK(h.server.database().findProgress(account->id)->equippedSkinId.empty());
    const auto* offlineAccount=h.server.database().findUser("offlinewearer"); CHECK(offlineAccount);
    CHECK(h.server.database().findProgress(offlineAccount->id)->equippedSkinId.empty());
    CHECK(h.stepUntil({&owner,&other},[&]{
        const auto found=other.view().entities().find(other.view().self().netId);
        return found!=other.view().entities().end() && found->second.equippedSkinId.empty();
    }));
}
TEST(private_skin_wire_and_storage_preserve_visibility_and_legacy_public_skins) {
    CustomSkin skin;skin.id="one";skin.name="private";skin.author="author";
    SkinShape shape;shape.t=SkinShapeType::Circle;shape.r=20;shape.fill="#e011cc";skin.shapes={shape};
    auto json=skinToJson(skin);CHECK(!skinFromJson(json).published);
    json.erase("published");CHECK(skinFromJson(json).published);
    ByteWriter w;writeCustomSkin(w,skin);ByteReader r(w.buffer());CustomSkin copy;
    CHECK(readCustomSkin(r,copy));CHECK(!copy.published);
}
