#pragma once

#include <stdint.h>

enum class DitherMode {
  Bayer4x4 = 0,
  Bayer8x8 = 1,
  WhiteNoise = 2,
  FloydSteinberg = 3,
  Atkinson = 4,
};

extern uint8_t g_epubDitherMode;
extern bool g_epubUseAA;

// ============================================================================
// Quantization helpers (shared by all modes)
// AA on  -> 4 gray levels (2-bit): 0, 1, 2, 3  -> values 0, 85, 170, 255
// AA off -> 2 gray levels (1-bit): 0, 1         -> values 0, 255
// ============================================================================
inline uint8_t quantize(int gray) {
  if (gray < 0) gray = 0;
  if (gray > 255) gray = 255;
  if (g_epubUseAA) {
    if (gray < 43) return 0;
    if (gray < 128) return 1;
    if (gray < 213) return 2;
    return 3;
  } else {
    return gray < 128 ? 0 : 3;
  }
}

inline int quantizedToGray(uint8_t level) {
  if (g_epubUseAA) {
    return level * 85;
  } else {
    return level ? 255 : 0;
  }
}

// ============================================================================
// 0 — Bayer 4x4 (original)
// ============================================================================
inline const uint8_t bayer4x4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

inline uint8_t applyBayer4x4(uint8_t gray, int x, int y) {
  int bayer = bayer4x4[y & 3][x & 3];
  int dither = (bayer - 8) * 5;
  return quantize(gray + dither);
}

// ============================================================================
// 1 — Bayer 8x8
// ============================================================================
inline const uint8_t bayer8x8[8][8] = {
    {0,  32, 8,  40, 2,  34, 10, 42},
    {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44, 4,  36, 14, 46, 6,  38},
    {60, 28, 52, 20, 62, 30, 54, 22},
    {3,  35, 11, 43, 1,  33, 9,  41},
    {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47, 7,  39, 13, 45, 5,  37},
    {63, 31, 55, 23, 61, 29, 53, 21},
};

inline uint8_t applyBayer8x8(uint8_t gray, int x, int y) {
  int bayer = bayer8x8[y & 7][x & 7];
  int dither = (bayer - 32) * 2;
  return quantize(gray + dither);
}

// ============================================================================
// 2 — White noise (hash-based, deterministic per-pixel)
// ============================================================================
inline uint8_t applyWhiteNoise(uint8_t gray, int x, int y) {
  uint32_t hash = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
  hash = (hash ^ (hash >> 13)) * 1274126177u;
  const int threshold = static_cast<int>(hash >> 24);

  if (g_epubUseAA) {
    const int scaled = gray * 3;
    if (scaled < 255) {
      return (scaled + threshold >= 255) ? 1 : 0;
    } else if (scaled < 510) {
      return ((scaled - 255) + threshold >= 255) ? 2 : 1;
    } else {
      return ((scaled - 510) + threshold >= 255) ? 3 : 2;
    }
  } else {
    return (gray + threshold >= 256) ? 3 : 0;
  }
}

// ============================================================================
// 3 & 4 — Error diffusion ditherers (stateful, per-block)
// For JPEG: reset at the start of each MCU callback.
// For PNG: reset at start of each page image (processes row by row).
// ============================================================================

// Max expected block width for error diffusion buffers.
// JPEG MCU = 8px source (may be scaled up). We use 16-bit error with 2 extra slots.
// 1024 is generous headroom for any upscale scenario.
constexpr int DITHER_MAX_BLOCK_WIDTH = 1024;

struct BlockDitherState {
  int16_t errorCur[DITHER_MAX_BLOCK_WIDTH + 2];
  int16_t errorNext[DITHER_MAX_BLOCK_WIDTH + 2];
  int width;
  int rowCount;

  void init(int w) {
    width = w < DITHER_MAX_BLOCK_WIDTH ? w : DITHER_MAX_BLOCK_WIDTH;
    rowCount = 0;
    reset();
  }

  void reset() {
    for (int i = 0; i <= width + 1; i++) {
      errorCur[i] = 0;
      errorNext[i] = 0;
    }
    rowCount = 0;
  }

  void nextRow() {
    for (int i = 0; i <= width + 1; i++) {
      int16_t temp = errorNext[i];
      errorNext[i] = 0;
      errorCur[i] = temp;
    }
    rowCount++;
  }
};

// Floyd-Steinberg: distributes 100% of error (7/16, 3/16, 5/16, 1/16)
// Serpentine scanning to reduce worm artifacts
inline uint8_t processFloydSteinberg(BlockDitherState& s, int gray, int x) {
  int adjusted = gray + s.errorCur[x + 1];
  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;

  uint8_t quantized = quantize(adjusted);
  int quantizedValue = quantizedToGray(quantized);
  int err = adjusted - quantizedValue;

  if ((s.rowCount & 1) == 0) {
    s.errorCur[x + 2] += (err * 7) >> 4;
    s.errorNext[x] += (err * 3) >> 4;
    s.errorNext[x + 1] += (err * 5) >> 4;
    s.errorNext[x + 2] += err >> 4;
  } else {
    s.errorCur[x] += (err * 7) >> 4;
    s.errorNext[x + 2] += (err * 3) >> 4;
    s.errorNext[x + 1] += (err * 5) >> 4;
    s.errorNext[x] += err >> 4;
  }

  return quantized;
}

// Atkinson: distributes 75% of error (1/8 to each of 6 neighbors)
// Produces cleaner results with less error buildup
inline uint8_t processAtkinson(BlockDitherState& s, int gray, int x) {
  int adjusted = gray + s.errorCur[x + 2];
  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;

  uint8_t quantized = quantize(adjusted);
  int quantizedValue = quantizedToGray(quantized);
  int err = (adjusted - quantizedValue) >> 3;

  s.errorCur[x + 3] += err;
  s.errorCur[x + 4] += err;
  s.errorNext[x + 1] += err;
  s.errorNext[x + 2] += err;
  s.errorNext[x + 3] += err;
  s.errorNext[x + 4] += err;

  return quantized;
}

// ============================================================================
// Main dispatch — call this from converter code
// For stateless modes (Bayer, Noise): blockState is unused.
// For error-diffusion modes (F-S, Atkinson):
//   - Call initBlockDither() once before processing pixels.
//   - Call nextRowDither() after each row.
//   - blockX/blockY track which block is active (for reset logic).
// ============================================================================

struct DitherCtx {
  DitherMode mode;
  BlockDitherState state;
  int lastBlockX = -1;
  int lastBlockY = -1;

  void initForBlock(int blockWidth) {
    if (mode == DitherMode::FloydSteinberg || mode == DitherMode::Atkinson) {
      state.init(blockWidth);
    }
  }

  void nextRow() {
    if (mode == DitherMode::FloydSteinberg || mode == DitherMode::Atkinson) {
      state.nextRow();
    }
  }

  uint8_t process(uint8_t gray, int x, int y, int blockX, int blockY) {
    if (mode == DitherMode::Bayer4x4) return applyBayer4x4(gray, x, y);
    if (mode == DitherMode::Bayer8x8) return applyBayer8x8(gray, x, y);
    if (mode == DitherMode::WhiteNoise) return applyWhiteNoise(gray, x, y);

    int localX = x - blockX;
    if (localX < 0) localX = 0;

    if (mode == DitherMode::FloydSteinberg) return processFloydSteinberg(state, gray, localX);
    if (mode == DitherMode::Atkinson) return processAtkinson(state, gray, localX);

    return quantize(gray);
  }
};
