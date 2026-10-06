// NIST-developed software is provided by NIST as a public service. You may use, copy and distribute copies of the
// software in any medium, provided that you keep intact this entire notice. You may improve, modify and create
// derivative works of the software or any portion of the software, and you may copy and distribute such modifications
// or works. Modified works should carry a notice stating that you changed the software and should note the date and
// nature of any such change. Please explicitly acknowledge the National Institute of Standards and Technology as the
// source of the software. NIST-developed software is expressly provided "AS IS." NIST MAKES NO WARRANTY OF ANY KIND,
// EXPRESS, IMPLIED, IN FACT OR ARISING BY OPERATION OF LAW, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTY OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, NON-INFRINGEMENT AND DATA ACCURACY. NIST NEITHER REPRESENTS NOR
// WARRANTS THAT THE OPERATION OF THE SOFTWARE WILL BE UNINTERRUPTED OR ERROR-FREE, OR THAT ANY DEFECTS WILL BE
// CORRECTED. NIST DOES NOT WARRANT OR MAKE ANY REPRESENTATIONS REGARDING THE USE OF THE SOFTWARE OR THE RESULTS
// THEREOF, INCLUDING BUT NOT LIMITED TO THE CORRECTNESS, ACCURACY, RELIABILITY, OR USEFULNESS OF THE SOFTWARE. You
// are solely responsible for determining the appropriateness of using and distributing the software and you assume
// all risks associated with its use, including but not limited to the risks and costs of program errors, compliance
// with applicable laws, damage to or loss of data, programs or equipment, and the unavailability or interruption of
// operation. This software is not intended to be used in any situation where a failure could cause risk of injury or
// damage to property. The software developed by NIST employees is not subject to copyright protection within the
// United States.

#include <gtest/gtest.h>
#include "../hedgehog/impl/memory/allocators.hpp"
#include <array>

// Test struct with destructor to verify cleanup
struct TestObject {
    int value = 0;
    static int destructor_count;

    TestObject() = default;
    explicit TestObject(int v) : value(v) {}

    ~TestObject() {
        ++destructor_count;
    }
};

int TestObject::destructor_count = 0;

// Arena Tests /////////////////////////////////////////////////////////////////

TEST(Arena, BasicAllocation) {
    hh::Arena arena(1024);

    auto* ptr = arena.allocate(100, 8);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(arena.bytes_allocated(), 100);
    EXPECT_EQ(arena.bytes_available(), 924);
}

TEST(Arena, TypedAllocation) {
    hh::Arena arena(1024);

    auto* ints = arena.allocate<int>(10);
    ASSERT_NE(ints, nullptr);

    // Manually construct
    for (int i = 0; i < 10; ++i) {
        new (&ints[i]) int(i * 10);
    }

    // Verify values
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(ints[i], i * 10);
    }

    // No cleanup needed for primitives
}

TEST(Arena, Alignment) {
    hh::Arena arena(1024);

    struct alignas(64) AlignedType {
        char data[32];
    };

    auto* ptr = arena.allocate<AlignedType>(1);
    ASSERT_NE(ptr, nullptr);

    auto addr = reinterpret_cast<uintptr_t>(ptr);
    EXPECT_EQ(addr % 64, 0) << "Pointer not 64-byte aligned";
}

TEST(Arena, UserBuffer) {
    std::array<std::byte, 512> buffer;
    hh::Arena arena(buffer.data(), buffer.size());

    auto* ptr = arena.allocate(100, 8);
    ASSERT_NE(ptr, nullptr);
    EXPECT_TRUE(arena.contains(ptr));
}

TEST(Arena, Exhaustion) {
    hh::Arena arena(100);

    auto* ptr1 = arena.allocate(48, 8);  // 48 bytes, 8-byte aligned
    ASSERT_NE(ptr1, nullptr);
    EXPECT_EQ(arena.bytes_allocated(), 48);

    auto* ptr2 = arena.allocate(48, 8);  // 48 bytes, fits at offset 48
    ASSERT_NE(ptr2, nullptr);
    EXPECT_EQ(arena.bytes_allocated(), 96);

    auto* ptr3 = arena.allocate(8, 8);  // 8 bytes, won't fit (need 104 total)
    EXPECT_EQ(ptr3, nullptr) << "Arena should be exhausted";
}

TEST(Arena, Reset) {
    hh::Arena arena(1024);

    auto* ptr1 = arena.allocate(500, 8);
    ASSERT_NE(ptr1, nullptr);
    EXPECT_EQ(arena.bytes_allocated(), 500);

    arena.reset();
    EXPECT_EQ(arena.bytes_allocated(), 0);
    EXPECT_EQ(arena.bytes_available(), 1024);

    auto* ptr2 = arena.allocate(500, 8);
    ASSERT_NE(ptr2, nullptr);
}

TEST(Arena, MoveSemantics) {
    hh::Arena arena1(1024);
    auto* ptr = arena1.allocate(100, 8);
    ASSERT_NE(ptr, nullptr);

    hh::Arena arena2 = std::move(arena1);
    EXPECT_EQ(arena1.bytes_allocated(), 0);
    EXPECT_EQ(arena2.bytes_allocated(), 100);
    EXPECT_TRUE(arena2.contains(ptr));
}

