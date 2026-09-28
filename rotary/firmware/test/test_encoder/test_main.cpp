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
  RUN_TEST(test_long_turn_counts_every_detent);
  return UNITY_END();
}
