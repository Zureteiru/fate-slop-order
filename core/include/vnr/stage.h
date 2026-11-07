// Plain-data description of everything on screen. The app draws this; the player changes it.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vnr/command.h"
#include "vnr/tween.h"

namespace vnr {

// Periodic shake (stage, character or text box). duration 0 = until stopped.
struct Shake {
    bool active = false;
    float interval = 0.05f, amp_x = 0, amp_y = 0;
    double duration = 0, elapsed = 0;
    Vec2 offset() const;  // current displacement
    void tick(double dt);
    bool timed_running() const { return active && duration > 0 && elapsed < duration; }
};

struct Filter {
    std::string mode;  // "", "silhouette", "gray", "sepia", "dark", "blur"… (renderer decides the look)
    std::optional<Rgba> color;
};

struct Cutin {
    bool reveal = true;     // charaCutin reveals, charaCutout hides
    std::string pattern;    // wipe pattern name
    float softness = 1;
    Anim progress;          // 0 -> 1
};

enum class ElementKind { Chara, Image, Scene };

struct Element {
    std::string code;
    ElementKind kind = ElementKind::Chara;
    int figure_id = 0;      // Chara: CharaFigure id
    std::string asset;      // Image: image name; Scene: background id
    std::string name;       // display name from charaSet (used to match speakers)
    int face = 0;

    // cross-fades started by charaChange / charaFaceFade: draw the previous one under, fading out
    int prev_figure_id = 0;
    int prev_face = 0;
    Anim swap_mix;          // 0 -> 1 while the new figure/face fades in

    bool shown = false;     // logically on stage (opacity may still be animating)
    bool overlay = false;   // overlayFadein: on top, never takes talk focus
    Anim opacity;           // 0..1
    Anim2 pos;              // offset from the centred figure, y up
    Anim scale;             // 1 = normal
    Anim roll;              // degrees
    Vec2 roll_origin;
    float depth = 0;
    std::string layer;      // sub-render layer key; empty = main
    Filter filter;
    bool shadow = false;
    Shake shake;
    std::optional<Cutin> cutin;
    Anim attack;            // charaAttack lunge progress 0 -> 1 -> done
    Vec2 attack_offset;
    bool dimmed = false;    // talk focus: someone else is speaking
};

struct ScreenFade {
    Rgba color{0, 0, 0, 255};
    Anim opacity;  // 0 = clear, 1 = fully covered
};

struct Wipe {
    std::string pattern;    // e.g. "circleIn", "sideBlind"
    bool reveal = false;    // wipein (uncover) vs wipeout (cover)
    float softness = 1;
    Rgba color{0, 0, 0, 255};
    Anim cover;             // 1 = screen covered
};

struct WipeFilter {         // wipeFilter: a static partial wipe used as a vignette
    bool active = false;
    std::string pattern;
    float fraction = 0, softness = 1;
};

struct Flash {
    bool active = false;
    bool loop = false;
    double rise = 0, duration = 0.2, elapsed = 0;
    Rgba from{255, 255, 255, 128}, to{255, 255, 255, 0};
    bool fading_out = false;
    Anim out;               // 1 -> 0 multiplier during flashout
    Rgba color() const;     // current colour, alpha includes the rise and flashout
    void tick(double dt);
};

struct Camera {
    Anim2 offset;
    Anim scale;
    Anim roll;
    std::string filter;     // "", "gray", "darkred", "aberration"…
    float filter_intensity = 1;
    std::string blur_mode;  // "lens" / "motion"; empty = none
    Anim blur;              // px
};

enum class EffectLayer { Back, Front, Chara, CharaBack };

struct Effect {
    std::string name;       // e.g. "bit_talk_41"
    EffectLayer layer = EffectLayer::Front;
    std::string target;     // character code for Chara layers
    std::optional<Vec2> pos;
    bool flip_h = false, flip_v = false;
    double elapsed = 0;
};

struct TitleCard {          // bit_chapterstart* / bit_queststart*
    bool chapter = true;
    std::string effect;
    double elapsed = 0, duration = 2.6;
};

struct Communication {      // communicationChara: codec-style hologram figure
    int figure_id = 0;
    int face = 0;
    bool loop = false;
    Anim opacity;
};

// A sub-camera layer (`charaLayer C sub #A`). Its characters draw only while it is shown, moved and
// scaled with it (subRenderFadein / subRenderMove / subCameraMove / subRenderScale), as Fate Café does.
struct SubLayer {
    bool shown = false;
    bool hiding = false;        // subRenderFadeout with a duration: hidden once `opacity` reaches 0
    bool placed = false;        // a position was given: drawn under the main characters
    Anim2 offset;               // y up, like character positions
    Anim scale;                 // 1 = normal
    Anim opacity;               // 0..1, the subRenderFadein / subRenderFadeout fade
    std::optional<float> depth; // subRenderDepth; otherwise the layer letter (#A = 1, #B = 2...)
    std::string mask_mode;      // subCameraFilter mode, e.g. "maskEdge" (the mask shape is not drawn yet)
    std::string mask;           // its mask texture name
    SubLayer();
};

// How an element's sub layer changes where it is drawn.
struct LayerPlacement {
    bool visible = true;          // false: its layer is hidden
    Vec2 offset;                  // added to the element position (y up)
    float scale = 1;              // multiplies the element scale
    bool scale_position = false;  // maskEdge layers move as one group: the position scales too
    float opacity = 1;
    float order = 50;             // draw order: main characters 50, moved layers by depth / letter
};
inline constexpr float kMainLayerOrder = 50;

struct Stage {
    std::string background;       // scene id
    std::string prev_background;  // fading out under the new one
    Anim background_mix;          // 0 -> 1 while the new background fades in

    std::map<std::string, Element> elements;
    std::map<std::string, SubLayer> sub_layers;  // by key ("#A")
    ScreenFade fade;
    Wipe wipe;
    WipeFilter wipe_filter;
    Flash flash;
    Camera camera;
    Shake shake;
    Shake message_shake;
    std::vector<Effect> effects;
    std::optional<TitleCard> title_card;
    std::optional<Communication> communication;
    std::string picture_frame;
    bool picture_frame_top = false;
    bool message_visible = true;

    std::string talk_code;        // charaTalk focus
    bool talk_enabled = true;

    Stage();
    void tick(double dt);
    bool busy(WaitCat c) const;   // anything of this category still animating?
    void finish_all();            // jump every animation to its end (used by Back)
    Element* find(const std::string& code);
    LayerPlacement placement(const Element& e) const;
};

}  // namespace vnr
