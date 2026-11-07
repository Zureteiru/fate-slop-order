// Atlas URLs, face crop (Atlas DB Scene.tsx maths) and slots.
#include "check.h"
#include "vnr/assets.h"

using namespace vnr;

TEST(asset_urls) {
    CHECK_EQ(script_url("0100000011", Region::NA), std::string("https://static.atlasacademy.io/NA/Script/01/0100000011.txt"));
    CHECK_EQ(script_folder("9400123456"), std::string("94/9400"));
    CHECK_EQ(script_folder("WarEpilogue108"), std::string("01"));
    CHECK_EQ(script_folder("Event1"), std::string("Common"));
    CHECK_EQ(background_url("10310", Region::NA), std::string("https://static.atlasacademy.io/NA/Back/back10310.png"));
    CHECK_EQ(background_url("101900", Region::JP),
             std::string("https://static.atlasacademy.io/JP/Back/back101900_1344_626.png"));
    CHECK_EQ(figure_url(98001000, Region::NA),
             std::string("https://static.atlasacademy.io/NA/CharaFigure/98001000/98001000_merged.png"));
    const AssetUrl bgm = bgm_url("BGM_EVENT_2", Region::NA);
    CHECK_EQ(bgm.primary, std::string("https://static.atlasacademy.io/NA/Audio/Bgm/BGM_EVENT_2/BGM_EVENT_2.mp3"));
    CHECK_EQ(bgm.fallback, std::string("https://static.atlasacademy.io/NA/Audio/BGM_EVENT_2/BGM_EVENT_2.mp3"));
    CHECK_EQ(se_folder("ba18"), std::string("Battle"));
    CHECK_EQ(se_folder("ar7"), std::string("ResidentSE"));
    CHECK_EQ(se_folder("21_ad1"), std::string("SE_21"));
    CHECK_EQ(se_folder("ad271"), std::string("SE"));
    const auto v = voice_url("100100_0_A010", Region::JP);
    REQUIRE(v.has_value());
    CHECK_EQ(v->primary, std::string("https://static.atlasacademy.io/JP/Audio/ChrVoice_100100/0_A010.mp3"));
    CHECK_EQ(v->fallback, std::string("https://static.atlasacademy.io/JP/Audio/Servants_100100/0_A010.mp3"));
    const auto battle = voice_url("700100_0_B160", Region::NA);  // battle line: Servants first
    REQUIRE(battle.has_value());
    CHECK_EQ(battle->primary, std::string("https://static.atlasacademy.io/NA/Audio/Servants_700100/0_B160.mp3"));
    CHECK_EQ(battle->fallback, std::string("https://static.atlasacademy.io/NA/Audio/ChrVoice_700100/0_B160.mp3"));
    const auto np = voice_url("NP_700900_1", Region::NA);
    REQUIRE(np.has_value());
    CHECK_EQ(np->primary, std::string("https://static.atlasacademy.io/NA/Audio/NoblePhantasm_700900/NP_700900_1.mp3"));
    CHECK(np->fallback.empty());
    CHECK(!voice_url("NP_x", Region::NA).has_value());
    CHECK(!voice_url("novoice", Region::NA).has_value());
    const AssetUrl mv = movie_url("Movie/op_01", Region::NA);
    CHECK_EQ(mv.primary, std::string("https://static.atlasacademy.io/NA/Movie/op_01.mp4"));
    CHECK_EQ(mv.fallback, std::string("https://static.atlasacademy.io/JP/Movie/op_01.mp4"));
    CHECK_EQ(svt_script_url({98001000, 8001000}, Region::NA),
             std::string("https://api.atlasacademy.io/raw/NA/svtScript?charaId=98001000&charaId=8001000"));
}

TEST(face_crop_classic_1024_sheet) {
    FigureMeta m;
    m.face_x = 380;
    m.face_y = 120;
    CHECK(!face_crop(m, 0, 1024).has_value());
    const auto f1 = face_crop(m, 1, 1024);
    REQUIRE(f1.has_value());
    CHECK_NEAR(f1->src.x, 0, 0);
    CHECK_NEAR(f1->src.y, 768, 0);
    CHECK_NEAR(f1->dst.x, 380, 0);
    const auto f6 = face_crop(m, 6, 1024);  // k=5 -> col 1, row 1
    CHECK_NEAR(f6->src.x, 256, 0);
    CHECK_NEAR(f6->src.y, 1024, 0);
}

TEST(face_crop_tall_sheet_pages) {
    FigureMeta m;
    const auto f1 = face_crop(m, 1, 1280);
    CHECK_NEAR(f1->src.y, 1280, 0);
    const auto f17 = face_crop(m, 17, 1280);  // k=16 -> row 4 -> page 1, row 0
    CHECK_NEAR(f17->src.x, 0, 0);
    CHECK_NEAR(f17->src.y, 1280 + 1024, 0);
    m.face_w = 300;
    m.face_h = 350;  // faceSizeRect: 3 per row
    const auto f4 = face_crop(m, 4, 1024);
    CHECK_NEAR(f4->src.x, 0, 0);
    CHECK_NEAR(f4->src.y, 1024 + 350, 0);
}

TEST(slots_and_body_origin) {
    CHECK(slot_offset(0) == (Vec2{-256, 0}));
    CHECK(slot_offset(2) == (Vec2{256, 0}));
    CHECK(slot_offset(6) == (Vec2{512, 0}));
    CHECK(slot_offset(42) == (Vec2{0, 0}));
    FigureMeta m;
    m.offset_x = 10;
    m.offset_y = -30;
    CHECK(body_origin(m, 1024) == (Vec2{170, 30}));
}
