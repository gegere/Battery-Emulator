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
  auto match = std::find_if(frames.rbegin(), frames.rend(), [id](const CAN_frame& frame) { return frame.ID == id; });
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

CAN_frame closed_contactors_212() {
  CAN_frame frame = {};
  frame.ID = 0x212;
  frame.DLC = 8;
  frame.data.u8[1] = 0x04;
  return frame;
}

CAN_frame charge_line_264(uint8_t dlc = 6) {
  CAN_frame frame = {};
  frame.ID = 0x264;
  frame.DLC = dlc;
  const uint8_t payload[6] = {0xB7, 0x0D, 0x1E, 0x0E, 0x78, 0x00};
  std::copy(payload, payload + 6, frame.data.u8);
  return frame;
}

CAN_frame stopped_charge_line_264() {
  CAN_frame frame = {};
  frame.ID = 0x264;
  frame.DLC = 6;
  return frame;
}

void call_five_phases(TeslaBattery& battery, unsigned long now) {
  for (int i = 0; i < 5; ++i) {
    battery.transmit_can(now);
  }
}

}  // namespace

TEST(TeslaChargeLine, DecodesCapturedPcsFrameIndependentlyOfChargeMode) {
  user_selected_battery_type = BatteryType::TeslaModel3Y;
  user_selected_tesla_digital_HVIL = false;
  set_millis64(1000);

  TeslaBattery battery;
  battery.setup();
  ASSERT_TRUE(battery.supports_charge_line_measurements());
  ASSERT_FALSE(battery.is_charge_mode_active());

  battery.handle_incoming_can_frame(charge_line_264());

  EXPECT_TRUE(battery.is_charge_line_data_valid());
  EXPECT_NEAR(battery.get_charge_line_voltage_V(), 116.9f, 0.05f);
  EXPECT_FLOAT_EQ(battery.get_charge_line_current_A(), 12.0f);
  EXPECT_FLOAT_EQ(battery.get_charge_line_power_W(), 1400.0f);
  EXPECT_FLOAT_EQ(battery.get_charge_line_current_limit_A(), 12.0f);
}

TEST(TeslaChargeLine, IgnoresShortFramesAndExpiresWithoutZeroingMeasurements) {
  user_selected_battery_type = BatteryType::TeslaModel3Y;
  user_selected_tesla_digital_HVIL = false;
  set_millis64(1000);

  TeslaBattery battery;
  battery.setup();
  battery.handle_incoming_can_frame(charge_line_264());
  ASSERT_TRUE(battery.is_charge_line_data_valid());

  // A short frame arriving later must not refresh the valid sample's timer.
  set_millis64(2500);
  CAN_frame short_frame = charge_line_264(5);
  short_frame.data.u8[0] = 0;
  battery.handle_incoming_can_frame(short_frame);
  EXPECT_TRUE(battery.is_charge_line_data_valid());
  EXPECT_NEAR(battery.get_charge_line_voltage_V(), 116.9f, 0.05f);

  set_millis64(3001);
  EXPECT_FALSE(battery.is_charge_line_data_valid());
  EXPECT_NEAR(battery.get_charge_line_voltage_V(), 116.9f, 0.05f);
  EXPECT_FLOAT_EQ(battery.get_charge_line_current_A(), 12.0f);
  EXPECT_FLOAT_EQ(battery.get_charge_line_power_W(), 1400.0f);
  EXPECT_FLOAT_EQ(battery.get_charge_line_current_limit_A(), 12.0f);
}

TEST(TeslaChargeLine, IsNotAdvertisedForTeslaModelSx) {
  user_selected_battery_type = BatteryType::TeslaModelSX;
  set_millis64(1000);

  TeslaBattery battery;
  battery.setup();
  EXPECT_FALSE(battery.supports_charge_line_measurements());
}

