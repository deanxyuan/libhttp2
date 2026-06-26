#include <stdio.h>
#include "src/utils/byte_order.h"
#include "src/utils/testutil.h"

class ByteOrderTest {};
TEST(ByteOrderTest, Change) {
    uint16_t n16 = 0x1234;
    uint32_t n32 = 0x12345678;
    uint16_t rn16 = change_byte_order(n16);
    uint32_t rn32 = change_byte_order(n32);
    ASSERT_EQ(rn16, 0x3412);
    ASSERT_EQ(rn32, 0x78563412);
}

TEST(ByteOrderTest, Get) {
    uint8_t buf[] = {0x12, 0x34, 0x56, 0x78, 0x00};
    uint16_t n16 = get_uint16_from_be_stream(buf);
    uint32_t n24 = get_uint32_from_be_stream(buf) >> 8;
    uint32_t n32 = get_uint32_from_be_stream(buf);
    ASSERT_EQ(n16, 0x1234);
    ASSERT_EQ(n24, 0x123456);
    ASSERT_EQ(n32, 0x12345678);
}

TEST(ByteOrderTest, Put) {
    uint8_t buf[8] = {0};
    put_uint16_in_be_stream(buf, 0x1234);
    ASSERT_EQ(buf[0], 0x12);
    ASSERT_EQ(buf[1], 0x34);

    put_uint32_in_be_stream(buf, 0x12345678);
    ASSERT_EQ(buf[0], 0x12);
    ASSERT_EQ(buf[1], 0x34);
    ASSERT_EQ(buf[2], 0x56);
    ASSERT_EQ(buf[3], 0x78);
}

int main(int argc, char *argv[]) {
    return test::RunAllTests();
}
