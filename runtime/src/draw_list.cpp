#include "vnr/rt/draw_list.h"

#include <cmath>

namespace vnr::rt {

void DrawList::rect(const Rect& d, Color c) {
    Quad q;
    q.texture = 0;
    q.pos[0] = {d.x, d.y};
    q.pos[1] = {d.x + d.w, d.y};
    q.pos[2] = {d.x + d.w, d.y + d.h};
    q.pos[3] = {d.x, d.y + d.h};
    q.color = c;
    quads.push_back(q);
}

void DrawList::image(TextureId tex, const Rect& src, const Rect& d, Color c) {
    Quad q;
    q.texture = tex;
    q.src = src;
    q.pos[0] = {d.x, d.y};
    q.pos[1] = {d.x + d.w, d.y};
    q.pos[2] = {d.x + d.w, d.y + d.h};
    q.pos[3] = {d.x, d.y + d.h};
    q.color = c;
    quads.push_back(q);
}

void rotate(Quad& q, Vec2 origin, float degrees) {
    if (degrees == 0) return;
    const float a = degrees * 3.14159265f / 180.f, c = std::cos(a), s = std::sin(a);
    for (Vec2& p : q.pos) {
        const float x = p.x - origin.x, y = p.y - origin.y;
        p = {origin.x + x * c - y * s, origin.y + x * s + y * c};
    }
}

TextureId TextureStore::create(Image image, bool keep_pixels) {
    const TextureId id = next_++;
    Entry e;
    e.width = image.width;
    e.height = image.height;
    e.image = std::move(image);
    e.keep = keep_pixels;
    entries_[id] = std::move(e);
    uploads_.push_back(id);
    return id;
}

void TextureStore::update(TextureId id) {
    if (entries_.count(id)) uploads_.push_back(id);
}

Image* TextureStore::pixels(TextureId id) {
    const auto it = entries_.find(id);
    return it == entries_.end() || it->second.image.rgba.empty() ? nullptr : &it->second.image;
}

int TextureStore::width(TextureId id) const {
    const auto it = entries_.find(id);
    return it == entries_.end() ? 0 : it->second.width;
}

int TextureStore::height(TextureId id) const {
    const auto it = entries_.find(id);
    return it == entries_.end() ? 0 : it->second.height;
}

void TextureStore::release(TextureId id) {
    if (entries_.erase(id)) releases_.push_back(id);
}

std::vector<TextureId> TextureStore::take_uploads() {
    std::vector<TextureId> out;
    out.swap(uploads_);
    return out;
}

std::vector<TextureId> TextureStore::take_releases() {
    std::vector<TextureId> out;
    out.swap(releases_);
    return out;
}

void TextureStore::uploaded(TextureId id) {
    const auto it = entries_.find(id);
    if (it != entries_.end() && !it->second.keep) {
        it->second.image.rgba.clear();
        it->second.image.rgba.shrink_to_fit();
    }
}

}  // namespace vnr::rt
