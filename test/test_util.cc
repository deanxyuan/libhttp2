#include <cstdio>
#include <cstring>
#include "src/utils/atomic.h"
#include "src/utils/mpscq.h"
#include "src/utils/murmur_hash.h"
#include "src/utils/testutil.h"

// ============================================================================
// Atomic tests
// ============================================================================

class AtomicTest {};

TEST(AtomicTest, LoadStore) {
    Atomic<int32_t> a(0);
    ASSERT_EQ(a.Load(), 0);
    a.Store(42);
    ASSERT_EQ(a.Load(), 42);
    a.Store(-1);
    ASSERT_EQ(a.Load(), -1);
}

TEST(AtomicTest, Exchange) {
    Atomic<int32_t> a(10);
    int32_t old = a.Exchange(20, MemoryOrder::SEQ_CST);
    ASSERT_EQ(old, 10);
    ASSERT_EQ(a.Load(), 20);

    old = a.Exchange(30, MemoryOrder::SEQ_CST);
    ASSERT_EQ(old, 20);
    ASSERT_EQ(a.Load(), 30);
}

TEST(AtomicTest, FetchAdd) {
    Atomic<int32_t> a(0);
    int32_t old = a.FetchAdd(5);
    ASSERT_EQ(old, 0);
    ASSERT_EQ(a.Load(), 5);

    old = a.FetchAdd(3);
    ASSERT_EQ(old, 5);
    ASSERT_EQ(a.Load(), 8);

    old = a.FetchAdd(-2);
    ASSERT_EQ(old, 8);
    ASSERT_EQ(a.Load(), 6);
}

TEST(AtomicTest, IncrementIfNonzero) {
    Atomic<int32_t> a(5);
    bool ok = a.IncrementIfNonzero();
    ASSERT_TRUE(ok);
    ASSERT_EQ(a.Load(), 6);

    // Decrement to 1 then increment
    a.Store(1);
    ok = a.IncrementIfNonzero();
    ASSERT_TRUE(ok);
    ASSERT_EQ(a.Load(), 2);

    // Set to zero -- should fail
    a.Store(0);
    ok = a.IncrementIfNonzero();
    ASSERT_TRUE(!ok);
    ASSERT_EQ(a.Load(), 0);
}

// ============================================================================
// MPSCQueue tests
// ============================================================================

class MPSCQueueTest {};

struct TestItem : public MultiProducerSingleConsumerQueue::Node {
    int value;
    explicit TestItem(int v) : value(v) {}
};

TEST(MPSCQueueTest, PushPop) {
    MultiProducerSingleConsumerQueue queue;

    TestItem a(1), b(2), c(3);
    queue.push(&a);
    queue.push(&b);
    queue.push(&c);

    // FIFO order: oldest first
    MultiProducerSingleConsumerQueue::Node *n = queue.pop();
    ASSERT_TRUE(n != nullptr);
    ASSERT_EQ(static_cast<TestItem *>(n)->value, 1);

    n = queue.pop();
    ASSERT_TRUE(n != nullptr);
    ASSERT_EQ(static_cast<TestItem *>(n)->value, 2);

    n = queue.pop();
    ASSERT_TRUE(n != nullptr);
    ASSERT_EQ(static_cast<TestItem *>(n)->value, 3);
}

TEST(MPSCQueueTest, Empty) {
    MultiProducerSingleConsumerQueue queue;
    MultiProducerSingleConsumerQueue::Node *n = queue.pop();
    ASSERT_TRUE(n == nullptr);
}

// ============================================================================
// MurmurHash tests
// ============================================================================

class MurmurHashTest {};

TEST(MurmurHashTest, KnownVector) {
    const char *input = "hello world";
    size_t len = strlen(input);
    uint32_t seed = 0;

    uint32_t h1 = murmur_hash3(input, len, seed);
    uint32_t h2 = murmur_hash3(input, len, seed);

    // Deterministic: same input produces same output
    ASSERT_EQ(h1, h2);
    // Non-trivial hash (not zero)
    ASSERT_TRUE(h1 != 0);
}

TEST(MurmurHashTest, DifferentSeeds) {
    const char *input = "test data";
    size_t len = strlen(input);

    uint32_t h1 = murmur_hash3(input, len, 0);
    uint32_t h2 = murmur_hash3(input, len, 42);

    ASSERT_TRUE(h1 != h2);
}

int main() {
    test::RunAllTests();
    return 0;
}
