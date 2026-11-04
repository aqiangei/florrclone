// The top-right corner: the world map, the maze map, and the arena's
// scoreboard where neither of those applies.
//
// One box, three fillings, because the three realms answer "where am I"
// differently. The overworld and the maze each have a map to fit into the
// box (client/minimap.h does the fitting); the arena has no geography worth
// drawing, so the reference puts the live scoreboard in the same corner
// instead.
//
// Both maps bake their static layer. The overworld's is the map's whole
// collision geometry -- thousands of paths, rescanned every frame if it were
// not cached -- and the maze's is the whole maze; what is left per frame is
// the dots. The bake keys are what to watch when either one changes: the
// realm and grid size for the world map, the day for the maze, and the pixel
// density for both, because a bitmap baked for a 1x display and shown on a
// Retina one is the single blocky rectangle on an otherwise crisp screen.

#include "client/app.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <utility>

#include "client/minimap.h"
#include "client/ui/draw.h"
#include "client/ui/menu_widgets.h"
#include "client/ui/menu_style.h"
#include "shared/game/constants.h"
#include "shared/game/difficulty.h"
#include "shared/game/terrain.h"

namespace flix {

using namespace flix::ui;

namespace {

constexpr double kMinimapSize = 200.0;
constexpr double kMinimapPadding = 10.0;
/// Online list under the map: a bit wider than the map for name/guild/map,
/// and tall so a busy server scrolls instead of crowding the HUD.
constexpr double kRosterWidth = 240.0;
constexpr double kRosterHeight = 320.0;
constexpr double kRosterGap = 26.0;
constexpr double kRosterHeader = 22.0;
constexpr double kRosterRow = 16.0;
constexpr double kRosterWheelStep = 28.0;
/// The spawn bands' tints, one per rarity, drawn at 0.4 alpha while ALT is
/// held. MINIMAP_SPAWN_COLORS (src/graphics/minimap.ts:11-22) is its own table
/// and not the item palette: it ends violet and cyan where kRarityColors ends
/// white and magenta, and a band read against the wrong one names the wrong
/// tier.
constexpr std::array<std::uint32_t, kLadderRarityCount> kMinimapSpawnColors = {
    0x7EEF6Du,  // common
    0xFFE65Du,  // uncommon
    0x4D52E3u,  // rare
    0x861FDEu,  // epic
    0xDE1F1Fu,  // legendary
    0x1FDBDEu,  // mythic
    0xDE1F65u,  // ultra
    0x2BFFA4u,  // super
    0xBF00FFu,  // unique
    0x00FFFFu,  // apex
};

/// Depth-band tints over the maze corridors, common through mythic, in the
/// game's rarity palette at the alpha the browser build uses (maze-render.ts).
constexpr std::array<std::uint32_t, kMazeZoneCount> kMazeZoneTints = {
    0x7EEF6Du, 0xFFE65Du, 0x4D52E3u, 0x861FDEu, 0x1FDBDEu, 0xDE1F65u,
};
constexpr double kMazeZoneTintAlpha = 0.30;

const char* mazeBiomeLabel(MazeBiome biome) {
    switch (biome) {
        case MazeBiome::Garden: return "Garden";
        case MazeBiome::Desert: return "Desert";
        case MazeBiome::Ocean: return "Ocean";
    }
    return "Maze";
}

/// The scoreboard's number: "1.2k", "3M", or the plain count (pvp-arena.ts
/// formatScore).
std::string arenaScoreLabel(double score) {
    char buffer[32];
    if (score >= 1e6) {
        std::snprintf(buffer, sizeof buffer, score >= 1e7 ? "%.0fM" : "%.1fM", score / 1e6);
        return buffer;
    }
    if (score >= 1e3) {
        std::snprintf(buffer, sizeof buffer, score >= 1e4 ? "%.0fk" : "%.1fk", score / 1e3);
        return buffer;
    }
    return std::to_string(static_cast<long long>(std::floor(score)));
}

} // namespace

const Canvas* App::minimapStatic(bool rarityGlow) {
    // ALT is NOT part of the key. The reference keys its bake cache on it
    // (minimap.ts:216) and so rebakes on every press and every release, and
    // this bake is the whole map's collision geometry -- tens of thousands of
    // points -- so doing the same made each ALT flip a visible hitch. Both
    // variants are baked together below and ALT only chooses between them.
    // uiScale is the first key. See minimapDensity_: the bake is a bitmap and
    // has to be rasterised at the density it will be shown at. The realm and
    // the grid's dimensions are the rest of it -- together they say WHICH MAP
    // this is, which is what the bake draws now that it draws all of one: a
    // teleport to another realm and the wire grid landing after the join both
    // change the picture and neither changes anything else in the key.
    const Realm realm = net_.view().realm();
    const Terrain& terrain = net_.terrain();
    const double density = window_.uiScale();
    const int cols = terrain.tileCols(realm);
    const int rows = terrain.tileRows(realm);
    if (minimapStatic_ && minimapStaticGlow_ && minimapRealm_ == realm && minimapCols_ == cols &&
        minimapRows_ == rows && minimapDensity_ == density) {
        return rarityGlow ? minimapStaticGlow_.get() : minimapStatic_.get();
    }

    const Vec2 extent = terrain.realmExtent(realm);
    const MinimapFit fit = minimapFit(extent, kMinimapSize);
    const auto toBox = [&](Vec2 world) { return fit.toBox(world); };

    const int bakeSide =
        std::max(1, static_cast<int>(std::lround(kMinimapSize * density)));
    const auto newBake = [&]() {
        auto bake = std::make_unique<Canvas>(Canvas::createVirtual(bakeSide, bakeSide));
        // Everything below is written in design units, exactly as it was when
        // the bake was always kMinimapSize pixels square. This one line is
        // what buys it the display's real resolution.
        bake->scale(static_cast<float>(density), static_cast<float>(density));
        return bake;
    };

    // The annotations of the map the flower is actually standing on. The arena
    // and the maze have none, which is correct: neither is an authored map, so
    // there are no bands and no pads to draw.
    const MapData* annotations = worldMaps_.forRealm(realm);
    double farmHighest=-1;
    if (annotations && !worldMaps_.isPrestige(realm) &&
        annotations->usesReferenceSpawnCurve() && supportsSuperFarm(annotations->id())) {
        for (const auto& element:annotations->elements())
            if (element.isSpawnBand() && !element.singular && element.forceRarity<0)
                farmHighest=std::max(farmHighest,element.difficulty);
    }
    const auto superFarmBand=[&](const MapElement& element){
        return farmHighest>=0 && element.isSpawnBand() && !element.singular &&
            element.forceRarity<0 && element.difficulty>=farmHighest-kSuperFarmDifficultyWindow;
    };

    // What lies UNDER the walls: the paper, the letterbox, and -- in the ALT
    // variant -- the spawn bands. Painted once per variant; the walls are
    // then laid over each from the one layer below.
    const auto paintGround = [&](Canvas& map, bool bands) {
        // The map's own rectangle, in the barely-translucent paper the corner has
        // always been drawn on, and the letterbox bars either side of it in solid
        // black. Beyond the map's edge is void and reads as void: paper out there
        // would show an open room the world draws black and the terrain treats as
        // solid. A square map has no bars; a map that is not square has exactly
        // one pair of them.
        const double mapLeft = fit.offsetX;
        const double mapTop = fit.offsetY;
        const double mapWidth = extent.x * fit.scale;
        const double mapHeight = extent.y * fit.scale;
        setFill(map, kPaper, kOverlayPlateAlpha);
        map.fillRect(static_cast<float>(mapLeft), static_cast<float>(mapTop),
                     static_cast<float>(mapWidth), static_cast<float>(mapHeight));
        setFill(map, 0x000000u);
        if (mapLeft > 0.0) {
            const double right = mapLeft + mapWidth;
            map.fillRect(0, 0, static_cast<float>(mapLeft), static_cast<float>(kMinimapSize));
            map.fillRect(static_cast<float>(right), 0,
                         static_cast<float>(std::max(0.0, kMinimapSize - right)),
                         static_cast<float>(kMinimapSize));
        }
        if (mapTop > 0.0) {
            const double bottom = mapTop + mapHeight;
            map.fillRect(0, 0, static_cast<float>(kMinimapSize), static_cast<float>(mapTop));
            map.fillRect(0, static_cast<float>(bottom), static_cast<float>(kMinimapSize),
                         static_cast<float>(std::max(0.0, kMinimapSize - bottom)));
        }

        // Spawn bands, under the walls, only while ALT is held. Their own palette,
        // not kRarityColors: MINIMAP_SPAWN_COLORS (minimap.ts:11-22) gives unique a
        // violet and apex a cyan where the item tiers are white and magenta.
        if (bands && annotations != nullptr) {
            for (const MapElement& element : annotations->elements()) {
                // Difficulty BANDS only. A spawn object with no difficulty is a
                // mob region -- a whole area, or a whole map, saying what lives
                // there -- and painting it as a difficulty-zero band washes the
                // entire minimap in the common colour with the real common bands
                // lost in it. The world renderer's rarity glow makes the same
                // distinction.
                if (!element.isSpawnBand()) continue;
                // A SINGULAR band is not ground either. It is drawn over the whole
                // range its one mob may turn up in, so painting it would tint a
                // district in that mob's tier and hide every real band under it --
                // and it would promise a district's worth of ultras where there is
                // exactly one. See MapElement::singular.
                if (element.singular) continue;
                // The whole map is in the box, so a band is only culled when it
                // lies off the MAP -- which an authored one never does.
                const Vec2 topLeft = toBox({element.bounds.x, element.bounds.y});
                const double w = element.bounds.w * fit.scale;
                const double h = element.bounds.h * fit.scale;
                if (topLeft.x + w <= 0 || topLeft.x >= kMinimapSize || topLeft.y + h <= 0 ||
                    topLeft.y >= kMinimapSize) {
                    continue;
                }
                // The band's DIFFICULTY decides the colour, through the one curve
                // the spawner rolls against: a band reads as the tier a player
                // will actually meet in it, and a band between two tiers takes the
                // one it mostly produces. See shared/game/difficulty.h.
                const Rarity tier = superFarmBand(element) ? Rarity::Super :
                    (annotations->usesReferenceSpawnCurve() ? static_cast<Rarity>(referenceSpawnTier(element.difficulty)) : dominantTierForDifficulty(element.difficulty));
                setFill(map, kMinimapSpawnColors[static_cast<std::size_t>(rarityIndex(tier))], 0.4);
                // The zone's outline, so the minimap shows the band the spawner
                // actually uses rather than the box around it. The bounding box
                // above is still what culls: it is a superset of the outline.
                if (element.polygon.size() >= 3) {
                    map.beginPath();
                    for (std::size_t i = 0; i < element.polygon.size(); ++i) {
                        const Vec2 point = toBox(element.polygon[i]);
                        const float px = static_cast<float>(point.x);
                        const float py = static_cast<float>(point.y);
                        if (i == 0) map.moveTo(px, py);
                        else map.lineTo(px, py);
                    }
                    map.closePath();
                    map.fill();
                    continue;
                }
                map.fillRect(static_cast<float>(topLeft.x), static_cast<float>(topLeft.y),
                             static_cast<float>(w), static_cast<float>(h));
            }
        }
    };

    // The walls: THE COLLISION GEOMETRY ITSELF, ring by ring.
    //
    // Not the coarse grid. A cell is Wall there when ANY shape blocks ANYWHERE
    // in it, so filling its square paints walls that are not there -- a third
    // of the shipped garden's solid cells are only partly solid -- and it
    // paints shut every passage narrower than the cell it runs through. With
    // the whole map in 200 units a cell is under two pixels, so no per-cell
    // shading would save it either. The authored rings are exact, the
    // rasteriser anti-aliases them for free, and a gap appears on the minimap
    // because the geometry genuinely has one.
    //
    // eachMinimapSolid() is what decides WHICH solids and in what order: every
    // ring once, whatever cells it is filed under, bottom layer first. See
    // minimap.h.
    //
    // ONE PATH PER RUN, not a fill per ring. Two rings that share an edge each
    // cover about half of the pixels along it, and half painted over half is
    // three quarters, not solid: filled one at a time, a wall mass comes out
    // hatched with pale seams along every tile boundary -- which is exactly the
    // "there is a tunnel there" the minimap must not invent. A nonzero fill of
    // equally wound contours is precisely their union, and its coverage is
    // computed over the union, so a batched path has seams nowhere and the true
    // edges everywhere. (The rasteriser's own stroker leans on the same
    // identity; see strokeOutline in third_party/cpp_canvas.) So the run is
    // broken only where the COLOUR changes going up the layer stack, which on
    // every map shipped here is once: water, then everything above it. A wall
    // layer and a castle layer above it are one fill and one silhouette.
    //
    // Walls are one black silhouette whatever kind of wall they are -- castle,
    // dirt, a boulder -- because the shape is what a player reads a minimap
    // for. WATER is the exception: it blocks like a wall but it is the one
    // kind of shape the map format still distinguishes, and a river drawn in
    // the same black as a castle turns a recognisable coastline into a blob.
    // The tileset's `water` tag exists for this and for the slow -- it never
    // decides whether a shape blocks, only what kind of blocker it is, and it
    // lives on the shape, never on the coarse cell.
    //
    // It is a few thousand short contours, and it happens ONCE per bake: this
    // whole layer is cached and only the key above rebuilds it. It goes on a
    // transparent layer of its own, with the teleporters over it, so the two
    // variants can share the one rasterisation instead of filling it twice.
    auto wallLayer = newBake();
    Canvas& walls = *wallLayer;
    Path2D run;
    bool runWater = false;
    const auto fillRun = [&]() {
        if (run.empty()) return;
        setFill(walls, runWater ? 0x4169E1u : 0x000000u);
        walls.fill(run);
        run.clear();
    };
    eachMinimapSolid(terrain, realm, [&](const MinimapSolid& solid) {
        if (!run.empty() && solid.water != runWater) fillRun();
        runWater = solid.water;
        if (solid.ring == nullptr) {
            // The whole-cell fallback: a blocking cell the map has no shapes
            // for, whose collision IS its square. See eachMinimapSolid().
            const Vec2 corner = toBox(solid.origin);
            const float side = static_cast<float>(kTileSize * fit.scale);
            run.rect(static_cast<float>(corner.x), static_cast<float>(corner.y), side, side);
            return;
        }
        // No closePath() after the ring. A fill closes every subpath on its
        // own, in the browser and in the software rasteriser alike, so it adds
        // nothing to the picture -- but Chrome's closePath costs time in
        // proportion to the whole path built so far, and ten thousand of them
        // in one run made this bake quadratic: a full second of frozen frame
        // (at 4x CPU throttle) on the first frame of every game, measured
        // 2026-10-03.
        const std::vector<Vec2>& ring = *solid.ring;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const Vec2 point = toBox(ring[i] + solid.origin);
            if (i == 0) run.moveTo(static_cast<float>(point.x), static_cast<float>(point.y));
            else run.lineTo(static_cast<float>(point.x), static_cast<float>(point.y));
        }
    });
    fillRun();

