#pragma once

#include <algorithm>

namespace kalwer::signal_geometry {
inline constexpr unsigned ink = 0x140f12;
inline constexpr unsigned paper = 0xf9f1eb;
inline constexpr unsigned stage = 0x111111;
inline constexpr unsigned coal = 0x1e1419;
inline constexpr unsigned rule = 0x66424a;
inline constexpr unsigned red = 0xef5148;
inline constexpr unsigned red_fill = 0x3a1c23;
inline constexpr unsigned cream = 0xffe08a;
inline constexpr unsigned muted = 0xc5b7bb;

inline constexpr float search_icon_width = 44;
inline constexpr float search_icon_height = 48;

template<class Fill>
void search(Fill fill, float x, float y, float width, float height, float opacity) {
    fill(x, y, width, height, stage, opacity);
    if (opacity < .1f) return;
    fill(x, y, search_icon_width, search_icon_height, coal);
    fill(x + search_icon_width, y, 1, search_icon_height, rule);
    fill(x + 46, y, width - 46, 1, rule);
    // The tab sits above the input. All lower rules sit below the shortcut labels.
    const float tab_x = x + 52;
    fill(tab_x, y + 1, 70, 17, red);
    for (int row = 0; row < 17; ++row)
        fill(tab_x + 70, y + 1 + row, 12.f * (17 - row) / 17, 1, red);
    fill(x, y + height - 2, 92, 2, red);
    fill(x + 92, y + height - 1, width - 92, 1, rule);
}

template<class Fill>
void row(Fill fill, float x, float y, float width, float height) {
    fill(x, y, width, height, coal, .95f);
    fill(x, y + 1, 54, height - 2, stage);
    fill(x + 54, y + 8, 1, height - 16, rule);
    fill(x, y + height - 1, width, 1, rule);
}

template<class Fill>
void popup(Fill fill, float x, float y, float width, float height) {
    if (height < 2) return;
    fill(x, y, std::min(width, 92.f), 2, red);
    if (width > 92) fill(x + 92, y, width - 92, 1, rule);
    fill(x, y + 2, 2, height - 2, cream);
    fill(x + width - 1, y + 1, 1, height - 1, rule);
    fill(x + 2, y + height - 1, width - 2, 1, rule);
}
}
