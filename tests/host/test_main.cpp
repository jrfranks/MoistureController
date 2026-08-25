/*
  Host tests: Logic.h primitives, persist slot choice, six-layer cycle.
  make test
*/

#include <cstdio>
#include <cstring>

#include "Config.h"
#include "Logic.h"
#include "Policy.h"
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

static PersistentState make_state() {
  PersistentState s;
  persist_init_defaults(&s);
  s.flags = 0;
  persist_set_crc(&s);
  return s;
}

static CivilTime at_hour(uint8_t hour) {
  CivilTime t;
  std::memset(&t, 0, sizeof(t));
  t.year = 2026;
  t.month = 8;
  t.day = 25;
  t.hour = hour;
  return t;
}

static CycleView view_at(uint8_t hour, uint16_t raw, uint16_t vbat) {
  CycleView v;
  std::memset(&v, 0, sizeof(v));
  v.time_ok = true;
  v.time = at_hour(hour);
  v.vbat = vbat;
  v.vbat_pulse = vbat;
  v.sensor_raw = raw;
  v.sensor_valid = true;
  v.rtc_wake = true;
  return v;
}

static void test_crc() {
  const char *s = "123456789";
  EXPECT(crc16_ccitt(reinterpret_cast<const uint8_t *>(s), 9) == 0x29B1);
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
  uint16_t s4[4] = {1, 5, 7, 100};
  EXPECT(average_trim_minmax(s4, 4) == 6);
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
  CivilTime t = at_hour(5);
  EXPECT(rtc_time_valid(&t));
  t.year = 2020;
  EXPECT(!rtc_time_valid(&t));
  t.year = 2026;
  t.month = 13;
  EXPECT(!rtc_time_valid(&t));
  t.month = 8;
  t.hour = 24;
  EXPECT(!rtc_time_valid(&t));
  t.hour = 5;
  t.day = 31;
  EXPECT(rtc_time_valid(&t));
  t.month = 2;
  t.day = 31;
  EXPECT(rtc_time_valid(&t));

  EXPECT(in_irrigation_window(5, 5, 8, 19, 22));
  EXPECT(in_irrigation_window(7, 5, 8, 19, 22));
  EXPECT(!in_irrigation_window(8, 5, 8, 19, 22));
  EXPECT(in_irrigation_window(19, 5, 8, 19, 22));
  EXPECT(!in_irrigation_window(12, 5, 8, 19, 22));
  EXPECT(hour_in_span(23, 22, 2));
  EXPECT(hour_in_span(0, 22, 2));
  EXPECT(!hour_in_span(12, 22, 2));
  EXPECT(!hour_in_span(5, 5, 5));
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
  next_daily_alarm(6, 5, 5, &hh, &days);
  EXPECT(hh == 5 && days == 1);
  next_daily_alarm(19, 19, 5, &hh, &days);
  EXPECT(hh == 5 && days == 1);

  uint8_t mm = 0;
  add_minutes(23, 50, 30, &hh, &mm, &days);
  EXPECT(hh == 0 && mm == 20 && days == 1);
  add_minutes(5, 0, 30, &hh, &mm, &days);
  EXPECT(hh == 5 && mm == 30 && days == 0);
  add_minutes(5, 0, 0, &hh, &mm, &days);
  EXPECT(hh == 5 && mm == 0 && days == 0);
}

static void test_bcd_and_hour() {
  EXPECT(bcd_to_bin(0x23) == 23);
  EXPECT(bin_to_bcd(23) == 0x23);
  EXPECT(bcd_to_bin(0x00) == 0);
  EXPECT(bin_to_bcd(9) == 0x09);
  EXPECT(bcd_to_bin(0x59) == 59);
  EXPECT(rtc_decode_hour(0x19) == 19);
  EXPECT(rtc_decode_hour(0x00) == 0);
  EXPECT(rtc_decode_hour(0x52) == 0);
  EXPECT(rtc_decode_hour(0x72) == 12);
  EXPECT(rtc_decode_hour(0x62) == 14);
}

