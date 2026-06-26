#include "src/hpack/static_metadata.h"
#include "src/utils/testutil.h"

class StaticMetadataTest {};

TEST(StaticMetadataTest, Empty) {
    hpack::mdelem_data md;
    size_t size = MDELEM_SIZE(md);
    ASSERT_EQ(size, 0);
}

bool check_static_metadata(const hpack::mdelem_data &md, const std::string &key, const std::string &value) {
    if (md.key == key && md.value == value) {
        return true;
    }
    return false;
}

TEST(StaticMetadataTest, Simple) {
    auto table = get_static_mdelem_table();
    ASSERT_TRUE(check_static_metadata(table[0].data(), "", ""));
    ASSERT_TRUE(check_static_metadata(table[1].data(), ":authority", ""));
    ASSERT_TRUE(check_static_metadata(table[2].data(), ":method", "GET"));
    ASSERT_TRUE(check_static_metadata(table[3].data(), ":method", "POST"));
    ASSERT_TRUE(check_static_metadata(table[4].data(), ":path", "/"));
    ASSERT_TRUE(check_static_metadata(table[5].data(), ":path", "/index.html"));
    ASSERT_TRUE(check_static_metadata(table[6].data(), ":scheme", "http"));
    ASSERT_TRUE(check_static_metadata(table[7].data(), ":scheme", "https"));
    ASSERT_TRUE(check_static_metadata(table[8].data(), ":status", "200"));
    ASSERT_TRUE(check_static_metadata(table[9].data(), ":status", "204"));

    ASSERT_TRUE(check_static_metadata(table[10].data(), ":status", "206"));
    ASSERT_TRUE(check_static_metadata(table[11].data(), ":status", "304"));
    ASSERT_TRUE(check_static_metadata(table[12].data(), ":status", "400"));
    ASSERT_TRUE(check_static_metadata(table[13].data(), ":status", "404"));
    ASSERT_TRUE(check_static_metadata(table[14].data(), ":status", "500"));
    ASSERT_TRUE(check_static_metadata(table[15].data(), "accept-charset", ""));
    ASSERT_TRUE(check_static_metadata(table[16].data(), "accept-encoding", "gzip, deflate"));
    ASSERT_TRUE(check_static_metadata(table[17].data(), "accept-language", ""));
    ASSERT_TRUE(check_static_metadata(table[18].data(), "accept-ranges", ""));
    ASSERT_TRUE(check_static_metadata(table[19].data(), "accept", ""));

    ASSERT_TRUE(check_static_metadata(table[20].data(), "access-control-allow-origin", ""));
    ASSERT_TRUE(check_static_metadata(table[21].data(), "age", ""));
    ASSERT_TRUE(check_static_metadata(table[22].data(), "allow", ""));
    ASSERT_TRUE(check_static_metadata(table[23].data(), "authorization", ""));
    ASSERT_TRUE(check_static_metadata(table[24].data(), "cache-control", ""));
    ASSERT_TRUE(check_static_metadata(table[25].data(), "content-disposition", ""));
    ASSERT_TRUE(check_static_metadata(table[26].data(), "content-encoding", ""));
    ASSERT_TRUE(check_static_metadata(table[27].data(), "content-language", ""));
    ASSERT_TRUE(check_static_metadata(table[28].data(), "content-length", ""));
    ASSERT_TRUE(check_static_metadata(table[29].data(), "content-location", ""));

    ASSERT_TRUE(check_static_metadata(table[30].data(), "content-range", ""));
    ASSERT_TRUE(check_static_metadata(table[31].data(), "content-type", ""));
    ASSERT_TRUE(check_static_metadata(table[32].data(), "cookie", ""));
    ASSERT_TRUE(check_static_metadata(table[33].data(), "date", ""));
    ASSERT_TRUE(check_static_metadata(table[34].data(), "etag", ""));
    ASSERT_TRUE(check_static_metadata(table[35].data(), "expect", ""));
    ASSERT_TRUE(check_static_metadata(table[36].data(), "expires", ""));
    ASSERT_TRUE(check_static_metadata(table[37].data(), "from", ""));
    ASSERT_TRUE(check_static_metadata(table[38].data(), "host", ""));
    ASSERT_TRUE(check_static_metadata(table[39].data(), "if-match", ""));

    ASSERT_TRUE(check_static_metadata(table[40].data(), "if-modified-since", ""));
    ASSERT_TRUE(check_static_metadata(table[41].data(), "if-none-match", ""));
    ASSERT_TRUE(check_static_metadata(table[42].data(), "if-range", ""));
    ASSERT_TRUE(check_static_metadata(table[43].data(), "if-unmodified-since", ""));
    ASSERT_TRUE(check_static_metadata(table[44].data(), "last-modified", ""));
    ASSERT_TRUE(check_static_metadata(table[45].data(), "link", ""));
    ASSERT_TRUE(check_static_metadata(table[46].data(), "location", ""));
    ASSERT_TRUE(check_static_metadata(table[47].data(), "max-forwards", ""));
    ASSERT_TRUE(check_static_metadata(table[48].data(), "proxy-authenticate", ""));
    ASSERT_TRUE(check_static_metadata(table[49].data(), "proxy-authorization", ""));

    ASSERT_TRUE(check_static_metadata(table[50].data(), "range", ""));
    ASSERT_TRUE(check_static_metadata(table[51].data(), "referer", ""));
    ASSERT_TRUE(check_static_metadata(table[52].data(), "refresh", ""));
    ASSERT_TRUE(check_static_metadata(table[53].data(), "retry-after", ""));
    ASSERT_TRUE(check_static_metadata(table[54].data(), "server", ""));
    ASSERT_TRUE(check_static_metadata(table[55].data(), "set-cookie", ""));
    ASSERT_TRUE(check_static_metadata(table[56].data(), "strict-transport-security", ""));
    ASSERT_TRUE(check_static_metadata(table[57].data(), "transfer-encoding", ""));
    ASSERT_TRUE(check_static_metadata(table[58].data(), "user-agent", ""));
    ASSERT_TRUE(check_static_metadata(table[59].data(), "vary", ""));

    ASSERT_TRUE(check_static_metadata(table[60].data(), "via", ""));
    ASSERT_TRUE(check_static_metadata(table[61].data(), "www-authenticate", ""));
}

int main(int argc, char **argv) {
    init_static_metadata_context();
    int r = test::RunAllTests();
    destroy_static_metadata_context();
    return r;
}
