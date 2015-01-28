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
  
  d.insert(4, 10, 'a');
  d.insert(1, 5, 'b');
  d.insert(8, 12, 'c');
  d.insert(4, 10, 'd');
  d.insert(2, 12, 'e');
  d.insert(5, 9, 'f');
  
  EXPECT_TRUE(d.find(0)  == set<char>({}));
  EXPECT_TRUE(d.find(1)  == set<char>({'b'}));
  EXPECT_TRUE(d.find(2)  == set<char>({'b', 'e'}));
  EXPECT_TRUE(d.find(3)  == set<char>({'b', 'e'}));
  EXPECT_TRUE(d.find(4)  == set<char>({'a', 'b', 'd', 'e'}));
  EXPECT_TRUE(d.find(5)  == set<char>({'a', 'd', 'e', 'f'}));
  EXPECT_TRUE(d.find(6)  == set<char>({'a', 'd', 'e', 'f'}));
  EXPECT_TRUE(d.find(7)  == set<char>({'a', 'd', 'e', 'f'}));
  EXPECT_TRUE(d.find(8)  == set<char>({'a', 'c', 'd', 'e', 'f'}));
  EXPECT_TRUE(d.find(9)  == set<char>({'a', 'c', 'd', 'e'}));
  EXPECT_TRUE(d.find(10) == set<char>({'c', 'e'}));
  EXPECT_EQ(d.find(11), set<char>({'c', 'e'}));
  EXPECT_TRUE(d.find(11) == set<char>({'c', 'e'}));
  EXPECT_TRUE(d.find(12) == set<char>({}));
  EXPECT_TRUE(d.find(13) == set<char>({}));
}
