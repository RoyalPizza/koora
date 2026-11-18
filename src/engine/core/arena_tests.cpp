#include "engine/core/arena.hpp"
#include "engine/core/test.hpp"

#include <cstddef>
#include <cstdint>

constexpr size_t TEST_ARENA_SIZE = 256;

struct TestVector
{
    float x;
    float y;
    double weight;
};

static void test_alignment();
static void test_reset();
static void test_exhaustion();

void arena_tests()
{
    test_alignment();
    test_reset();
    test_exhaustion();
}

static void test_alignment()
{
    alignas(64) uint8_t memory[TEST_ARENA_SIZE];
    Arena arena;
    arena_init(arena, "test", memory, TEST_ARENA_SIZE);

    uint8_t* one_byte = (uint8_t*)arena_push(arena, 1, 1);
    TEST_CHECK(one_byte == memory);

    void* aligned_16 = arena_push(arena, 4, 16);
    TEST_CHECK(aligned_16 != nullptr);
    TEST_CHECK(((uintptr_t)aligned_16 & 15) == 0);
    TEST_CHECK(arena.used == 20);

    TestVector* vector = ARENA_PUSH_STRUCT(arena, TestVector);
    TEST_CHECK(vector != nullptr);
    TEST_CHECK(((uintptr_t)vector % alignof(TestVector)) == 0);

    uint16_t* shorts = ARENA_PUSH_ARRAY(arena, uint16_t, 3);
    TEST_CHECK(shorts != nullptr);
    TEST_CHECK(((uintptr_t)shorts & 1) == 0);
    TEST_CHECK(shorts[0] == 0 && shorts[1] == 0 && shorts[2] == 0);
}

static void test_reset()
{
    alignas(64) uint8_t memory[TEST_ARENA_SIZE];
    Arena arena;
    arena_init(arena, "test", memory, TEST_ARENA_SIZE);

    uint8_t* first = (uint8_t*)arena_push(arena, 32, 8);
    TEST_CHECK(first != nullptr);
    first[0] = 0xAB;

    arena_reset(arena);
    TEST_CHECK(arena.used == 0);

    uint8_t* again = (uint8_t*)arena_push(arena, 32, 8);
    TEST_CHECK(again == first);
    TEST_CHECK(again[0] == 0);
}

static void test_exhaustion()
{
    alignas(64) uint8_t memory[TEST_ARENA_SIZE];
    Arena arena;
    arena_init(arena, "test", memory, TEST_ARENA_SIZE);

    TEST_CHECK(arena_push(arena, TEST_ARENA_SIZE, 1) != nullptr);
    TEST_CHECK(arena_push(arena, 1, 1) == nullptr);

    arena_reset(arena);
    TEST_CHECK(arena_push(arena, TEST_ARENA_SIZE + 1, 1) == nullptr);
    TEST_CHECK(arena.used == 0);

    // Padding alone pushes this past the end.
    TEST_CHECK(arena_push(arena, TEST_ARENA_SIZE - 4, 1) != nullptr);
    TEST_CHECK(arena_push(arena, 1, 16) == nullptr);
}
