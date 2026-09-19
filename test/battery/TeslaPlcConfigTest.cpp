#include <gtest/gtest.h>

#include <tuple>
#include <vector>

#include "../../Software/src/battery/TESLA-BATTERY.h"
#include "../../Software/src/charger/CanCharger.h"
#include "Arduino.h"

void clear_transmitted_frames();
const std::vector<CAN_frame>& get_transmitted_frames();

class TeslaPlcConfigTest : public ::testing::TestWithParam<std::tuple<uint16_t, uint8_t, bool>> {
 protected:
  void SetUp() override {
    user_selected_battery_type = BatteryType::TeslaModel3Y;
    user_selected_charger_type = ChargerType::TeslaModel3YPcs;
    user_selected_tesla_digital_HVIL = false;
  }

  void TearDown() override {
    user_selected_tesla_GTW_plcSupportType = original_plc;
    user_selected_battery_type = original_battery;
    user_selected_charger_type = original_charger;
    user_selected_tesla_digital_HVIL = original_hvil;
  }

  std::vector<CAN_frame> capture(uint16_t plc, bool charge_mode) {
    user_selected_tesla_GTW_plcSupportType = plc;
    set_millis64(1000);
    TeslaBattery battery;
    battery.setup();
    if (charge_mode) {
      battery.start_charge_mode();
    }
    clear_transmitted_frames();
    for (unsigned long now = 1100; now <= 2100; now += 100) {
      set_millis64(now);
      for (int phase = 0; phase < 5; ++phase) {
        battery.transmit_can(now);
      }
    }
    return get_transmitted_frames();
  }

 private:
  const uint16_t original_plc = user_selected_tesla_GTW_plcSupportType;
  const BatteryType original_battery = user_selected_battery_type;
  const ChargerType original_charger = user_selected_charger_type;
  const bool original_hvil = user_selected_tesla_digital_HVIL;
};

TEST_P(TeslaPlcConfigTest, ChangesOnlyPlcHardwareBitsAndLeavesContactorRequestsToBms) {
  const auto [setting, expected, charge_mode] = GetParam();
  const auto baseline = capture(0, charge_mode);
  const auto configured = capture(setting, charge_mode);
  ASSERT_EQ(baseline.size(), configured.size());
  size_t config_frames = 0;
  for (size_t i = 0; i < configured.size(); ++i) {
    const auto& before = baseline[i];
    const auto& after = configured[i];
    ASSERT_EQ(before.ID, after.ID);
    ASSERT_EQ(before.DLC, after.DLC);
    EXPECT_NE(after.ID, 0x21DU);  // Charge-port status is received, not fabricated.
    EXPECT_NE(after.ID, 0x232U);  // Fast-charge contactor requests belong to the BMS.
    EXPECT_NE(after.ID, 0x20AU);  // HVP contactor feedback is received.
    const bool plc_frame = after.ID == 0x7FF && after.data.u8[0] == 3;
    if (plc_frame) {
      ++config_frames;
      EXPECT_EQ((after.data.u8[3] >> 4) & 3, expected);
    }
    for (uint8_t byte = 0; byte < after.DLC; ++byte) {
      // 0x213 uses a function-static rolling counter shared across instances.
      // Compare its payload and checksum offset independently of that counter.
      if (after.ID == 0x213 && byte == 1) {
        EXPECT_EQ(static_cast<uint8_t>(before.data.u8[1] - before.data.u8[0]),
                  static_cast<uint8_t>(after.data.u8[1] - after.data.u8[0]));
        continue;
      }
      if (after.ID == 0x213 && byte == 0) {
        EXPECT_EQ(before.data.u8[0] & 0x0F, after.data.u8[0] & 0x0F);
        continue;
      }
      const uint8_t mask = plc_frame && byte == 3 ? 0xCF : 0xFF;
      EXPECT_EQ(before.data.u8[byte] & mask, after.data.u8[byte] & mask)
          << "CAN ID " << after.ID << ", byte " << static_cast<int>(byte);
    }
  }
  EXPECT_GE(config_frames, 2U);
}

INSTANTIATE_TEST_SUITE_P(HardwareSelection, TeslaPlcConfigTest,
                         ::testing::Values(std::make_tuple(0, 0, false), std::make_tuple(1, 1, false),
                                           std::make_tuple(2, 2, false), std::make_tuple(3, 0, false),
                                           std::make_tuple(65535, 0, false), std::make_tuple(0, 0, true),
                                           std::make_tuple(1, 1, true), std::make_tuple(2, 2, true),
                                           std::make_tuple(3, 0, true), std::make_tuple(65535, 0, true)));
