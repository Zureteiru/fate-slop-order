#include "vnr/rt/app.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <set>

#include "vnr/rt/json.h"
#include "vnr/rt/picker.h"
#include "vnr/script_lines.h"

namespace vnr::rt {

// The game font from the user's folder when it is there and readable, else the OS font.
App::FontChoice App::pick_font(Platform& p) {
    if (p.user) {
        std::vector<std::uint8_t> bytes;
        if (p.user->read(kGameFontKey, bytes)) {
            if (GlyphAtlas::usable(bytes, 0)) return {std::move(bytes), 0, true};
            p.log.write(std::string(kGameFontKey) + " could not be opened; using the system font");
        }
    }
    return {p.font, p.font_index, false};
}

App::App(Platform& platform, AppConfig config)
    : platform_(platform),
      config_(std::move(config)),
      cache_(platform_, textures_),
      audio_(cache_, platform_.log, config_.region),
      font_(pick_font(platform_)),
      atlas_(textures_, font_.bytes, font_.index) {
    font_.bytes = {};
    scene_.region = config_.region;
    scene_.cache = &cache_;
    scene_.fx_atlas = textures_.create(make_effect_atlas());
    if (!atlas_.ok()) platform_.log.write("font could not be loaded: text will not be drawn");
    if (platform_.user) scene_.skin = load_ui_skin(*platform_.user, textures_, platform_.log);
    if (!scene_.skin.has_talk()) platform_.log.write("no UI sheets (ui/003.png, ui/030.png): using plain boxes");
    if (config_.picker) {
        picker_ = std::make_unique<Picker>(platform_, cache_, atlas_);
        phase_ = Phase::Picker;
    } else {
        for (const auto& id : config_.script_ids) cache_.request_bytes(script_url(id, config_.region));
    }
    if (platform_.user) {
        std::vector<std::uint8_t> settings;
        if (platform_.user->read(kSettingsKey, settings)) audio_.set_volumes(parse_audio_settings(settings));
        std::vector<std::uint8_t> click;
        if (platform_.user->read(kChoiceSoundKey, click) && !click.empty())
            choice_sound_ = std::make_shared<const std::vector<std::uint8_t>>(std::move(click));
    }
    std::vector<std::uint8_t> saves;
    Files* sf = platform_.user ? platform_.user : &platform_.files;
    if (sf->read(config_.picker ? kSavesKey : kScriptSavesKey, saves)) saves_ = parse_saves(std::string(saves.begin(), saves.end()));
}

App::~App() {
    if (autosave_dirty_) write_saves();  // quitting: keep the line being read
}

std::string App::status() const {
    switch (phase_) {
        case Phase::Picker: return picker_ ? picker_->status() : std::string();
        case Phase::LoadingScripts: return "Loading scripts...";
        case Phase::LoadingAssets: {
            const std::size_t done = assets_total_ - std::min(assets_total_, cache_.pending());
            return "Loading images " + std::to_string(done) + "/" + std::to_string(assets_total_);
        }
        case Phase::Playing:
            return config_.script_ids[script_index_] + " line " + std::to_string(player_ ? player_->line() : 0);
        case Phase::Finished: return "End of part";
    }
    return {};
}

void App::start_part(const PartChoice& part, std::optional<SaveEntry> resume) {
    if (autosave_dirty_) write_saves();
    pending_resume_ = std::move(resume);
    save_screen_.close();
    stop_script_ = stop_pc_ = SIZE_MAX;
    part_ = part;
    scene_.titles = {part.chapter_label, part.chapter_title, part.section_label, part.section_title};
    config_.region = part.region;
    config_.script_ids = part.script_ids;
    script_text_.clear();
    programs_.clear();
    player_.reset();
    figure_ids_.clear();
    assets_total_ = 0;
    script_index_ = 0;
    scene_.figures.clear();
    scene_.masks.clear();
    mask_names_.clear();
    scene_.region = part.region;
    log_.close();
    backdrop_.clear_all();
    audio_.reset(part.region, 0.5);
    for (const auto& id : config_.script_ids) cache_.request_bytes(script_url(id, config_.region));
    platform_.log.write("part: " + part.chapter_id + " #" + std::to_string(part.section) + " " + part.label + " (" +
                        (part.region == Region::JP ? "JP" : "NA") + ", " + std::to_string(part.script_ids.size()) + " scripts)");
    phase_ = Phase::LoadingScripts;
}

// Back to the picker: the end card after the last script, or the chapter page when the reader left early.
void App::leave_part(bool finished) {
    if (finished) {  // read to the end: nothing to resume in this part
        saves_.autosave.reset();
        autosave_dirty_ = true;
    }
    if (autosave_dirty_) write_saves();
    save_screen_.close();
    player_.reset();
    log_.close();
    audio_.stop_all(1.0);
    if (!finished) backdrop_.clear_all();
    phase_ = Phase::Picker;
    if (finished) picker_->part_finished(part_);
    else picker_->part_left(part_);
}

void App::load_scripts() {
    for (const auto& id : config_.script_ids)
        if (cache_.state(script_url(id, config_.region)) == AssetState::Loading) return;
    for (const auto& id : config_.script_ids) {
        const auto* bytes = cache_.bytes(script_url(id, config_.region));
        std::string text = bytes ? std::string(bytes->begin(), bytes->end()) : std::string();
        if (!bytes) platform_.log.write("script " + id + " could not be downloaded");
        Diagnostics diag;
        script_text_.push_back(text);
        programs_.push_back(compile(text, config_.settings.text, diag));
        for (const auto& [name, n] : diag.unknown_commands)
            platform_.log.write(id + ": unsupported command " + name + " x" + std::to_string(n));
    }
    request_assets();
    phase_ = Phase::LoadingAssets;
}

// Wipe textures are local user files (tools/install_wipes.sh); a missing one uses a built-in field.
void App::load_wipes(const std::set<std::string>& patterns) {
    if (!platform_.user) return;
    for (const std::string& pat : patterns) {
        const std::string file = wipe_texture_file(pat);
        if (file.empty() || wipes_.installed(pat)) continue;
        std::vector<std::uint8_t> bytes;
        Image img;
        if (!platform_.user->read(file, bytes)) continue;
        if (!decode_image(bytes, img)) {
            platform_.log.write(file + " could not be decoded; using the built-in wipe");
            continue;
        }
        wipes_.install(pat, wipe_field_from_image(img));
    }
}

void App::request_assets() {
    std::set<int> figures;
    std::set<std::string> backgrounds, images, masks, wipes;
    bool gray = false, sepia = false;
    const bool female = config_.settings.text.gender == Gender::Female;
    for (const Program& p : programs_) {
        for (const Instr& in : p.code) {
            const auto* ex = std::get_if<ExecOp>(&in);
            if (!ex) continue;
            const Command& c = ex->cmd;
            const std::string& n = c.name;
            if (n == "charaSet" || n == "charaChange" || n == "charaCrossFade") figures.insert(std::atoi(c.arg(1).c_str()));
            else if (n == "masterSet") figures.insert(std::atoi((female ? c.arg(2) : c.arg(1)).c_str()));
            else if (n == "communicationChara" || n == "communicationCharaLoop") figures.insert(std::atoi(c.arg(0).c_str()));
            else if (n == "scene") backgrounds.insert(c.arg(0));
            else if (n == "masterScene") backgrounds.insert(female && c.has(1) ? c.arg(1) : c.arg(0));
            else if (n == "sceneSet") backgrounds.insert(c.arg(1));
            else if (n == "imageSet" || n == "verticalImageSet" || n == "horizontalImageSet" || n == "imageChange") images.insert(c.arg(1));
            else if (n == "subCameraFilter" && c.arg(1) == "maskEdge" && !c.arg(2).empty()) masks.insert(c.arg(2));
            else if (n == "wipein" || n == "wipeout") wipes.insert(c.has(0) ? c.arg(0) : "leftToRight");
            else if ((n == "pictureFrame" || n == "pictureFrameTop") && c.has(0)) images.insert(c.arg(0));
            else if (n == "cameraFilter" && c.arg(0) == "gray") gray = true;
            else if (n == "effect" || n == "fowardEffect" || n == "forwardEffect" || n == "backEffect" ||
                     n == "charaEffect" || n == "charaBackEffect") {
                const EffectDef* fx = find_effect(c.arg(n == "charaEffect" || n == "charaBackEffect" ? 1 : 0));
                sepia = sepia || (fx && fx->program == FxProgram::Backdrop);
            }
            else if (n == "wipeFilter") wipes.insert(c.has(0) ? c.arg(0) : "cinema");
        }
    }
    load_wipes(wipes);
    cache_.set_filter_copies(gray, sepia);
    figures.erase(0);
    figure_ids_.assign(figures.begin(), figures.end());
    for (int f : figure_ids_) {
        cache_.request_texture(figure_url(f, config_.region));
        // body size: the first 32 bytes of the plain figure PNG (as Atlas DB does)
        const std::string body = base_figure_url(f);
        cache_.request_bytes(body, "0-31");
    }
    if (!figure_ids_.empty()) cache_.request_bytes(svt_script_url(figure_ids_, config_.region));
    for (const auto& b : backgrounds)
        if (!b.empty()) cache_.request_texture(background_url(b, config_.region));
    for (const auto& i : images)
        if (!i.empty()) cache_.request_texture(image_url(i, config_.region));
    mask_names_.assign(masks.begin(), masks.end());
    for (const auto& m : mask_names_) cache_.request_bytes(image_url(m, config_.region));  // decoded on the CPU
    assets_total_ = cache_.pending();
}

std::string App::base_figure_url(int figure_id) const {
    std::string u = figure_url(figure_id, config_.region);
    const std::string merged = "_merged.png";
    if (u.size() > merged.size()) u.replace(u.size() - merged.size(), merged.size(), ".png");
    return u;
}

void App::finish_assets() {
    if (!figure_ids_.empty()) {
        if (const auto* svt = cache_.bytes(svt_script_url(figure_ids_, config_.region))) {
            if (const auto json = Json::parse(std::string(svt->begin(), svt->end()))) {
                for (const Json& row : json->items()) {
                    FigureInfo& fi = scene_.figures[row["id"].integer()];
                    fi.meta.face_x = row["faceX"].integer();
                    fi.meta.face_y = row["faceY"].integer();
                    fi.meta.offset_x = row["offsetX"].integer();
                    fi.meta.offset_y = row["offsetY"].integer();
                    const Json& ext = row["extendData"];
                    if (ext["faceSizeRect"].is_array()) {
                        fi.meta.face_w = ext["faceSizeRect"][0].integer(kDefaultFaceSize);
                        fi.meta.face_h = ext["faceSizeRect"][1].integer(kDefaultFaceSize);
                    } else if (ext["faceSize"].type() == Json::Type::Number) {
                        fi.meta.face_w = fi.meta.face_h = ext["faceSize"].integer(kDefaultFaceSize);
                    }
                }
            } else {
                platform_.log.write("svtScript JSON could not be parsed");
            }
        }
    }
    for (const auto& name : mask_names_) {
        const std::vector<std::uint8_t> bytes = cache_.take_bytes(image_url(name, config_.region));
        Image img;
        if (bytes.empty() || !decode_image(bytes, img)) {
            platform_.log.write("mask " + name + " could not be loaded; that layer draws unclipped");
            continue;
        }
        LayerMask m = build_layer_mask(img);
        if (!m.ok) platform_.log.write("mask " + name + " has no window; that layer draws unclipped");
        else scene_.masks[name] = std::move(m);
    }
    for (int f : figure_ids_) {
        FigureInfo& fi = scene_.figures[f];
        if (const auto* head = cache_.bytes(base_figure_url(f), "0-31")) {
            int w = 0, h = 0;
            if (png_size(*head, w, h)) {
                fi.body_width = w;
                fi.body_height = h;
            }
        }
    }
    for (const Program& p : programs_) audio_.prefetch(p);  // in the background, in script order
    if (!pending_resume_) {
        start_script(0);
        return;
    }
    const SaveEntry e = std::move(*pending_resume_);
    pending_resume_.reset();
    std::size_t first = e.script_index;
    if (first >= config_.script_ids.size() || config_.script_ids[first] != e.script_id) {
        const auto it = std::find(config_.script_ids.begin(), config_.script_ids.end(), e.script_id);
        first = it == config_.script_ids.end() ? SIZE_MAX : static_cast<std::size_t>(it - config_.script_ids.begin());
    }
    if (first == SIZE_MAX) {
        platform_.log.write("load: script " + e.script_id + " is no longer in this part; starting from its top");
        start_script(0);
        return;
    }
    start_script(first);
    if (!player_) return;
    const auto rp = resume_point(e, programs_[first], fnv1a(script_text_[first]));
    if (rp && rp->stop_pc != e.stop_pc)
        platform_.log.write("load: script " + e.script_id + " changed; resuming at line " + std::to_string(e.line));
    if (!rp || !player_->resume(rp->stop_pc, rp->choices)) {
        platform_.log.write("load: line " + std::to_string(e.line) + " of " + e.script_id +
                            " not found; starting that script from its top");
        player_ = std::make_unique<Player>(programs_[first], config_.settings);
    }
    player_->settings().auto_mode = e.auto_mode;
}

void App::start_script(std::size_t index) {
    script_index_ = index;
    if (index >= programs_.size()) {
        if (picker_) {
            backdrop_.quads = draw_.quads;  // the last frame drawn, dimmed under the end card
            leave_part(true);
            return;
        }
        phase_ = Phase::Finished;
        player_.reset();
        log_.close();
        audio_.stop_all(1.0);
        return;
    }
    const bool carry = player_ != nullptr;  // AUTO keeps its state across the scripts of a part
    const bool auto_mode = carry && player_->settings().auto_mode;
    player_ = std::make_unique<Player>(programs_[index], config_.settings);
    if (carry) player_->settings().auto_mode = auto_mode;
    phase_ = Phase::Playing;
}

void App::handle_log(const InputEvent& e) {
    switch (e.kind) {
        case InputEvent::Kind::Pointer: log_.press(e.pos); break;
        case InputEvent::Kind::PointerMove: log_.drag(e.pos); break;
        case InputEvent::Kind::PointerUp:
            if (log_.release(e.pos)) log_.close();
            break;
        case InputEvent::Kind::Scroll: log_.scroll_by(e.pos.y); break;
        case InputEvent::Kind::Advance:
        case InputEvent::Kind::Back:
        case InputEvent::Kind::OpenLog:
        case InputEvent::Kind::Cancel: log_.close(); break;
        case InputEvent::Kind::OpenSave:
        case InputEvent::Kind::OpenLoad:
            log_.close();
            open_save_screen(e.kind == InputEvent::Kind::OpenSave ? SaveScreen::Mode::Save : SaveScreen::Mode::Load);
            break;
        default: break;  // the story is paused under the log
    }
}

void App::choose(std::size_t index) {
    if (player_->choose(index) && choice_sound_) audio_.play_ui("choice", choice_sound_, kChoiceSoundVolume);
}

bool App::ensure_dict() {
    if (dict_.loaded()) return true;
    if (!dict_tried_) {  // read once: a missing or bad file is not re-read on every press
        dict_tried_ = true;
        std::vector<std::uint8_t> bytes;
        if (!platform_.user || !platform_.user->read(kDictKey, bytes))
            platform_.log.write("dictionary: no dict.bin in the user folder (tools/install_dict.sh)");
        else if (!dict_.load(std::move(bytes)))
            platform_.log.write("dictionary: dict.bin is not a vnpop dictionary");
        if (dict_.loaded()) return true;
    }
    toast("No dictionary (tools/install_dict.sh)");
    return false;
}

bool App::handle_lookup(const InputEvent& e) {
    using K = InputEvent::Kind;
    if (swallow_pointer_ && (e.kind == K::PointerMove || e.kind == K::PointerUp)) {
        if (e.kind == K::PointerUp) swallow_pointer_ = false;
        return true;
    }
    if (e.kind == K::LookupAt) {
        if (lookup_.contains(e.pos)) return true;  // moving onto the popup keeps it
        const std::vector<LookupTarget> story{story_target()};
        for (const LookupTarget& t : log_.is_open() ? log_targets_ : story) {
            const int i = box_at(t.boxes, e.pos);
            if (i < 0) continue;
            if (lookup_.is_open() && lookup_.target_id() == t.id && lookup_.start() == static_cast<std::size_t>(i))
                return true;
            if (ensure_dict()) lookup_.open(dict_, t, static_cast<std::size_t>(i));  // closes when nothing matches
            return true;
        }
        lookup_.close();  // Shift over no text
        return true;
    }
    if (e.kind == K::Lookup) {
        if (lookup_.is_open()) {
            lookup_.close();
            return true;
        }
        if (!ensure_dict()) return true;
        if (log_.is_open()) {  // the lowest entry on screen: the newest one shown
            const LookupTarget* best = nullptr;
            for (const LookupTarget& t : log_targets_)
                if (!t.boxes.empty() && (!best || t.boxes.back().rect.y > best->boxes.back().rect.y)) best = &t;
            if (best) lookup_.open_first(dict_, *best);
        } else {
            lookup_.open_first(dict_, story_target());
        }
        if (!lookup_.is_open()) toast("No dictionary words here");
        return true;
    }
    if (!lookup_.is_open()) return false;
    switch (e.kind) {
        case K::Nav:
            switch (static_cast<NavDir>(e.index)) {
                case NavDir::Left: lookup_.hop(dict_, -1); break;
                case NavDir::Right: lookup_.hop(dict_, +1); break;
                case NavDir::Up: lookup_.select(-1); break;
                case NavDir::Down: lookup_.select(+1); break;
            }
            return true;
        case K::Scroll: lookup_.scroll(e.pos.y); return true;
        case K::Pointer:  // a press outside the popup closes it (and does nothing else)
            if (!lookup_.contains(e.pos)) lookup_.close();
            swallow_pointer_ = true;
            return true;
        case K::PointerMove:
        case K::PointerUp: return true;
        case K::Advance:
        case K::Back:
        case K::Cancel: lookup_.close(); return true;
        case K::VolumeDown:
        case K::VolumeUp:
        case K::Mute: return false;  // handled before; keeps the popup
        default:  // LOG, AUTO, save keys...: close the popup and do their usual thing
            lookup_.close();
            return false;
    }
}

bool App::lookup_text(const std::string& text) {
    std::vector<char32_t> needle;
    for (std::size_t i = 0; i < text.size();) needle.push_back(utf8::next(text, i));
    if (needle.empty() || !ensure_dict()) return false;
    for (std::size_t s = 0; s + needle.size() <= story_boxes_.size(); ++s) {
        std::size_t k = 0;
        while (k < needle.size() && story_boxes_[s + k].cp == needle[k]) ++k;
        if (k == needle.size()) return lookup_.open(dict_, story_target(), s);
    }
    return false;
}

void App::handle(const InputEvent& e) {
    if (!player_) return;
    if (handle_lookup(e)) return;
    if (e.kind == InputEvent::Kind::Nav) {  // arrows / d-pad while reading: as before the picker existed
        InputEvent t = e;
        switch (static_cast<NavDir>(e.index)) {
            case NavDir::Right: t.kind = InputEvent::Kind::Advance; break;
            case NavDir::Left: t.kind = InputEvent::Kind::Back; break;
            case NavDir::Up: t = {InputEvent::Kind::Scroll, {0, -kNavScrollStep}, 0}; break;
            case NavDir::Down: t = {InputEvent::Kind::Scroll, {0, kNavScrollStep}, 0}; break;
        }
        handle(t);
        return;
    }
    if (log_.is_open()) {
        handle_log(e);
        return;
    }
    switch (e.kind) {
        case InputEvent::Kind::Advance:
            player_->advance();
            break;
        case InputEvent::Kind::Back:
            if (player_->back()) audio_.on_back();  // the BGM of that line follows from the Player
            break;
        case InputEvent::Kind::Choose:
            choose(e.index);
            break;
        case InputEvent::Kind::Pointer: {
            const TextView tv = player_->text();
            const StoryButton b = story_button_at(ui_, e.pos, tv.visible && tv.say);
            if (b == StoryButton::Log) {  // buttons take the tap: it does not also advance
                log_.open(player_->backlog(), atlas_);
                break;
            }
            if (b == StoryButton::Auto) {
                player_->settings().auto_mode = !player_->settings().auto_mode;
                break;
            }
            if (b == StoryButton::Save || b == StoryButton::Load) {
                open_save_screen(b == StoryButton::Save ? SaveScreen::Mode::Save : SaveScreen::Mode::Load);
                break;
            }
            if (const ChoiceOp* c = player_->pending_choice()) {
                const auto rects = choice_rects(*c, ui_, tv.visible);
                for (std::size_t i = 0; i < rects.size(); ++i) {
                    const Rect& r = rects[i];
                    if (e.pos.x >= r.x && e.pos.x <= r.x + r.w && e.pos.y >= r.y && e.pos.y <= r.y + r.h) {
                        choose(i);
                        break;
                    }
                }
            } else {
                player_->advance();
            }
            break;
        }
        case InputEvent::Kind::ToggleAuto:
            player_->settings().auto_mode = !player_->settings().auto_mode;
            break;
        case InputEvent::Kind::OpenLog:
            log_.open(player_->backlog(), atlas_);
            break;
        case InputEvent::Kind::Cancel:
            if (picker_) leave_part(false);
            else quit_ = true;
            break;
        case InputEvent::Kind::SkipScript:
            player_->skip();
            break;
        case InputEvent::Kind::OpenSave: open_save_screen(SaveScreen::Mode::Save); break;
        case InputEvent::Kind::OpenLoad: open_save_screen(SaveScreen::Mode::Load); break;
        case InputEvent::Kind::QuickSave:
            if (!save_to(kQuickSlot)) toast("Nothing to save yet");
            break;
        case InputEvent::Kind::QuickLoad:
            if (!load_save(kQuickSlot)) toast("No quick save");
            break;
        case InputEvent::Kind::Nav:
        case InputEvent::Kind::PointerMove:
        case InputEvent::Kind::PointerUp:
        case InputEvent::Kind::Scroll:
        case InputEvent::Kind::VolumeDown:
        case InputEvent::Kind::VolumeUp:
        case InputEvent::Kind::Mute:
        case InputEvent::Kind::DeleteSave:
        case InputEvent::Kind::LookupAt:  // handle_lookup() takes these
        case InputEvent::Kind::Lookup:
            break;
    }
}

// ---- saves

std::optional<SaveEntry>& App::slot_ref(int slot) {
    static std::optional<SaveEntry> none;
    if (slot == kAutosaveSlot) return saves_.autosave;
    if (slot == kQuickSlot) return saves_.quick;
    if (slot >= 0 && static_cast<std::size_t>(slot) < kSaveSlots) return saves_.slots[static_cast<std::size_t>(slot)];
    none.reset();
    return none;
}

std::optional<SaveEntry> App::capture() const {
    if (phase_ != Phase::Playing || !player_ || script_index_ >= config_.script_ids.size()) return std::nullopt;
    const Status st = player_->status();
    if (st != Status::Typing && st != Status::AwaitAdvance && st != Status::AwaitChoice) {
        // between lines (a wait): the last line reached, which the autosave holds
        if (stop_script_ == SIZE_MAX) return std::nullopt;
        return saves_.autosave;
    }
    SaveEntry e;
    e.chapter_id = part_.chapter_id.empty() ? "scripts" : part_.chapter_id;  // --scripts runs have no chapter
    e.section = part_.section;
    e.region = config_.region;
    e.chapter_label = part_.chapter_label;
    e.part_label = part_.label;
    e.script_index = script_index_;
    e.script_id = config_.script_ids[script_index_];
    e.script_hash = script_index_ < script_text_.size() ? fnv1a(script_text_[script_index_]) : 0;
    e.stop_pc = player_->pc();
    e.choices = player_->choice_path();
    e.line = player_->line();
    if (const ChoiceOp* c = player_->pending_choice()) {
        std::string opts;
        for (const ChoiceOption& o : c->options) opts += (opts.empty() ? "" : " / ") + o.text.plain();
        e.text = preview_text(opts);
    } else if (const SayOp* say = player_->text().say) {
        e.line = say->line;
        e.speaker = preview_text(say->speaker.plain(), 20);
        e.text = preview_text(say->text.plain());
    }
    e.background = player_->stage().background;
    e.auto_mode = player_->settings().auto_mode;
    e.saved_at = static_cast<std::int64_t>(std::time(nullptr));
    return e;
}

bool App::save_to(int slot) {
    if (slot == kAutosaveSlot) return false;
    auto e = capture();
    if (!e) return false;
    e->saved_at = static_cast<std::int64_t>(std::time(nullptr));
    slot_ref(slot) = std::move(e);
    write_saves();
    toast(slot == kQuickSlot ? "Quick saved" : "Saved");
    return true;
}

bool App::load_save(int slot) {
    const std::optional<SaveEntry> e = slot_ref(slot);  // a copy: start_part may rewrite the autosave
    if (!e) return false;
    std::optional<PartChoice> part;
    if (picker_ && picker_->catalogue()) {
        part = part_for(*picker_->catalogue(), e->chapter_id, e->section, e->region);
    } else if (!picker_ && e->chapter_id == "scripts") {
        PartChoice p;
        p.region = e->region;
        p.script_ids = config_.script_ids;
        part = p;
    }
    if (!part) {
        platform_.log.write("load: " + e->chapter_id + " #" + std::to_string(e->section) + " is not in the catalogue");
        return false;
    }
    if (picker_) picker_->opened(*part);
    platform_.log.write("load: " + e->script_id + " line " + std::to_string(e->line));
    start_part(*part, e);
    return true;
}

void App::write_saves() {
    const std::string s = serialize_saves(saves_);
    Files* f = platform_.user ? platform_.user : &platform_.files;
    const char* key = config_.picker ? kSavesKey : kScriptSavesKey;
    if (!f->write(key, {s.begin(), s.end()})) platform_.log.write(std::string("could not save ") + key);
    autosave_dirty_ = false;
}

// The autosave follows the reader: every new line or choice replaces it in memory; the file is written at most
// every kAutosaveInterval seconds (and when a part is left or started, and when the app quits).
void App::track_autosave(double dt) {
    toast_left_ = std::max(0.0, toast_left_ - dt);
    autosave_wait_ = std::max(0.0, autosave_wait_ - dt);
    if (phase_ == Phase::Playing && player_) {
        const Status st = player_->status();
        const bool at_stop = st == Status::Typing || st == Status::AwaitAdvance || st == Status::AwaitChoice;
        if (at_stop && (script_index_ != stop_script_ || player_->pc() != stop_pc_)) {
            stop_script_ = script_index_;
            stop_pc_ = player_->pc();
            saves_.autosave = capture();
            autosave_dirty_ = true;
        }
    }
    if (autosave_dirty_ && autosave_wait_ <= 0) {
        write_saves();
        autosave_wait_ = kAutosaveInterval;
    }
}

void App::open_save_screen(SaveScreen::Mode mode) {
    if (mode == SaveScreen::Mode::Save && !capture()) {
        toast("Nothing to save yet");
        return;
    }
    auto request = [&](const std::optional<SaveEntry>& e) {
        if (e && !e->background.empty()) cache_.request_texture(background_url(e->background, e->region));
    };
    request(saves_.autosave);
    request(saves_.quick);
    for (const auto& e : saves_.slots) request(e);
    save_screen_.open(mode, saves_, phase_ == Phase::Playing);
}

void App::handle_save_screen(const InputEvent& e) {
    const SaveScreen::Action a = save_screen_.handle(e, saves_);
    switch (a.kind) {
        case SaveScreen::Action::Kind::None: break;
        case SaveScreen::Action::Kind::Close: save_screen_.close(); break;
        case SaveScreen::Action::Kind::Save:
            save_to(a.slot);
            save_screen_.refresh(saves_);
            break;
        case SaveScreen::Action::Kind::Load:
            if (!load_save(a.slot)) toast("That part is not available");
            break;
        case SaveScreen::Action::Kind::Delete:
            slot_ref(a.slot).reset();
            if (a.slot == kAutosaveSlot) stop_script_ = stop_pc_ = SIZE_MAX;
            write_saves();
            save_screen_.refresh(saves_);
            break;
    }
}

void App::toast(std::string text) {
    toast_ = std::move(text);
    toast_left_ = 1.6;
}

void App::handle_volume(const InputEvent& e) {
    AudioVolumes v = audio_.volumes();
    if (e.kind == InputEvent::Kind::Mute) v.muted = !v.muted;
    else v.master = std::clamp(std::round((v.master + (e.kind == InputEvent::Kind::VolumeUp ? 0.1f : -0.1f)) * 10) / 10, 0.f, 1.f);
    audio_.set_volumes(v);
    platform_.log.write(v.muted ? std::string("audio: muted")
                                : "audio: master volume " + std::to_string(static_cast<int>(std::lround(v.master * 100))) + "%");
}

// Hands the Player's audio events to the director. Movies are not played yet: they count as
// finished at once (which may run the script on and queue more events).
void App::play_audio_events() {
    for (int round = 0; round < 8 && player_; ++round) {
        bool movie = false;
        for (const AudioEvent& ev : player_->drain_audio()) {
            if (ev.kind == AudioEvent::Kind::Movie) {
                platform_.log.write("movie " + ev.name + ": not supported yet, skipped");
                movie = true;
            } else {
                audio_.handle(ev);
            }
        }
        if (!movie) return;
        player_->movie_finished();
    }
}

void App::update(double dt, const std::vector<InputEvent>& input) {
    scene_.clock += dt;
    cache_.poll();
    for (const InputEvent& e : input)
        if (e.kind == InputEvent::Kind::VolumeDown || e.kind == InputEvent::Kind::VolumeUp || e.kind == InputEvent::Kind::Mute)
            handle_volume(e);
    audio_.update(dt);
    if (player_ && audio_.take_voice_finished()) player_->voice_finished();
    if (phase_ == Phase::LoadingScripts || phase_ == Phase::LoadingAssets || phase_ == Phase::Finished)
        for (const InputEvent& e : input)
            if (e.kind == InputEvent::Kind::Cancel) {
                if (picker_ && phase_ != Phase::Finished) {
                    leave_part(false);
                    render();
                    return;  // the Esc is used up: the picker must not also see it
                }
                quit_ = true;
                break;
            }
    if (phase_ != Phase::Playing) lookup_.close();
    switch (phase_) {
        case Phase::Picker: {
            picker_->update(dt);
            if (!config_.picker_screen.empty() && picker_->catalogue()) {
                picker_->open(config_.picker_screen);
                config_.picker_screen.clear();
            }
            for (const InputEvent& e : input) {
                if (save_screen_.is_open()) {
                    handle_save_screen(e);
                    if (phase_ != Phase::Picker) break;  // a save was loaded
                    continue;
                }
                if (e.kind == InputEvent::Kind::OpenLoad && picker_->catalogue()) {
                    open_save_screen(SaveScreen::Mode::Load);
                    continue;
                }
                const Picker::Action a = picker_->handle(e);
                if (a.kind == Picker::Action::Kind::Quit) quit_ = true;
                if (a.kind == Picker::Action::Kind::OpenLoad) open_save_screen(SaveScreen::Mode::Load);
                if (a.kind == Picker::Action::Kind::Play) {
                    const auto& as = saves_.autosave;
                    const bool resume = a.resume && as && as->chapter_id == a.part.chapter_id &&
                                        as->section == a.part.section && as->region == a.part.region;
                    start_part(a.part, resume ? as : std::nullopt);
                    break;
                }
            }
            track_autosave(dt);
            break;
        }
        case Phase::LoadingScripts:
            load_scripts();
            break;
        case Phase::LoadingAssets:
            if (cache_.pending() == 0) finish_assets();
            break;
        case Phase::Playing:
            for (const InputEvent& e : input) {
                if (save_screen_.is_open()) handle_save_screen(e);
                else handle(e);
                if (phase_ != Phase::Playing) break;  // the reader left the part (Esc / Start) or loaded a save
            }
            if (phase_ != Phase::Playing) break;
            // the log and the save screen pause the story (and auto mode), not the sound
            // the popup closes when the text it is on goes away (the log opened or closed)
            if (lookup_.is_open() && (lookup_.target_id() == 0) == log_.is_open()) lookup_.close();
            if (!log_.is_open() && !save_screen_.is_open() && !lookup_.is_open()) player_->update(dt);
            play_audio_events();
            if (player_->status() == Status::Ended) start_script(script_index_ + 1);
            track_autosave(dt);
            break;
        case Phase::Finished:
            break;
    }
    render();
}

void App::render() {
    draw_.clear_all();
    draw_.clear = {0, 0, 0, 1};
    if (phase_ == Phase::Picker && picker_) {
        picker_->draw(draw_, &backdrop_);
        save_screen_.draw(saves_, cache_, atlas_, draw_);
        return;
    }
    if (phase_ == Phase::Playing && player_) {
        scene_.auto_mode = player_->settings().auto_mode;
        wipes_.update(player_->stage(), textures_);
        scene_.wipe = wipes_.wipe;
        scene_.wipe_filter = wipes_.filter;
        const std::string& frame = player_->stage().picture_frame;
        if (frame != frame_name_) {  // a new frame fades in over Fate Café's default 0.2 s
            frame_name_ = frame;
            frame_start_ = scene_.clock;
        }
        scene_.picture_frame_opacity = static_cast<float>(std::clamp((scene_.clock - frame_start_) / 0.2, 0.0, 1.0));
        story_boxes_.clear();
        scene_.text_boxes = &story_boxes_;
        render_scene(*player_, scene_, atlas_, ui_, draw_);
        scene_.text_boxes = nullptr;
        log_targets_.clear();
        log_.draw(player_->backlog(), atlas_, draw_, log_.is_open() ? &log_targets_ : nullptr);
        lookup_.draw(atlas_, draw_);
        save_screen_.draw(saves_, cache_, atlas_, draw_);
        if (toast_left_ > 0 && !toast_.empty()) {
            const RichText msg = parse_rich_text(toast_, {});
            TextStyleBox box;
            box.px = 20;
            box.max_lines = 1;
            box.first_line = 0;
            box.ink = {1, 1, 1, static_cast<float>(std::min(1.0, toast_left_ / 0.3))};
            const float w = text_width(atlas_, msg, box.px);
            box.box = {kStageWidth - 190.f - w, 18, w + 4, 30};
            draw_.rect({box.box.x - 14, 12, w + 32, 40}, {0, 0, 0, 0.6f * box.ink.a});
            draw_text(atlas_, msg, msg.glyph_count, box, draw_);
        }
        return;
    }
    // loading / end screen: a status line in the middle of the stage
    const RichText msg = parse_rich_text(status(), {});
    TextStyleBox box;
    box.px = 22;
    box.max_lines = 1;
    const float w = text_width(atlas_, msg, box.px);
    box.box = {(kStageWidth - w) / 2, kStageHeight / 2.f - 11, w + 4, 30};
    draw_text(atlas_, msg, msg.glyph_count, box, draw_);
}

}  // namespace vnr::rt