// Smart Pointer Tests /////////////////////////////////////////////////////////

TEST(Arena, MakeShared) {
    TestObject::destructor_count = 0;

    {
        hh::Arena arena(1024);
        auto obj = hh::make_shared<TestObject>(arena, 42);

        ASSERT_NE(obj, nullptr);
        EXPECT_EQ(obj->value, 42);
    }

    EXPECT_EQ(TestObject::destructor_count, 1) << "Destructor should have been called";
}

TEST(Arena, MakeSharedRefCounting) {
    TestObject::destructor_count = 0;

    {
        hh::Arena arena(1024);
        auto obj1 = hh::make_shared<TestObject>(arena, 10);
        {
            auto obj2 = obj1;  // Copy shared_ptr
            EXPECT_EQ(obj2->value, 10);
        }
        // obj2 destroyed, but obj1 still alive
        EXPECT_EQ(TestObject::destructor_count, 0);
    }

    // Both destroyed now
    EXPECT_EQ(TestObject::destructor_count, 1);
}

TEST(Arena, MakeUnique) {
    TestObject::destructor_count = 0;

    {
        hh::Arena arena(1024);
        auto obj = hh::make_unique<TestObject>(arena, 99);

        ASSERT_NE(obj, nullptr);
        EXPECT_EQ(obj->value, 99);
    }

    EXPECT_EQ(TestObject::destructor_count, 1);
}

// DynamicArena Tests //////////////////////////////////////////////////////////

TEST(DynamicArena, BasicAllocation) {
    hh::DynamicArena arena(1024);

    auto* ptr = arena.allocate(100, 8);
    ASSERT_NE(ptr, nullptr);
    EXPECT_GE(arena.num_blocks(), 1);
}

TEST(DynamicArena, Growth) {
    hh::DynamicArena arena(512);

    auto* ptr1 = arena.allocate(400, 8);
    ASSERT_NE(ptr1, nullptr);
    EXPECT_EQ(arena.num_blocks(), 1);

    // Fill current block
    auto* ptr2 = arena.allocate(100, 8);
    ASSERT_NE(ptr2, nullptr);

    // This should trigger new block
    auto* ptr3 = arena.allocate(400, 8);
    ASSERT_NE(ptr3, nullptr);
    EXPECT_GE(arena.num_blocks(), 2);
}

TEST(DynamicArena, OversizedAllocation) {
    // block_size=1024, out_band_threshold=512, numa_id=-1
    hh::DynamicArena arena(1024, 512, -1);

    // Regular allocation
    auto* ptr1 = arena.allocate(100, 8);
    ASSERT_NE(ptr1, nullptr);

    // Oversized allocation (> 512 bytes)
    auto* ptr2 = arena.allocate(800, 8);
    ASSERT_NE(ptr2, nullptr);
    EXPECT_EQ(arena.num_out_band(), 1);
}

TEST(DynamicArena, Reset) {
    hh::DynamicArena arena(1024);

    auto* ptr1 = arena.allocate(500, 8);
    ASSERT_NE(ptr1, nullptr);
    EXPECT_GE(arena.num_blocks(), 1);

    arena.reset();

    // After reset, blocks are reused
    auto* ptr2 = arena.allocate(500, 8);
    ASSERT_NE(ptr2, nullptr);
    EXPECT_GE(arena.num_blocks(), 1);
}

TEST(DynamicArena, Resize) {
    hh::DynamicArena arena(1024);

    auto* ptr = arena.allocate(100, 8);
    ASSERT_NE(ptr, nullptr);

    // Resize last allocation (grow)
    bool result = arena.resize(ptr, 100, 200);
    EXPECT_TRUE(result);

    // Resize last allocation (shrink)
    result = arena.resize(ptr, 200, 150);
    EXPECT_TRUE(result);

    // Allocate something else - resize should fail now
    auto* ptr2 = arena.allocate(50, 8);
    ASSERT_NE(ptr2, nullptr);

    result = arena.resize(ptr, 150, 200);
    EXPECT_FALSE(result) << "Cannot resize non-last allocation";
}

TEST(DynamicArena, MakeShared) {
    TestObject::destructor_count = 0;

    {
        hh::DynamicArena arena(1024);
        auto obj = hh::make_shared<TestObject>(arena, 77);

        ASSERT_NE(obj, nullptr);
        EXPECT_EQ(obj->value, 77);
    }

    EXPECT_EQ(TestObject::destructor_count, 1);
}

// Integration with data_t /////////////////////////////////////////////////////

#include "../hedgehog/tool/data.hpp"

TEST(Allocators, MakeDataIntegration) {
    hh::Arena arena(1024);

    auto data = hh::make_data<int>(arena, 123);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(*data, 123);
}

TEST(Allocators, MakeDataDynamicArena) {
    hh::DynamicArena arena(1024);

    auto data = hh::make_data<double>(arena, 3.14);
    ASSERT_NE(data, nullptr);
    EXPECT_DOUBLE_EQ(*data, 3.14);
}
