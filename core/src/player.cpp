#include "vnr/player.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <set>

#include "vnr/assets.h"

namespace vnr {

namespace {

// Commands the player implements, plus ones that are deliberate no-ops.
const std::set<std::string, std::less<>>& supported_names() {
    static const std::set<std::string, std::less<>> names = {
        // characters and images
        "charaSet", "masterSet", "imageSet", "verticalImageSet", "horizontalImageSet", "masterImageSet", "imageChange",
        "sceneSet", "charaChange", "charaCrossFade", "charaFace", "charaFaceFade", "charaFadein", "charaFadeout",
        "overlayFadein", "overlayFadeout", "charaClear", "charaFadeTime", "charaPut", "charaMove", "charaMoveEase",
        "charaMoveReturn", "charaScale", "charaMoveScale", "charaMoveScaleEase", "charaDepth", "charaLayer",
        "charaFilter", "charaShadow", "charaShake", "charaShakeStop", "charaRoll", "charaRollMove", "charaRollAxis",
        "charaAttack", "charaCutin", "charaCutinPause", "charaCutout", "charaTalk", "charaEffect", "charaBackEffect",
        "charaEffectStop", "charaEffectDestroy", "charaBackEffectStop", "charaBackEffectDestroy",
        "charaEffectEdgeBlur", "charaEffectEdgeBlurStop", "charaEffectEdgeBlurDestroy", "charaSpecialEffect",
        "communicationChara", "communicationCharaLoop", "communicationCharaFace", "communicationCharaClear",
        // effects
        "effect", "fowardEffect", "forwardEffect", "backEffect", "effectStop", "effectDestroy", "fowardEffectStop",
        "fowardEffectDestroy", "forwardEffectStop", "forwardEffectDestroy", "backEffectStop", "backEffectDestroy",
        // screen and camera
        "scene", "masterScene", "fadein", "fadeout", "maskin", "maskout", "fadeMove", "wipein", "wipeout",
        "wipeFilter", "wipeOff", "flashin", "flashout", "flashOff", "shake", "shakeStop", "cameraMove",
        "cameraMoveEase", "cameraHome", "cameraRoll", "cameraRollMove", "cameraFilter", "blur", "blurOff",
        "pictureFrame", "pictureFrameTop", "pictureFrameOff", "pictureFrameClear", "messageOff", "messageOn",
        "messageShake",
        // audio and movies
        "bgm", "bgmStop", "bgmStopEnd", "se", "seLoop", "cueSe", "cueSeLoop", "seStop", "cueSeStop", "seVolume",
        "cueSeVolume", "soundStopAll", "soundStopAllEnd", "soundStopAllFade", "seStopAll", "voice", "voiceStop",
        "tVoice", "criMovie", "movie", "moviePlay", "playMovie",
        // timing
        "wt", "twt", "wait",
        // structural (handled by the compiler)
        "label", "branch", "branchQuestClear", "branchQuestNotClear", "branchMaterial", "revivalBranch",
        "masterBranch", "else", "endIf", "end", "k", "q", "page",
        // deliberate no-ops: name entry and route prompts are app UI; skip toggles the game's skip button
        "input", "skip",
    };
    return names;
}

bool starts_with(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

// Prefix families the game uses with suffixed variants (e.g. "charaFadeinFSL").
std::string_view family(std::string_view n) {
    if (starts_with(n, "charaFadein")) return "charaFadein";
    if (starts_with(n, "charaFadeout")) return "charaFadeout";
    if (starts_with(n, "charaPut")) return "charaPut";
    if (starts_with(n, "charaMoveScale")) return "charaMoveScale";
    if (starts_with(n, "charaMove")) return "charaMove";
    return n;
}

int to_int(const std::string& s) { return std::atoi(s.c_str()); }

std::string plain_name(const std::string& raw) { return parse_rich_text(raw, TextContext{}).plain(); }

constexpr double kCharaFade = 0.3;   // used only when a script omits the time
constexpr double kScreenFade = 0.4;

}  // namespace

bool command_supported(std::string_view name) {
    const auto& names = supported_names();
    return names.count(name) > 0 || names.count(family(name)) > 0;
}

Player::Player(const Program& program, Settings settings, Conditions* conditions)
    : prog_(program), settings_(std::move(settings)), cond_(conditions ? conditions : &default_conditions_) {
    run();
}

void Player::emit(AudioEvent e) {
    if (!mute_events_) audio_.push_back(std::move(e));
}

std::vector<AudioEvent> Player::drain_audio() {
    std::vector<AudioEvent> out;
    out.swap(audio_);
    return out;
}

bool Player::eval(const CondJumpOp& op) {
    switch (op.cond) {
        case Condition::QuestCleared: return cond_->quest_cleared(op.arg);
        case Condition::QuestNotCleared: return !cond_->quest_cleared(op.arg);
        case Condition::HasMaterial: return cond_->has_material(op.arg);
        case Condition::Revival: return cond_->revival();
        case Condition::MasterFemale: return settings_.text.gender == Gender::Female;
    }
    return false;
}

void Player::run() {
    while (status_ == Status::Running) {
        if (pc_ >= prog_.code.size()) {
            status_ = Status::Ended;
            return;
        }
        const Instr& in = prog_.code[pc_];
        line_ = instr_line(in);
        if (const auto* ex = std::get_if<ExecOp>(&in)) {
            ++pc_;
            exec(ex->cmd, false, false);
        } else if (const auto* say = std::get_if<SayOp>(&in)) {
            begin_say(*say);
        } else if (std::get_if<ChoiceOp>(&in)) {
            history_.push_back({pc_, stage_, bgm_, backlog_.size()});
            status_ = Status::AwaitChoice;
        } else if (const auto* j = std::get_if<JumpOp>(&in)) {
            pc_ = j->target;
        } else if (const auto* cj = std::get_if<CondJumpOp>(&in)) {
            pc_ = eval(*cj) ? cj->target : (cj->has_else ? cj->else_target : pc_ + 1);
        } else {
            status_ = Status::Ended;
        }
    }
}

void Player::update(double dt) {
    stage_.tick(dt);
    switch (status_) {
        case Status::Waiting: {
            if (waiting_movie_) return;
            if (wait_cat_ == WaitCat::None) {
                wait_timer_ -= dt;
                if (wait_timer_ > 1e-9) return;
            } else if (wait_cat_ == WaitCat::Voice) {
                wait_timer_ -= dt;
                if (voice_active_ && wait_timer_ > 0) return;
            } else if (stage_.busy(wait_cat_)) {
                return;
            }
            status_ = Status::Running;
            run();
            return;
        }
        case Status::Typing:
            step_typing(dt);
            return;
        case Status::AwaitAdvance:
            if (settings_.auto_mode) {
                auto_timer_ += dt;
                const bool voice_ok = !settings_.auto_waits_for_voice || !voice_active_;
                if (auto_timer_ >= settings_.auto_seconds && voice_ok) advance();
            }
            return;
        default:
            return;
    }
}

void Player::start_wait(double seconds, WaitCat cat) {
    if (cat == WaitCat::None && seconds <= 0) return;
    if (status_ == Status::Typing) {  // inline: pause typing
        inline_wait_ = seconds;
        inline_cat_ = cat;
        return;
    }
    wait_timer_ = seconds;
    wait_cat_ = cat;
    status_ = Status::Waiting;
}

void Player::begin_say(const SayOp& op) {
    history_.push_back({pc_, stage_, bgm_, backlog_.size()});
    say_ = &op;
    reveal_ = reveal_times(op.text, settings_.cps);
    typed_ = 0;
    shown_ = 0;
    next_inline_ = 0;
    inline_wait_ = 0;
    inline_cat_ = WaitCat::None;
    auto_timer_ = 0;

    BacklogEntry entry{op.speaker, op.text, {}, {}, op.line};
    if (!op.tvoice_file.empty()) {
        emit({AudioEvent::Kind::VoicePlay, op.tvoice_file, op.tvoice_folder, 0, 1, false});
        voice_active_ = true;
        entry.voice_name = op.tvoice_file;
        entry.voice_folder = op.tvoice_folder;
    } else if (!pending_voice_.empty()) {
        emit({AudioEvent::Kind::VoicePlay, pending_voice_, {}, 0, 1, false});
        voice_active_ = true;
        entry.voice_name = pending_voice_;
    }
    pending_voice_.clear();
    backlog_.push_back(std::move(entry));

    update_talk_focus(op.speaker_code, op.speaker.plain());
    stage_.message_visible = true;  // [messageOff] hides the box only until the next line of dialogue
    status_ = Status::Typing;
    step_typing(0);
}

void Player::step_typing(double dt) {
    if (inline_wait_ > 0 || inline_cat_ != WaitCat::None) {
        if (inline_cat_ == WaitCat::None) {
            inline_wait_ -= dt;
            if (inline_wait_ > 1e-9) return;
        } else if (inline_cat_ == WaitCat::Voice) {
            inline_wait_ -= dt;
            if (voice_active_ && inline_wait_ > 0) return;
        } else if (stage_.busy(inline_cat_)) {
            return;
        }
        inline_wait_ = 0;
        inline_cat_ = WaitCat::None;
        dt = 0;
    }
    typed_ += dt;
    const auto& cmds = say_->text.commands;
    while (true) {
        while (next_inline_ < cmds.size() && cmds[next_inline_].glyph <= shown_) {
            exec(cmds[next_inline_++].cmd, true, false);
            if (inline_wait_ > 0 || inline_cat_ != WaitCat::None) return;
            if (status_ != Status::Typing) return;  // e.g. a movie
        }
        if (shown_ >= say_->text.glyph_count) break;
        if (reveal_[shown_] <= typed_ + 1e-9) ++shown_;
        else return;
    }
    status_ = Status::AwaitAdvance;
    auto_timer_ = 0;
}

void Player::finish_typing(bool skipping) {
    const auto& cmds = say_->text.commands;
    while (next_inline_ < cmds.size()) exec(cmds[next_inline_++].cmd, true, skipping);
    shown_ = say_->text.glyph_count;
    inline_wait_ = 0;
    inline_cat_ = WaitCat::None;
}

void Player::leave_say() {
    if (voice_active_) {
        emit({AudioEvent::Kind::VoiceStop, {}, {}, 0, 1, false});
        voice_active_ = false;
    }
}

bool Player::advance() {
    if (status_ == Status::Typing) {
        finish_typing(true);
        status_ = Status::AwaitAdvance;
        auto_timer_ = 0;
        return true;
    }
    if (status_ == Status::AwaitAdvance) {
        leave_say();
        ++pc_;
        status_ = Status::Running;
        run();
        return true;
    }
    return false;
}

bool Player::choose(std::size_t index) {
    const ChoiceOp* c = pending_choice();
    if (!c || index >= c->options.size()) return false;
    pc_ = c->options[index].target;
    status_ = Status::Running;
    run();
    return true;
}

const ChoiceOp* Player::pending_choice() const {
    if (status_ != Status::AwaitChoice) return nullptr;
    return std::get_if<ChoiceOp>(&prog_.code[pc_]);
}

bool Player::back() {
    const bool at_stop = status_ == Status::Typing || status_ == Status::AwaitAdvance || status_ == Status::AwaitChoice;
    if (history_.size() < (at_stop ? 2u : 1u)) return false;
    if (at_stop) history_.pop_back();
    Snapshot snap = std::move(history_.back());
    history_.pop_back();

    leave_say();
    const std::string bgm_before = bgm_;
    stage_ = std::move(snap.stage);
    stage_.finish_all();
    pc_ = snap.pc;
    bgm_ = snap.bgm;
    backlog_.resize(snap.backlog_size);
    waiting_movie_ = false;

    mute_events_ = true;
    status_ = Status::Running;
    run();  // re-enters the dialogue or choice at pc_
    if (status_ == Status::Typing) {
        finish_typing(true);
        status_ = Status::AwaitAdvance;
    }
    if (voice_active_) voice_active_ = false;  // muted: nothing is actually playing
    mute_events_ = false;

    if (bgm_ != bgm_before) {
        if (bgm_.empty()) emit({AudioEvent::Kind::BgmStop, {}, {}, 0, 1, false});
        else emit({AudioEvent::Kind::BgmPlay, bgm_, {}, 0, 1, true});
    }
    return true;
}

void Player::skip() {
    leave_say();
    pc_ = prog_.code.size() - 1;
    status_ = Status::Ended;
}

void Player::voice_finished() { voice_active_ = false; }

void Player::movie_finished() {
    if (!waiting_movie_) return;
    waiting_movie_ = false;
    status_ = Status::Running;
    run();
}

TextView Player::text() const {
    TextView v;
    v.say = say_;
    v.shown = status_ == Status::Typing ? shown_ : (say_ ? say_->text.glyph_count : 0);
    v.visible = say_ && stage_.message_visible &&
                (status_ == Status::Typing || status_ == Status::AwaitAdvance || status_ == Status::AwaitChoice);
    return v;
}

void Player::update_talk_focus(const std::string& code, const std::string& name) {
    Element* sp = nullptr;
    if (!code.empty()) {
        sp = stage_.find(code);
        if (sp && sp->kind != ElementKind::Chara) sp = nullptr;
    }
    if (!sp && !name.empty()) {
        for (auto& [c, e] : stage_.elements)
            if (e.kind == ElementKind::Chara && e.shown && !e.overlay && e.name == name) { sp = &e; break; }
        if (!sp && !stage_.talk_code.empty()) sp = stage_.find(stage_.talk_code);
    }
    const bool focus = stage_.talk_enabled && sp && sp->shown;
    for (auto& [c, e] : stage_.elements) {
        e.dimmed = focus && e.kind == ElementKind::Chara && e.shown && !e.overlay && &e != sp && e.layer == sp->layer;
    }
}

void Player::exec(const Command& c, bool inline_cmd, bool skipping) {
    (void)inline_cmd;
    const std::string_view n = family(c.name);
    auto num = [&](std::size_t i, double def) {
        const auto v = parse_number(c.arg(i));
        return v ? *v : def;
    };
    auto has_num = [&](std::size_t i) { return parse_number(c.arg(i)).has_value(); };
    Element* el = c.has(0) ? stage_.find(c.arg(0)) : nullptr;
    const bool female = settings_.text.gender == Gender::Female;
    auto new_element = [&](ElementKind kind) -> Element& {
        Element e;
        e.code = c.arg(0);
        e.kind = kind;
        e.opacity.snap(0);
        e.scale.snap(1);
        e.swap_mix.snap(1);
        Element& slot = stage_.elements[e.code];
        slot = std::move(e);
        return slot;
    };
    auto fade_chara = [&](Element& e, bool in, double t, const std::string& pos_arg, bool overlay) {
        e.shown = in;
        e.cutin.reset();
        if (in) {
            e.overlay = overlay;
            if (const auto p = parse_position(pos_arg)) {
                e.pos.snap(position_offset(*p));
                if (p->scale) e.scale.snap(*p->scale);
            }
        }
        e.opacity.go(in ? 1.f : 0.f, t, Ease::Ease, WaitCat::CharaFade);
    };

    // ---- timing
    if (n == "wt" || n == "twt") {
        if (!skipping) start_wait(num(0, 0), WaitCat::None);
        return;
    }
    if (n == "wait") {
        if (skipping) return;
        const WaitCat cat = parse_wait(c.arg(0));
        if (cat == WaitCat::Voice) {
            if (voice_active_) start_wait(settings_.voice_wait_cap, WaitCat::Voice);
        } else if (stage_.busy(cat)) {
            start_wait(0, cat);
        }
        return;
    }

    // ---- characters and images
    if (n == "charaSet" || n == "masterSet") {
        Element& e = new_element(ElementKind::Chara);
        if (n == "charaSet") {
            e.figure_id = to_int(c.arg(1));
            e.face = to_int(c.arg(2));
            e.name = plain_name(c.arg(3));
        } else {
            e.figure_id = to_int(female ? c.arg(2) : c.arg(1));
            e.face = to_int(c.arg(3));
            e.name = plain_name(c.arg(4));
        }
        return;
    }
    if (n == "imageSet" || n == "verticalImageSet" || n == "horizontalImageSet" || n == "masterImageSet") {
        Element& e = new_element(ElementKind::Image);
        e.asset = n == "masterImageSet" ? (female ? c.arg(2) : c.arg(1)) : c.arg(1);
        return;
    }
    if (n == "sceneSet") {
        Element& e = new_element(ElementKind::Scene);
        e.asset = c.arg(1);
        return;
    }
    if (n == "communicationChara" || n == "communicationCharaLoop") {
        Communication comm;
        comm.figure_id = to_int(c.arg(0));
        comm.face = to_int(c.args.size() >= 5 ? c.arg(4) : c.arg(1));
        comm.loop = n == "communicationCharaLoop";
        comm.opacity.snap(0);
        comm.opacity.go(1, kCharaFade, Ease::Ease, WaitCat::Communication);
        stage_.communication = comm;
        return;
    }
    if (n == "communicationCharaFace") {
        if (stage_.communication) stage_.communication->face = to_int(c.arg(0));
        return;
    }
    if (n == "communicationCharaClear") {
        if (stage_.communication) stage_.communication->opacity.go(0, kCharaFade, Ease::Ease, WaitCat::Communication);
        return;
    }
    if (n == "charaTalk") {
        const std::string& a = c.arg(0);
        if (a == "on") stage_.talk_enabled = true;
        else if (a == "off") stage_.talk_enabled = false;
        else if (a == "depthOn" || a == "depthOff") { /* depth sorting by focus: renderer concern */ }
        else {
            stage_.talk_code = a;
            update_talk_focus(a, {});
        }
        return;
    }
    if (n.substr(0, 5) == "chara" || n == "imageChange" || n == "overlayFadein" || n == "overlayFadeout") {
        if (!el) return;  // unknown code: the game ignores it too
        Element& e = *el;
        if (n == "imageChange") { e.asset = c.arg(1); return; }
        if (n == "charaChange" || n == "charaCrossFade") {
            e.prev_figure_id = e.figure_id;
            e.prev_face = e.face;
            e.figure_id = to_int(c.arg(1));
            e.face = to_int(c.arg(2));
            double t = 0;
            for (std::size_t i = c.args.size(); i-- > 3;)
                if (has_num(i)) { t = num(i, 0); break; }
            e.swap_mix.snap(0);
            e.swap_mix.go(1, t, Ease::Linear, WaitCat::CharaFade);
            return;
        }
        if (n == "charaFace") { e.face = to_int(c.arg(1)); return; }
        if (n == "charaFaceFade") {
            e.prev_figure_id = e.figure_id;
            e.prev_face = e.face;
            e.face = to_int(c.arg(1));
            e.swap_mix.snap(0);
            e.swap_mix.go(1, num(2, 0), Ease::Linear, WaitCat::CharaFade);
            return;
        }
        if (n == "charaFadein") { fade_chara(e, true, num(1, kCharaFade), c.arg(2), false); return; }
        if (n == "overlayFadein") { fade_chara(e, true, num(1, kCharaFade), c.arg(2), true); return; }
        if (n == "charaFadeout" || n == "overlayFadeout") { fade_chara(e, false, num(1, kCharaFade), {}, false); return; }
        if (n == "charaClear") { e.shown = false; e.opacity.snap(0); return; }
        if (n == "charaFadeTime") {
            const float target = static_cast<float>(std::clamp(num(2, 0), 0.0, 1.0));
            e.shown = target > 0;
            e.opacity.go(target, num(1, 0), Ease::Ease, WaitCat::CharaFade);
            return;
        }
        if (n == "charaPut") {
            if (const auto p = parse_position(c.arg(1))) {
                e.pos.snap(position_offset(*p));
                if (p->scale) e.scale.snap(*p->scale);
            }
            return;
        }
        if (n == "charaMove" || n == "charaMoveReturn") {
            if (const auto p = parse_position(c.arg(1))) {
                const Ease ease = parse_ease(c.arg(3), Ease::Ease);
                e.pos.go(position_offset(*p), num(2, kCharaFade), ease, WaitCat::CharaMove);
                if (p->scale) e.scale.go(*p->scale, num(2, kCharaFade), ease, WaitCat::CharaMove);
            }
            return;
        }
        if (n == "charaScale" || n == "charaMoveScale") {
            e.scale.go(static_cast<float>(num(1, 1)), num(2, 0), parse_ease(c.arg(3), Ease::Ease), WaitCat::CharaMove);
            return;
        }
        if (n == "charaDepth") { e.depth = static_cast<float>(num(1, 0)); return; }
        if (n == "charaLayer") { e.layer = c.arg(1) == "sub" ? c.arg(2) : std::string(); return; }
        if (n == "charaFilter") {
            const std::string& mode = c.arg(1);
            e.filter = (mode.empty() || mode == "normal") ? Filter{} : Filter{mode, parse_color(c.arg(2))};
            return;
        }
        if (n == "charaShadow") {
            const std::string& v = c.arg(1);
            e.shadow = v == "true" || v == "1" || v == "on";
            return;
        }
        if (n == "charaShake") {
            e.shake = Shake{};
            e.shake.active = true;
            e.shake.interval = static_cast<float>(std::max(0.01, num(1, 0.05)));
            e.shake.amp_x = static_cast<float>(std::abs(num(2, 0)));
            e.shake.amp_y = static_cast<float>(std::abs(num(3, 0)));
            e.shake.duration = std::max(0.0, num(4, 0));
            return;
        }
        if (n == "charaShakeStop") { e.shake = Shake{}; return; }
        if (n == "charaRoll") {
            e.roll.snap(static_cast<float>(num(1, 0)));
            if (const auto p = parse_position(c.arg(2))) e.roll_origin = position_offset(*p);
            return;
        }
        if (n == "charaRollMove") { e.roll.go(static_cast<float>(num(2, 0)), num(1, 0), Ease::Ease, WaitCat::CharaMove); return; }
        if (n == "charaRollAxis") { e.roll.go(static_cast<float>(num(2, 0)), num(3, 0), Ease::Ease, WaitCat::CharaMove); return; }
        if (n == "charaAttack") {
            if (const auto p = parse_position(c.arg(2))) e.attack_offset = position_offset(*p);
            e.attack.snap(0);
            e.attack.go(1, num(3, 0.2), c.arg(1) == "quick" ? Ease::EaseOutCubic : Ease::Ease, WaitCat::CharaMove);
            return;
        }
        if (n == "charaCutin" || n == "charaCutinPause") {
            Cutin cut;
            cut.reveal = true;
            cut.pattern = c.has(1) ? c.arg(1) : "leftToRight";
            cut.softness = static_cast<float>(num(3, 1));
            cut.progress.snap(0);
            cut.progress.go(1, num(2, kScreenFade), Ease::Linear, WaitCat::Wipe);
            e.shown = true;
            e.opacity.snap(1);
            e.cutin = cut;
            return;
        }
        if (n == "charaCutout") {
            Cutin cut;
            cut.reveal = false;
            cut.pattern = e.cutin ? e.cutin->pattern : "leftToRight";
            cut.softness = e.cutin ? e.cutin->softness : 1;
            cut.progress.snap(0);
            cut.progress.go(1, num(1, kScreenFade), Ease::Linear, WaitCat::Wipe);
            e.cutin = cut;
            return;
        }
        if (n == "charaEffect" || n == "charaBackEffect") {
            if (!c.has(1)) return;
            Effect fx;
            fx.name = c.arg(1);
            fx.layer = n == "charaBackEffect" ? EffectLayer::CharaBack : EffectLayer::Chara;
            fx.target = e.code;
            std::size_t flip_arg = 2;
            if (c.arg(2).find(',') != std::string::npos) {
                if (const auto p = parse_position(c.arg(2))) fx.pos = p->offset;
                flip_arg = 3;
            }
            const std::string& flip = c.arg(flip_arg);
            fx.flip_h = flip == "H" || flip == "HV" || flip == "VH";
            fx.flip_v = flip == "V" || flip == "HV" || flip == "VH";
            auto& fxs = stage_.effects;
            fxs.erase(std::remove_if(fxs.begin(), fxs.end(), [&](const Effect& o) {
                          return o.name == fx.name && o.target == fx.target && o.layer == fx.layer;
                      }), fxs.end());
            fxs.push_back(fx);
            return;
        }
        if (n == "charaEffectStop" || n == "charaEffectDestroy" || n == "charaBackEffectStop" ||
            n == "charaBackEffectDestroy" || n == "charaEffectEdgeBlurStop" || n == "charaEffectEdgeBlurDestroy") {
            const std::string name = (n.find("EdgeBlur") != std::string_view::npos) ? "edge_blur" : c.arg(1);
            auto& fxs = stage_.effects;
            fxs.erase(std::remove_if(fxs.begin(), fxs.end(), [&](const Effect& o) {
                          return o.target == e.code && (name.empty() || o.name == name);
                      }), fxs.end());
            return;
        }
        if (n == "charaEffectEdgeBlur") {
            Effect fx;
            fx.name = "edge_blur";
            fx.layer = EffectLayer::Chara;
            fx.target = e.code;
            stage_.effects.push_back(fx);
            return;
        }
        if (n == "charaSpecialEffect") {
            static const std::set<std::string, std::less<>> outgoing = {
                "erasure", "enemyerasure", "darkenemyerasure", "flasherasure", "erasurereverse", "wipe", "darkwipe", "wipetime"};
            static const std::set<std::string, std::less<>> incoming = {"appearance", "appearancereverse", "wipetimere"};
            std::string mode = c.arg(1);
            std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char ch) { return std::tolower(ch); });
            const double t = has_num(3) ? num(3, 2) : 2.0;
            if (outgoing.count(mode)) fade_chara(e, false, t, {}, false);
            else if (incoming.count(mode)) fade_chara(e, true, t, c.arg(2), false);
            return;
        }
        return;
    }