static void test_persist_crc_and_pick() {
  PersistentState s = make_state();
  EXPECT(persist_image_ok(&s));
  EXPECT(persist_crc_ok(&s));
  s.water_pulses_today = 1;
  EXPECT(!persist_crc_ok(&s));
  persist_set_crc(&s);
  EXPECT(persist_crc_ok(&s));

  PersistentState blank;
  std::memset(&blank, 0xFF, sizeof(blank));
  EXPECT(persist_looks_unprogrammed(&blank));
  EXPECT(!persist_looks_unprogrammed(&s));

  PersistentState primary = s;
  PersistentState backup = s;
  EXPECT(persist_pick(&primary, &backup) == PERSIST_SLOT_PRIMARY);

  primary.crc ^= 1;
  EXPECT(persist_pick(&primary, &backup) == PERSIST_SLOT_BACKUP);

  backup.crc ^= 1;
  EXPECT(persist_pick(&primary, &backup) == PERSIST_SLOT_CORRUPT);

  EXPECT(persist_pick(&blank, &blank) == PERSIST_SLOT_BLANK);

  PersistentState garbage;
  std::memset(&garbage, 0xA5, sizeof(garbage));
  EXPECT(persist_pick(&blank, &garbage) == PERSIST_SLOT_CORRUPT);
  EXPECT(persist_pick(&garbage, &blank) == PERSIST_SLOT_CORRUPT);

  backup = make_state();
  primary = make_state();
  primary.version = 99;
  persist_set_crc(&primary);
  EXPECT(persist_pick(&primary, &backup) == PERSIST_SLOT_BACKUP);

  EXPECT(persist_next_log_head(0) == 1);
  EXPECT(persist_next_log_head(15) == 0);
  EXPECT(persist_next_log_head(16) == 1);

  PersistentState d;
  persist_init_defaults(&d);
  EXPECT(d.magic == PERSIST_MAGIC);
  EXPECT(d.version == PERSIST_VERSION);
  EXPECT(d.state == (uint8_t)SystemState::NORMAL);
  EXPECT((d.flags & FLAG_NEEDS_CAL) != 0);
  EXPECT(d.cal.dry_min == DEFAULT_DRY_MIN);
  EXPECT(d.pulse_ms == DEFAULT_VALVE_PULSE_MS);
  EXPECT(persist_image_ok(&d));
}

static void test_cycle_fault_hold() {
  PersistentState st = make_state();
  st.state = (uint8_t)SystemState::FAULT;
  st.fault_reason = (uint8_t)FaultReason::SENSOR_DEAD;
  CycleView v = view_at(5, 200, 3300);
  CycleOutcome o = evaluate_cycle(&st, &v);
  EXPECT(o.cmd == CMD_HOLD_FAULT);
  EXPECT(o.reason == FaultReason::SENSOR_DEAD);
  EXPECT(!o.persist);
  EXPECT(o.schedule);
  EXPECT(st.state == (uint8_t)SystemState::FAULT);
}

static void test_cycle_rtc_lost() {
  PersistentState st = make_state();
  CycleView v = view_at(5, 200, 3300);
  v.time_ok = false;
  CycleOutcome o = evaluate_cycle(&st, &v);
  EXPECT(o.cmd == CMD_FAULT);
  EXPECT(o.reason == FaultReason::RTC_LOST);
  EXPECT(st.state == (uint8_t)SystemState::FAULT);
  EXPECT(!o.schedule);
  EXPECT(o.persist);
  EXPECT(o.log_action == EventAction::FAULT);
}

static void test_cycle_low_battery() {
  PersistentState st = make_state();
  CycleView v = view_at(5, 200, 2900);
  CycleOutcome o = evaluate_cycle(&st, &v);
  EXPECT(o.cmd == CMD_REFUSE);
  EXPECT(o.reason == FaultReason::LOW_BATTERY);
  EXPECT(st.low_batt_streak == 1);
  EXPECT((st.flags & FLAG_LOW_BATT) != 0);
  EXPECT(st.state == (uint8_t)SystemState::NORMAL);

  o = evaluate_cycle(&st, &v);
  EXPECT(st.low_batt_streak == 2);
  EXPECT(o.cmd == CMD_REFUSE);

  o = evaluate_cycle(&st, &v);
  EXPECT(o.cmd == CMD_FAULT);
  EXPECT(o.reason == FaultReason::LOW_BATTERY_PERSISTENT);
  EXPECT(st.state == (uint8_t)SystemState::FAULT);
  EXPECT(o.schedule);
}

static void test_cycle_window_and_budget() {
  PersistentState st = make_state();
  CycleView noon = view_at(12, 200, 3300);
  CycleOutcome o = evaluate_cycle(&st, &noon);
  EXPECT(o.cmd == CMD_REFUSE);
  EXPECT(o.reason == FaultReason::OUTSIDE_WINDOW);
  EXPECT(o.schedule);
  EXPECT(!o.schedule_soak);

  st.water_pulses_today = DEFAULT_MAX_PULSES_PER_DAY;
  st.last_water_day = 25;
  st.last_water_month = 8;
  CycleView morning = view_at(5, 200, 3300);
  o = evaluate_cycle(&st, &morning);
  EXPECT(o.cmd == CMD_REFUSE);
  EXPECT(o.reason == FaultReason::DAILY_BUDGET_EXCEEDED);

  morning.time.day = 26;
  o = evaluate_cycle(&st, &morning);
  EXPECT(o.cmd == CMD_OPEN);
  EXPECT(st.water_pulses_today == 1);
  EXPECT(st.last_water_day == 26);
}

