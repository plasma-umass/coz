#include "gtest/gtest.h"

#include "interval.h"

TEST(Interval, Empty) {
  interval<int> a(3, 2);
  interval<int> b(3, 3);
  interval<int> c(3, 4);
  
  EXPECT_TRUE(a.empty());
  EXPECT_TRUE(b.empty());
  EXPECT_FALSE(c.empty());
}

TEST(Interval, Contains) {
  interval<int> a(1, 3);
  interval<int> b(2, 4);
  interval<int> c(3, 5);
  interval<int> d(4, 6);
  interval<int> e(5, 7);
  
  EXPECT_FALSE(a.contains(4));
  EXPECT_FALSE(b.contains(4));
  EXPECT_TRUE(c.contains(4));
  EXPECT_TRUE(d.contains(4));
  EXPECT_FALSE(e.contains(4));
}

TEST(Interval, LessThan) {
  interval<int> a(3, 5);
  interval<int> b(4, 6);
  interval<int> c(5, 7);
  
  EXPECT_TRUE(a < 5);
  EXPECT_FALSE(b < 5);
  EXPECT_FALSE(c < 5);
}

TEST(Interval, GreaterThan) {
  interval<int> a(3, 5);
  interval<int> b(4, 6);
  interval<int> c(5, 7);
  interval<int> d(6, 8);
  
  EXPECT_FALSE(a > 5);
  EXPECT_FALSE(b > 5);
  EXPECT_FALSE(c > 5);
  EXPECT_TRUE(d > 5);
}

// |----- a -----|
//                  |----- b -----|
TEST(Interval, SplitRight) {
  interval<int> a(3, 4);
  interval<int> b(5, 6);
  array<interval<int>, 3> parts = a.split(b);
  
  EXPECT_TRUE(parts[0].empty());
  EXPECT_TRUE(parts[1].empty());
  EXPECT_TRUE(parts[2] == b);
}

// |----- a -----|
//           |----- b -----|
TEST(Interval, SplitOverlapRight) {
  interval<int> a(3, 5);
  interval<int> b(4, 6);
  array<interval<int>, 3> parts = a.split(b);
  
  EXPECT_TRUE(parts[0].empty());
  EXPECT_TRUE(parts[1] == interval<int>(4, 5));
  EXPECT_TRUE(parts[2] == interval<int>(5, 6));
}

// |----- a -----|
// |----- b -----|
TEST(Interval, SplitEqual) {
  interval<int> a(3, 5);
  interval<int> b(3, 5);
  array<interval<int>, 3> parts = a.split(b);
  
  EXPECT_TRUE(parts[0].empty());
  EXPECT_TRUE(parts[1] == b);
  EXPECT_TRUE(parts[2].empty());
}

// |----- a -----|
//    |-- b --|
TEST(Interval, SplitContains) {
  interval<int> a(3, 6);
  interval<int> b(4, 5);
  array<interval<int>, 3> parts = a.split(b);
  
  EXPECT_TRUE(parts[0].empty());
  EXPECT_TRUE(parts[1] == b);
  EXPECT_TRUE(parts[2].empty());
}

//   |--- a ---|
// |----- b -----|
TEST(Interval, SplitContainedBy) {
  interval<int> a(4, 5);
  interval<int> b(3, 6);
  array<interval<int>, 3> parts = a.split(b);
  
  EXPECT_TRUE(parts[0] == interval<int>(3, 4));
  EXPECT_TRUE(parts[1] == a);
  EXPECT_TRUE(parts[2] == interval<int>(5, 6));
}

//        |----- a -----|
// |----- b -----|
TEST(Interval, SplitOverlapLeft) {
  interval<int> a(4, 6);
  interval<int> b(3, 5);
  array<interval<int>, 3> parts = a.split(b);
  
  EXPECT_TRUE(parts[0] == interval<int>(3, 4));
  EXPECT_TRUE(parts[1] == interval<int>(4, 5));
  EXPECT_TRUE(parts[2].empty());
}

//                  |----- a -----|
// |----- b -----|
TEST(Interval, SplitLeft) {
  interval<int> a(4, 6);
  interval<int> b(2, 4);
  array<interval<int>, 3> parts = a.split(b);
  
  EXPECT_TRUE(parts[0] == b);
  EXPECT_TRUE(parts[1].empty());
  EXPECT_TRUE(parts[2].empty());
}
