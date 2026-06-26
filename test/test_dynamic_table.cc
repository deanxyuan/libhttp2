#include "src/hpack/static_metadata.h"
#include "src/hpack/dynamic_metadata.h"
#include "src/hpack/send_record.h"
#include "src/utils/testutil.h"

class DynamicTableTest {};

TEST(DynamicTableTest, Simple) {
    hpack::dynamic_metadata_table table(4096);
    hpack::mdelem_data md1;
    md1.key.assign(":authority");
    md1.value.assign("10.158.12.140:50051");
    table.push_mdelem_data(md1);

    hpack::mdelem_data md2;
    md2.key.assign(":path");
    md2.value.assign("/hellostreamingworld.MultiGreeter/sayRegularMsg");
    table.push_mdelem_data(md2);

    hpack::mdelem_data md3;
    md3.key.assign("te");
    md3.value.assign("trailers");
    table.push_mdelem_data(md3);

    hpack::mdelem_data md4;
    md4.key.assign("content-type");
    md4.value.assign("application/grpc");
    table.push_mdelem_data(md4);

    hpack::mdelem_data md5;
    md5.key.assign("user-agent");
    md5.value.assign("grpc-c++/1.33.2 grpc-c/13.0.0 (linux; chttp2)");
    table.push_mdelem_data(md5);

    hpack::mdelem_data md6;
    md6.key.assign("grpc-accept-encoding");
    md6.value.assign("identity,deflate,gzip");
    table.push_mdelem_data(md6);

    hpack::mdelem_data md7;
    md7.key.assign("accept-encoding");
    md7.value.assign("identity,gzip");
    table.push_mdelem_data(md7);

    auto index = table.get_mdelem_data_index(md1);
    ASSERT_GE(index, 0);
    index += HPACK_STATIC_MDELEM_STANDARD_COUNT + 1;
    ASSERT_EQ(index, 68);
}

int main(int argc, char *argv[]) {
    return test::RunAllTests();
}
