/*
  Host-side unit tests for Logic.h (CRC, hysteresis, windows, persist CRC).
  Built with: make test
*/

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Logic.h"
#include "Types.h"

static int g_failed = 0;
static int g_passed = 0;

#define EXPECT(cond)                                                           \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      g_failed++;                                                              \
    } else {                                                                   \
      g_passed++;                                                              \
    }                                                                          \
  } while (0)

static void test_crc() {
  const char *s = "123456789";
  EXPECT(crc16_ccitt(reinterpret_cast<const uint8_t *>(s), 9) == 0x29B1);
  uint8_t empty = 0;
  (void)empty;
  EXPECT(crc16_ccitt(reinterpret_cast<const uint8_t *>(""), 0) == 0xFFFF);
}

static void test_moisture_percent() {
  EXPECT(moisture_percent(200, 200, 800, true) == 0);
  EXPECT(moisture_percent(800, 200, 800, true) == 100);
  EXPECT(moisture_percent(500, 200, 800, true) == 50);
  EXPECT(moisture_percent(0, 200, 800, true) == 0);
  EXPECT(moisture_percent(1023, 200, 800, true) == 100);
  EXPECT(moisture_percent(500, 200, 200, true) == 0);
  EXPECT(moisture_percent(850, 200, 1500, true) == 50);
  EXPECT(moisture_percent(300, 800, 200, false) == 83);
  EXPECT(moisture_percent(800, 800, 200, false) == 0);
  EXPECT(moisture_percent(200, 800, 200, false) == 100);
}

static void test_hysteresis() {
  EXPECT(decide_watering(20, 35, 55) == WD_WATER);
  EXPECT(decide_watering(35, 35, 55) == WD_WATER);
  EXPECT(decide_watering(40, 35, 55) == WD_NONE);
  EXPECT(decide_watering(55, 35, 55) == WD_WET);
  EXPECT(decide_watering(90, 35, 55) == WD_WET);
  EXPECT(decide_watering(10, 55, 35) == WD_NONE);
}

static void test_offset() {
  EXPECT(apply_threshold_offset(35, 0) == 35);
  EXPECT(apply_threshold_offset(35, 10) == 45);
  EXPECT(apply_threshold_offset(35, -40) == 5);
  EXPECT(apply_threshold_offset(80, 20) == 90);
}

static void test_average() {
  uint16_t s3[3] = {10, 20, 100};
  EXPECT(average_trim_minmax(s3, 3) == 20);
  uint16_t s1[1] = {42};
  EXPECT(average_trim_minmax(s1, 1) == 42);
  uint16_t s0[1] = {0};
  EXPECT(average_trim_minmax(s0, 0) == 0);
  uint16_t s7[7] = {1, 10, 10, 10, 10, 10, 99};
  EXPECT(average_trim_minmax(s7, 7) == 10);
}

static void test_plausible() {
  EXPECT(raw_in_plausible_range(500, 10, 1013));
  EXPECT(!raw_in_plausible_range(0, 10, 1013));
  EXPECT(!delta_implausible(100, 120, 450, true));
  EXPECT(delta_implausible(100, 700, 450, true));
  EXPECT(!delta_implausible(100, 700, 450, false));
  EXPECT(moisture_rose_enough(400, 430, 25, true));
  EXPECT(!moisture_rose_enough(400, 410, 25, true));
  EXPECT(moisture_rose_enough(400, 370, 25, false));
}

static void test_time_and_window() {
  CivilTime t;
  std::memset(&t, 0, sizeof(t));
  t.year = 2026;
  t.month = 8;
  t.day = 25;
  t.hour = 5;
  t.minute = 0;
  t.second = 0;
  EXPECT(rtc_time_valid(&t));
  t.year = 2020;
  EXPECT(!rtc_time_valid(&t));
  t.year = 2026;
  t.month = 13;
  EXPECT(!rtc_time_valid(&t));
  t.month = 8;
  t.hour = 24;
  EXPECT(!rtc_time_valid(&t));

  EXPECT(in_irrigation_window(5, 5, 8, 19, 22));
  EXPECT(in_irrigation_window(7, 5, 8, 19, 22));
  EXPECT(!in_irrigation_window(8, 5, 8, 19, 22));
  EXPECT(in_irrigation_window(19, 5, 8, 19, 22));
  EXPECT(!in_irrigation_window(12, 5, 8, 19, 22));
  EXPECT(hour_in_span(23, 22, 2));
  EXPECT(hour_in_span(0, 22, 2));
  EXPECT(!hour_in_span(12, 22, 2));
}

static void test_budget_battery_pulse() {
  EXPECT(battery_ok(3300, 3000));
  EXPECT(!battery_ok(2999, 3000));
  EXPECT(daily_budget_remaining(0, 4));
  EXPECT(daily_budget_remaining(3, 4));
  EXPECT(!daily_budget_remaining(4, 4));
  EXPECT(cap_pulse_ms(50, 150) == 50);
  EXPECT(cap_pulse_ms(5, 150) == 10);
  EXPECT(cap_pulse_ms(200, 150) == 150);
}

static void test_alarm_math() {
  uint8_t hh = 0;
  uint8_t days = 0;
  next_daily_alarm(3, 5, 19, &hh, &days);
  EXPECT(hh == 5 && days == 0);
  next_daily_alarm(5, 5, 19, &hh, &days);
  EXPECT(hh == 19 && days == 0);
  next_daily_alarm(18, 5, 19, &hh, &days);
  EXPECT(hh == 19 && days == 0);
  next_daily_alarm(19, 5, 19, &hh, &days);
  EXPECT(hh == 5 && days == 1);
  next_daily_alarm(20, 5, 19, &hh, &days);
  EXPECT(hh == 5 && days == 1);

  uint8_t mm = 0;
  add_minutes(23, 50, 30, &hh, &mm, &days);
  EXPECT(hh == 0 && mm == 20 && days == 1);
  add_minutes(5, 0, 30, &hh, &mm, &days);
  EXPECT(hh == 5 && mm == 30 && days == 0);
}

static void test_bcd() {
  EXPECT(bcd_to_bin(0x23) == 23);
  EXPECT(bin_to_bcd(23) == 0x23);
  EXPECT(bcd_to_bin(0x00) == 0);
  EXPECT(bin_to_bcd(9) == 0x09);
  EXPECT(bcd_to_bin(0x59) == 59);
}

static void test_persist_crc_roundtrip() {
  PersistentState s;
  std::memset(&s, 0, sizeof(s));
  s.magic = 0xC0DE;
  s.version = 1;
  s.state = (uint8_t)SystemState::NORMAL;
  s.cal.dry_min = 200;
  s.cal.wet_max = 800;
  persist_set_crc(&s);
  EXPECT(persist_crc_ok(&s));
  s.water_pulses_today = 1;
  EXPECT(!persist_crc_ok(&s));
  persist_set_crc(&s);
  EXPECT(persist_crc_ok(&s));

  PersistentState blank;
  std::memset(&blank, 0xFF, sizeof(blank));
  EXPECT(persist_looks_unprogrammed(&blank));
  EXPECT(!persist_looks_unprogrammed(&s));
}

int main() {
  test_crc();
  test_moisture_percent();
  test_hysteresis();
  test_offset();
  test_average();
  test_plausible();
  test_time_and_window();
  test_budget_battery_pulse();
  test_alarm_math();
  test_bcd();
  test_persist_crc_roundtrip();

  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? 1 : 0;
}
