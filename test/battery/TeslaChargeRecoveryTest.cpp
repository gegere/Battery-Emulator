#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string>
#include <vector>

#include "../../Software/src/battery/TESLA-BATTERY.h"
#include "../../Software/src/charger/CanCharger.h"
#include "Arduino.h"

void clear_transmitted_frames();
const std::vector<CAN_frame>& get_transmitted_frames();

namespace {
class TeslaChargeRecoveryTest : public ::testing::Test {
 protected:
  TeslaBattery battery;
  void SetUp() override {
    user_selected_battery_type = BatteryType::TeslaModel3Y;
    user_selected_charger_type = ChargerType::TeslaModel3YPcs;
    user_selected_tesla_digital_HVIL = false;
    datalayer.system.status.inverter_allows_contactor_closing = true;
    set_millis64(1000);
    battery.setup();
    clear_transmitted_frames();
  }
  void receive(uint32_t id, std::initializer_list<uint8_t> bytes) {
    CAN_frame frame = {};
    frame.ID = id;
    frame.DLC = bytes.size();
    std::copy(bytes.begin(), bytes.end(), frame.data.u8);
    battery.handle_incoming_can_frame(frame);
  }
  void open_fast_path() { receive(0x20A, {0xF6, 0x15, 0x09, 0x82, 0x18, 0x01}); }
  void zero_ac() { receive(0x264, {0, 0, 0, 0, 0, 0}); }
  void pending_stop() {
    battery.start_charge_mode();
    receive(0x21D, {0x0C});
    battery.stop_charge_mode();
  }
  void removed_for_recovery(uint64_t start = 1000) {
    set_millis64(start);
    receive(0x21D, {0x04});
    set_millis64(start + 1000);
    receive(0x21D, {0x04});
    set_millis64(start + 2001);
    receive(0x21D, {0x04});
    open_fast_path();
    zero_ac();
  }
  void full_release() {
    receive(0x21D, {0x2A});
    receive(0x25D, {0x6C, 0x81, 0x23});
    receive(0x21D, {0x04});
    zero_ac();
  }
  void tick() {
    clear_transmitted_frames();
    for (int i = 0; i < 5; ++i) {
      battery.transmit_can(millis());
    }
  }
  const CAN_frame* tx(uint32_t id) {
    const auto& frames = get_transmitted_frames();
    auto found = std::find_if(frames.begin(), frames.end(), [id](const CAN_frame& f) { return f.ID == id; });
    return found == frames.end() ? nullptr : &*found;
  }
};
}  // namespace

TEST_F(TeslaChargeRecoveryTest, RejectsFreshStartDuringEquipmentStopOrSystemFault) {
  datalayer.system.info.equipment_stop_active = true;
  EXPECT_FALSE(battery.can_start_charge_mode());
  battery.start_charge_mode();
  EXPECT_FALSE(battery.is_charge_mode_active());
  datalayer.system.info.equipment_stop_active = false;
  datalayer.system.status.system_status = FAULT;
  EXPECT_FALSE(battery.can_start_charge_mode());
  battery.start_charge_mode();
  EXPECT_FALSE(battery.is_charge_mode_active());
  tick();
  EXPECT_EQ(tx(0x339), nullptr);
  EXPECT_EQ(tx(0x052), nullptr);
  EXPECT_EQ(tx(0x207), nullptr);
}

TEST_F(TeslaChargeRecoveryTest, ExplicitRestartRecoversMissedLatchFeedbackWithRealUnpluggedProof) {
  pending_stop();
  removed_for_recovery();  // No latch report: automatic handoff cannot finish.
  tick();
  ASSERT_TRUE(battery.is_charge_mode_active());
  ASSERT_NE(tx(0x118), nullptr);
  EXPECT_EQ(tx(0x118)->data.u8[7], 0x80);
  Battery* command_target = &battery;
  ASSERT_TRUE(command_target->can_start_charge_mode());
  EXPECT_NE(std::string(battery.get_charge_mode_status_html().c_str()).find("Unplugged recovery checks passed"),
            std::string::npos);
  command_target->start_charge_mode();
  EXPECT_TRUE(battery.is_charge_mode_active());
  EXPECT_FALSE(battery.can_prepare_to_unplug());
  EXPECT_FALSE(battery.can_start_charge_mode());  // Already running; no repeated restart.
  set_millis64(3101);
  tick();
  ASSERT_NE(tx(0x118), nullptr);
  EXPECT_EQ(tx(0x118)->data.u8[7], 0x00);
  EXPECT_EQ(tx(0x118)->data.u8[2], 0x2D);  // Starts from the existing initial profile.
  EXPECT_EQ(tx(0x207), nullptr);
  EXPECT_EQ(tx(0x500), nullptr);
  ASSERT_NE(tx(0x333), nullptr);
  EXPECT_EQ(tx(0x333)->data.u8[0], 0x05);
}

