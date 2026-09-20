#include <gtest/gtest.h>

#include <algorithm>
#include <initializer_list>
#include <string>
#include <vector>

#include "../../Software/src/battery/TESLA-BATTERY.h"
#include "../../Software/src/charger/CanCharger.h"
#include "Arduino.h"

void clear_transmitted_frames();
const std::vector<CAN_frame>& get_transmitted_frames();

namespace {
CAN_frame diagnostic_frame(uint32_t id, std::initializer_list<uint8_t> bytes) {
  CAN_frame result = {};
  result.ID = id;
  result.DLC = bytes.size();
  std::copy(bytes.begin(), bytes.end(), result.data.u8);
  return result;
}

class TeslaChargeDiagnosticsTest : public ::testing::Test {
 protected:
  const BatteryType saved_battery_type = user_selected_battery_type;
  const ChargerType saved_charger_type = user_selected_charger_type;
  const bool saved_hvil = user_selected_tesla_digital_HVIL;
  const bool saved_stop = datalayer.system.info.equipment_stop_active;
  const bool saved_permission = datalayer.system.status.inverter_allows_contactor_closing;
  const decltype(datalayer.system.status.system_status) saved_system_status = datalayer.system.status.system_status;
  const DATALAYER_INFO_TESLA saved_extended = datalayer_extended.tesla;
  TeslaBattery battery;

  void SetUp() override {
    user_selected_battery_type = BatteryType::TeslaModel3Y;
    user_selected_charger_type = ChargerType::TeslaModel3YPcs;
    user_selected_tesla_digital_HVIL = false;
    datalayer.system.info.equipment_stop_active = false;
    datalayer.system.status.inverter_allows_contactor_closing = true;
    datalayer.system.status.system_status = ACTIVE;
    // The extended layer is a union shared with other battery drivers. The
    // global test listener resets the main layer, but not this one.
    datalayer_extended.tesla = {};
    set_millis64(1000);
    battery.setup();
    clear_transmitted_frames();
  }

  void TearDown() override {
    user_selected_battery_type = saved_battery_type;
    user_selected_charger_type = saved_charger_type;
    user_selected_tesla_digital_HVIL = saved_hvil;
    datalayer.system.info.equipment_stop_active = saved_stop;
    datalayer.system.status.inverter_allows_contactor_closing = saved_permission;
    datalayer.system.status.system_status = saved_system_status;
    datalayer_extended.tesla = saved_extended;
    clear_transmitted_frames();
  }

  std::string html() { return battery.get_charge_mode_status_html().c_str(); }
  void receive(uint32_t id, std::initializer_list<uint8_t> bytes) {
    battery.handle_incoming_can_frame(diagnostic_frame(id, bytes));
  }
};
}  // namespace

TEST_F(TeslaChargeDiagnosticsTest, UnknownFeedbackIsNotReportedAsUnpluggedOrZero) {
  receive(0x21D, {});
  receive(0x264, {0});
  const std::string page = battery.get_status_renderer().get_status_html().c_str();
  EXPECT_NE(page.find("Battery Manufacture Date: Not received"), std::string::npos);
  EXPECT_NE(page.find("Last connector report: Not received"), std::string::npos);
  EXPECT_NE(page.find("AC charge-line report: Not received"), std::string::npos);
  EXPECT_NE(page.find("Normal inverter profile"), std::string::npos);
  EXPECT_NE(page.find("AC line readings do not establish DC"), std::string::npos);
  EXPECT_TRUE(get_transmitted_frames().empty());
  EXPECT_FALSE(battery.is_charge_mode_active());
}

TEST_F(TeslaChargeDiagnosticsTest, ShowsFreshnessAcrossMillisWrapWithoutClearingLastReport) {
  set_millis64(0xFFFFFF00ULL);
  receive(0x21D, {0x04});
  receive(0x264, {0, 0, 0, 0, 0, 0});
  set_millis64(0xFFFFFF00ULL + 2000);
  EXPECT_NE(html().find("Removed (2000 ms ago; recent)"), std::string::npos);
  EXPECT_NE(html().find("AC charge-line report: Recent (2000 ms ago)"), std::string::npos);
  set_millis64(0xFFFFFF00ULL + 2001);
  EXPECT_NE(html().find("Removed (2001 ms ago; stale)"), std::string::npos);
  EXPECT_NE(html().find("AC charge-line report: Stale (2001 ms ago)"), std::string::npos);
}

