#include "test.h"
#include "shared/game/config.h"
#include "client/camera.h"
#include "client/ui/menus.h"
#include <fstream>
#include <limits>
using namespace flix;
TEST(observer_black_screen_all_rarities) {
 ContentRegistry r; std::string error;
 CHECK(r.loadFiles("data/mobs.json", "data/petals.json", error));
 auto observer=r.petalIndex("observer"), antenna=r.petalIndex("antennae");
 for(int i=0;i<kLadderRarityCount;++i) {
  auto tier=static_cast<Rarity>(i);
  double z=petalCameraZoom(r,observer,tier);
  std::printf("observer tier %d zoom %.9f\n",i,z);
  CHECK(std::isfinite(z)); CHECK(z>0); CHECK(z<=petalCameraZoom(r,antenna,tier));
  Camera c; c.setViewport(1920,1080); c.snapTo({1000,2000}); c.loadoutZoom=z;
  auto visible=c.visibleWorld(); CHECK(visible.w>0); CHECK(visible.h>0);
 }
}
TEST(observer_black_screen_invalid_camera_guard) {
 Camera c;
 for(double z:{0.0,-0.5,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
  c.loadoutZoom=z; CHECK(std::isfinite(c.zoom())); CHECK(c.zoom()>0);
 }
}
TEST(observer_black_screen_loadout_swap_and_saved_zoom) {
 ContentRegistry r; std::string error;
 CHECK(r.loadFiles("data/mobs.json", "data/petals.json", error));
 Profile p; p.loadout.resize(kLoadoutActiveSlots*2);
 CHECK_NEAR(loadoutCameraZoom(p,r),1.0,1e-9);
 for(int i=0;i<kLadderRarityCount;++i) {
  p.loadout[0]={r.petalIndex("observer"),static_cast<Rarity>(i)};
  CHECK(loadoutCameraZoom(p,r)>0);
  std::swap(p.loadout[0],p.loadout[kLoadoutActiveSlots]);
  CHECK_NEAR(loadoutCameraZoom(p,r),1.0,1e-9);
  std::swap(p.loadout[0],p.loadout[kLoadoutActiveSlots]);
  CHECK(loadoutCameraZoom(p,r)>0);
 }
 const char* file="/tmp/flix-observer-zoom-settings.txt";
 {std::ofstream out(file);out<<"zoom 0.25\n";}
 ClientSettings settings; CHECK(settings.load(file));
 CHECK_NEAR(settings.zoom,1.0,1e-9);
 CHECK_NEAR(kMinZoom,1.0,1e-9);
 std::remove(file);
}
