// Payload lớn: Buffer đếm tham chiếu + các allocator, và vòng đời qua EventBus / Queue / ModuleManager.
#include <cstring>

#include "app/module_manager.hpp"
#include "mocks.hpp"
#include "osal/queue.hpp"
#include "messaging/buffer_allocators.hpp"
#include "test_framework.hpp"

namespace {
std::size_t used_blocks(const msg::IBufferAllocator& a) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.pool_count(); ++i) n += a.pool_stats(i).used;
    return n;
}
}  // namespace

TEST(buffer_pool_picks_smallest_fitting_class_and_falls_back) {
    msg::StaticBlockPool<512, 1> large;   // cố ý truyền không theo thứ tự
    msg::StaticBlockPool<64, 2> small;
    msg::PoolAllocator pools({&large, &small});
    CHECK_EQ(pools.pool_stats(0).block_size, 64u);

    msg::Buffer a = msg::Buffer::allocate(pools, 10);
    CHECK(a && a.size() == 10 && a.capacity() >= 10 && a.capacity() < 64);
    CHECK_EQ(pools.pool_stats(0).used, 1u);

    msg::Buffer b = msg::Buffer::allocate(pools, 10);
    msg::Buffer c = msg::Buffer::allocate(pools, 10);   // lớp 64 hết -> lấy lớp 512
    CHECK(b && c);
    CHECK_EQ(pools.pool_stats(0).used, 2u);
    CHECK_EQ(pools.pool_stats(1).used, 1u);

    msg::Buffer d = msg::Buffer::allocate(pools, 10);   // hết sạch
    CHECK(!d);
    CHECK_EQ(pools.pool_stats(0).failures, 1u);
    CHECK(!msg::Buffer::allocate(pools, 4096));          // quá lớn cho mọi lớp

    a.reset();
    b = msg::Buffer{};
    c = std::move(b);   // c cũ (lớp 512) được nhả khi bị gán đè
    CHECK_EQ(used_blocks(pools), 0u);
    CHECK_EQ(pools.pool_stats(0).peak, 2u);
}

TEST(buffer_reference_counting_shares_memory) {
    msg::StaticBlockPool<256, 2> blocks;
    msg::PoolAllocator pools({&blocks});
    msg::Buffer a = msg::Buffer::allocate(pools, 100);
    std::memset(a.data(), 0xAB, a.size());
    {
        msg::Buffer b = a;   // copy: cùng vùng nhớ
        CHECK(b.data() == a.data());
        CHECK_EQ(a.use_count(), 2u);
        CHECK_EQ(blocks.stats().used, 1u);
    }
    CHECK_EQ(a.use_count(), 1u);
    CHECK(a.resize(10) && a.size() == 10);
    CHECK(!a.resize(a.capacity() + 1));
    a.reset();
    CHECK_EQ(blocks.stats().used, 0u);
}

TEST(buffer_chain_of_pools) {
    // Pool chung hết chỗ -> dùng pool dự trữ; tất cả vẫn là bộ nhớ tĩnh.
    msg::StaticBlockPool<64, 1> common_blocks;
    msg::PoolAllocator common({&common_blocks});
    msg::StaticBlockPool<256, 2> reserve_blocks;
    msg::PoolAllocator reserve({&reserve_blocks});
    msg::ChainAllocator chain(common, reserve);

    msg::Buffer a = msg::Buffer::allocate(chain, 8);   // pool chung
    msg::Buffer b = msg::Buffer::allocate(chain, 8);   // chung hết -> dự trữ
    msg::Buffer c = msg::Buffer::allocate(chain, 200); // quá lớn cho pool chung -> dự trữ
    CHECK(a && b && c);
    CHECK(!msg::Buffer::allocate(chain, 8));            // cả hai đều hết
    CHECK_EQ(chain.pool_count(), 2u);
    CHECK_EQ(chain.pool_stats(0).used, 1u);
    CHECK_EQ(chain.pool_stats(1).used, 2u);

    a.reset();
    b.reset();
    c.reset();
    CHECK_EQ(chain.pool_stats(0).used, 0u);
    CHECK_EQ(chain.pool_stats(1).used, 0u);

    CHECK(!msg::Buffer::allocate(1));   // chưa cấu hình allocator mặc định -> rỗng, không crash
    msg::Buffer::set_default_allocator(&chain);
    msg::Buffer copy = msg::Buffer::copy_of("hello", 5);
    CHECK(copy.size() == 5 && std::memcmp(copy.data(), "hello", 5) == 0);
    copy.reset();
    msg::Buffer::set_default_allocator(nullptr);
}