    static const std::vector<MapElement> kNoElements;
    for (const MapElement& element :
         annotations != nullptr ? annotations->elements() : kNoElements) {
        const bool pad = element.kind == MapElementKind::Teleporter;
        const bool npc = element.kind == MapElementKind::Npc;
        if (!pad && !npc) continue;
        // An area's NPC stands somewhere in it that the server picks: there
        // is no one spot to mark, and its middle would be a lie.
        if (npc && element.npcRoams()) continue;
        const Vec2 dot = toBox(element.centre());
        // Strictly inside, as the reference's test is: a zero-sized teleporter
        // sitting on the box's own edge is a dot the browser does not draw,
        // and a tolerance here would paint a clipped one it never shows.
        if (dot.x <= 0 || dot.x >= kMinimapSize || dot.y <= 0 || dot.y >= kMinimapSize) continue;
        // Green, never gold: gold marks a teleporter that hands the player to
        // another server, and this build has no such thing to mark. An NPC is
        // marked in its own mob's colour -- the oracle's flower yellow -- and
        // a size up, because it is somewhere a player goes on purpose.
        std::uint32_t color = 0x00FF00u;
        float radius = 3.0f;
        if (npc) {
            const std::uint16_t mob = content().mobIndex(element.npcId);
            if (mob == kInvalidIndex) continue;
            color = static_cast<std::uint32_t>(content().mob(mob).colorRgba >> 8);
            radius = 4.0f;
        }
        setFill(walls, color);
        walls.fillCircle(static_cast<float>(dot.x), static_cast<float>(dot.y), radius);
        setStroke(walls, kInk);
        walls.setLineWidth(1.0f);
        walls.strokeCircle(static_cast<float>(dot.x), static_cast<float>(dot.y), radius);
    }

