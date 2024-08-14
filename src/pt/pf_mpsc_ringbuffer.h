/* Copyright (c) 2024 Masaaki Hamada
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include "pf_base.h"

namespace pf {

template<size_t max_size, typename ElementType>
class MpscRingBuffer : private pf::NonCopyable
{
  public:
    enum
    {
        kMaxSize = max_size,
    };
    struct WriteStats
    {
        int32_t max_queued;
        int32_t wait_count;
    };

  protected:
#if defined(__clang__)
    static constexpr inline size_t cache_line_size = 64;
#else
    static_assert(std::hardware_destructive_interference_size == 64);
    static constexpr inline size_t cache_line_size = std::hardware_destructive_interference_size;
#endif
    alignas(cache_line_size) std::atomic<int64_t> m_write_index = 0;
    std::atomic<int64_t> m_read_max_index = 0;
    struct
    {
        std::atomic<int32_t> max_queued = 0;
        std::atomic<int32_t> wait_count = 0;
    } m_write_stats{};
    alignas(cache_line_size) std::atomic<int64_t> m_read_index = 0;
    int64_t m_read_index_expected = 0;
    int64_t m_read_index_desired = 0;
    ElementType m_buffer[max_size];

  public:
    MpscRingBuffer(void) = default;
    ~MpscRingBuffer(void) { this->cancel(); }
    MpscRingBuffer(MpscRingBuffer&&) = delete;
    MpscRingBuffer& operator=(MpscRingBuffer&&) = delete;

    void cancel(void)
    {
        for (;;) {
            auto index = m_write_index.load(std::memory_order_relaxed);
            if (0 > index) {
                break;
            }
            auto result = m_write_index.compare_exchange_strong(/*expected*/ index,
                                                                /*desired */ -1);
            if (result) {
                // m_write_index.notify_all();
                break;
            }
        }
        for (;;) {
            auto index = m_read_max_index.load(std::memory_order_relaxed);
            if (0 > index) {
                break;
            }
            auto result = m_read_max_index.compare_exchange_strong(/*expected*/ index,
                                                                   /*desired */ -1);
            if (result) {
                m_read_max_index.notify_all();
                break;
            }
        }
        for (;;) {
            auto index = m_read_index.load(std::memory_order_relaxed);
            if (0 > index) {
                break;
            }
            auto result = m_read_index.compare_exchange_strong(/*expected*/ index,
                                                               /*desired */ -1);
            if (result) {
                m_read_index.notify_all();
                break;
            }
        }
    }

    bool push(const ElementType& data) { return this->pushCommon(data, /*wait*/ true); }
    bool tryPush(const ElementType& data) { return this->pushCommon(data, /*wait*/ false); }

    int64_t tryPeek(ElementType** ppData, size_t num)
    {
        return this->peekCommon(ppData, num, /*wait*/ false);
    }
    int64_t peek(ElementType** ppData, size_t num)
    {
        return this->peekCommon(ppData, num, /*wait*/ true);
    }
    void commitPop(void) { return this->commitPopCommon(); }
    WriteStats getWriteStats(void) const noexcept
    {
        return { m_write_stats.max_queued.load(std::memory_order_relaxed),
                 m_write_stats.wait_count.load(std::memory_order_relaxed) };
    }

  public:
    bool pushCommon(const ElementType& data, bool wait)
    {
        int64_t write_index;
        int64_t new_write_index;
        int32_t queue_size;
        for (;;) {
            write_index = m_write_index.load(std::memory_order_relaxed);
            if (0 > write_index) {
                return false; // canceled
            }
            auto read_index = m_read_index.load(std::memory_order_acquire);
            if (0 > read_index) {
                return false; // canceled;
            }
            new_write_index = (write_index + 1) % max_size;
            if (new_write_index == read_index) {
                // the queue is full
                if (!wait) {
                    return false;
                }
                m_write_stats.wait_count.fetch_add(1, std::memory_order_relaxed);
                m_read_index.wait(read_index, std::memory_order_relaxed);
                continue;
            }

            // queue size
            queue_size = int32_t(max_size + new_write_index - read_index) % max_size;

            // strongを使うと場合は、失敗した＝他のwriterとコンフリクトしたということになるので
            // m_write_index.load()からやり直しする必要があることが確定する
            auto result = m_write_index.compare_exchange_strong(/*expected*/ write_index,
                                                                /*desired*/ new_write_index,
                                                                std::memory_order_relaxed);
            if (result) {
                break;
            }
        }

        m_buffer[write_index] = data;

        // update stat
        {
            auto max_queued = m_write_stats.max_queued.load(std::memory_order_relaxed);
            while (max_queued < queue_size) {
                // while (max_queued < num) でループさせるのでweakで十分
                auto result =
                  m_write_stats.max_queued.compare_exchange_weak(/*expected*/ max_queued,
                                                                 /*desired*/ queue_size,
                                                                 std::memory_order_relaxed);
                if (result) {
                    break;
                }
            }
        }

        auto new_read_max_index = new_write_index;
        for (;;) {
            auto read_max_index = write_index;
            auto result = m_read_max_index.compare_exchange_weak(/*expected*/ read_max_index,
                                                                 /*desired*/ new_read_max_index,
                                                                 std::memory_order_release);
            if (result) {
                m_read_max_index.notify_one();
                break;
            }
            if (0 > read_max_index) {
                return false; // canceled
            }
        }

        return true;
    }

    static int64_t read_availe(int64_t read_max_index, int64_t read_index)
    {
        if (read_index <= read_max_index) {
            return read_max_index - read_index;
        }
        return max_size - read_index;
    }

    int64_t peekCommon(ElementType** ppData, int64_t num, bool wait)
    {
        const auto read_index = m_read_index.load(std::memory_order_relaxed);
        int64_t read_max_index;
        for (;;) {
            read_max_index = m_read_max_index.load(std::memory_order_acquire);
            if (0 > read_max_index) {
                return -1; // canceled
            }
            bool empty = (read_index == read_max_index);
            if (!empty) {
                break;
            }
            // empty
            if (!wait) {
                return false;
            }
            m_read_max_index.wait(read_max_index, std::memory_order_relaxed);
        }

        auto avail = read_availe(read_max_index, read_index);
        num = (std::min)(avail, num);
        *ppData = &m_buffer[read_index];

        m_read_index_expected = read_index;
        m_read_index_desired = (read_index + num) % max_size;
        return num;
    }

    void commitPopCommon(void)
    {
        auto new_read_index = m_read_index_desired;
        for (;;) {
            auto read_index_expected = m_read_index_expected;
            auto result = m_read_index.compare_exchange_weak(/*expected*/ read_index_expected,
                                                             /*desired */ new_read_index,
                                                             std::memory_order_release);
            if (result) {
                m_read_index.notify_one();
                break;
            }
            if (0 > read_index_expected) {
                return; // canceled
            }
        }
    }
};

} // namespace pf
