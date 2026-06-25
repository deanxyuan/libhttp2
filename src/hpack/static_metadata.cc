/**
 * @file static_metadata.cc
 * @brief HPACK static table initialization and lookup implementations.
 */

#include "src/hpack/static_metadata.h"
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include "src/utils/useful.h"
#include "src/utils/murmur_hash.h"

#define METADATA_KV_HASH(k_hash, v_hash) (ROTL((k_hash), 2) ^ (v_hash))

static uint32_t g_seed = 0;
static std::once_flag g_seed_flag;
static std::mutex g_ctx_mutex;

struct MdelemHash {
    size_t operator()(const hpack::mdelem_data &md) const { return mdelem_data_hash(md); }
};

struct MdelemEqual {
    bool operator()(const hpack::mdelem_data &a, const hpack::mdelem_data &b) const {
        return a.key == b.key && a.value == b.value;
    }
};

struct SliceHash {
    size_t operator()(const slice &s) const { return mdelem_kv_hash(s); }
};

struct SliceEqual {
    bool operator()(const slice &a, const slice &b) const { return a == b; }
};

static std::unordered_map<hpack::mdelem_data, uint32_t, MdelemHash, MdelemEqual> g_full_match_map;
static std::unordered_set<slice, SliceHash, SliceEqual> g_key_exists_set;

static void init_g_seed() {
    auto d = std::chrono::system_clock::now().time_since_epoch();
    auto n = std::chrono::duration_cast<std::chrono::nanoseconds>(d);
    uint32_t seconds = 1 * 1000 * 1000000;
    g_seed = n.count() % seconds;
}

/**
 * @brief Compute a MurmurHash3 hash for a slice.
 * @param s The slice to hash.
 * @return Hash value.
 */
template <typename T>
static uint32_t get_slice_hash(const T &s) {
    return murmur_hash3(s.data(), s.size(), g_seed);
}

/**
 * @brief Compute a combined hash for a metadata element (key + value).
 *
 * Initializes the hash seed from the system clock on first call.
 * @param oth The metadata element to hash.
 * @return Combined hash value.
 */
uint32_t mdelem_data_hash(const hpack::mdelem_data &oth) {
    std::call_once(g_seed_flag, init_g_seed);
    uint32_t k_hash = get_slice_hash(oth.key);
    uint32_t v_hash = get_slice_hash(oth.value);
    uint32_t hash = METADATA_KV_HASH(k_hash, v_hash);
    return hash;
}
/**
 * @brief Compute a hash for a single slice (used for key-only lookups).
 * @param kv The slice to hash.
 * @return Hash value.
 */
uint32_t mdelem_kv_hash(const slice &kv) {
    return get_slice_hash(kv);
}

namespace hpack {

/**
 * @brief Construct a static metadata entry with precomputed hash.
 * @param key   The header field name.
 * @param value The header field value.
 * @param idx   The 1-based index in the static table.
 */
static_metadata::static_metadata(const slice &key, const slice &value, uint32_t idx)
    : _kv({key, value})
    , _index(idx) {

    _hash = mdelem_data_hash(_kv);
}

/**
 * @brief Get the key-value data for this entry.
 * @return Reference to the mdelem_data.
 */
const mdelem_data &static_metadata::data() const {
    return _kv;
}

/**
 * @brief Get the precomputed hash of this entry.
 * @return Hash value.
 */
uint32_t static_metadata::hash() const {
    return _hash;
}

/**
 * @brief Get the 1-based index of this entry in the static table.
 * @return Table index.
 */
uint32_t static_metadata::index() const {
    return _index;
}

struct static_metadata_context {