    if (annotations) {
        int markers=0;
        for (const auto& element:annotations->elements()) {
            if (!superFarmBand(element) || markers>=8) continue;
            const auto at=toBox(Vec2{element.bounds.x+element.bounds.w*.5,
                                    element.bounds.y+element.bounds.h*.5});
            TextStyle label;label.size=10;label.align=Align::Centre;label.baseline=Baseline::Middle;
            label.fill=rarityColor(Rarity::Super);
            outlinedText(walls,"S",at.x,at.y,label,1.5);
            ++markers;
        }
    }
    // The two bakes the corner shows: the ground, then the wall layer laid
    // over it pixel for pixel. The blit is in raw pixels (identity transform,
    // the source's own size), so the walls land exactly where they were
    // rasterised rather than being resampled through the design scale.
    const auto compose = [&](bool bands) {
        auto bake = newBake();
        paintGround(*bake, bands);
        bake->save();
        bake->resetTransform();
        bake->drawCanvas(walls, 0, 0);
        bake->restore();
        return bake;
    };
    minimapStatic_ = compose(false);
    minimapStaticGlow_ = compose(true);
    minimapRealm_ = realm;
    minimapCols_ = cols;
    minimapRows_ = rows;
    minimapDensity_ = density;
    return rarityGlow ? minimapStaticGlow_.get() : minimapStatic_.get();
}

