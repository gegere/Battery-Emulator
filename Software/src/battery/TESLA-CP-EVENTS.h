#ifndef TESLA_CP_EVENTS_H
#define TESLA_CP_EVENTS_H

#include <WString.h>
#include <stdint.h>
#include <stdio.h>
#include "../devboard/utils/events.h"

// CP_a001..CP_a096, matching web_data/dtc/tesla_model3y_dtc.json.
// Keep these descriptions local so Events also explains faults without internet.
inline constexpr const char* TESLA_CP_EVENT_DESCRIPTIONS[] = {
    "Can Rx",
    "Can Tx",
    "Can Error",
    "Proximity Rationality",
    "Gbdc Live Disconnect",
    "Lost Comms BMS",
    "Watchdog",
    "Memory Error",
    "Cover Open All Chg Blocked (until 2022.36.20: Cover Open)",
    "Pilot Rationality",
    "Eeprom",
    "Led Driver",
    "Lost Comms GTW",
    "Lost Comms CHG",
    "Aps Vov",
    "Aps Vuv",
    "Five Vov",
    "Five Vuv",
    "Three Vov",
    "Three Vuv",
    "Zero Vov",
    "Zero Vuv",
    "Gbdc Session Failed",
    "Leds UC",
    "Leds OC",
    "Network Management",
    "Door Sensor Out Of Spec",
    "Insert Enable Mismatch",
    "Door Closed Prox Pilot",
    "Bus Off",
    "Door Closed Commanded Open",
    "Door Open Expected Closed",
    "Spi Open",
    "Calibration Incomplete",
    "Latch Movement 1",
    "Latch Not Disengaged",
    "Latch Not Engaged",
    "Latch Not Blocking",
    "Latch Movement 2",
    "Do Not Use",
    "Door Sensor Unplugged",
    "Door Assembly Broken",
    "Door Pot Irrational",
    "Lost Comms HVP",
    "Lost Comms VCSEC",
    "Lost Comms EVSE",
    "Lost Comms VCFRONT",
    "Lost Comms UI",
    "Multiple Cables Detected",
    "Latch Not Connected",
    "Door Inductive Sensor MIA",
    "Chademo Not Supported (until 2023.38.9.1: Evse Not Supported)",
    "Prox Latched No Pilot",
    "Cable Not Secured",
    "Charge Stopped No Pilot",
    "Prox Disconnected",
    "Ccs1 Adapter Btn Press (until 2022.12.3.16: Deprecated Alert57) (earlier firmware: Evse Faulted)",
    "Ac Charge Retry Pending (until 2024.33.5: Ac Charging Blocked)",
    "Swcan Error",
    "Lost Comms PCS",
    "Uhf Receiver MIA",
    "Sc Out Of Service",
    "Sc Update In Progress",
    "Supercharging Blocked",
    "Self Test Failed",
    "Prox Latched Idle Pilot",
    "Gbdc Conn Fault",
    "Door Sensor Mismatch",
    "Door Inductive Sensor Error",
    "Door Inductive Sensor Reset",
    "Exi Decode Failure",
    "V2g Evcc Timeout",
    "Iec Combo Shutdown",
    "Failed To Establish V2g Comm",
    "V2g Comms Failure",
    "LDC1612error Watchdog",
    "Invalid Mac Address",
    "Latch Not Disengaged Cold",
    "Cable Not Secured Cold",
    "Task Stack Overflow",
    "Sw Exception",
    "Power On Reset",
    "Watchdog Trace Data",
    "Prox Pe Disconnected Gb (earlier firmware: Proximity Pe Disconnected)",
    "Dc Pin Temp Faulted",
    "Dc Pin Temp Irrational",
    "Dc Temp Model Fault",
    "Dc Temp Model Deviation",
    "Plc Config Mismatch",
    "Ccs Evse Low Iso",
    "Wrong Supercharger Handle",
    "Modem App Load Failed",
    "Modem Loaded With Reset",
    "Inductive Reset Successful",
    "Thermal Dc Limit Active",
    "Pilot Wake",
};

inline constexpr EVENTS_ENUM_TYPE tesla_cp_event(unsigned group) {
  return static_cast<EVENTS_ENUM_TYPE>(EVENT_TESLA_CP_ALERTS_001_016 + group * 3);
}

static_assert(EVENT_TESLA_CP_ALERTS_081_096_BAT3 == EVENT_TESLA_CP_ALERTS_001_016 + 17,
              "Tesla CP events must remain contiguous per-pack triplets");
static_assert(sizeof(TESLA_CP_EVENT_DESCRIPTIONS) / sizeof(TESLA_CP_EVENT_DESCRIPTIONS[0]) == 96);

inline String tesla_cp_event_message(unsigned group, uint16_t mask) {
  String message = "Tesla charge port: ";
  bool first = true;
  for (unsigned bit = 0; bit < 16; ++bit) {
    if (!(mask & (uint16_t(1) << bit))) {
      continue;
    }
    const unsigned index = group * 16 + bit;
    if (index >= 96) {
      continue;
    }
    char code[16];
    snprintf(code, sizeof(code), "CP_a%03u: ", index + 1);
    if (!first) {
      message += "; ";
    }
    first = false;
    message += code;
    message += TESLA_CP_EVENT_DESCRIPTIONS[index];
  }
  if (first) {
    message += "no reported alerts in this group";
  }
  return message;
}
#endif
