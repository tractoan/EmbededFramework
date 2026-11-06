// Payload lớn: Buffer đếm tham chiếu + các allocator, và vòng đời qua EventBus / Queue / ModuleManager.
#include <cstring>

#include "app/module_manager.hpp"
#include "mocks.hpp"
#include "osal/queue.hpp"
#include "services/buffer_allocators.hpp"
#include "test_framework.hpp"

namespace {
std::size_t used_blocks(const svc::IBufferAllocator& a) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.pool_count(); ++i) n += a.pool_stats(i).used;
    return n;
}
}  // namespace

TEST(buffer_pool_picks_smallest_fitting_class_and_falls_back) {
    svc::StaticBlockPool<512, 1> large;   // cố ý truyền không theo thứ tự
    svc::StaticBlockPool<64, 2> small;
    svc::PoolAllocator pools({&large, &small});
    CHECK_EQ(pools.pool_stats(0).block_size, 64u);

    svc::Buffer a = svc::Buffer::allocate(pools, 10);
    CHECK(a && a.size() == 10 && a.capacity() >= 10 && a.capacity() < 64);
    CHECK_EQ(pools.pool_stats(0).used, 1u);

    svc::Buffer b = svc::Buffer::allocate(pools, 10);
    svc::Buffer c = svc::Buffer::allocate(pools, 10);   // lớp 64 hết -> lấy lớp 512
    CHECK(b && c);
    CHECK_EQ(pools.pool_stats(0).used, 2u);
    CHECK_EQ(pools.pool_stats(1).used, 1u);

    svc::Buffer d = svc::Buffer::allocate(pools, 10);   // hết sạch
    CHECK(!d);
    CHECK_EQ(pools.pool_stats(0).failures, 1u);
    CHECK(!svc::Buffer::allocate(pools, 4096));          // quá lớn cho mọi lớp

    a.reset();
    b = svc::Buffer{};
    c = std::move(b);   // c cũ (lớp 512) được nhả khi bị gán đè
    CHECK_EQ(used_blocks(pools), 0u);
    CHECK_EQ(pools.pool_stats(0).peak, 2u);
}

TEST(buffer_reference_counting_shares_memory) {
    svc::StaticBlockPool<256, 2> blocks;
    svc::PoolAllocator pools({&blocks});
    svc::Buffer a = svc::Buffer::allocate(pools, 100);
    std::memset(a.data(), 0xAB, a.size());
    {
        svc::Buffer b = a;   // copy: cùng vùng nhớ
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

TEST(buffer_heap_budget_and_chain) {
    svc::StaticBlockPool<64, 1> blocks;
    svc::PoolAllocator pools({&blocks});
    svc::HeapAllocator heap(100000);
    svc::ChainAllocator chain(pools, heap);

    svc::Buffer small = svc::Buffer::allocate(chain, 8);     // vào pool
    svc::Buffer big   = svc::Buffer::allocate(chain, 50000); // quá pool -> heap
    CHECK(small && big && big.size() == 50000);
    std::memset(big.data(), 1, big.size());
    CHECK_EQ(chain.pool_count(), 2u);
    CHECK_EQ(chain.pool_stats(0).used, 1u);
    CHECK(chain.pool_stats(1).used >= 50000u);
    CHECK(!svc::Buffer::allocate(chain, 60000));             // vượt ngân sách heap
    CHECK_EQ(chain.pool_stats(1).failures, 1u);

    small.reset();
    big.reset();
    CHECK_EQ(chain.pool_stats(0).used, 0u);
    CHECK_EQ(chain.pool_stats(1).used, 0u);

    CHECK(!svc::Buffer::allocate(1));   // chưa cấu hình allocator mặc định -> rỗng, không crash
    svc::Buffer::set_default_allocator(&chain);
    svc::Buffer copy = svc::Buffer::copy_of("hello", 5);
    CHECK(copy.size() == 5 && std::memcmp(copy.data(), "hello", 5) == 0);
    copy.reset();
    svc::Buffer::set_default_allocator(nullptr);
}

TEST(buffer_released_after_bus_queue_and_listeners) {
    svc::StaticBlockPool<2048, 2> blocks;
    svc::PoolAllocator pools({&blocks});
    constexpr svc::EventId kFrame = 0x3300;
    struct FrameInfo { std::uint16_t channel; std::uint16_t format; };

    svc::EventBus bus;
    mock::Recorder keeper;   // listener giữ bản copy Event (vd. để xử lý sau)
    bus.subscribe(kFrame, &keeper);
    osal::Queue<svc::Event, 4> q;

    {
        svc::Buffer data = svc::Buffer::allocate(pools, 1500);
        for (std::size_t i = 0; i < data.size(); ++i) data.data()[i] = static_cast<std::uint8_t>(i);
        svc::Event e = svc::Event::make(kFrame, FrameInfo{3, 1}, std::move(data));
        CHECK(q.send(e, 0));
        bus.publish(std::move(e));
    }
    CHECK_EQ(blocks.stats().used, 1u);   // vẫn một block duy nhất dù có 2 bản Event
    CHECK_EQ(keeper.events.size(), 1u);
    CHECK_EQ(keeper.events[0].buffer.use_count(), 2u);   // keeper + bản trong queue

    svc::Event got;
    CHECK(q.receive(got, 0));
    FrameInfo info{};
    CHECK(got.get(info) && info.channel == 3 && info.format == 1);
    CHECK(got.buffer.size() == 1500 && got.buffer.data()[1499] == static_cast<std::uint8_t>(1499 & 0xFF));
    CHECK(got.buffer.data() == keeper.events[0].buffer.data());   // không copy dữ liệu

    got = svc::Event{};
    keeper.events.clear();
    CHECK_EQ(blocks.stats().used, 0u);   // slot queue không giữ lại tham chiếu
}

TEST(buffer_lifecycle_through_module_manager) {
    svc::StaticBlockPool<512, 4> blocks;
    svc::PoolAllocator pools({&blocks});
    constexpr svc::EventId kBlob = 0x3301;

    class Reader final : public app::IModule {
    public:
        const char* name() const override { return "reader"; }
        core::Status on_init(app::SystemContext&) override { return core::Status::Ok; }
        void on_event(const svc::Event& e) override {
            if (e.id == 0x3301) bytes += e.buffer.size();
        }
        std::size_t bytes = 0;
    };

    svc::EventBus bus;
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

    for (int i = 0; i < 3; ++i) bus.publish(svc::Event::make(kBlob, svc::Buffer::allocate(pools, 300)));
    CHECK_EQ(blocks.stats().used, 3u);   // đang xếp hàng chờ thread nghiệp vụ
    for (int i = 0; i < 3; ++i) mm.process(0);
    CHECK_EQ(r1.bytes, 900u);
    CHECK_EQ(r2.bytes, 900u);
    CHECK_EQ(blocks.stats().used, 0u);   // xử lý xong -> toàn bộ trả về pool
}