void App::drawMinimap(Canvas& canvas) {
    // The corner belongs to whichever realm the flower is in: the maze draws
    // its own layout there, and the arena -- which has no map worth showing --
    // puts its scoreboard there instead, as the reference does.
    // altMode_ (ALT latch): spawn-band bake. Player dots + names always on.
    switch (net_.view().realm()) {
        case Realm::Maze:
            drawMazeMinimap(canvas);
            return;
        case Realm::Arena:
            drawArenaLeaderboard(canvas);
            return;
        case Realm::Overworld:
            break;
    }
    // A map that hides its minimap (florr's `minimap: false`) gets nothing in
    // the corner at all: the pyramid's maze is to be found, not read.
    if (const MapData* map = worldMaps_.forRealm(net_.view().realm())) {
        if (!map->showsMinimap() && !(map->id() == "pyramid" && net_.pyramidMapRevealed)) return;
    }

    const double x = canvas.width() - kMinimapSize - kMinimapPadding;
    const double y = kMinimapPadding;

    // THE WHOLE MAP, fitted into the box. There is no zoom and no scrolling --
    // the reference's scroll and zoom entry points are both no-ops -- and
    // there is no longer a window onto the map either: one map is in play at a
    // time, it says how big it is, and all of it belongs in the corner. A map
    // that is not square letterboxes rather than stretching; see minimap.h.
    const Realm realm = net_.view().realm();
    const MinimapFit fit = minimapFit(net_.terrain().realmExtent(realm), kMinimapSize);
    const Vec2 me = net_.view().selfDrawnPosition();

    if (const Canvas* baked = minimapStatic(altMode_)) {
        // Sized explicitly rather than left to drawCanvas's two-argument form:
        // that one takes the source's PIXEL size as its user-space extent,
        // which would draw a bake rasterised for a Retina display at twice
        // the size the minimap is meant to be.
        canvas.drawCanvas(*baked, static_cast<float>(x), static_cast<float>(y),
                          static_cast<float>(kMinimapSize), static_cast<float>(kMinimapSize));
    }

    canvas.save();
    canvas.beginPath();
    canvas.rect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(kMinimapSize),
                static_cast<float>(kMinimapSize));
    canvas.clip();
    const auto dot = [&](Vec2 world, double radius, std::uint32_t fill, bool outlined,
                         const std::string* label) {
        const Vec2 box = fit.toBox(world);
        const double dx = x + box.x;
        const double dy = y + box.y;
        // Strictly inside, as the reference tests it: a body outside the map's
        // own rectangle -- in the letterbox bar, or off the map altogether --
        // is not on this map and gets no dot.
        if (dx <= x || dx >= x + kMinimapSize || dy <= y || dy >= y + kMinimapSize) return;
        setFill(canvas, fill);
        canvas.fillCircle(static_cast<float>(dx), static_cast<float>(dy),
                          static_cast<float>(radius));
        if (outlined) {
            setStroke(canvas, kInk);
            canvas.setLineWidth(1.0f);
            canvas.strokeCircle(static_cast<float>(dx), static_cast<float>(dy),
                                static_cast<float>(radius));
        }
        if (label == nullptr || label->empty()) return;
        TextStyle name;
        name.size = 9.0;
        name.fill = kPaper;
        name.stroke = kInk;
        name.align = Align::Centre;
        name.baseline = Baseline::Bottom;
        text(canvas, *label, dx, dy - radius - 2.0, name);
    };
    // AOI stream first (fresh); then OnlineRoster fills in far-away humans the
    // viewport never carries. Bots only exist in AOI -- they are not sessions.
    const SquadState& squad = net_.squad();
    const auto squadByName = [&](const std::string& name) {
        if (!squad.inSquad || name.empty()) return false;
        for (const auto& m : squad.members) {
            if (m.name == name || m.account == name) return true;
        }
        return false;
    };
    std::vector<std::string> painted;
    painted.reserve(16);
    for (const auto& entry : net_.view().entities()) {
        const RemoteEntity& entity = entry.second;
        if (entity.kind != net::EntityKind::Player || entity.isSelf()) continue;
        const bool squadmate = squad.contains(entry.first);
        const std::string label =
            entity.name.empty() ? std::string(tr(Msg::Flower)) : entity.name;
        if (squadmate) dot(entity.position, 4.0, 0xFF69B4u, true, &label);
        else dot(entity.position, 4.0, kInk, false, &label);
        painted.push_back(label);
    }
    const std::string selfName =
        net_.profile().username.empty() ? std::string(tr(Msg::You)) : net_.profile().username;
    const std::string& selfAccount = net_.profile().username;
    const std::uint8_t myRealm = static_cast<std::uint8_t>(realm);
    for (const auto& row : net_.onlineRoster()) {
        Vec2 at;bool projected=false;
        if(!row.mapPosition(myRealm,at,projected))continue;
        if (row.name == selfName || (!selfAccount.empty() && row.name == selfAccount)) continue;
        bool already = false;
        for (const std::string& n : painted) {
            if (n == row.name) {
                already = true;
                break;
            }
        }
        if (already) continue;
        const std::string markerLabel=row.name+(projected?(ui::language()==ui::Language::Zh?"（白蚁丘内）":" (inside mound)"):"");
        if (squadByName(row.name)) dot(at, 4.0, 0xFF69B4u, true, &markerLabel);
        else dot(at, 4.0, kInk, false, &markerLabel);
    }
    // Self last, so the blue dot is never hidden under someone standing on it.
    dot(me, 3.0, 0x0000FFu, true, &selfName);

    // The camera's own rectangle, inside the clip, with hitboxes on. The
    // reference strokes it from cameraX/cameraY, which are the world-space TOP
    // LEFT of the view (core.ts:418-424), so it is visibleWorld() here and not
    // the camera centre. A whole map in 200 units makes it small, which is
    // exactly what it is: the view is a hundredth of this map.
    if (menus_.settings().render.hitboxes) {
        const Rect view = camera_.visibleWorld();
        const Vec2 corner = fit.toBox({view.x, view.y});
        setStroke(canvas, kInk);
        canvas.setLineWidth(2.0f);
        canvas.beginPath();
        canvas.rect(static_cast<float>(x + corner.x), static_cast<float>(y + corner.y),
                    static_cast<float>(view.w * fit.scale),
                    static_cast<float>(view.h * fit.scale));
        canvas.stroke();
    }
    canvas.restore();

    canvas.save();
    setStroke(canvas, 0xFFD700u);
    canvas.setLineWidth(2.0f);
    canvas.setLineJoin("miter");
    canvas.beginPath();
    canvas.rect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(kMinimapSize),
                static_cast<float>(kMinimapSize));
    canvas.stroke();
    canvas.restore();

    // The caption names the MAP, because the map is what the corner shows.
    // It used to name the biome of the section under the flower, which is what
    // the reference captioned a one-ninth-of-the-world minimap with; there are
    // several maps now and each is drawn whole, so its own name is the label
    // that means something.
    //
    // A client whose annotations did not load -- worldMaps_.load() failing is
    // a warning, not a refusal to start -- has no name for the map and gets NO
    // caption. The old fallback was the biome of the ground under the flower,
    // which is biomeOf(sectionAt(p)): the fixed 20000-unit nine-square grid of
    // a world that no longer exists. On a 38400-unit map it renamed the corner
    // as the player walked past x = 20000, which is the section jump this
    // minimap was rewritten to be rid of -- better no label than one that
    // moves.
    const MapData* named = worldMaps_.forRealm(realm);
    if (named != nullptr) {
        TextStyle caption;
        caption.size = 14.0;
        caption.align = Align::Centre;
        caption.baseline = Baseline::Alphabetic;
        const std::string captionText =
            named->displayName().empty() ? named->id() : named->displayName();
        text(canvas, captionText, x + kMinimapSize * 0.5, y + kMinimapSize + 18.0, caption);
    }
}