TEST(buffer_released_after_bus_queue_and_listeners) {
    msg::StaticBlockPool<2048, 2> blocks;
    msg::PoolAllocator pools({&blocks});
    constexpr msg::EventId kFrame = 0x3300;
    struct FrameInfo { std::uint16_t channel; std::uint16_t format; };

    msg::EventBus bus;
    mock::Recorder keeper;   // listener giữ bản copy Event (vd. để xử lý sau)
    bus.subscribe(kFrame, &keeper);
    osal::Queue<msg::Event, 4> q;

    {
        msg::Buffer data = msg::Buffer::allocate(pools, 1500);
        for (std::size_t i = 0; i < data.size(); ++i) data.data()[i] = static_cast<std::uint8_t>(i);
        msg::Event e = msg::Event::make(kFrame, FrameInfo{3, 1}, std::move(data));
        CHECK(q.send(e, 0));
        bus.publish(std::move(e));
    }
    CHECK_EQ(blocks.stats().used, 1u);   // vẫn một block duy nhất dù có 2 bản Event
    CHECK_EQ(keeper.events.size(), 1u);
    CHECK_EQ(keeper.events[0].buffer.use_count(), 2u);   // keeper + bản trong queue

    msg::Event got;
    CHECK(q.receive(got, 0));
    FrameInfo info{};
    CHECK(got.get(info) && info.channel == 3 && info.format == 1);
    CHECK(got.buffer.size() == 1500 && got.buffer.data()[1499] == static_cast<std::uint8_t>(1499 & 0xFF));
    CHECK(got.buffer.data() == keeper.events[0].buffer.data());   // không copy dữ liệu

    got = msg::Event{};
    keeper.events.clear();
    CHECK_EQ(blocks.stats().used, 0u);   // slot queue không giữ lại tham chiếu
}

TEST(buffer_lifecycle_through_module_manager) {
    msg::StaticBlockPool<512, 4> blocks;
    msg::PoolAllocator pools({&blocks});
    constexpr msg::EventId kBlob = 0x3301;

    class Reader final : public app::IModule {
    public:
        const char* name() const override { return "reader"; }
        core::Status on_init(app::SystemContext&) override { return core::Status::Ok; }
        void on_event(const msg::Event& e) override {
            if (e.id == 0x3301) bytes += e.buffer.size();
        }
        std::size_t bytes = 0;
    };

    msg::EventBus bus;
    svc::ParamStore params;
    svc::CommandService commands;
    svc::LedService leds;
    app::Devices devices;
    app::SystemContext ctx{bus, params, commands, leds, devices};
    app::ModuleManager mm;
    Reader r1, r2;
    mm.add(&r1);
    mm.add(&r2);
    mm.init_all(ctx);

    for (int i = 0; i < 3; ++i) bus.publish(msg::Event::make(kBlob, msg::Buffer::allocate(pools, 300)));
    CHECK_EQ(blocks.stats().used, 3u);   // đang xếp hàng chờ thread nghiệp vụ
    for (int i = 0; i < 3; ++i) mm.process(0);
    CHECK_EQ(r1.bytes, 900u);
    CHECK_EQ(r2.bytes, 900u);
    CHECK_EQ(blocks.stats().used, 0u);   // xử lý xong -> toàn bộ trả về pool
}