    // ---- effects
    if (n == "effect" || n == "fowardEffect" || n == "forwardEffect" || n == "backEffect") {
        const std::string& name = c.arg(0);
        if (name.empty()) return;
        if (starts_with(name, "bit_chapterstart") || starts_with(name, "bit_queststart")) {
            stage_.title_card = TitleCard{starts_with(name, "bit_chapterstart"), name, 0, 2.6};
            return;
        }
        Effect fx;
        fx.name = name;
        fx.layer = n == "backEffect" ? EffectLayer::Back : EffectLayer::Front;
        std::size_t flip_arg = 1;
        if (c.arg(1).find(',') != std::string::npos) {
            if (const auto p = parse_position(c.arg(1))) fx.pos = p->offset;
            flip_arg = 2;
        }
        const std::string& flip = c.arg(flip_arg);
        fx.flip_h = flip == "H" || flip == "HV" || flip == "VH";
        fx.flip_v = flip == "V" || flip == "HV" || flip == "VH";
        stage_.effects.push_back(fx);
        return;
    }
    if (n == "effectStop" || n == "effectDestroy" || n == "fowardEffectStop" || n == "fowardEffectDestroy" ||
        n == "forwardEffectStop" || n == "forwardEffectDestroy" || n == "backEffectStop" || n == "backEffectDestroy") {
        const bool back_layer = starts_with(n, "back");
        auto& fxs = stage_.effects;
        fxs.erase(std::remove_if(fxs.begin(), fxs.end(), [&](const Effect& o) {
                      const bool layer_ok = back_layer ? o.layer == EffectLayer::Back : o.layer == EffectLayer::Front;
                      return layer_ok && (c.arg(0).empty() || o.name == c.arg(0));
                  }), fxs.end());
        if (stage_.title_card && (c.arg(0).empty() || stage_.title_card->effect == c.arg(0))) stage_.title_card.reset();
        return;
    }