bool App::scrollOnlineRoster() {
    if (window_.wheelDelta() == 0) return false;
    if (onlineRosterBox_.w <= 0) return false;
    const Vec2 mouse{window_.mouseX(), window_.mouseY()};
    if (!onlineRosterBox_.contains(mouse)) return false;
    onlineRosterScroll_ =
        std::max(0.0, onlineRosterScroll_ + window_.wheelDelta() * kRosterWheelStep);
    return true;
}

void App::drawOnlineRoster(Canvas& canvas) {
    onlineRosterBox_ = {};
    const auto& roster = net_.onlineRoster();
    // Nothing to show until the first push; keeps the corner clean on connect.
    if (roster.empty() && !net_.haveSession()) return;

    const double x = canvas.width() - kRosterWidth - kMinimapPadding;
    const double y = kMinimapPadding + kMinimapSize + kRosterGap;
    // Leave room above the bottom HUD / chat; never eat the whole screen.
    const double maxH = std::max(120.0, canvas.height() - y - 140.0);
    const double boxH = std::min(kRosterHeight, maxH);
    onlineRosterBox_ = {x, y, kRosterWidth, boxH};

    canvas.save();
    setFill(canvas, 0x141419u, kOverlayPlateAlpha);
    canvas.beginPath();
    canvas.roundRect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(kRosterWidth),
                     static_cast<float>(boxH), 6.0f);
    canvas.fill();
    setStroke(canvas, 0xFFD700u);
    canvas.setLineWidth(1.5f);
    canvas.beginPath();
    canvas.roundRect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(kRosterWidth),
                     static_cast<float>(boxH), 6.0f);
    canvas.stroke();

    TextStyle header;
    header.size = 12.0;
    header.fill = kPaper;
    header.align = Align::Left;
    header.baseline = Baseline::Middle;
    const std::string title =
        std::string(tr(Msg::Online)) + " " + std::to_string(roster.size());
    text(canvas, title, x + 8.0, y + kRosterHeader * 0.5, header);

    const double listTop = y + kRosterHeader;
    const double listH = std::max(0.0, boxH - kRosterHeader - 4.0);
    const double contentH = static_cast<double>(roster.size()) * kRosterRow;
    const double maxScroll = std::max(0.0, contentH - listH);
    if (onlineRosterScroll_ > maxScroll) onlineRosterScroll_ = maxScroll;

    canvas.beginPath();
    canvas.rect(static_cast<float>(x + 1.0), static_cast<float>(listTop),
                static_cast<float>(kRosterWidth - 2.0), static_cast<float>(listH));
    canvas.clip();

    TextStyle rowStyle;
    rowStyle.size = 10.0;
    rowStyle.fill = 0xCFC3B0u;
    rowStyle.align = Align::Left;
    rowStyle.baseline = Baseline::Middle;

    // Only paint rows that intersect the clip — cheap with dozens of players.
    const int first = static_cast<int>(onlineRosterScroll_ / kRosterRow);
    const int last = static_cast<int>(
        std::ceil((onlineRosterScroll_ + listH) / kRosterRow));
    const int n = static_cast<int>(roster.size());
    for (int i = std::max(0, first); i < std::min(n, last); ++i) {
        const auto& entry = roster[static_cast<std::size_t>(i)];
        const double cy =
            listTop + (static_cast<double>(i) + 0.5) * kRosterRow - onlineRosterScroll_;
        std::string line = entry.name + "  Lv" + std::to_string(entry.level);
        if (!entry.guild.empty()) line += "  [" + entry.guild + "]";
        if (!entry.location.empty()) line += "  " + entry.location;
        else {
            line += "  ";
            line += tr(Msg::LobbyParen);
        }
        text(canvas, ellipsize(line, rowStyle.size, kRosterWidth - 16.0), x + 8.0, cy, rowStyle);
    }
    canvas.restore();

    if (contentH > listH + 0.5) {
        scrollbar(canvas, Rect{x, listTop, kRosterWidth - 2.0, listH}, contentH,
                  onlineRosterScroll_, 0xFFD700u, 4.0);
    }
}

