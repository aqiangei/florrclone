#include "test.h"
#include "server_harness.h"
using namespace flix;
using namespace flix::testsupport;
TEST(private_skin_requires_owner_until_explicit_publication) {
    Harness h("skin-privacy", seedSkinReviewer); CHECK(h.ready); if(!h.ready)return;
    NetClient owner, other, reviewer;
    CHECK(loginAs(h, reviewer, "reviewer", "password7"));
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
    CHECK(owner.findSkin(id)->review == SkinReviewState::Draft);
    owner.equipSkin(id);
    CHECK(h.stepUntil({&owner,&other},[&]{return owner.equippedSkinId().empty();}));
    owner.publishSavedSkin(id);
    CHECK(h.stepUntil({&owner,&other},[&]{return owner.findSkin(id)->review == SkinReviewState::Pending;}));
    CHECK(approveSkin(h, reviewer, owner, id));
    CHECK(!owner.findSkin(id)->published && !other.findSkin(id));
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


TEST(skin_moderator_can_remove_public_skins_without_admin_access) {
    Harness h("skin-moderation", [](const std::string& path) {
        seedUser(path, "artist", "password7");
        seedUser(path, "other", "password7");
        seedSkinReviewer(path);
    });
    CHECK(h.ready);
    if (!h.ready) return;
    NetClient artist, moderator, other;
    CHECK(loginAs(h, artist, "artist", "password7"));
    CHECK(loginAs(h, moderator, "reviewer", "password7"));
    CHECK(loginAs(h, other, "other", "password7"));
    CHECK(h.stepUntil({&artist, &moderator, &other},
        [&] { return moderator.canModeratePublicSkins(); }));
    CHECK(!moderator.isSkinAdmin());
    CHECK(!other.canModeratePublicSkins());
    moderator.sendChat("/server-status");
    other.sendChat("/server-status");
    const auto saw = [](const NetClient& client, const std::string& text) {
        return std::any_of(client.chat().begin(), client.chat().end(),
            [&](const ChatLine& line) { return line.text.find(text) != std::string::npos; });
    };
    CHECK(h.stepUntil({&artist, &moderator, &other},
        [&] { return saw(moderator, "TPS") && saw(other, "Command does not exist."); }));
    CHECK(!saw(other, "TPS"));

    SkinShape shape;
    shape.t = SkinShapeType::Rect;
    shape.rx = 19;
    shape.ry = 15;
    shape.fill = "#7c16de";
    artist.publishSkin("private", {shape});
    CHECK(h.stepUntil({&artist, &moderator, &other},
        [&] { return artist.skinCatalog().size() == 1; }));
    if (artist.skinCatalog().empty()) return;
    const auto privateId = artist.skinCatalog().front().id;
    const auto beforePrivate = moderator.chat().size();
    moderator.deleteSkin(privateId);
    CHECK(h.stepUntil({&artist, &moderator, &other},
        [&] { return moderator.chat().size() > beforePrivate; }));
    CHECK(artist.findSkin(privateId));

    artist.publishSavedSkin(privateId);
    CHECK(h.stepUntil({&artist, &moderator, &other}, [&] {
        return artist.findSkin(privateId)->review == SkinReviewState::Pending;
    }));
    other.requestSkinReview(net::SkinReviewAction::List);
    other.requestSkinReview(net::SkinReviewAction::Approve, privateId);
    artist.publishSavedSkin(privateId);
    moderator.requestSkinReview(net::SkinReviewAction::List);
    CHECK(h.stepUntil({&artist, &moderator, &other}, [&] {
        return !moderator.skinReviewQueue().empty() && moderator.skinReviewQueue().front().id == privateId;
    }));
    CHECK(other.skinReviewQueue().empty());
    CHECK(!other.findSkin(privateId) && !moderator.findSkin(privateId));
    CHECK(artist.findSkin(privateId)->review == SkinReviewState::Pending);
    CHECK(approveSkin(h, moderator, artist, privateId));
    CHECK(!artist.findSkin(privateId)->published);
    artist.publishSavedSkin(privateId);
    CHECK(h.stepUntil({&artist, &moderator, &other}, [&] {
        const auto* skin = moderator.findSkin(privateId);
        return skin && skin->published;
    }));
    const auto beforeOther = other.chat().size();
    other.deleteSkin(privateId);
    CHECK(h.stepUntil({&artist, &moderator, &other},
        [&] { return other.chat().size() > beforeOther; }));
    CHECK(artist.findSkin(privateId));

    artist.equipSkin(privateId);
    artist.requestLeaderboard();
    CHECK(h.stepUntil({&artist, &moderator, &other}, [&] {
        return !artist.leaderboardPending() && artist.equippedSkinId() == privateId;
    }));
    moderator.deleteSkin(privateId);
    CHECK(h.stepUntil({&artist, &moderator, &other}, [&] {
        return artist.equippedSkinId().empty() && !artist.findSkin(privateId) &&
            !moderator.findSkin(privateId) && !other.findSkin(privateId);
    }));
    const auto* account = h.server.database().findUser("artist");
    CHECK(account && h.server.database().findProgress(account->id)->equippedSkinId.empty());

    artist.publishSkin("reject me", {shape}, true);
    CHECK(h.stepUntil({&artist, &moderator, &other}, [&] {
        return !artist.skinCatalog().empty() && artist.skinCatalog().front().review == SkinReviewState::Pending;
    }));
    if (artist.skinCatalog().empty()) return;
    const auto rejectedId = artist.skinCatalog().front().id;
    moderator.requestSkinReview(net::SkinReviewAction::Reject, rejectedId);
    CHECK(h.stepUntil({&artist, &moderator, &other}, [&] {
        return artist.findSkin(rejectedId)->review == SkinReviewState::Rejected;
    }));
    artist.publishSavedSkin(rejectedId);
    artist.equipSkin(rejectedId);
    CHECK(h.stepUntil({&artist, &moderator, &other}, [&] {
        return artist.equippedSkinId().empty();
    }));
    CHECK(!artist.findSkin(rejectedId)->published && !other.findSkin(rejectedId));
}