TEST(TeslaChargeMode, EmitsMeasuredStartupAndSuccessfulChargeProfile) {
  user_selected_battery_type = BatteryType::TeslaModel3Y;
  user_selected_tesla_digital_HVIL = false;
  set_millis64(1000);

  TeslaBattery battery;
  battery.setup();
  battery.handle_incoming_can_frame(closed_contactors_212());
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

  const CAN_frame* frame333 = last_frame_with_id(0x333);
  ASSERT_NE(frame333, nullptr);
  EXPECT_EQ(frame333->data.u8[0], 0x04);

  const CAN_frame* frame334 = last_frame_with_id(0x334);
  ASSERT_NE(frame334, nullptr);
  EXPECT_EQ((frame334->data.u8[1] >> 6) & 0x03, 0x01);
  EXPECT_EQ(frame334->data.u8[7], tesla_checksum(*frame334));

  const CAN_frame* frame3c2 = last_frame_with_id(0x3C2);
  ASSERT_NE(frame3c2, nullptr);
  EXPECT_TRUE(frame3c2->data.u8[0] == 0x10 || frame3c2->data.u8[0] == 0x01);

  const CAN_frame* frame3a1 = last_frame_with_id(0x3A1);
  ASSERT_NE(frame3a1, nullptr);
  if (frame3a1->data.u8[0] == 0x88) {
    EXPECT_EQ(frame3a1->data.u8[6] & 0x0F, 0x02);
    EXPECT_EQ(frame3a1->data.u8[6] >> 4 & 0x01, 0x00);
  } else {
    EXPECT_EQ(frame3a1->data.u8[0], 0x03);
    EXPECT_EQ(frame3a1->data.u8[6] & 0x0F, 0x00);
    EXPECT_EQ(frame3a1->data.u8[6] >> 4 & 0x01, 0x01);
  }
}

TEST(TeslaChargeMode, ReplaysExact3A1CounterChecksumCycle) {
  user_selected_battery_type = BatteryType::TeslaModel3Y;
  user_selected_tesla_digital_HVIL = false;
  set_millis64(1000);

  TeslaBattery battery;
  battery.setup();
  battery.handle_incoming_can_frame(closed_contactors_212());
  battery.start_charge_mode();

  const uint8_t expectedByte6[16] = {0x02, 0x10, 0x22, 0x30, 0x42, 0x50, 0x62, 0x70,
                                     0x82, 0x90, 0xA2, 0xB0, 0xC2, 0xD0, 0xE2, 0xF0};
  const uint8_t expectedByte7[16] = {0xBA, 0xE2, 0xDA, 0x02, 0xFA, 0x22, 0x1A, 0x42,
                                     0x3A, 0x62, 0x5A, 0x82, 0x7A, 0xA2, 0x9A, 0xC2};

  uint8_t previousCounter = 0xFF;
  for (uint8_t i = 0; i < 16; ++i) {
    clear_transmitted_frames();
    call_five_phases(battery, 1050 + i * 50);
    const CAN_frame* frame3a1 = last_frame_with_id(0x3A1);
    ASSERT_NE(frame3a1, nullptr);
    const uint8_t counter = frame3a1->data.u8[6] >> 4;
    EXPECT_EQ(frame3a1->data.u8[6], expectedByte6[counter]);
    EXPECT_EQ(frame3a1->data.u8[7], expectedByte7[counter]);
    EXPECT_EQ(frame3a1->data.u8[0], (counter & 1) == 0 ? 0x88 : 0x03);
    if (previousCounter != 0xFF) {
      EXPECT_EQ(counter, static_cast<uint8_t>((previousCounter + 1) % 16));
    }
    previousCounter = counter;
  }
}

TEST(TeslaChargeMode, AdvertisesClosuresConfirmedFromFirst334Frame) {
  user_selected_battery_type = BatteryType::TeslaModel3Y;
  user_selected_tesla_digital_HVIL = false;
  set_millis64(1000);

  TeslaBattery battery;
  battery.setup();

  clear_transmitted_frames();
  call_five_phases(battery, 1500);
  const CAN_frame* frame334 = last_frame_with_id(0x334);
  ASSERT_NE(frame334, nullptr);
  EXPECT_EQ((frame334->data.u8[1] >> 6) & 0x03, 0x01);
  EXPECT_EQ(frame334->data.u8[7], tesla_checksum(*frame334));

  clear_transmitted_frames();
  call_five_phases(battery, 2000);
  frame334 = last_frame_with_id(0x334);
  ASSERT_NE(frame334, nullptr);
  EXPECT_EQ((frame334->data.u8[1] >> 6) & 0x03, 0x01);
  EXPECT_EQ(frame334->data.u8[7], tesla_checksum(*frame334));
}