    static_metadata static_mdelem_table[HPACK_STATIC_MDELEM_COUNT + 1] = {
        static_metadata(MakeStaticSlice(nullptr), MakeStaticSlice(nullptr), 0),
        static_metadata(MakeStaticSlice(":authority"), MakeStaticSlice(nullptr), 1),
        static_metadata(MakeStaticSlice(":method"), MakeStaticSlice("GET"), 2),
        static_metadata(MakeStaticSlice(":method"), MakeStaticSlice("POST"), 3),
        static_metadata(MakeStaticSlice(":path"), MakeStaticSlice("/"), 4),
        static_metadata(MakeStaticSlice(":path"), MakeStaticSlice("/index.html"), 5),
        static_metadata(MakeStaticSlice(":scheme"), MakeStaticSlice("http"), 6),
        static_metadata(MakeStaticSlice(":scheme"), MakeStaticSlice("https"), 7),
        static_metadata(MakeStaticSlice(":status"), MakeStaticSlice("200"), 8),
        static_metadata(MakeStaticSlice(":status"), MakeStaticSlice("204"), 9),
        static_metadata(MakeStaticSlice(":status"), MakeStaticSlice("206"), 10),
        static_metadata(MakeStaticSlice(":status"), MakeStaticSlice("304"), 11),
        static_metadata(MakeStaticSlice(":status"), MakeStaticSlice("400"), 12),
        static_metadata(MakeStaticSlice(":status"), MakeStaticSlice("404"), 13),
        static_metadata(MakeStaticSlice(":status"), MakeStaticSlice("500"), 14),
        static_metadata(MakeStaticSlice("accept-charset"), MakeStaticSlice(nullptr), 15),
        static_metadata(MakeStaticSlice("accept-encoding"), MakeStaticSlice("gzip, deflate"), 16),
        static_metadata(MakeStaticSlice("accept-language"), MakeStaticSlice(nullptr), 17),
        static_metadata(MakeStaticSlice("accept-ranges"), MakeStaticSlice(nullptr), 18),
        static_metadata(MakeStaticSlice("accept"), MakeStaticSlice(nullptr), 19),
        static_metadata(MakeStaticSlice("access-control-allow-origin"), MakeStaticSlice(nullptr), 20),
        static_metadata(MakeStaticSlice("age"), MakeStaticSlice(nullptr), 21),
        static_metadata(MakeStaticSlice("allow"), MakeStaticSlice(nullptr), 22),
        static_metadata(MakeStaticSlice("authorization"), MakeStaticSlice(nullptr), 23),
        static_metadata(MakeStaticSlice("cache-control"), MakeStaticSlice(nullptr), 24),
        static_metadata(MakeStaticSlice("content-disposition"), MakeStaticSlice(nullptr), 25),
        static_metadata(MakeStaticSlice("content-encoding"), MakeStaticSlice(nullptr), 26),
        static_metadata(MakeStaticSlice("content-language"), MakeStaticSlice(nullptr), 27),
        static_metadata(MakeStaticSlice("content-length"), MakeStaticSlice(nullptr), 28),
        static_metadata(MakeStaticSlice("content-location"), MakeStaticSlice(nullptr), 29),
        static_metadata(MakeStaticSlice("content-range"), MakeStaticSlice(nullptr), 30),
        static_metadata(MakeStaticSlice("content-type"), MakeStaticSlice(nullptr), 31),
        static_metadata(MakeStaticSlice("cookie"), MakeStaticSlice(nullptr), 32),
        static_metadata(MakeStaticSlice("date"), MakeStaticSlice(nullptr), 33),
        static_metadata(MakeStaticSlice("etag"), MakeStaticSlice(nullptr), 34),
        static_metadata(MakeStaticSlice("expect"), MakeStaticSlice(nullptr), 35),
        static_metadata(MakeStaticSlice("expires"), MakeStaticSlice(nullptr), 36),
        static_metadata(MakeStaticSlice("from"), MakeStaticSlice(nullptr), 37),
        static_metadata(MakeStaticSlice("host"), MakeStaticSlice(nullptr), 38),
        static_metadata(MakeStaticSlice("if-match"), MakeStaticSlice(nullptr), 39),
        static_metadata(MakeStaticSlice("if-modified-since"), MakeStaticSlice(nullptr), 40),
        static_metadata(MakeStaticSlice("if-none-match"), MakeStaticSlice(nullptr), 41),
        static_metadata(MakeStaticSlice("if-range"), MakeStaticSlice(nullptr), 42),
        static_metadata(MakeStaticSlice("if-unmodified-since"), MakeStaticSlice(nullptr), 43),
        static_metadata(MakeStaticSlice("last-modified"), MakeStaticSlice(nullptr), 44),
        static_metadata(MakeStaticSlice("link"), MakeStaticSlice(nullptr), 45),
        static_metadata(MakeStaticSlice("location"), MakeStaticSlice(nullptr), 46),
        static_metadata(MakeStaticSlice("max-forwards"), MakeStaticSlice(nullptr), 47),
        static_metadata(MakeStaticSlice("proxy-authenticate"), MakeStaticSlice(nullptr), 48),
        static_metadata(MakeStaticSlice("proxy-authorization"), MakeStaticSlice(nullptr), 49),
        static_metadata(MakeStaticSlice("range"), MakeStaticSlice(nullptr), 50),
        static_metadata(MakeStaticSlice("referer"), MakeStaticSlice(nullptr), 51),
        static_metadata(MakeStaticSlice("refresh"), MakeStaticSlice(nullptr), 52),
        static_metadata(MakeStaticSlice("retry-after"), MakeStaticSlice(nullptr), 53),
        static_metadata(MakeStaticSlice("server"), MakeStaticSlice(nullptr), 54),
        static_metadata(MakeStaticSlice("set-cookie"), MakeStaticSlice(nullptr), 55),
        static_metadata(MakeStaticSlice("strict-transport-security"), MakeStaticSlice(nullptr), 56),
        static_metadata(MakeStaticSlice("transfer-encoding"), MakeStaticSlice(nullptr), 57),
        static_metadata(MakeStaticSlice("user-agent"), MakeStaticSlice(nullptr), 58),
        static_metadata(MakeStaticSlice("vary"), MakeStaticSlice(nullptr), 59),
        static_metadata(MakeStaticSlice("via"), MakeStaticSlice(nullptr), 60),
        static_metadata(MakeStaticSlice("www-authenticate"), MakeStaticSlice(nullptr), 61),

        //
        // The following data (62-85) are from gRPC
        //
        static_metadata(MakeStaticSlice("grpc-status"), MakeStaticSlice("0"), 62),
        static_metadata(MakeStaticSlice("grpc-status"), MakeStaticSlice("1"), 63),
        static_metadata(MakeStaticSlice("grpc-status"), MakeStaticSlice("2"), 64),
        static_metadata(MakeStaticSlice("grpc-encoding"), MakeStaticSlice("identity"), 65),
        static_metadata(MakeStaticSlice("grpc-encoding"), MakeStaticSlice("gzip"), 66),
        static_metadata(MakeStaticSlice("grpc-encoding"), MakeStaticSlice("deflate"), 67),
        static_metadata(MakeStaticSlice("te"), MakeStaticSlice("trailers"), 68),
        static_metadata(MakeStaticSlice("content-type"), MakeStaticSlice("application/grpc"), 69),
        static_metadata(MakeStaticSlice(":scheme"), MakeStaticSlice("grpc"), 70),
        static_metadata(MakeStaticSlice(":method"), MakeStaticSlice("PUT"), 71),
        static_metadata(MakeStaticSlice("accept-encoding"), MakeStaticSlice(nullptr), 72),
        static_metadata(MakeStaticSlice("content-encoding"), MakeStaticSlice("identity"), 73),
        static_metadata(MakeStaticSlice("content-encoding"), MakeStaticSlice("gzip"), 74),
        static_metadata(MakeStaticSlice("lb-cost-bin"), MakeStaticSlice(nullptr), 75),
        static_metadata(MakeStaticSlice("grpc-accept-encoding"), MakeStaticSlice("identity"), 76),
        static_metadata(MakeStaticSlice("grpc-accept-encoding"), MakeStaticSlice("deflate"), 77),
        static_metadata(MakeStaticSlice("grpc-accept-encoding"), MakeStaticSlice("identity,deflate"), 78),
        static_metadata(MakeStaticSlice("grpc-accept-encoding"), MakeStaticSlice("gzip"), 79),
        static_metadata(MakeStaticSlice("grpc-accept-encoding"), MakeStaticSlice("identity,gzip"), 80),
        static_metadata(MakeStaticSlice("grpc-accept-encoding"), MakeStaticSlice("deflate,gzip"), 81),
        static_metadata(MakeStaticSlice("grpc-accept-encoding"), MakeStaticSlice("identity,deflate,gzip"), 82),
        static_metadata(MakeStaticSlice("accept-encoding"), MakeStaticSlice("identity"), 83),
        static_metadata(MakeStaticSlice("accept-encoding"), MakeStaticSlice("gzip"), 84),
        static_metadata(MakeStaticSlice("accept-encoding"), MakeStaticSlice("identity,gzip"), 85),
    };
};  // struct static_metadata_context

static static_metadata_context *g_static_metadata_ctx = nullptr;
static_metadata *g_static_mdelem_table = nullptr;
}  // namespace hpack

