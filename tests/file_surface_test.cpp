#include "../file_surface.hpp"
#include <cassert>
#include <iostream>
int main() {
  kalwer::files::SurfaceMask mask;
  for (int mode = 0; mode < 6; ++mode)
    for (bool keep : {false, true}) {
      kalwer::appearance.popup_mode = mode;
      kalwer::appearance.popup_keep_halftone = keep;
      kalwer::appearance.opacity = 72;
      auto tile = mask.get();
      assert(tile == mask.get());
      auto data = cairo_image_surface_get_data(tile);
      int stride = cairo_image_surface_get_stride(tile);
      unsigned total = 0;
      for (int y = 0; y < 56; y++)
        for (int x = 0; x < 56; x++)
          total += data[y * stride + x];
      double alpha = total / (56. * 56. * 255.);
      assert(alpha > .1 && alpha < .9);
    }
  kalwer::appearance.popup_mode = 5;
  kalwer::appearance.popup_keep_halftone = false;
  kalwer::appearance.opacity = 30;
  auto low = mask.get();
  int value = cairo_image_surface_get_data(low)[0];
  kalwer::appearance.opacity = 95;
  auto high = mask.get();
  assert(cairo_image_surface_get_data(high)[0] > value);
  std::cout << "Cached alpha masks: six modes, combined halftone, real "
               "transparency and opacity control passed\n";
}
