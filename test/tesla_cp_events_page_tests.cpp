#include <gtest/gtest.h>

#include <string>

// Compile the real Events renderer against the host String shim. These Arduino
// flash-string annotations have no meaning on the host, but the HTML does.
#define F(text) text
#define FPSTR(text) text
#include "../Software/src/devboard/webserver/events_html.cpp"
#undef FPSTR
#undef F

TEST(TeslaCpEventsPage, ShowsOfflineDescriptionAndExplicitActiveOrClearedState) {
  init_events();
  reset_all_events();
  set_event(EVENT_TESLA_CP_ALERTS_001_016, 1 << 12, 1);
  std::string html = events_processor("X").c_str();
  EXPECT_NE(html.find("<div>State</div>"), std::string::npos);
  EXPECT_NE(html.find("<div>WARNING</div><div>Active</div>"), std::string::npos);
  EXPECT_NE(html.find("CP_a013: Lost Comms GTW"), std::string::npos);
  EXPECT_EQ(html.find("fetch("), std::string::npos);

  clear_event(EVENT_TESLA_CP_ALERTS_001_016, 1);
  html = events_processor("X").c_str();
  EXPECT_NE(html.find("<div>WARNING</div><div>Cleared</div>"), std::string::npos);
  EXPECT_NE(html.find("CP_a013: Lost Comms GTW"), std::string::npos);
  reset_all_events();
}

TEST(TeslaCpEventsPage, MissingChargePortIncludesPowerAndCanExplanation) {
  init_events();
  reset_all_events();
  set_event(EVENT_TESLA_CP_MISSING, 7, 1);
  const std::string html = events_processor("X").c_str();
  EXPECT_NE(html.find("Check charge-port low-voltage power and CAN"), std::string::npos);
  EXPECT_NE(html.find("BMS_a091 BMS_a092 PCS_a023"), std::string::npos);
  reset_all_events();
}