/**
 * @brief Initialize the global static metadata context.
 *
 * Allocates the static_metadata_context and sets the global table pointer.
 * Safe to call multiple times; subsequent calls are no-ops.
 */
void init_static_metadata_context(void) {
    std::lock_guard<std::mutex> lck(g_ctx_mutex);
    if (!hpack::g_static_metadata_ctx) {
        hpack::g_static_metadata_ctx = new hpack::static_metadata_context();
        hpack::g_static_mdelem_table = hpack::g_static_metadata_ctx->static_mdelem_table;

        // Build hash-based lookup tables for O(1) static table lookups
        for (size_t i = 1; i <= HPACK_STATIC_MDELEM_STANDARD_COUNT; i++) {
            const auto &md = hpack::g_static_mdelem_table[i].data();
            g_full_match_map.emplace(md, static_cast<uint32_t>(i));
            g_key_exists_set.insert(md.key);
        }
    }
}

/** @brief Destroy the global static metadata context and free resources. */
void destroy_static_metadata_context(void) {
    std::lock_guard<std::mutex> lck(g_ctx_mutex);
    if (hpack::g_static_metadata_ctx) {
        g_full_match_map.clear();
        g_key_exists_set.clear();
        delete hpack::g_static_metadata_ctx;
        hpack::g_static_metadata_ctx = nullptr;
        hpack::g_static_mdelem_table = nullptr;
    }
}

/**
 * @brief Find the exact matching entry index in the standard HPACK static table.
 * @param mdel The metadata element to match.
 * @return 1-based index if found, 0 if not found.
 */
uint32_t full_match_static_mdelem_index(const hpack::mdelem_data &mdel) {
    auto it = g_full_match_map.find(mdel);
    if (it != g_full_match_map.end()) {
        return it->second;
    }
    return 0;
}

/**
 * @brief Check whether a header field name exists in the standard HPACK static table.
 * @param key The header field name to search for.
 * @return True if the key exists in the static table.
 */
bool check_key_exists(const slice &key) {
    return g_key_exists_set.find(key) != g_key_exists_set.end();
}
