#pragma once
#include "appearance.hpp"
#include <cairo.h>
#include <cmath>
namespace kalwer::files {
// Small cached alpha stencil. Native compositing exposes the window beneath it;
// text, thumbnails and controls are drawn separately and remain sharp.
struct SurfaceMask {
  cairo_surface_t *tile = nullptr;
  int mode = -1, opacity = -1, spacing = -1;
  bool keep = false;
  ~SurfaceMask() {
    if (tile)
      cairo_surface_destroy(tile);
  }
  cairo_surface_t *get() {
    if (tile && mode == appearance.popup_mode &&
        opacity == appearance.opacity && spacing == appearance.scale &&
        keep == appearance.popup_keep_halftone)
      return tile;
    if (tile)
      cairo_surface_destroy(tile);
    mode = appearance.popup_mode;
    opacity = appearance.opacity;
    spacing = appearance.scale;
    keep = appearance.popup_keep_halftone;
    constexpr int n = 56, work = 112;
    const float coverage = std::clamp(opacity, 30, 95) / 100.f;
    tile = cairo_image_surface_create(CAIRO_FORMAT_A8, n, n);
    auto bytes = cairo_image_surface_get_data(tile);
    auto stride = cairo_image_surface_get_stride(tile);
    float error[work + 4][work + 4]{};
    static constexpr int coarse[4] = {0, 2, 3, 1};
    static constexpr int bayer[16] = {0, 8,  2, 10, 12, 4, 14, 6,
                                      3, 11, 1, 9,  15, 7, 13, 5};
    for (int y = 0; y < work; ++y)
      for (int x = 0; x < work; ++x) {
        float value = coverage + error[y + 2][x + 2], threshold = .5f;
        if (mode == 3)
          threshold = (bayer[(y % 4) * 4 + x % 4] + .5f) / 16.f;
        if (mode == 4)
          threshold = (4 * bayer[(y % 4) * 4 + x % 4] +
                       coarse[((y / 4) % 2) * 2 + (x / 4) % 2] + .5f) /
                      64.f;
        float bit = value >= threshold ? 1.f : 0.f, e = value - bit;
        if (mode == 1) {
          for (auto p : {std::pair{1, 0}, std::pair{2, 0}, std::pair{-1, 1},
                         std::pair{0, 1}, std::pair{1, 1}, std::pair{0, 2}})
            error[y + 2 + p.second][x + 2 + p.first] += e / 8;
        }
        if (mode == 2) {
          error[y + 2][x + 3] += e * 7 / 16;
          error[y + 3][x + 1] += e * 3 / 16;
          error[y + 3][x + 2] += e * 5 / 16;
          error[y + 3][x + 3] += e / 16;
        }
        if (x < n || y < n)
          continue;
        int px = x - n, py = y - n;
        float alpha = (mode == 0 || mode == 5) ? coverage : bit;
        if (mode == 0 || keep) {
          float dx = (px % 7 + .5f) - 3.5f, dy = (py % 7 + .5f) - 3.5f;
          float dot =
              std::clamp(3.15f - std::sqrt(dx * dx + dy * dy), 0.f, 1.f);
          alpha *= .4f + .6f * dot;
        }
        bytes[py * stride + px] =
            static_cast<unsigned char>(std::clamp(alpha, 0.f, 1.f) * 255 + .5f);
      }
    cairo_surface_mark_dirty(tile);
    return tile;
  }
  void paint(cairo_t *cr, double x, double y, double width, double height) {
    auto mask = cairo_pattern_create_for_surface(get());
    cairo_pattern_set_extend(mask, CAIRO_EXTEND_REPEAT);
    cairo_pattern_set_filter(mask, CAIRO_FILTER_NEAREST);
    cairo_matrix_t matrix;
    cairo_matrix_init_scale(&matrix, 1. / std::max(1, spacing),
                            1. / std::max(1, spacing));
    cairo_pattern_set_matrix(mask, &matrix);
    auto color = appearance.tint(0x00130b);
    cairo_save(cr);
    cairo_rectangle(cr, x, y, width, height);
    cairo_clip(cr);
    cairo_set_source_rgb(cr, ((color >> 16) & 255) / 255.,
                         ((color >> 8) & 255) / 255., (color & 255) / 255.);
    cairo_mask(cr, mask);
    cairo_restore(cr);
    cairo_pattern_destroy(mask);
  }
};
inline SurfaceMask surface_mask;
} // namespace kalwer::files
