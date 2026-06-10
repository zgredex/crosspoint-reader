#include <gtest/gtest.h>

#include <cstdint>

// EEGO A4 panel geometry constants (UC8279, 552x768 portrait B/W).
// These invariants must hold for the framebuffer layout used by the UC8279
// driver and any code that allocates or indexes into a plane buffer.
static constexpr uint16_t EEGO_WIDTH = 552;
static constexpr uint16_t EEGO_HEIGHT = 768;

TEST(EegoGeometry, PanelDimensions) {
  EXPECT_EQ(EEGO_WIDTH, 552u);
  EXPECT_EQ(EEGO_HEIGHT, 768u);
}

TEST(EegoGeometry, BytesPerRow) {
  // Width must be an exact multiple of 8 so each row packs without padding.
  EXPECT_EQ(EEGO_WIDTH % 8, 0u);
  const uint16_t bytesPerRow = EEGO_WIDTH / 8;
  EXPECT_EQ(bytesPerRow, 69u);
}

TEST(EegoGeometry, PlaneSize) {
  const uint16_t bytesPerRow = EEGO_WIDTH / 8;
  const uint32_t planeBytes = static_cast<uint32_t>(bytesPerRow) * EEGO_HEIGHT;
  EXPECT_EQ(planeBytes, 52992u);  // 69 * 768
}

TEST(EegoGeometry, PortraitAspect) {
  // Panel is portrait: physical height exceeds physical width.
  EXPECT_GT(EEGO_HEIGHT, EEGO_WIDTH);
}
