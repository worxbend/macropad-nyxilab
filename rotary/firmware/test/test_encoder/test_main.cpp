// Host-side unit tests of src/quad_decoder.h (run: pio test -e native).
#include <unity.h>

#include "quad_decoder.h"

void setUp() {}
void tearDown() {}

// one detent as (A, B) levels; the KY-040 rests with both contacts open (pulled high)
static const bool kCw[4][2] = {{0, 1}, {0, 0}, {1, 0}, {1, 1}};  // A (CLK) leads
static const bool kCcw[4][2] = {{1, 0}, {0, 0}, {0, 1}, {1, 1}};

static int feed(QuadDecoder& q, const bool (*seq)[2], int n) {
  int sum = 0;
  for (int i = 0; i < n; i++) sum += q.update(seq[i][0], seq[i][1]);
  return sum;
}

void test_one_step_per_detent() {
  QuadDecoder q;
  q.reset(true, true);
  TEST_ASSERT_EQUAL(1, feed(q, kCw, 4));
  TEST_ASSERT_EQUAL(1, feed(q, kCw, 4));
  TEST_ASSERT_EQUAL(-1, feed(q, kCcw, 4));
}

void test_ignores_bounce_and_half_turns() {
  QuadDecoder q;
  q.reset(true, true);
  const bool bounce[][2] = {{0, 1}, {1, 1}, {0, 1}, {1, 1}, {0, 1}, {0, 0}, {0, 1}, {0, 0}, {1, 0}, {1, 1}};
  TEST_ASSERT_EQUAL(1, feed(q, bounce, 10));  // chatter on A, still exactly one detent
  const bool half[][2] = {{0, 1}, {0, 0}, {0, 1}, {1, 1}};  // halfway and back
  TEST_ASSERT_EQUAL(0, feed(q, half, 4));
  const bool skipped[][2] = {{0, 1}, {1, 0}, {1, 1}};  // a missed edge (01 -> 10) still counts
  TEST_ASSERT_EQUAL(1, feed(q, skipped, 3));
}

void test_half_step_encoders() {
  QuadDecoder q(2);
  q.reset(true, true);
  const bool cw[][2] = {{0, 1}, {0, 0}};  // detent at 00
  TEST_ASSERT_EQUAL(1, feed(q, cw, 2));
  const bool cw2[][2] = {{1, 0}, {1, 1}};  // and at 11
  TEST_ASSERT_EQUAL(1, feed(q, cw2, 2));
}

void test_two_edges_decide_a_detent_in_both_directions() {
  QuadDecoder q;
  q.reset(true, true);
  const bool cw_start[][2] = {{0, 1}, {0, 0}, {1, 1}};  // only the first two edges seen
  TEST_ASSERT_EQUAL(1, feed(q, cw_start, 3));
  const bool cw_end[][2] = {{0, 0}, {1, 0}, {1, 1}};  // only the last two
  TEST_ASSERT_EQUAL(1, feed(q, cw_end, 3));
  const bool ccw_start[][2] = {{1, 0}, {0, 0}, {1, 1}};
  TEST_ASSERT_EQUAL(-1, feed(q, ccw_start, 3));
  const bool ccw_end[][2] = {{0, 0}, {0, 1}, {1, 1}};
  TEST_ASSERT_EQUAL(-1, feed(q, ccw_end, 3));
  const bool ccw_skipped[][2] = {{1, 0}, {0, 1}, {1, 1}};  // a missed edge (10 -> 01)
  TEST_ASSERT_EQUAL(-1, feed(q, ccw_skipped, 3));
  const bool ccw_half[][2] = {{1, 0}, {0, 0}, {1, 0}, {1, 1}};  // halfway and back
  TEST_ASSERT_EQUAL(0, feed(q, ccw_half, 4));
}