TEST_F(TeslaChargeRecoveryTest, ReinsertionAndBackendStartDoNotClearPendingStop) {
  pending_stop();
  removed_for_recovery();
  ASSERT_TRUE(battery.can_start_charge_mode());
  receive(0x21D, {0x0C});  // Race after the web page offered the control.
  EXPECT_FALSE(battery.can_start_charge_mode());
  battery.start_charge_mode();
  tick();
  ASSERT_NE(tx(0x118), nullptr);
  EXPECT_EQ(tx(0x118)->data.u8[7], 0x80);
}

TEST_F(TeslaChargeRecoveryTest, RecoveryRequiresStableFreshRemovalAcrossRolloverAndGaps) {
  pending_stop();
  removed_for_recovery(0xFFFFFF00ULL);
  ASSERT_TRUE(battery.can_start_charge_mode());
  set_millis64(0xFFFFFF00ULL + 4002);
  open_fast_path();
  zero_ac();
  EXPECT_FALSE(battery.can_start_charge_mode());  // CP is stale.
  receive(0x21D, {0x04});
  EXPECT_FALSE(battery.can_start_charge_mode());  // Gap restarts the dwell.
  set_millis64(0xFFFFFF00ULL + 6002);
  receive(0x21D, {0x04});
  open_fast_path();
  zero_ac();
  EXPECT_TRUE(battery.can_start_charge_mode());
}

TEST_F(TeslaChargeRecoveryTest, InvalidShortOrStaleHvpCannotAuthorizeRecovery) {
  pending_stop();
  removed_for_recovery();
  // Independently invalidate each required field in the captured open report.
  const std::array<std::pair<unsigned, uint8_t>, 8> invalid_fields = {
      {{0, 0xB6}, {0, 0x76}, {1, 0x05}, {2, 0x08}, {2, 0x29}, {5, 0x11}, {5, 0x21}, {5, 0x31}}};
  for (auto [offset, value] : invalid_fields) {
    uint8_t data[6] = {0xF6, 0x15, 0x09, 0x82, 0x18, 0x01};
    data[offset] = value;
    receive(0x20A, {data[0], data[1], data[2], data[3], data[4], data[5]});
    EXPECT_FALSE(battery.can_start_charge_mode());
  }
  receive(0x20A, {0x36, 0x15, 0x09, 0x82, 0x18, 0x01});  // Aux disagrees.
  EXPECT_FALSE(battery.can_start_charge_mode());
  open_fast_path();
  set_millis64(4000);
  receive(0x21D, {0x04});
  set_millis64(5002);
  receive(0x21D, {0x04});
  receive(0x20A, {0xF6, 0x15, 0x09});  // Must not refresh the HVP timestamp.
  zero_ac();
  EXPECT_FALSE(battery.can_start_charge_mode());
  battery.start_charge_mode();
  tick();
  ASSERT_NE(tx(0x118), nullptr);
  EXPECT_EQ(tx(0x118)->data.u8[7], 0x80);
}

TEST_F(TeslaChargeRecoveryTest, EachExistingPermissionAndLiveAcIndependentlyBlockRecovery) {
  pending_stop();
  removed_for_recovery();
  ASSERT_TRUE(battery.can_start_charge_mode());
  datalayer.system.info.equipment_stop_active = true;
  EXPECT_FALSE(battery.can_start_charge_mode());
  datalayer.system.info.equipment_stop_active = false;
  datalayer.system.status.system_status = FAULT;
  EXPECT_FALSE(battery.can_start_charge_mode());
  datalayer.system.status.system_status = ACTIVE;
  datalayer.system.status.inverter_allows_contactor_closing = false;
  EXPECT_FALSE(battery.can_start_charge_mode());
  datalayer.system.status.inverter_allows_contactor_closing = true;
  receive(0x264, {0xB7, 0x0D, 0x1E, 0x0E, 0x78, 0});
  EXPECT_FALSE(battery.can_start_charge_mode());
  zero_ac();
  EXPECT_TRUE(battery.can_start_charge_mode());
}

TEST_F(TeslaChargeRecoveryTest, AcZeroCannotAuthorizeHandoffWithUnknownOrClosedFastContactors) {
  pending_stop();
  full_release();
  tick();
  EXPECT_TRUE(battery.is_charge_mode_active());  // No HVP report at all.
  receive(0x20A, {0x36, 0x65, 0x2E, 0x82, 0x18, 0x21});
  tick();
  EXPECT_TRUE(battery.is_charge_mode_active());
  open_fast_path();
  tick();
  EXPECT_FALSE(battery.is_charge_mode_active());
}

TEST_F(TeslaChargeRecoveryTest, LatchedRemovalCannotHandoffAfterReinsertionOrFeedbackLoss) {
  pending_stop();
  full_release();
  open_fast_path();
  receive(0x21D, {0x0C});
  tick();
  EXPECT_TRUE(battery.is_charge_mode_active());
  receive(0x21D, {0x04});
  set_millis64(3001);
  open_fast_path();
  zero_ac();
  tick();
  EXPECT_TRUE(battery.is_charge_mode_active());
  receive(0x21D, {0x04});
  tick();
  EXPECT_FALSE(battery.is_charge_mode_active());
}
