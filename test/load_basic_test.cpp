#include <sstream>
#include <string>
#include <unordered_set>

#include <gtest/gtest.h>

#include "../libcoz/inspect.h"

using namespace std;

TEST(BasicTestCases, TestMapForPrebuiltToy) {
    auto toy_file = "../benchmarks/toy/toy";
    unordered_set<string> source_scope{"%/toy.cpp"};
    auto& mm = memory_map::get_instance();
    mm.process_file(toy_file, 0, source_scope);
    auto x = mm.find_line("toy.cpp:17");
    EXPECT_EQ(x->get_line(), 17);
}