TEST(TeslaChargeMode, SendsMeasured052OnlyWhileChargeModeIsActive) {
  user_selected_battery_type = BatteryType::TeslaModel3Y;
  user_selected_tesla_digital_HVIL = false;
  set_millis64(1000);

  TeslaBattery battery;
  battery.setup();
  battery.start_charge_mode();

  clear_transmitted_frames();
  call_five_phases(battery, 1100);

  const CAN_frame* frame052 = last_frame_with_id(0x052);
  ASSERT_NE(frame052, nullptr);
  const uint8_t expected052[8] = {0x85, 0x9B, 0xE4, 0x27, 0x65, 0x28, 0x30, 0x00};
  EXPECT_EQ(frame052->DLC, 8);
  EXPECT_TRUE(std::equal(expected052, expected052 + 8, frame052->data.u8));

  battery.stop_charge_mode();
  EXPECT_TRUE(battery.is_charge_mode_active());
  clear_transmitted_frames();
  call_five_phases(battery, 15999);
  EXPECT_NE(last_frame_with_id(0x052), nullptr);

  clear_transmitted_frames();
  call_five_phases(battery, 16000);
  EXPECT_EQ(last_frame_with_id(0x052), nullptr);
}

TEST(TeslaChargeMode, GracefullyStopsThenRequestsChargePortReleaseAtZeroCurrent) {
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
  EXPECT_TRUE(battery.is_charge_mode_active());
  clear_transmitted_frames();
  call_five_phases(battery, 4640);

  const CAN_frame* frame118 = last_frame_with_id(0x118);
  ASSERT_NE(frame118, nullptr);
  EXPECT_EQ(frame118->data.u8[1] & 0xF0, 0x80);
  EXPECT_EQ(frame118->data.u8[2], 0xE9);
  // Ingenext keeps DI_proximity asserted until after physical removal. This
  // authorization must survive the zero-current wait and release request.
  EXPECT_EQ(frame118->data.u8[5], 0x48);

  const CAN_frame* frame333 = last_frame_with_id(0x333);
  ASSERT_NE(frame333, nullptr);
  EXPECT_EQ(frame333->data.u8[0] & 0x04, 0x00);
  EXPECT_EQ(frame333->data.u8[0] & 0x01, 0x00);

  set_millis64(4640);
  battery.handle_incoming_can_frame(stopped_charge_line_264());
  call_five_phases(battery, 4640);

  set_millis64(5641);
  battery.handle_incoming_can_frame(stopped_charge_line_264());
  clear_transmitted_frames();
  call_five_phases(battery, 5641);

  ASSERT_TRUE(battery.is_charge_mode_active());
  frame333 = last_frame_with_id(0x333);
  ASSERT_NE(frame333, nullptr);
  EXPECT_EQ(frame333->data.u8[0], 0x01);

  set_millis64(6042);
  clear_transmitted_frames();
  call_five_phases(battery, 6042);

  EXPECT_FALSE(battery.is_charge_mode_active());
  frame118 = last_frame_with_id(0x118);
  ASSERT_NE(frame118, nullptr);
  EXPECT_EQ(frame118->data.u8[1] & 0xF0, 0x60);
  EXPECT_EQ(frame118->data.u8[2], 0x2A);
  EXPECT_EQ(frame118->data.u8[5], 0x08);
  EXPECT_EQ(frame118->data.u8[7], 0x00);
  EXPECT_EQ(frame118->data.u8[0], tesla_checksum(*frame118, 0));
}

TEST(TeslaChargeMode, NeverRequestsReleaseWithoutConfirmingZeroCurrent) {
  user_selected_battery_type = BatteryType::TeslaModel3Y;
  user_selected_tesla_digital_HVIL = false;
  set_millis64(1000);

  TeslaBattery battery;
  battery.setup();
  battery.start_charge_mode();
  battery.handle_incoming_can_frame(charge_line_264());
  battery.stop_charge_mode();

  set_millis64(15999);
  battery.handle_incoming_can_frame(charge_line_264());
  clear_transmitted_frames();
  call_five_phases(battery, 15999);
  ASSERT_TRUE(battery.is_charge_mode_active());
  const CAN_frame* frame333 = last_frame_with_id(0x333);
  ASSERT_NE(frame333, nullptr);
  EXPECT_EQ(frame333->data.u8[0] & 0x01, 0x00);

  set_millis64(16000);
  clear_transmitted_frames();
  call_five_phases(battery, 16000);
  EXPECT_FALSE(battery.is_charge_mode_active());
  EXPECT_EQ(last_frame_with_id(0x333), nullptr);
}
