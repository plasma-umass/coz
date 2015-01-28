#include "gtest/gtest.h"

#include "interval_dict.h"

// 0  1  2  3  4  5  6  7  8  9  10 11 12 13
//             |------- a -------|
//    |---- b ----|
//                         |---- c ----|
//             |------- d -------|
//       |------------- e -------------|
//                |---- f ----|

TEST(IntervalDict, Simple) {
  interval_dict<int, char> d;
  
  // TODO: Try all permutations of insertion order
  
  d.insert(4, 10, 'a');
  d.insert(1, 5, 'b');
  d.insert(8, 12, 'c');
  d.insert(4, 10, 'd');
  d.insert(2, 12, 'e');
  d.insert(5, 9, 'f');
  
  EXPECT_EQ(set<char>({}), d.find(0));
  EXPECT_EQ(set<char>({'b'}), d.find(1));
  EXPECT_EQ(set<char>({'b', 'e'}), d.find(2));
  EXPECT_EQ(set<char>({'b', 'e'}), d.find(3));
  EXPECT_EQ(set<char>({'a', 'b', 'd', 'e'}), d.find(4));
  EXPECT_EQ(set<char>({'a', 'd', 'e', 'f'}), d.find(5));
  EXPECT_EQ(set<char>({'a', 'd', 'e', 'f'}), d.find(6));
  EXPECT_EQ(set<char>({'a', 'd', 'e', 'f'}), d.find(7));
  EXPECT_EQ(set<char>({'a', 'c', 'd', 'e', 'f'}), d.find(8));
  EXPECT_EQ(set<char>({'a', 'c', 'd', 'e'}), d.find(9));
  EXPECT_EQ(set<char>({'c', 'e'}), d.find(10));
  EXPECT_EQ(set<char>({'c', 'e'}), d.find(11));
  EXPECT_EQ(set<char>({}), d.find(12));
  EXPECT_EQ(set<char>({}), d.find(13));
}
