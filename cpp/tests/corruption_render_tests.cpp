#include "test.h"
#include "client/render/world_renderer.h"
#include "client/world_view.h"
#include "render_rig.h"
#include "shared/game/skin_format.h"
using namespace flix;
TEST(corruption_overrides_skins_and_restores_them_when_removed) {
    SkinShape shape;shape.t=SkinShapeType::Circle;shape.r=25;shape.fill="#e011cc";
    CustomSkin skin;skin.id="corruption-test";skin.shapes.push_back(shape);
    const std::vector<CustomSkin> catalog{skin};
    for(const auto flags:{PlayerRenderNone,PlayerRenderPumpkin,PlayerRenderRobot}){
        for(const bool custom:{false,true}){
            WorldRenderer renderer;renderer.setSkinCatalog(&catalog);
            RemoteEntity flower;flower.netId=11;flower.kind=net::EntityKind::Player;
            flower.radius=playerRadiusForLevel(1);flower.level=1;flower.needsSnap=false;
            flower.renderFlags=flags;if(custom)flower.equippedSkinId=skin.id;
            auto render=[&](){Canvas canvas=Canvas::createVirtual(160,160);WorldView view;
                view.setRealm(Realm::Overworld);view.seedForTest(flower);
                renderer.draw(canvas,view,testsupport::frameCamera(160,{}),{},0);
                return canvas.getImageData(0,0,160,160);};
            const auto before=render();flower.faceFlags=FaceHasCorruption|FacePoisoned;
            const auto corrupted=render();const auto centre=(80*160+80)*4;
            CHECK(corrupted.size()>centre+3);
            if(corrupted.size()>centre+3){CHECK_EQ(corrupted[centre],0xD9);CHECK_EQ(corrupted[centre+1],0x13);CHECK_EQ(corrupted[centre+2],0x13);}
            CHECK(testsupport::differingPixels(before,corrupted)>100);
            flower.faceFlags=0;const auto after=render();CHECK_EQ(testsupport::differingPixels(before,after),0);
        }
    }
}

TEST(pharaoh_crown_is_visible_over_every_player_skin) {
    SkinShape shape;shape.t=SkinShapeType::Circle;shape.r=25;shape.fill="#e011cc";
    CustomSkin skin;skin.id="corruption-test";skin.shapes.push_back(shape);
    const std::vector<CustomSkin> catalog{skin};
    for(const auto flags:{PlayerRenderNone,PlayerRenderPumpkin,PlayerRenderRobot}){
        for(const bool custom:{false,true}){
            WorldRenderer renderer;renderer.setSkinCatalog(&catalog);
            RemoteEntity flower;flower.netId=11;flower.kind=net::EntityKind::Player;
            flower.radius=playerRadiusForLevel(1);flower.level=1;flower.needsSnap=false;
            flower.renderFlags=flags;if(custom)flower.equippedSkinId=skin.id;
            auto render=[&](){Canvas canvas=Canvas::createVirtual(160,160);WorldView view;
                view.setRealm(Realm::Overworld);view.seedForTest(flower);
                renderer.draw(canvas,view,testsupport::frameCamera(160,{}),{},0);
                return canvas.getImageData(0,0,160,160);};
            const auto before=render();flower.equipFlags=EquipPharaohCrown;
            const auto corrupted=render();const auto centre=(80*160+80)*4;


            CHECK(testsupport::differingPixels(before,corrupted)>100);
            flower.equipFlags=0;const auto after=render();CHECK_EQ(testsupport::differingPixels(before,after),0);
        }
    }
}