// ---------------------------------------------------------------------------
// The maze, and the arena's scoreboard
// ---------------------------------------------------------------------------

const Canvas* App::mazeMinimapStatic() {
    const Maze& maze = activeMaze();
    const double density = window_.uiScale();
    if (mazeMinimapStatic_ && mazeMinimapDay_ == maze.day() && mazeMinimapDensity_ == density) {
        return mazeMinimapStatic_.get();
    }
    const int dim = maze.gridDim();
    if (dim <= 0) return nullptr;

    const int bakeSide = std::max(1, static_cast<int>(std::lround(kMinimapSize * density)));
    auto baked = std::make_unique<Canvas>(Canvas::createVirtual(bakeSide, bakeSide));
    Canvas& map = *baked;
    map.scale(static_cast<float>(density), static_cast<float>(density));
    const double s = kMinimapSize / dim;   // design units per maze cell

    // Dark backdrop: the walls.
    setFill(map, 0x141419u, kOverlayPlateAlpha);
    map.fillRect(0, 0, static_cast<float>(kMinimapSize), static_cast<float>(kMinimapSize));

    // Walkable shapes in white, corner cells with the same fillet geometry
    // the world view draws -- the complement of the wall shape: a quarter-disc
    // around the corner vertex for a convex floor corner, the carved curved
    // triangle for a concave wall cell.
    setFill(map, 0xFFFFFFu, 0.92);
    for (int gy = 0; gy < dim; ++gy) {
        for (int gx = 0; gx < dim; ++gx) {
            const int v = maze.cellValue(gx, gy);
            if (v == 0) continue;
            const double x0 = gx * s;
            const double y0 = gy * s;
            map.beginPath();
            if (v == 1) {
                map.rect(static_cast<float>(x0), static_cast<float>(y0),
                         static_cast<float>(s + 0.5), static_cast<float>(s + 0.5));
                map.fill();
                continue;
            }
            const int left = (v >> 1) & 1;
            const int top = v & 1;
            const int concave = (v >> 3) & 1;
            const double cx = x0 + left * s;
            const double cy = y0 + top * s;
            const double sx = concave ? x0 + (1 - left) * s : cx;
            const double sy = concave ? y0 + (1 - top) * s : cy;
            const double a0 = mazeFilletStartAngle(v);
            map.moveTo(static_cast<float>(sx), static_cast<float>(sy));
            map.arc(static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(s),
                    static_cast<float>(a0), static_cast<float>(a0 + kPi * 0.5), false);
            map.fill();
        }
    }

    // Depth-zone tint over the corridors; walls carry none.
    for (int gy = 0; gy < dim; ++gy) {
        for (int gx = 0; gx < dim; ++gx) {
            const int v = maze.cellValue(gx, gy);
            if (v == 0 || (v >= 12 && v <= 15)) continue;
            const int zone = maze.zoneOfCell(gx, gy);
            if (zone < 0 || zone >= kMazeZoneCount) continue;
            setFill(map, kMazeZoneTints[static_cast<std::size_t>(zone)], kMazeZoneTintAlpha);
            map.fillRect(static_cast<float>(gx * s), static_cast<float>(gy * s),
                         static_cast<float>(s + 0.5), static_cast<float>(s + 0.5));
        }
    }

    mazeMinimapStatic_ = std::move(baked);
    mazeMinimapDay_ = maze.day();
    mazeMinimapDensity_ = density;
    return mazeMinimapStatic_.get();
}

