#pragma once
#include <stdint.h>

// Small C boundary keeps private, locally generated assets out of Kalwer
// builds. Pixels are Cairo RGB24 (0x00RRGGBB), always 256 x 192.
struct KalwerFredApi {
  uint32_t version;
  void *(*create)();
  void (*destroy)(void *);
  int (*step)(void *, uint32_t buttons);
  const uint32_t *(*pixels)(void *);
};
enum KalwerFredButton {
  FredLeft = 1,
  FredRight = 2,
  FredUp = 4,
  FredDown = 8,
  FredFire = 16,
  FredStart = 32
};
