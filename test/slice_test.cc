#include <deque>
#include <string>
#include "src/utils/testutil.h"
#include "src/utils/slice.h"
#include "src/utils/slice_buffer.h"

class SliceTest {};

struct testobject {
    slice s1;
    slice s2;
};

TEST(SliceTest, Simple) {
    slice_buffer sb;
    slice s;
    slice s2 = MakeSliceByLength(128);
    testobject obj;
    obj.s1.assign("Hello");
    obj.s2.assign("World");
    std::string str("HelloWorld");
    s = obj.s1 + obj.s2;
    ASSERT_EQ(s.to_string(), str);
    s.pop_front(3);
    ASSERT_EQ(s.to_string(), std::string("loWorld"));
    s.pop_back(3);
    ASSERT_EQ(s.to_string(), std::string("loWo"));
}

TEST(SliceTest, Deque) {
    slice s;
    testobject obj;
    obj.s1.assign("Hello");
    obj.s2.assign("World");

    std::deque<testobject> vs;
    ASSERT_EQ((int)vs.size(), 0);
    vs.push_back(obj);
    ASSERT_EQ((int)vs.size(), 1);
    ASSERT_EQ(vs.front().s1.to_string(), std::string("Hello"));
    ASSERT_EQ(vs.front().s2.to_string(), std::string("World"));
    vs.push_back(obj);
    ASSERT_EQ((int)vs.size(), 2);
}

TEST(SliceTest, SliceBuffer) {
    slice s("HelloWorld");
    ASSERT_EQ(s.to_string(), std::string("HelloWorld"));
    slice_buffer sb;
    sb.add_slice(slice("Hello"));
    sb.add_slice(slice("World"));
    slice m = sb.merge();
    ASSERT_EQ(s.to_string(), m.to_string());
}

int main(int argc, char **argv) {
    return test::RunAllTests();
}