// a bounced pin is back at its old level by the time the interrupt reads it
void test_repeated_states_add_nothing() {
  QuadDecoder q;
  q.reset(true, true);
  const bool a_low[][2] = {{0, 1}, {0, 1}, {0, 1}, {1, 1}};
  TEST_ASSERT_EQUAL(0, feed(q, a_low, 4));
  const bool b_low[][2] = {{1, 0}, {1, 0}, {1, 0}, {1, 1}};
  TEST_ASSERT_EQUAL(0, feed(q, b_low, 4));
  const bool both_low[][2] = {{0, 1}, {0, 0}, {0, 0}, {0, 0}, {0, 1}, {1, 1}};
  TEST_ASSERT_EQUAL(0, feed(q, both_low, 6));
  QuadDecoder half(2);  // one edge is a step here, so a repeat at rest would show
  half.reset(true, true);
  TEST_ASSERT_EQUAL(0, half.update(true, true));
}

void test_half_step_counter_clockwise_and_bounce() {
  QuadDecoder q(2);
  q.reset(true, true);
  const bool ccw[][2] = {{1, 0}, {0, 0}};
  TEST_ASSERT_EQUAL(-1, feed(q, ccw, 2));
  const bool ccw2[][2] = {{0, 1}, {1, 1}};
  TEST_ASSERT_EQUAL(-1, feed(q, ccw2, 2));
  const bool a_bounce[][2] = {{0, 1}, {1, 1}};  // one edge and back
  TEST_ASSERT_EQUAL(0, feed(q, a_bounce, 2));
  const bool b_bounce[][2] = {{1, 0}, {1, 1}};
  TEST_ASSERT_EQUAL(0, feed(q, b_bounce, 2));
}

void test_two_contact_jumps_carry_no_direction() {
  QuadDecoder q(2);  // half-step: every edge is visible at the next detent
  q.reset(true, true);
  TEST_ASSERT_EQUAL(0, q.update(false, false));  // 11 -> 00
  TEST_ASSERT_EQUAL(0, q.update(true, true));    // 00 -> 11
  const bool a_then_b[][2] = {{0, 1}, {1, 0}, {0, 0}};  // 01 -> 10 between a CW and a CCW edge
  TEST_ASSERT_EQUAL(0, feed(q, a_then_b, 3));
  q.reset(true, true);
  const bool b_then_a[][2] = {{1, 0}, {0, 1}, {0, 0}};  // 10 -> 01
  TEST_ASSERT_EQUAL(0, feed(q, b_then_a, 3));
}

void test_reset_sets_the_state_and_clears_the_count() {
  QuadDecoder q;
  q.reset(false, false);
  const bool from_closed[][2] = {{1, 0}, {1, 1}};  // two CW edges from 00, not a bounce from 11
  TEST_ASSERT_EQUAL(1, feed(q, from_closed, 2));
  const bool half_turn[][2] = {{0, 1}, {0, 0}};
  TEST_ASSERT_EQUAL(0, feed(q, half_turn, 2));
  q.reset(true, false);
  TEST_ASSERT_EQUAL(0, q.update(true, true));  // one edge after the reset: the half turn is forgotten
}

void test_other_step_counts_decode_as_full_step() {
  QuadDecoder none(0);
  none.reset(true, true);
  TEST_ASSERT_EQUAL(0, none.update(true, true));  // no step without an edge
  QuadDecoder eight(8);
  eight.reset(true, true);
  const bool skipped[][2] = {{0, 1}, {1, 0}, {1, 1}};
  TEST_ASSERT_EQUAL(1, feed(eight, skipped, 3));  // two edges are still enough
}

void test_long_turn_counts_every_detent() {
  QuadDecoder q;
  q.reset(true, true);
  int total = 0;
  for (int i = 0; i < 20; i++) total += feed(q, kCw, 4);  // one full turn
  TEST_ASSERT_EQUAL(20, total);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_one_step_per_detent);
  RUN_TEST(test_ignores_bounce_and_half_turns);
  RUN_TEST(test_half_step_encoders);
  RUN_TEST(test_two_edges_decide_a_detent_in_both_directions);
  RUN_TEST(test_repeated_states_add_nothing);
  RUN_TEST(test_half_step_counter_clockwise_and_bounce);
  RUN_TEST(test_two_contact_jumps_carry_no_direction);
  RUN_TEST(test_reset_sets_the_state_and_clears_the_count);
  RUN_TEST(test_other_step_counts_decode_as_full_step);
  RUN_TEST(test_long_turn_counts_every_detent);
  return UNITY_END();
}