static void test_cycle_sensor_fail_and_jump() {
  PersistentState st = make_state();
  CycleView v = view_at(5, 0, 3300);
  v.sensor_valid = false;
  CycleOutcome o = evaluate_cycle(&st, &v);
  EXPECT(o.cmd == CMD_REFUSE);
  EXPECT(o.reason == FaultReason::SENSOR_IMPLAUSIBLE);
  EXPECT(st.sensor_fail_count == 1);

  o = evaluate_cycle(&st, &v);
  o = evaluate_cycle(&st, &v);
  EXPECT(o.cmd == CMD_FAULT);
  EXPECT(o.reason == FaultReason::SENSOR_DEAD);

  st = make_state();
  st.flags = FLAG_HAVE_LAST_MOISTURE;
  st.last_moisture = 200;
  v = view_at(5, 900, 3300);
  o = evaluate_cycle(&st, &v);
  EXPECT(o.cmd == CMD_REFUSE);
  EXPECT(o.reason == FaultReason::SENSOR_IMPLAUSIBLE);

  st = make_state();
  st.last_moisture = 200;
  v = view_at(5, 200, 3300);
  o = evaluate_cycle(&st, &v);
  EXPECT(o.cmd == CMD_OPEN);
}

static void test_cycle_absorb() {
  PersistentState st = make_state();
  st.flags = FLAG_AWAITING_ABSORPTION | FLAG_HAVE_LAST_MOISTURE;
  st.last_moisture = 200;
  CycleView v = view_at(5, 250, 3300);

  v.rtc_wake = false;
  CycleOutcome o = evaluate_cycle(&st, &v);
  EXPECT(o.cmd == CMD_SKIP_SOAK);
  EXPECT(o.schedule_soak);
  EXPECT((st.flags & FLAG_AWAITING_ABSORPTION) != 0);
  EXPECT(!o.log);

  v.rtc_wake = true;
  o = evaluate_cycle(&st, &v);
  EXPECT(o.cmd == CMD_REFUSE);
  EXPECT(o.log_action == EventAction::ABSORB_OK);
  EXPECT(o.reason == FaultReason::WET_ENOUGH);
  EXPECT((st.flags & FLAG_AWAITING_ABSORPTION) == 0);
  EXPECT(st.no_response_count == 0);

  v.sensor_raw = 210;
  st.no_response_count = 0;
  for (uint8_t i = 0; i < NO_RESPONSE_FAULT_STREAK; i++) {
    st.flags = (uint8_t)(FLAG_AWAITING_ABSORPTION | FLAG_HAVE_LAST_MOISTURE);
    st.last_moisture = 200;
    o = evaluate_cycle(&st, &v);
  }
  EXPECT(o.cmd == CMD_FAULT);
  EXPECT(o.reason == FaultReason::NO_RESPONSE_TO_WATER);
}

static void test_cycle_water_and_layer4() {
  PersistentState st = make_state();
  CycleView v = view_at(5, 200, 3300);
  CycleOutcome o = evaluate_cycle(&st, &v);
  EXPECT(o.cmd == CMD_OPEN);
  EXPECT(o.log_action == EventAction::OPEN_PULSE);
  EXPECT((st.flags & FLAG_AWAITING_ABSORPTION) != 0);
  EXPECT(st.water_pulses_today == 1);
  EXPECT(o.schedule_soak);
  EXPECT(o.moisture == 200);

  st = make_state();
  v.sensor_raw = 800;
  o = evaluate_cycle(&st, &v);
  EXPECT(o.cmd == CMD_REFUSE);
  EXPECT(o.reason == FaultReason::WET_ENOUGH);

  st = make_state();
  v.sensor_raw = 200;
  v.vbat = 3300;
  v.vbat_pulse = 2900;
  o = evaluate_cycle(&st, &v);
  EXPECT(o.cmd == CMD_REFUSE);
  EXPECT(o.reason == FaultReason::LOW_BATTERY);
  EXPECT((st.flags & FLAG_AWAITING_ABSORPTION) == 0);
}

static void test_cycle_evening_window() {
  PersistentState st = make_state();
  CycleView v = view_at(19, 200, 3300);
  CycleOutcome o = evaluate_cycle(&st, &v);
  EXPECT(o.cmd == CMD_OPEN);

  st = make_state();
  v.time.hour = 8;
  v.sensor_raw = 200;
  o = evaluate_cycle(&st, &v);
  EXPECT(o.cmd == CMD_REFUSE);
  EXPECT(o.reason == FaultReason::OUTSIDE_WINDOW);
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
  test_bcd_and_hour();
  test_persist_crc_and_pick();
  test_cycle_fault_hold();
  test_cycle_rtc_lost();
  test_cycle_low_battery();
  test_cycle_window_and_budget();
  test_cycle_sensor_fail_and_jump();
  test_cycle_absorb();
  test_cycle_water_and_layer4();
  test_cycle_evening_window();

  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? 1 : 0;
}
