// Host-side unit tests of src/joystick.cpp (run: pio test -e native).
#include <unity.h>

#include "joystick.h"

using namespace mp;

void setUp() {}
void tearDown() {}

static Joystick make_joy() {
  Joystick j;
  JoyCal c;
  c.cx = 2000;
  c.cy = 2100;
  c.min_x = c.min_y = 100;
  c.max_x = c.max_y = 4000;
  j.set_cal(c);
  return j;
}

void test_center_is_still() {
  Joystick j = make_joy();
  JoyOutput o;
  for (int i = 0; i < 50; i++) j.update(2010, 2090, 0.01f, JoyMode::Mouse, o);
  TEST_ASSERT_EQUAL(0, o.dx);
  TEST_ASSERT_EQUAL(0, o.dy);
  TEST_ASSERT_EQUAL_FLOAT(0.f, j.x());
}

void test_full_right_moves_at_max_speed() {
  Joystick j = make_joy();
  JoyOutput o;
  int total = 0;
  for (int i = 0; i < 100; i++) {  // 1 s
    j.update(4000, 2100, 0.01f, JoyMode::Mouse, o);
    total += o.dx;
    TEST_ASSERT_EQUAL(0, o.dy);
  }
  TEST_ASSERT_INT_WITHIN(20, int(j.config().mouse_speed), total);
}

void test_curve_is_monotonic() {
  Joystick j = make_joy();
  JoyOutput o;
  float prev = 0;
  for (int raw = 2000; raw <= 4000; raw += 100) {
    j.update(uint16_t(raw), 2100, 0.01f, JoyMode::Off, o);
    TEST_ASSERT_TRUE(j.x() >= prev);
    prev = j.x();
  }
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.f, prev);
}

void test_up_scrolls_up_and_pointer_up() {
  Joystick j = make_joy();
  JoyOutput o;
  int wheel = 0;
  for (int i = 0; i < 100; i++) {
    j.update(2000, 4000, 0.01f, JoyMode::Scroll, o);
    wheel += o.wheel;
  }
  TEST_ASSERT_TRUE(wheel > 10);
  j.update(2000, 4000, 0.05f, JoyMode::Mouse, o);
  TEST_ASSERT_TRUE(o.dy < 0);
}

void test_arrows_hysteresis() {
  Joystick j = make_joy();
  JoyOutput o;
  j.update(2000 + uint16_t(0.6f * 2000), 2100, 0.01f, JoyMode::Arrows, o);
  TEST_ASSERT_EQUAL(1, o.n_keys);
  TEST_ASSERT_EQUAL_HEX8(hid::RIGHT, o.keys[0]);
  j.update(2000 + uint16_t(0.45f * 2000), 2100, 0.01f, JoyMode::Arrows, o);  // between thresholds
  TEST_ASSERT_EQUAL(1, o.n_keys);
  j.update(2000 + uint16_t(0.2f * 2000), 2100, 0.01f, JoyMode::Arrows, o);
  TEST_ASSERT_EQUAL(0, o.n_keys);
}

void test_invert_and_swap() {
  Joystick j = make_joy();
  JoyConfig c;
  c.invert_x = true;
  c.swap_xy = true;
  j.set_config(c);
  JoyOutput o;
  j.update(2000, 4000, 0.01f, JoyMode::Off, o);  // raw "up" becomes x, inverted
  TEST_ASSERT_TRUE(j.x() < -0.9f);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.f, j.y());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_center_is_still);
  RUN_TEST(test_full_right_moves_at_max_speed);
  RUN_TEST(test_curve_is_monotonic);
  RUN_TEST(test_up_scrolls_up_and_pointer_up);
  RUN_TEST(test_arrows_hysteresis);
  RUN_TEST(test_invert_and_swap);
  return UNITY_END();
}
