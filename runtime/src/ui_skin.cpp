#include "vnr/rt/ui_skin.h"

#include <string>
#include <utility>
#include <vector>

#include "vnr/rt/image.h"

namespace vnr::rt {

bool UiSkin::adopt_talk(Image sheet, TextureStore& textures) {
    if (sheet.width != skin::kSheetTalkSize || sheet.height != skin::kSheetTalkSize) return false;
    talk = textures.create(std::move(sheet));
    return true;
}

bool UiSkin::adopt_next(Image sheet, TextureStore& textures) {
    if (sheet.width != skin::kSheetNextSize || sheet.height != skin::kSheetNextSize) return false;
    next = textures.create(std::move(sheet));
    return true;
}

UiSkin load_ui_skin(Files& files, TextureStore& textures, Log& log) {
    UiSkin s;
    auto load = [&](const char* key, bool (UiSkin::*adopt)(Image, TextureStore&)) {
        std::vector<std::uint8_t> bytes;
        if (!files.read(key, bytes)) return;
        Image img;
        if (!decode_image(bytes, img)) {
            log.write(std::string("UI sheet ") + key + " could not be decoded; using plain boxes");
            return;
        }
        if (!(s.*adopt)(std::move(img), textures))
            log.write(std::string("UI sheet ") + key + " has an unexpected size; using plain boxes");
    };
    load("ui/003.png", &UiSkin::adopt_talk);
    load("ui/030.png", &UiSkin::adopt_next);
    return s;
}

}  // namespace vnr::rt
