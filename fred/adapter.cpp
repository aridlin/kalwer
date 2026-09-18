#include "api.h"
#include "fred/original_data/original_data.h"
#include "fred/original_presentation/render.h"
#include <array>
#include <new>

namespace {
struct Session {
  fred::GameState state{};
  fred::OriginalRules rules = fred::make_original_rules();
  fred::OriginalFrameState frame{};
  fred::IndexedFramebuffer indexed{};
  std::array<uint32_t, 256 * 192> rgb{};
};
bool render(Session &s) {
  if (fred::render_original_frame(s.frame, fred::original_game_data(),
                                  s.indexed) !=
      fred::PresentationStatus::complete)
    return false;
  for (unsigned i = 0; i < s.rgb.size(); ++i) {
    unsigned c = s.indexed.pixels[i], v = (c & 8) ? 255 : 205;
    s.rgb[i] =
        ((c & 2) ? v << 16 : 0) | ((c & 4) ? v << 8 : 0) | ((c & 1) ? v : 0);
  }
  return true;
}
void *create() {
  auto *s = new (std::nothrow) Session;
  if (!s)
    return nullptr;
  if (!fred::original_game_data().reference_verified ||
      !fred::reset_original_game(s->state, s->rules, 0)) {
    delete s;
    return nullptr;
  }
  s->state.mode = fred::GameMode::menu;
  fred::capture_original_frame_state(s->state, s->frame);
  if (!render(*s)) {
    delete s;
    return nullptr;
  }
  return s;
}
void destroy(void *p) { delete static_cast<Session *>(p); }
int step(void *p, uint32_t b) {
  auto &s = *static_cast<Session *>(p);
  auto input = fred::make_neutral_input_frame();
  input.left = b & FredLeft;
  input.right = b & FredRight;
  input.up = b & FredUp;
  input.down = b & FredDown;
  input.fire = b & FredFire;
  input.start = b & FredStart;
  fred::StepEvents events;
  return fred::step_game_with_frame_state(s.state, fred::original_game_data(),
                                          s.rules, input, events, s.frame) ==
             fred::StepStatus::complete &&
         render(s);
}
const uint32_t *pixels(void *p) {
  return static_cast<Session *>(p)->rgb.data();
}
const KalwerFredApi api = {1, create, destroy, step, pixels};
} // namespace
extern "C" const KalwerFredApi *kalwer_fred_api() { return &api; }
