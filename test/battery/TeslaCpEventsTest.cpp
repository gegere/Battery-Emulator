#include <gtest/gtest.h>

#include <algorithm>
#include <initializer_list>
#include <string>

#include "../../Software/src/battery/TESLA-BATTERY.h"
#include "Arduino.h"

namespace {
CAN_frame frame(uint32_t id, std::initializer_list<uint8_t> bytes) {
  CAN_frame result = {};
  result.ID = id;
  result.DLC = bytes.size();
  std::copy(bytes.begin(), bytes.end(), result.data.u8);
  return result;
}

class TeslaCpEventsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    saved_recovery = datalayer.battery.settings.user_requests_forced_charging_recovery_mode;
    datalayer.battery.settings.user_requests_forced_charging_recovery_mode = false;
    init_events();
    reset_all_events();
    set_millis64(1000);
  }
  void TearDown() override {
    reset_all_events();
    datalayer.battery.settings.user_requests_forced_charging_recovery_mode = saved_recovery;
  }
  bool saved_recovery = false;
  TeslaBattery battery;

  static const EVENTS_STRUCT_TYPE& event(EVENTS_ENUM_TYPE id) { return *get_event_pointer(id); }
  static std::string message(EVENTS_ENUM_TYPE id) { return get_event_message_string(id).c_str(); }
};

TEST_F(TeslaCpEventsTest, CapturedChargePortFaultsAppearTogetherAsWarnings) {
  battery.handle_incoming_can_frame(frame(0x31E, {0, 0, 1, 0, 8, 0, 13, 0}));
  EXPECT_EQ(event(EVENT_TESLA_CP_ALERTS_001_016).state, EVENT_STATE_ACTIVE);
  EXPECT_EQ(event(EVENT_TESLA_CP_ALERTS_017_032).state, EVENT_STATE_ACTIVE);
  EXPECT_EQ(event(EVENT_TESLA_CP_ALERTS_033_048).state, EVENT_STATE_ACTIVE);
  EXPECT_NE(message(EVENT_TESLA_CP_ALERTS_001_016).find("CP_a013: Lost Comms GTW"), std::string::npos);
  EXPECT_NE(message(EVENT_TESLA_CP_ALERTS_017_032).find("CP_a032: Door Open Expected Closed"), std::string::npos);
  const auto comms = message(EVENT_TESLA_CP_ALERTS_033_048);
  EXPECT_NE(comms.find("CP_a045: Lost Comms VCSEC"), std::string::npos);
  EXPECT_NE(comms.find("CP_a047: Lost Comms VCFRONT"), std::string::npos);
  EXPECT_NE(comms.find("CP_a048: Lost Comms UI"), std::string::npos);
  EXPECT_EQ(get_event_level(), EVENT_LEVEL_WARNING);
  EXPECT_NE(datalayer.system.status.system_status, FAULT);
  EXPECT_FALSE(battery.is_charge_mode_active());
}

TEST_F(TeslaCpEventsTest, RepeatedFramesDoNotFloodCountsAndClearKeepsHistory) {
  const auto active = frame(0x31E, {0, 0, 1, 0, 0, 0, 0, 0});
  battery.handle_incoming_can_frame(active);
  const auto first = event(EVENT_TESLA_CP_ALERTS_001_016);
  set_millis64(9000);
  for (int i = 0; i < 300; ++i)
    battery.handle_incoming_can_frame(active);
  EXPECT_EQ(event(EVENT_TESLA_CP_ALERTS_001_016).occurences, 1);
  EXPECT_EQ(event(EVENT_TESLA_CP_ALERTS_001_016).timestamp, first.timestamp);
  battery.handle_incoming_can_frame(frame(0x31E, {0, 0, 0, 0, 0, 0, 0, 0}));
  EXPECT_EQ(event(EVENT_TESLA_CP_ALERTS_001_016).state, EVENT_STATE_INACTIVE);
  EXPECT_EQ(event(EVENT_TESLA_CP_ALERTS_001_016).occurences, 1);
  EXPECT_NE(message(EVENT_TESLA_CP_ALERTS_001_016).find("CP_a013"), std::string::npos);
  battery.handle_incoming_can_frame(active);
  EXPECT_EQ(event(EVENT_TESLA_CP_ALERTS_001_016).occurences, 2);
}

TEST_F(TeslaCpEventsTest, MuxBoundaryPreservesOtherMuxAndHighSignedBit) {
  battery.handle_incoming_can_frame(frame(0x31E, {0, 0, 0, 0, 0, 0, 0, 128}));  // a060
  battery.handle_incoming_can_frame(frame(0x31E, {17, 0, 0, 0, 128}));          // a061, a096
  EXPECT_NE(message(EVENT_TESLA_CP_ALERTS_049_064).find("CP_a060"), std::string::npos);
  EXPECT_NE(message(EVENT_TESLA_CP_ALERTS_049_064).find("CP_a061"), std::string::npos);
  EXPECT_NE(message(EVENT_TESLA_CP_ALERTS_081_096).find("CP_a096"), std::string::npos);
  EXPECT_EQ(static_cast<uint16_t>(event(EVENT_TESLA_CP_ALERTS_081_096).data), 0x8000);
  battery.handle_incoming_can_frame(frame(0x31E, {0, 0, 0, 0, 0, 0, 0, 0}));
  EXPECT_EQ(event(EVENT_TESLA_CP_ALERTS_049_064).state, EVENT_STATE_ACTIVE);
  EXPECT_EQ(message(EVENT_TESLA_CP_ALERTS_049_064).find("CP_a060"), std::string::npos);
  EXPECT_NE(message(EVENT_TESLA_CP_ALERTS_049_064).find("CP_a061"), std::string::npos);
}

