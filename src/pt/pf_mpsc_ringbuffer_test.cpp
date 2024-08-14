/* Copyright (c) 2024 Masaaki Hamada
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#define GTEST_DONT_DEFINE_TEST 1
#include <gtest/gtest.h>

#include "pf_mpsc_ringbuffer.h"
#include <random>

namespace pf {

struct X
{
    int32_t id;
    int32_t seq;
    bool done;
};

class MpscRingBufferTest : public ::testing::Test
{
    static constexpr size_t s_max_size = 128;
    static constexpr size_t s_sizeListLength = 10000;
    std::vector<int> m_sizeList = generateSizeList();
    std::vector<char> m_sendData = generateSendData();
    using RingBuffer = pf::MpscRingBuffer<s_max_size, X>;

  private:
    static std::vector<int> generateSizeList()
    {
        volatile unsigned seed = 4946;
        std::mt19937 mt{ seed };
        std::uniform_int_distribution<int> dist(
          1, s_max_size - 1); // s_max_size-1はエラーになるので注意
        std::vector<int> ret(s_sizeListLength);
        std::generate(ret.begin(), ret.end(), [&]() { return dist(mt); });
        return ret;
    }
    static std::vector<char> generateSendData()
    {
        volatile unsigned seed = 8888;
        std::mt19937 mt{ seed };
        std::vector<char> ret(s_max_size);
        std::generate(ret.begin(), ret.end(), [&]() { return char(mt()); });
        return ret;
    }

  public:
    void producer(RingBuffer& rb, int64_t n, int32_t id)
    {
        int32_t seq = 0;
        for (auto i = 0ll; i < n;) {
            bool done = (i == n - 1);
            X x{ id, int32_t(i), done };
            if (seq & 0x100) {
                std::this_thread::yield();
            }
            if (seq & 8) {
                auto result = rb.push(x);
                PT_ASSERT(result);
                ++i;
            } else {
                auto result = rb.tryPush(x);
                if (result) {
                    ++i;
                }
            }
        }
    };

    void consumer(RingBuffer& rb, int producerNum, int& ret, int64_t& total)
    {
        struct ProducerInfo
        {
            int seq = 0;
            bool done = false;
        };
        std::vector<ProducerInfo> producers(producerNum);
        int doneCount = 0;
        int wrong = 0;
        int64_t popCount = 0;
        int64_t write_index = 0;
        for (;;) {
            X* p = nullptr;
            size_t num = 10;
            auto n = rb.peek(&p, num);
            if (0 > n) {
                break;
            }
            for (auto i = 0; i < n; ++i) {
                X& x = p[i];
                ++popCount;
                if (!((unsigned)x.id < producers.size())) {
                    ++wrong;
                }
                auto& prod = producers[x.id];
                if (x.seq != prod.seq) {
                    ++wrong;
                }
                ++prod.seq;
                if (prod.done) {
                    ++wrong;
                }
                if (x.done) {
                    prod.done = true;
                    ++doneCount;
                }
            }
            if (0 < n) {
                rb.commitPop();
            }
            if (doneCount == producerNum) {
                break;
            }
        }
        ret = wrong;
        total = popCount;
    };

    void execute(int64_t count, int producerNum)
    {
        auto rb0 = std::make_unique<RingBuffer>();
        RingBuffer& rb = *rb0.get();
        int wrongCount = 0;
        int64_t total = 0;
        std::thread c(&MpscRingBufferTest::consumer,
                      this,
                      std::ref(rb),
                      producerNum,
                      std::ref(wrongCount),
                      std::ref(total));
        std::vector<std::thread> producers(producerNum);
        for (auto id = 0u; id < producers.size(); ++id) {
            producers[id] =
              std::thread(&MpscRingBufferTest::producer, this, std::ref(rb), count, id);
        }
        for (auto id = 0u; id < producers.size(); ++id) {
            producers[id].join();
        }
        c.join();
        ASSERT_EQ(wrongCount, 0);
        auto totalExpected = count * producerNum;
        ASSERT_EQ(total, totalExpected);
    }

    void producer_cancel(RingBuffer& rb, int64_t n, int32_t id)
    {
        int32_t seq = 0;
        for (auto i = 0ll; i < n;) {
            bool done = (i == n - 1);
            X x{ id, int32_t(i), done };
            if (seq & 0x100) {
                std::this_thread::yield();
            }
            auto result = rb.push(x);
            if (!result) {
                break;
            }
            ++i;
        }
    };

    void execute_cancel(int64_t count, int producerNum)
    {
        int64_t sendCount = 100000;
        for (auto i = 0; i < count; ++i) {
            auto rb0 = std::make_unique<RingBuffer>();
            RingBuffer& rb = *rb0.get();
            int wrongCount = 0;
            int64_t total = 0;
            std::thread c(&MpscRingBufferTest::consumer,
                          this,
                          std::ref(rb),
                          producerNum,
                          std::ref(wrongCount),
                          std::ref(total));
            std::vector<std::thread> producers(producerNum);
            for (auto id = 0u; id < producers.size(); ++id) {
                producers[id] = std::thread(
                  &MpscRingBufferTest::producer_cancel, this, std::ref(rb), sendCount, id);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            rb.cancel();

            for (auto id = 0u; id < producers.size(); ++id) {
                producers[id].join();
            }
            c.join();
        }
        ASSERT_TRUE(true);
    }
};

GTEST_TEST_F(MpscRingBufferTest, Normal)
{
    ASSERT_NO_THROW({ execute(/*count*/ 300000, /*producerNum*/ 1); });

    ASSERT_NO_THROW({ execute(/*count*/ 400000, /*producerNum*/ 10); });
}

GTEST_TEST_F(MpscRingBufferTest, Cancel)
{
    ASSERT_NO_THROW({ execute_cancel(/*count*/ 100, /*producerNum*/ 10); });
}

} // namespace pf
