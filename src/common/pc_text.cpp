// PC wording for the save messages.
//
// The English save system messages talk about the Xbox console, blocks, memory units and the Xbox Dashboard. They
// live in fonts\Save\Save_EN.fnt and again in fonts\Advertise\Advertise_EN.fnt . The files are rewritten as the game opens them, from the player's own disc (kernel PatchGameFile).
//
// .fnt layout: u32 count; count x {u32 text offset, u32 id, u32 20, u32 0, u32 -1}; then the texts, UTF-16LE and
// zero-terminated, in entry order; zero padding at the end. A text may only use characters the file already holds,
// because the font sheet beside it (.txd/.met) only draws those. A replacement that does not fit keeps the original.

#include "common/pc_text.h"

#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"

namespace shadow {
namespace {

const std::u16string kNoSpace = u"There is not enough free disk\nspace to save Game Data.\nPress A button to continue\nwithout saving or B button to\nfree more space.";
const std::u16string kNoSpaceA = u"There is not enough free disk\nspace to save Game Data.\nPress A button to continue\nwithout saving.";

struct Rule {
    std::u16string needle;  // in the original text
    bool exact;             // the whole text
    std::u16string text;
};

// matched on the original wording, so both files (and any entry order) work
const std::vector<Rule>& Rules() {
    static const std::vector<Rule> rules{
        {u"enough free blocks to save Game\nData.", false, u""},  // two forms, see Pick
        {u"free blocks to save games.", false, kNoSpace},
        {u"The memory unit was removed", false, u"The save location was removed\nduring use. Saving failed.\nPress A to continue."},
        {u"has been removed.", false, u"The save location <NAME>\nhas been removed.\nPlease select another save location."},
        {u"Xbox Dashboard to free blocks", false, u"The save location <NAME>\ncannot be used.\nPlease select another save location."},
        {u"Loading Game Data. \nPlease do not turn off your Xbox console.", true, u"Loading Game Data.\nPlease do not close the game."},
        {u"Do you want to create Game Data on\nyour Xbox console?", false, u"Would you like to create save data?"},
        {u"Deleting Game Data.\nPlease don't turn off your Xbox console.", true, u"Deleting Game Data.\nPlease do not close the game."},
        {u"There is no Game Data on your Xbox\nconsole.", true, u"There is no save data."},
        {u"Please delete it using the Xbox Dashboard", false, u"The Game Data is damaged.\nPlease delete it and try again.\nPress A to continue without\nloading."},
    };
    return rules;
}

// the new wording for an original text, empty to keep it
std::u16string Pick(const std::u16string& t) {
    for (const Rule& r : Rules()) {
        if (r.exact ? t != r.needle : t.find(r.needle) == std::u16string::npos) continue;
        if (!r.text.empty()) return r.text;
        return t.find(u"B button") != std::u16string::npos ? kNoSpace : kNoSpaceA;
    }
    return {};
}

uint32_t Rd(const std::vector<uint8_t>& d, size_t at) {
    uint32_t v;
    std::memcpy(&v, d.data() + at, 4);
    return v;
}

bool Rewrite(std::vector<uint8_t>& d) {
    if (d.size() < 4) return false;
    uint32_t n = Rd(d, 0);
    size_t start = 4 + size_t(n) * 20;
    if (n == 0 || start > d.size() || Rd(d, 4) != start) return false;
    std::vector<std::u16string> texts(n);
    std::set<char16_t> chars;
    size_t end = start;
    for (uint32_t i = 0; i < n; i++) {
        size_t o = Rd(d, 4 + i * 20);
        if (o + 2 > d.size()) return false;
        for (; o + 1 < d.size(); o += 2) {
            char16_t c = char16_t(d[o] | d[o + 1] << 8);
            if (!c) break;
            texts[i] += c;
            chars.insert(c);
        }
        end = std::max(end, o + 2);
    }
    size_t pad = d.size() > end ? d.size() - end : 0;
    int changed = 0;
    for (uint32_t i = 0; i < n; i++) {
        std::u16string text = Pick(texts[i]);
        if (text.empty()) continue;
        bool fits = true;
        for (char16_t c : text) fits = fits && (c == u'\n' || chars.count(c));
        if (!fits) {
            LOGW("pc text: entry %u does not fit the font, kept", i);
            continue;
        }
        texts[i] = text;
        changed++;
    }
    std::vector<uint8_t> out(d.begin(), d.begin() + long(start));
    for (uint32_t i = 0; i < n; i++) {
        uint32_t at = uint32_t(out.size());
        std::memcpy(out.data() + 4 + i * 20, &at, 4);
        for (char16_t c : texts[i]) out.push_back(uint8_t(c)), out.push_back(uint8_t(c >> 8));
        out.push_back(0), out.push_back(0);
    }
    out.resize(out.size() + pad, 0);
    d.swap(out);
    LOGI("pc text: %d save messages reworded", changed);
    return changed > 0;
}

}  // namespace

void InitPcText() {
    kernel::SetPatchDir((app::ExeDir() / "cache" / "patched").string());
    kernel::PatchGameFile("fonts\\Save\\Save_EN.fnt", [](std::vector<uint8_t>& d) { return Rewrite(d); });
    kernel::PatchGameFile("fonts\\Advertise\\Advertise_EN.fnt", [](std::vector<uint8_t>& d) { return Rewrite(d); });
}

}  // namespace shadow