TEST_F(TeslaCpEventsTest, ShortAndUnknownMuxFramesCannotClearReportedFaults) {
  battery.handle_incoming_can_frame(frame(0x31E, {0, 0, 1, 0, 0, 0, 0, 0}));
  battery.handle_incoming_can_frame(frame(0x31E, {}));
  battery.handle_incoming_can_frame(frame(0x31E, {0, 0, 0, 0, 0, 0, 0}));
  battery.handle_incoming_can_frame(frame(0x31E, {1, 0, 0, 0}));
  battery.handle_incoming_can_frame(frame(0x31E, {2, 8, 0, 0, 0, 0, 0, 0}));
  EXPECT_EQ(event(EVENT_TESLA_CP_ALERTS_001_016).state, EVENT_STATE_ACTIVE);
  EXPECT_EQ(event(EVENT_TESLA_CP_ALERTS_001_016).occurences, 1);
  EXPECT_EQ(event(EVENT_TESLA_CP_ALERTS_065_080).occurences, 0);
}

TEST_F(TeslaCpEventsTest, ClearEventsReappearsOnNextActiveReport) {
  const auto active = frame(0x31E, {1, 8, 0, 0, 0});
  battery.handle_incoming_can_frame(active);
  reset_all_events();
  battery.handle_incoming_can_frame(active);
  EXPECT_EQ(event(EVENT_TESLA_CP_ALERTS_065_080).state, EVENT_STATE_ACTIVE);
  EXPECT_NE(message(EVENT_TESLA_CP_ALERTS_065_080).find("CP_a068: Door Sensor Mismatch"), std::string::npos);
}

TEST_F(TeslaCpEventsTest, MissingChargePortCombinesBmsAndPcsUntilBothRecover) {
  battery.handle_incoming_can_frame(frame(0x320, {1, 0, 0, 0, 12, 0, 0, 0}));
  battery.handle_incoming_can_frame(frame(0x3A4, {0, 0, 0, 4, 0, 0, 0, 0}));
  EXPECT_EQ(event(EVENT_TESLA_CP_MISSING).data, 7);
  EXPECT_NE(message(EVENT_TESLA_CP_MISSING).find("BMS_a091 BMS_a092 PCS_a023"), std::string::npos);
  battery.handle_incoming_can_frame(frame(0x320, {1, 0, 0, 0, 0, 0, 0}));  // incomplete
  battery.handle_incoming_can_frame(frame(0x3A4, {0, 0, 0, 0}));           // incomplete
  EXPECT_EQ(event(EVENT_TESLA_CP_MISSING).data, 7);
  battery.handle_incoming_can_frame(frame(0x320, {1, 0, 0, 0, 0, 0, 0, 0}));
  EXPECT_EQ(event(EVENT_TESLA_CP_MISSING).data, 4);
  EXPECT_EQ(event(EVENT_TESLA_CP_MISSING).state, EVENT_STATE_ACTIVE);
  battery.handle_incoming_can_frame(frame(0x3A4, {0, 0, 0, 0, 0, 0, 0, 0}));
  EXPECT_EQ(event(EVENT_TESLA_CP_MISSING).state, EVENT_STATE_INACTIVE);
}

TEST_F(TeslaCpEventsTest, SeparatePacksCannotClearEachOthersWarnings) {
  TeslaBattery second;
  second.battery_index = 2;
  battery.handle_incoming_can_frame(frame(0x31E, {0, 0, 1, 0, 0, 0, 0, 0}));
  second.handle_incoming_can_frame(frame(0x31E, {0, 0, 2, 0, 0, 0, 0, 0}));
  EXPECT_NE(message(EVENT_TESLA_CP_ALERTS_001_016_BAT2).find("CP_a014"), std::string::npos);
  EXPECT_NE(message(EVENT_TESLA_CP_ALERTS_001_016_BAT2).find("Battery 2"), std::string::npos);
  second.handle_incoming_can_frame(frame(0x31E, {0, 0, 0, 0, 0, 0, 0, 0}));
  EXPECT_EQ(event(EVENT_TESLA_CP_ALERTS_001_016).state, EVENT_STATE_ACTIVE);
  EXPECT_EQ(event(EVENT_TESLA_CP_ALERTS_001_016_BAT2).state, EVENT_STATE_INACTIVE);
}

TEST_F(TeslaCpEventsTest, DiagnosticWarningDoesNotClearBlockingSafetyEvent) {
  set_event(EVENT_EQUIPMENT_STOP, 1);
  battery.handle_incoming_can_frame(frame(0x31E, {0, 0, 1, 0, 0, 0, 0, 0}));
  EXPECT_EQ(datalayer.system.status.system_status, FAULT);
  battery.handle_incoming_can_frame(frame(0x31E, {0, 0, 0, 0, 0, 0, 0, 0}));
  EXPECT_EQ(datalayer.system.status.system_status, FAULT);
  EXPECT_EQ(event(EVENT_EQUIPMENT_STOP).state, EVENT_STATE_ACTIVE);
}
}  // namespace