    // ---- background and screen
    if (n == "scene" || n == "masterScene") {
        const std::string id = n == "masterScene" ? (female && c.has(1) ? c.arg(1) : c.arg(0)) : c.arg(0);
        const double t = n == "scene" ? num(1, 0) : 0;
        if (t > 0 && !stage_.background.empty() && stage_.background != id) {
            stage_.prev_background = stage_.background;
            stage_.background_mix.snap(0);
            stage_.background_mix.go(1, t, Ease::Linear, WaitCat::Scene);
        } else {
            stage_.prev_background.clear();
            stage_.background_mix.snap(1);
        }
        stage_.background = id;
        return;
    }
    if (n == "fadein" || n == "maskin" || n == "fadeout" || n == "maskout") {
        const bool reveal = n == "fadein" || n == "maskin";
        stage_.fade.color = parse_color(c.has(0) ? c.arg(0) : "black").value_or(Rgba{});
        if (reveal && stage_.fade.opacity.value() <= 0 && !stage_.fade.opacity.active()) stage_.fade.opacity.snap(1);
        stage_.fade.opacity.go(reveal ? 0.f : 1.f, num(1, kScreenFade), Ease::Linear, WaitCat::Fade);
        return;
    }
    if (n == "fadeMove") {
        stage_.fade.color = parse_color(c.has(0) ? c.arg(0) : "black").value_or(Rgba{});
        stage_.fade.opacity.go(static_cast<float>(std::clamp(num(2, 1), 0.0, 1.0)), num(1, kScreenFade), Ease::Linear,
                               WaitCat::Fade);
        return;
    }
    if (n == "wipein" || n == "wipeout") {
        auto& w = stage_.wipe;
        w.pattern = c.has(0) ? c.arg(0) : "leftToRight";
        w.reveal = n == "wipein";
        w.softness = static_cast<float>(std::max(0.0, num(2, 1)));
        if (w.reveal && w.cover.value() <= 0 && !w.cover.active()) w.cover.snap(1);
        w.cover.go(w.reveal ? 0.f : 1.f, num(1, kScreenFade), Ease::Linear, WaitCat::Wipe);
        return;
    }
    if (n == "wipeFilter") {
        stage_.wipe_filter = {true, c.has(0) ? c.arg(0) : "cinema", static_cast<float>(std::clamp(num(1, 0), 0.0, 1.0)),
                              static_cast<float>(std::max(0.0, num(2, 1)))};
        return;
    }
    if (n == "wipeOff") { stage_.wipe_filter = WipeFilter{}; return; }
    if (n == "flashin") {
        Flash f;
        f.active = true;
        f.loop = c.arg(0) == "loop";
        const bool four = c.has(2) && has_num(2);
        f.duration = std::max(0.0, four ? num(2, 0.2) : num(1, 0.2));
        f.rise = (!f.loop && four) ? std::max(0.0, num(1, 0)) : 0;
        f.from = parse_color(c.arg(3)).value_or(Rgba{255, 255, 255, 128});
        f.to = parse_color(c.arg(4)).value_or(Rgba{255, 255, 255, 0});
        stage_.flash = f;
        return;
    }
    if (n == "flashout" || n == "flashOff") {
        auto& f = stage_.flash;
        if (!f.active) return;
        const double t = num(0, 0);
        if (t <= 0) { f.active = false; return; }
        f.fading_out = true;
        f.out.snap(1);
        f.out.go(0, t, Ease::Linear, WaitCat::Flash);
        return;
    }
    if (n == "shake") {
        Shake s;
        s.active = true;
        s.interval = static_cast<float>(std::max(0.01, num(0, 0.05)));
        s.amp_x = static_cast<float>(std::abs(num(1, 0)));
        s.amp_y = static_cast<float>(std::abs(num(2, 0)));
        s.duration = std::max(0.0, num(3, 0));
        stage_.shake = s;
        return;
    }
    if (n == "shakeStop") { stage_.shake = Shake{}; return; }
    if (n == "messageShake") {
        Shake s;
        s.active = true;
        s.interval = static_cast<float>(std::max(0.01, num(0, 0.05)));
        s.amp_x = static_cast<float>(std::abs(num(1, 0)));
        s.amp_y = static_cast<float>(std::abs(num(2, 0)));
        s.duration = std::max(0.0, num(3, 0));
        stage_.message_shake = s;
        return;
    }
    if (n == "cameraMove" || n == "cameraMoveEase") {
        const bool ease_form = n == "cameraMoveEase";
        const auto p = parse_position(ease_form ? c.arg(0) : c.arg(1));
        const double t = ease_form ? num(1, 0) : num(0, 0);
        const Ease ease = ease_form ? parse_ease(c.arg(2), Ease::Ease) : Ease::Ease;
        const std::size_t scale_arg = ease_form ? 3 : 2;
        auto& cam = stage_.camera;
        if (p) cam.offset.go(p->offset, t, ease, WaitCat::Camera);
        std::optional<float> s;
        if (has_num(scale_arg) && num(scale_arg, 0) > 0) s = static_cast<float>(num(scale_arg, 1));
        else if (p && p->scale && *p->scale > 0) s = p->scale;
        if (s) cam.scale.go(*s, t, ease, WaitCat::Camera);
        return;
    }
    if (n == "cameraHome") {
        const double t = num(0, 0);
        auto& cam = stage_.camera;
        cam.offset.go({0, 0}, t, Ease::Ease, WaitCat::Camera);
        cam.scale.go(1, t, Ease::Ease, WaitCat::Camera);
        cam.roll.go(0, t, Ease::Ease, WaitCat::Camera);
        return;
    }
    if (n == "cameraRoll") { stage_.camera.roll.snap(static_cast<float>(num(0, 0))); return; }
    if (n == "cameraRollMove") {
        stage_.camera.roll.go(static_cast<float>(num(1, 0)), num(0, 0), Ease::Ease, WaitCat::Camera);
        return;
    }
    if (n == "cameraFilter") {
        const std::string& f = c.arg(0);
        stage_.camera.filter = (f.empty() || f == "normal") ? std::string() : f;
        stage_.camera.filter_intensity = static_cast<float>(num(1, 1));
        return;
    }
    if (n == "blur") {
        auto& cam = stage_.camera;
        cam.blur_mode = c.has(0) ? c.arg(0) : "lens";
        const double raw = has_num(3) ? num(3, 10) : num(2, 10);
        const double div = cam.blur_mode.find("motion") != std::string::npos ? 100 : 3;
        cam.blur.go(static_cast<float>(std::clamp(raw / div, 0.8, 14.0)), num(1, 0.2), Ease::Linear, WaitCat::Camera);
        return;
    }
    if (n == "blurOff") {
        stage_.camera.blur.go(0, has_num(0) ? num(0, 0.2) : num(1, 0.2), Ease::Linear, WaitCat::Camera);
        return;
    }
    if (n == "pictureFrame" || n == "pictureFrameTop") {
        stage_.picture_frame = c.arg(0);
        stage_.picture_frame_top = n == "pictureFrameTop";
        return;
    }
    if (n == "pictureFrameOff" || n == "pictureFrameClear") { stage_.picture_frame.clear(); return; }
    if (n == "messageOff") { stage_.message_visible = false; return; }
    if (n == "messageOn") { stage_.message_visible = true; return; }

