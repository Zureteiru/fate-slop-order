#include "vnr/rt/app.h"

#include <algorithm>
#include <cstdlib>
#include <set>

#include "vnr/rt/json.h"

namespace vnr::rt {

App::App(Platform& platform, AppConfig config)
    : platform_(platform),
      config_(std::move(config)),
      cache_(platform_, textures_),
      atlas_(textures_, platform_.font, platform_.font_index) {
    scene_.region = config_.region;
    scene_.cache = &cache_;
    if (!atlas_.ok()) platform_.log.write("font could not be loaded: text will not be drawn");
    if (platform_.user) scene_.skin = load_ui_skin(*platform_.user, textures_, platform_.log);
    if (!scene_.skin.has_talk()) platform_.log.write("no UI sheets (ui/003.png, ui/030.png): using plain boxes");
    for (const auto& id : config_.script_ids) cache_.request_bytes(script_url(id, config_.region));
}

App::~App() = default;

std::string App::status() const {
    switch (phase_) {
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

void App::request_assets() {
    std::set<int> figures;
    std::set<std::string> backgrounds, images;
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
        }
    }
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
    start_script(0);
}

void App::start_script(std::size_t index) {
    script_index_ = index;
    if (index >= programs_.size()) {
        phase_ = Phase::Finished;
        player_.reset();
        return;
    }
    player_ = std::make_unique<Player>(programs_[index], config_.settings);
    phase_ = Phase::Playing;
}

void App::handle(const InputEvent& e) {
    if (!player_) return;
    switch (e.kind) {
        case InputEvent::Kind::Advance:
            player_->advance();
            break;
        case InputEvent::Kind::Back:
            player_->back();
            break;
        case InputEvent::Kind::Choose:
            player_->choose(e.index);
            break;
        case InputEvent::Kind::Pointer:
            if (const ChoiceOp* c = player_->pending_choice()) {
                const auto rects = choice_rects(*c, ui_, player_->text().visible);
                for (std::size_t i = 0; i < rects.size(); ++i) {
                    const Rect& r = rects[i];
                    if (e.pos.x >= r.x && e.pos.x <= r.x + r.w && e.pos.y >= r.y && e.pos.y <= r.y + r.h) {
                        player_->choose(i);
                        break;
                    }
                }
            } else {
                player_->advance();
            }
            break;
        case InputEvent::Kind::ToggleAuto:
            player_->settings().auto_mode = !player_->settings().auto_mode;
            break;
        case InputEvent::Kind::SkipScript:
            player_->skip();
            break;
    }
}

void App::update(double dt, const std::vector<InputEvent>& input) {
    scene_.clock += dt;
    cache_.poll();
    switch (phase_) {
        case Phase::LoadingScripts:
            load_scripts();
            break;
        case Phase::LoadingAssets:
            if (cache_.pending() == 0) finish_assets();
            break;
        case Phase::Playing:
            for (const InputEvent& e : input) handle(e);
            player_->update(dt);
            for (const AudioEvent& ev : player_->drain_audio()) (void)ev;  // audio: later step
            if (player_->status() == Status::Ended) start_script(script_index_ + 1);
            break;
        case Phase::Finished:
            break;
    }
    render();
}

void App::render() {
    draw_.clear_all();
    if (phase_ == Phase::Playing && player_) {
        render_scene(*player_, scene_, atlas_, ui_, draw_);
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
