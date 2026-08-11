#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "../../Software/src/battery/TESLA-BATTERY.h"
#include "../../Software/src/datalayer/datalayer.h"
#include "Arduino.h"

void clear_transmitted_frames();
const std::vector<CAN_frame>& get_transmitted_frames();

namespace {

const CAN_frame* last_frame_with_id(uint32_t id) {
  const auto& frames = get_transmitted_frames();
  auto match = std::find_if(frames.rbegin(), frames.rend(),
                            [id](const CAN_frame& frame) { return frame.ID == id; });
  return match == frames.rend() ? nullptr : &*match;
}

uint8_t tesla_checksum(const CAN_frame& frame, uint8_t checksum_byte = 7) {
  uint8_t checksum = static_cast<uint8_t>((frame.ID & 0xFF) + ((frame.ID >> 8) & 0x0F));
  for (uint8_t i = 0; i < frame.DLC; ++i) {
    if (i != checksum_byte) {
      checksum = static_cast<uint8_t>(checksum + frame.data.u8[i]);
    }
  }
  return checksum;
}

CAN_frame charge_port_056() {
  CAN_frame frame = {};
  frame.ID = 0x056;
  frame.DLC = 8;
  frame.data.u8[7] = 0x56;
  return frame;
}

void call_five_phases(TeslaBattery& battery, unsigned long now) {
  for (int i = 0; i < 5; ++i) {
    battery.transmit_can(now);
  }
}

}  // namespace

TEST(TeslaChargeMode, EmitsMeasuredStartupAndSuccessfulChargeProfile) {
  user_selected_battery_type = BatteryType::TeslaModel3Y;
  user_selected_tesla_digital_HVIL = false;
  set_millis64(1000);

  TeslaBattery battery;
  battery.setup();
  ASSERT_TRUE(battery.supports_charge_mode());

  battery.start_charge_mode();
  ASSERT_TRUE(battery.is_charge_mode_active());

  clear_transmitted_frames();
  battery.transmit_can(1010);  // phase 0: 10 ms frames
  battery.transmit_can(1010);  // phase 1: 50 ms frames

  const CAN_frame* frame053 = last_frame_with_id(0x053);
  ASSERT_NE(frame053, nullptr);
  EXPECT_EQ(frame053->data.u8[0], 0x54);
  EXPECT_EQ(frame053->data.u8[3], 0xC3);

  const CAN_frame* frame055 = last_frame_with_id(0x055);
  ASSERT_NE(frame055, nullptr);
  EXPECT_EQ(frame055->data.u8[0], 0x00);
  EXPECT_EQ(frame055->data.u8[1], 0x00);
  EXPECT_EQ(frame055->data.u8[7], tesla_checksum(*frame055));

  const CAN_frame* frame118 = last_frame_with_id(0x118);
  ASSERT_NE(frame118, nullptr);
  EXPECT_EQ(frame118->data.u8[1] & 0xF0, 0x80);
  EXPECT_EQ(frame118->data.u8[2], 0x2D);
  EXPECT_EQ(frame118->data.u8[5], 0x08);
  EXPECT_EQ(frame118->data.u8[7], 0x00);
  EXPECT_EQ(frame118->data.u8[0], tesla_checksum(*frame118, 0));

  const CAN_frame* frame221 = last_frame_with_id(0x221);
  ASSERT_NE(frame221, nullptr);
  EXPECT_EQ(frame221->data.u8[0] & 0xF0, 0x00);
  EXPECT_EQ(frame221->data.u8[7], tesla_checksum(*frame221));

  // Advance through one more phase-0 cycle so the alternating 0x053 sender is
  // due again when the profile reaches steady state.
  for (int i = 0; i < 4; ++i) {
    battery.transmit_can(1150);
  }
  clear_transmitted_frames();
  call_five_phases(battery, 4140);

  frame053 = last_frame_with_id(0x053);
  ASSERT_NE(frame053, nullptr);
  EXPECT_EQ(frame053->data.u8[0], 0xD4);
  EXPECT_EQ(frame053->data.u8[3], 0xCB);

  frame118 = last_frame_with_id(0x118);
  ASSERT_NE(frame118, nullptr);
  EXPECT_EQ(frame118->data.u8[2], 0xE9);
  EXPECT_EQ(frame118->data.u8[5], 0x48);
  EXPECT_EQ(frame118->data.u8[7], 0x00);
  EXPECT_EQ(frame118->data.u8[0], tesla_checksum(*frame118, 0));

  frame221 = last_frame_with_id(0x221);
  ASSERT_NE(frame221, nullptr);
  EXPECT_EQ(frame221->data.u8[0] & 0xF0, 0x20);
  EXPECT_EQ(frame221->data.u8[7], tesla_checksum(*frame221));

  const CAN_frame* frame3c2 = last_frame_with_id(0x3C2);
  ASSERT_NE(frame3c2, nullptr);
  EXPECT_TRUE(frame3c2->data.u8[0] == 0x10 || frame3c2->data.u8[0] == 0x01);
}

TEST(TeslaChargeMode, DoesNotDuplicateLiveChargePortFrameAndRestoresDriveProfile) {
  user_selected_battery_type = BatteryType::TeslaModel3Y;
  user_selected_tesla_digital_HVIL = false;
  set_millis64(1000);

  TeslaBattery battery;
  battery.setup();
  battery.start_charge_mode();

  set_millis64(4135);
  battery.handle_incoming_can_frame(charge_port_056());
  clear_transmitted_frames();
  call_five_phases(battery, 4140);
  EXPECT_EQ(last_frame_with_id(0x056), nullptr);

  battery.stop_charge_mode();
  EXPECT_FALSE(battery.is_charge_mode_active());
  clear_transmitted_frames();
  call_five_phases(battery, 4200);

  const CAN_frame* frame118 = last_frame_with_id(0x118);
  ASSERT_NE(frame118, nullptr);
  EXPECT_EQ(frame118->data.u8[1] & 0xF0, 0x60);
  EXPECT_EQ(frame118->data.u8[2], 0x2A);
  EXPECT_EQ(frame118->data.u8[5], 0x08);
  EXPECT_EQ(frame118->data.u8[7], 0x00);
  EXPECT_EQ(frame118->data.u8[0], tesla_checksum(*frame118, 0));
}