void App::drawMazeMinimap(Canvas& canvas) {
    const Maze& maze = activeMaze();
    const double x = canvas.width() - kMinimapSize - kMinimapPadding;
    const double y = kMinimapPadding;
    const double size = maze.worldSize();
    if (!(size > 0.0)) return;
    const double scale = kMinimapSize / size;

    if (const Canvas* baked = mazeMinimapStatic()) {
        canvas.drawCanvas(*baked, static_cast<float>(x), static_cast<float>(y),
                          static_cast<float>(kMinimapSize), static_cast<float>(kMinimapSize));
    }

    canvas.save();
    canvas.beginPath();
    canvas.rect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(kMinimapSize),
                static_cast<float>(kMinimapSize));
    canvas.clip();
    // AOI + OnlineRoster (same realm), labeled — roster covers far humans.
    const auto dot = [&](Vec2 world, double radius, std::uint32_t fill, bool outlined,
                         const std::string* label) {
        const double dx = x + (world.x - kMazeOriginX) * scale;
        const double dy = y + (world.y - kMazeOriginY) * scale;
        setFill(canvas, fill);
        canvas.fillCircle(static_cast<float>(dx), static_cast<float>(dy),
                          static_cast<float>(radius));
        if (outlined) {
            setStroke(canvas, kInk);
            canvas.setLineWidth(1.0f);
            canvas.strokeCircle(static_cast<float>(dx), static_cast<float>(dy),
                                static_cast<float>(radius));
        }
        if (label == nullptr || label->empty()) return;
        TextStyle name;
        name.size = 9.0;
        name.fill = kPaper;
        name.stroke = kInk;
        name.align = Align::Centre;
        name.baseline = Baseline::Bottom;
        text(canvas, *label, dx, dy - radius - 2.0, name);
    };
    const SquadState& squad = net_.squad();
    const auto squadByName = [&](const std::string& name) {
        if (!squad.inSquad || name.empty()) return false;
        for (const auto& m : squad.members) {
            if (m.name == name || m.account == name) return true;
        }
        return false;
    };
    std::vector<std::string> painted;
    for (const auto& entry : net_.view().entities()) {
        const RemoteEntity& entity = entry.second;
        if (entity.kind != net::EntityKind::Player || entity.isSelf()) continue;
        const bool squadmate = squad.contains(entry.first);
        const std::string label =
            entity.name.empty() ? std::string(tr(Msg::Flower)) : entity.name;
        if (squadmate) dot(entity.position, 4.0, 0xFF69B4u, true, &label);
        else dot(entity.position, 4.0, kInk, false, &label);
        painted.push_back(label);
    }
    const std::string selfName =
        net_.profile().username.empty() ? std::string(tr(Msg::You)) : net_.profile().username;
    const std::string& selfAccount = net_.profile().username;
    constexpr std::uint8_t kMazeRealm = static_cast<std::uint8_t>(Realm::Maze);
    for (const auto& row : net_.onlineRoster()) {
        if (!row.placed || row.realm != kMazeRealm) continue;
        if (row.name == selfName || (!selfAccount.empty() && row.name == selfAccount)) continue;
        bool already = false;
        for (const std::string& n : painted) {
            if (n == row.name) {
                already = true;
                break;
            }
        }
        if (already) continue;
        const Vec2 at{row.x, row.y};
        if (squadByName(row.name)) dot(at, 4.0, 0xFF69B4u, true, &row.name);
        else dot(at, 4.0, kInk, false, &row.name);
    }
    dot(net_.view().selfDrawnPosition(), 3.0, 0x0000FFu, true, &selfName);
    canvas.restore();

    canvas.save();
    setStroke(canvas, 0xFFD700u);
    canvas.setLineWidth(2.0f);
    canvas.setLineJoin("miter");
    canvas.beginPath();
    canvas.rect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(kMinimapSize),
                static_cast<float>(kMinimapSize));
    canvas.stroke();
    canvas.restore();

    TextStyle caption;
    caption.size = 14.0;
    caption.align = Align::Centre;
    caption.baseline = Baseline::Alphabetic;
    text(canvas, std::string("Maze \xE2\x80\x94 ") + mazeBiomeLabel(maze.biome()),
         x + kMinimapSize * 0.5, y + kMinimapSize + 18.0, caption);
}