TEST_F(TeslaChargeDiagnosticsTest, ExposesRetainedStopOnReinsertionWithoutChangingTransmitProfile) {
  battery.start_charge_mode();
  receive(0x21D, {0x0C});
  battery.stop_charge_mode();
  // No completed handle/latch/removal sequence. A repeated start is a no-op.
  receive(0x21D, {0x0C});
  battery.start_charge_mode();
  const std::string page = battery.get_status_renderer().get_status_html().c_str();
  EXPECT_NE(page.find("Emulator profile: Stop/release pending"), std::string::npos);
  EXPECT_NE(page.find("Prepare to Charge cannot start a new session yet"), std::string::npos);
  EXPECT_NE(page.find("Connector reported inserted while stop/release is pending"), std::string::npos);
  EXPECT_NE(page.find("Handle press observed or inferred: No"), std::string::npos);
  EXPECT_TRUE(get_transmitted_frames().empty());
  ASSERT_TRUE(battery.is_charge_mode_active());
  set_millis64(1100);
  for (int i = 0; i < 5; ++i) {
    battery.transmit_can(1100);
  }
  const auto& frames = get_transmitted_frames();
  auto di = std::find_if(frames.begin(), frames.end(), [](const CAN_frame& frame) { return frame.ID == 0x118; });
  ASSERT_NE(di, frames.end());
  EXPECT_EQ(di->data.u8[7], 0x80);
}

TEST_F(TeslaChargeDiagnosticsTest, ShowsAllHandoffBlocksAndThenNormalProfileAfterExistingHandoff) {
  battery.start_charge_mode();
  receive(0x21D, {0x0C});
  receive(0x21D, {0x2A});
  receive(0x25D, {0x6C, 0x81, 0x23});
  receive(0x21D, {0x04});
  receive(0x264, {0, 0, 0, 0, 0, 0});
  receive(0x20A, {0xF6, 0x15, 0x09, 0x82, 0x18, 0x01});
  datalayer.system.info.equipment_stop_active = true;
  datalayer.system.status.system_status = FAULT;
  datalayer.system.status.inverter_allows_contactor_closing = false;
  const auto page = html();
  EXPECT_NE(page.find("Equipment stop: Active"), std::string::npos);
  EXPECT_NE(page.find("Emulator system fault: Active"), std::string::npos);
  EXPECT_NE(page.find("Inverter contactor permission: Not allowed"), std::string::npos);
  EXPECT_NE(page.find("Connector removal observed after release: Yes"), std::string::npos);
  EXPECT_TRUE(get_transmitted_frames().empty());
  battery.transmit_can(1000);
  ASSERT_TRUE(battery.is_charge_mode_active());

  datalayer.system.info.equipment_stop_active = false;
  datalayer.system.status.system_status = ACTIVE;
  datalayer.system.status.inverter_allows_contactor_closing = true;
  battery.transmit_can(1000);
  EXPECT_FALSE(battery.is_charge_mode_active());
  EXPECT_NE(html().find("Normal inverter profile"), std::string::npos);
  EXPECT_NE(html().find("Stop request pending: No"), std::string::npos);
  EXPECT_EQ(html().find("Recorded stop-sequence evidence"), std::string::npos);
}

TEST_F(TeslaChargeDiagnosticsTest, EachRendererUsesItsOwnChargeSession) {
  TeslaBattery other;
  other.setup();
  battery.start_charge_mode();
  EXPECT_NE(html().find("Charge profile active"), std::string::npos);
  const std::string other_page = other.get_status_renderer().get_status_html().c_str();
  EXPECT_NE(other_page.find("Normal inverter profile"), std::string::npos);
  EXPECT_EQ(other_page.find("Charge profile active"), std::string::npos);
}
