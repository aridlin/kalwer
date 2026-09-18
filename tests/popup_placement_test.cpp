#include "../popup_placement.hpp"
#include <cassert>
#include <iostream>
int main() {
    auto p = kalwer::popup_geometry({0, 0, 1920, 1080}, 320, 378);
    assert(p.x == 1590 && p.y == 96);
    p = kalwer::popup_geometry({-1920, 40, 1920, 1040}, 320, 378);
    assert(p.x == -330 && p.y == 136);
    p = kalwer::popup_geometry({1360, 1440, 3200, 1760}, 640, 756);
    assert(p.x == 3910 && p.y == 1536);
    p = kalwer::popup_geometry({100, 200, 300, 250}, 320, 378);
    assert(p.x == 100 && p.y == 200);
    p = kalwer::popup_geometry({0, 0, 500, 400}, 320, 378);
    assert(p.x == 170 && p.y == 22);
    const GdkRectangle area{0, 0, 1920, 1080};
    const GdkRectangle launcher{635, 224, 650, 632};
    const auto beside = kalwer::popup_beside_launcher(area, launcher, 320, 378, 635, 27);
    assert(beside.x == 1270 && beside.y == 251);
    const auto corner = kalwer::popup_geometry(area, 320, 378);
    assert(kalwer::popup_corner_progress(5000, 820, false) == 0.0);
    assert(kalwer::popup_corner_progress(800, 820, true) == 0.0);
    assert(kalwer::popup_corner_progress(820, 820, true) == 0.0);
    assert(kalwer::popup_corner_progress(1030, 820, true) == 0.5);
    assert(kalwer::popup_corner_progress(1240, 820, true) == 1.0);
    auto moving = kalwer::popup_interpolate(beside, corner, 0.0);
    assert(moving.x == beside.x && moving.y == beside.y);
    moving = kalwer::popup_interpolate(beside, corner, 1.0);
    assert(moving.x == corner.x && moving.y == corner.y);
    const auto clamped = kalwer::popup_beside_launcher({-800,0,800,600}, {-250,100,650,632}, 320,378,635,27);
    assert(clamped.x == -320 && clamped.y == 127);
    std::cout << "PTY attachment, delayed game easing, monitor offsets, and screen clamping passed\n";
}
