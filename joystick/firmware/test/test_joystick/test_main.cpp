// Host-side unit tests of src/joystick.cpp (run: pio test -e native).
#include <math.h>
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

void test_diagonal_is_no_faster_than_one_axis() {
  Joystick j = make_joy();
  JoyOutput o;
  j.update(4000, 4000, 0.01f, JoyMode::Off, o);  // a corner: both axes at full travel
  TEST_ASSERT_TRUE(j.x() > 0.5f && j.y() > 0.5f);
  TEST_ASSERT_TRUE(sqrtf(j.x() * j.x() + j.y() * j.y()) <= 1.001f);
}

void test_invert_y() {
  Joystick j = make_joy();
  JoyConfig c;
  c.invert_y = true;
  j.set_config(c);
  JoyOutput o;
  j.update(2000, 4000, 0.01f, JoyMode::Off, o);
  TEST_ASSERT_TRUE(j.y() < -0.9f);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.f, j.x());
}

void test_right_pans_right() {
  Joystick j = make_joy();
  JoyOutput o;
  int pan = 0;
  for (int i = 0; i < 100; i++) {
    j.update(4000, 2100, 0.01f, JoyMode::Scroll, o);
    pan += o.pan;
    TEST_ASSERT_EQUAL(0, o.wheel);
  }
  TEST_ASSERT_TRUE(pan > 10);
}

void test_motion_beyond_one_report_carries_over() {
  Joystick j = make_joy();
  JoyOutput o;
  j.update(4000, 2100, 1.f, JoyMode::Mouse, o);  // a second at full speed in one step
  TEST_ASSERT_EQUAL(127, o.dx);
  j.update(2000, 2100, 0.01f, JoyMode::Mouse, o);  // centred again: the rest still goes out
  TEST_ASSERT_EQUAL(127, o.dx);
}

void test_mode_change_drops_the_fraction_of_a_pixel() {
  Joystick j = make_joy();
  JoyOutput o;
  const float dt = 0.6f / j.config().mouse_speed;  // 0.6 px at full right
  j.update(4000, 2100, dt, JoyMode::Mouse, o);
  TEST_ASSERT_EQUAL(0, o.dx);
  j.update(4000, 2100, dt, JoyMode::Off, o);
  j.update(4000, 2100, dt, JoyMode::Mouse, o);
  TEST_ASSERT_EQUAL(0, o.dx);  // 0.6 again, not 1.2
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_center_is_still);
  RUN_TEST(test_full_right_moves_at_max_speed);
  RUN_TEST(test_curve_is_monotonic);
  RUN_TEST(test_up_scrolls_up_and_pointer_up);
  RUN_TEST(test_arrows_hysteresis);
  RUN_TEST(test_invert_and_swap);
  RUN_TEST(test_diagonal_is_no_faster_than_one_axis);
  RUN_TEST(test_invert_y);
  RUN_TEST(test_right_pans_right);
  RUN_TEST(test_motion_beyond_one_report_carries_over);
  RUN_TEST(test_mode_change_drops_the_fraction_of_a_pixel);
  return UNITY_END();
}