    // ---- audio
    if (n == "bgm") {
        if (!c.has(0)) return;
        bgm_ = c.arg(0);
        emit({AudioEvent::Kind::BgmPlay, bgm_, {}, num(1, 0), static_cast<float>(std::clamp(num(2, 1), 0.0, 1.0)), true});
        return;
    }
    if (n == "bgmStop" || n == "bgmStopEnd") {
        double fade = 0;
        for (std::size_t i = c.args.size(); i-- > 0;)
            if (has_num(i)) { fade = num(i, 0); break; }
        bgm_.clear();
        emit({AudioEvent::Kind::BgmStop, c.arg(0), {}, fade, 1, false});
        return;
    }
    if (n == "se" || n == "seLoop" || n == "cueSe" || n == "cueSeLoop") {
        const bool cue = starts_with(n, "cueSe");
        const std::size_t name_i = cue ? 1 : 0;
        if (!c.has(name_i)) return;
        AudioEvent e{AudioEvent::Kind::SePlay, c.arg(name_i), cue ? c.arg(0) : std::string(), 0, 1,
                     n == "seLoop" || n == "cueSeLoop"};
        if (c.has(name_i + 2)) {
            e.fade = num(name_i + 1, 0);
            e.volume = static_cast<float>(std::clamp(num(name_i + 2, 1), 0.0, 1.0));
        } else if (c.has(name_i + 1)) {
            e.volume = static_cast<float>(std::clamp(num(name_i + 1, 1), 0.0, 1.0));
        }
        emit(e);
        return;
    }
    if (n == "seStop" || n == "cueSeStop") {
        std::size_t key_i = 0;
        if (n == "cueSeStop" && c.has(1) && !has_num(1)) key_i = 1;
        if (!c.has(key_i)) return;
        double fade = 0;
        for (std::size_t i = c.args.size(); i-- > key_i + 1;)
            if (has_num(i)) { fade = num(i, 0); break; }
        emit({AudioEvent::Kind::SeStop, c.arg(key_i), {}, fade, 1, false});
        return;
    }
    if (n == "seVolume" || n == "cueSeVolume") {
        std::size_t key_i = 0;
        if (n == "cueSeVolume" && c.has(1) && !has_num(1)) key_i = 1;
        if (!c.has(key_i)) return;
        AudioEvent e{AudioEvent::Kind::SeVolume, c.arg(key_i), {}, 0, 1, false};
        if (c.has(key_i + 2)) {
            e.fade = num(key_i + 1, 0);
            e.volume = static_cast<float>(std::clamp(num(key_i + 2, 1), 0.0, 1.0));
        } else {
            e.volume = static_cast<float>(std::clamp(num(key_i + 1, 1), 0.0, 1.0));
        }
        emit(e);
        return;
    }
    if (n == "soundStopAll" || n == "soundStopAllEnd" || n == "soundStopAllFade" || n == "seStopAll") {
        emit({AudioEvent::Kind::SeStopAll, {}, {}, num(0, 0), 1, false});
        return;
    }
    if (n == "voice") { pending_voice_ = c.arg(0); return; }
    if (n == "voiceStop") { leave_say(); return; }
    if (n == "criMovie" || n == "movie" || n == "moviePlay" || n == "playMovie") {
        if (!c.has(0) || skipping) return;
        std::string id = c.arg(0);
        const std::size_t slash = id.rfind('/');
        if (slash != std::string::npos) id = id.substr(slash + 1);
        emit({AudioEvent::Kind::Movie, id, {}, 0, 1, false});
        if (status_ != Status::Typing) {
            waiting_movie_ = true;
            status_ = Status::Waiting;
        }
        return;
    }
    // input / skip / anything unsupported: nothing to do
}

}  // namespace vnr
