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
    std::cout << "Popup monitor offsets, work areas, and small-screen clamping passed\n";
}