void App::drawArenaLeaderboard(Canvas& canvas) {
    // Every flower in the ring, best score first. Only arena players are ever
    // streamed to an arena client, so the entity table IS the roster.
    struct Row {
        std::string name;
        double score = 0;
    };
    std::vector<Row> rows;
    for (const auto& entry : net_.view().entities()) {
        const RemoteEntity& entity = entry.second;
        if (entity.kind != net::EntityKind::Player) continue;
        rows.push_back({entity.name.empty() ? std::string(tr(Msg::Unnamed)) : entity.name,
                        static_cast<double>(entity.arenaScore)});
    }
    if (rows.empty()) return;
    std::stable_sort(rows.begin(), rows.end(),
                     [](const Row& a, const Row& b) { return a.score > b.score; });
    const std::size_t shown = std::min<std::size_t>(rows.size(), 10);
    const double maxScore = std::max(1.0, rows.front().score);

    // gardn's proportions, as the browser build lays them out: a green pill
    // header with the flower count, a dark container, and a dark pill per row
    // with a flower-coloured progress bar behind centred "Name - Score".
    constexpr double kRowW = 200.0;
    constexpr double kRowH = 20.0;
    constexpr double kRowGap = 4.0;
    constexpr double kHeaderH = 36.0;
    constexpr double kHeaderPadX = 14.0;
    constexpr double kPanelPad = 10.0;
    constexpr double kBorder = 5.0;
    constexpr double kRadius = 7.0;

    const double headerW = kRowW + kHeaderPadX * 2.0;
    const double panelW = headerW + kPanelPad * 2.0;
    const double rowsH = shown * kRowH + (shown > 0 ? shown - 1 : 0) * kRowGap;
    const double panelH = kHeaderH + kPanelPad * 2.0 + rowsH + kPanelPad;
    const double x = canvas.width() - panelW - 20.0;
    const double y = 20.0;

    canvas.save();
    canvas.setLineJoin("round");

    setFill(canvas, 0x555555u);
    setStroke(canvas, 0x454545u);
    canvas.setLineWidth(static_cast<float>(kBorder));
    canvas.beginPath();
    canvas.roundRect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(panelW),
                     static_cast<float>(panelH), static_cast<float>(kRadius));
    canvas.stroke();
    canvas.fill();

    const double headerX = x + (panelW - headerW) * 0.5;
    const double headerY = y + kPanelPad;
    setFill(canvas, 0x55BB55u);
    setStroke(canvas, 0x469646u);
    canvas.setLineWidth(static_cast<float>(kBorder));
    canvas.beginPath();
    canvas.roundRect(static_cast<float>(headerX), static_cast<float>(headerY),
                     static_cast<float>(headerW), static_cast<float>(kHeaderH),
                     static_cast<float>(kRadius));
    canvas.stroke();
    canvas.fill();

    TextStyle header;
    header.size = 18.0;
    header.fill = 0xFFFFFFu;
    header.stroke = 0x222222u;
    header.align = Align::Centre;
    header.baseline = Baseline::Middle;
    text(canvas,
         language() == Language::Zh
             ? (std::to_string(rows.size()) + " 朵花")
             : (rows.size() == 1 ? std::string("1 Flower")
                                 : std::to_string(rows.size()) + " Flowers"),
         headerX + headerW * 0.5, headerY + kHeaderH * 0.5, header);

    const double rowsX = x + (panelW - kRowW) * 0.5;
    const double rowsY = headerY + kHeaderH + kPanelPad;
    const double rowFont = kRowH * 0.75;
    TextStyle rowStyle;
    rowStyle.size = rowFont;
    rowStyle.fill = 0xFFFFFFu;
    rowStyle.stroke = 0x222222u;
    rowStyle.align = Align::Centre;
    rowStyle.baseline = Baseline::Middle;

    canvas.setLineCap("round");
    for (std::size_t i = 0; i < shown; ++i) {
        const Row& row = rows[i];
        const double rowY = rowsY + i * (kRowH + kRowGap);
        const double cy = rowY + kRowH * 0.5;
        const double ratio = clamp(row.score / maxScore, 0.0, 1.0);
        const double inset = kRowH * 0.5;

        // The dark track, as a thick rounded line.
        setStroke(canvas, 0x222222u);
        canvas.setLineWidth(static_cast<float>(kRowH));
        canvas.beginPath();
        canvas.moveTo(static_cast<float>(rowsX + inset), static_cast<float>(cy));
        canvas.lineTo(static_cast<float>(rowsX + kRowW - inset), static_cast<float>(cy));
        canvas.stroke();

        // The flower-coloured fill, a little narrower so the track shows.
        if (ratio > 0.0) {
            setStroke(canvas, 0xFFE763u);
            canvas.setLineWidth(static_cast<float>(kRowH * 0.8));
            const double segment = (kRowW - kRowH) * ratio;
            canvas.beginPath();
            canvas.moveTo(static_cast<float>(rowsX + inset), static_cast<float>(cy));
            canvas.lineTo(static_cast<float>(rowsX + inset + segment), static_cast<float>(cy));
            canvas.stroke();
        }

        text(canvas, row.name + " - " + arenaScoreLabel(row.score), rowsX + kRowW * 0.5, cy,
             rowStyle);
    }
    canvas.restore();
}

} // namespace flix
