/* Copyright (c) 2024 Masaaki Hamada
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include "pf_base.h"

namespace pf {

// ElementTypeの型の配列を格納するQueue
// 一度のpushごとにElementAlignmentにアライメントをそろえる
template<typename ElementType = char, size_t ElementAlignment = alignof(int64_t)>
class BoundedSpscZeroCopy : private pf::NonCopyable
{
  protected:
#if defined(__clang__)
    static constexpr inline size_t cache_line_size = 64;
#else
    static_assert(std::hardware_destructive_interference_size == 64);
    static constexpr inline size_t cache_line_size = std::hardware_destructive_interference_size;
#endif
    alignas(cache_line_size) std::atomic<uint64_t> m_write_ctx = 0;
    struct
    {
        uint64_t write_ctx;
        int32_t write_index2;
        int32_t read_end_index2;
        int32_t read_index;
        int32_t avail;
    } m_write_im{};
    struct
    {
        int32_t wait_count;
        int32_t insufficient;
        int32_t max_read_end_index;
    } m_write_stats{};

    alignas(cache_line_size) std::atomic<int32_t> m_read_index = 0;
    struct
    {
        int32_t read_index;
        int32_t read_index2;
        int32_t write_index;
        int32_t read_end_index;
        int32_t avail;
    } m_read_im{};
    std::unique_ptr<ElementType[]> m_bufferPtr;
    ElementType* m_buffer;
    const int32_t kMaxSize;

#ifdef LIBPT_ENABLE_BUILD_TESTS
    std::atomic<int32_t> m_waiter_count = 0;
#endif

  private:
    static constexpr size_t calcSafeElementArraySize(size_t queueSize) noexcept
    {
        // queueSizeは要素数(not byte数)
        auto eleAlign = alignof(ElementType);
        auto reqAlign = ElementAlignment;
        if (reqAlign <= eleAlign) {
            return queueSize;
        }
        // 例えばcharで書き込むが、書き込んだ跡はElementAlignmentにアライメントさせたい
        //
        return queueSize + (reqAlign / eleAlign - 1);
    }
    static constexpr bool isAligned(void* ptr, size_t alignment) noexcept
    {
        auto intptr = reinterpret_cast<uintptr_t>(ptr);
        return (intptr % alignment) == 0;
    }
    static ElementType* toAlignedElement(ElementType* ptr) noexcept
    {
        auto eleAlign = alignof(ElementType);
        auto reqAlign = ElementAlignment;
        auto n = (reqAlign / eleAlign - 1);
        for (decltype(n) i = 0L; i < n; ++i) {
            if (isAligned(ptr, ElementAlignment)) {
                return ptr;
            }
            ++ptr;
        }
        PT_UNREACHABLE();
        return nullptr;
    }

  public:
    BoundedSpscZeroCopy(int32_t queueSize) noexcept
      : m_bufferPtr{ std::make_unique<ElementType[]>(calcSafeElementArraySize(queueSize)) }
      , kMaxSize(queueSize)
    {
        m_buffer = toAlignedElement(m_bufferPtr.get());
    }
    ~BoundedSpscZeroCopy(void) noexcept { this->cancel(); }
    BoundedSpscZeroCopy(BoundedSpscZeroCopy&&) = delete;
    BoundedSpscZeroCopy& operator=(BoundedSpscZeroCopy&&) = delete;

    void cancel(void) noexcept
    {
        for (;;) {
            auto ctx = m_write_ctx.load(std::memory_order_relaxed);
            auto [index0, index1] = decode_ctx(ctx);
            if (0 > index0) {
                break;
            }
            auto newCtx = encode_ctx(-1, -1);
            auto result = m_write_ctx.compare_exchange_strong(/*expected*/ ctx,
                                                              /*desired */ newCtx);
            if (result) {
                m_write_ctx.notify_one();
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
                m_read_index.notify_one();
                break;
            }
        }
    }

    std::pair<ElementType*, int32_t> getWritePtr(int32_t wantSize) noexcept
    {
        if (kMaxSize / 2 < wantSize) {
            return { nullptr, -1 };
        }
        auto write_ctx =
          m_write_ctx.load(std::memory_order_relaxed); // only written from writer thread
        auto [write_index, read_end_index] = decode_ctx(write_ctx);
        if (0 > write_index) {
            return { nullptr, -1 }; // canceled
        }
        for (;;) {
            // synchronize-with & happens-before
            // readerとwriterのm_read_indexとm_write_ctxで互いに
            // synchronize-with & happens-beforeの関係を作って更新していく
            //
            // writer                      reader
            // 1.m_read_index.load
            // 2.write m_buffer   -----+
            // 3.m_write_ctx.store  => |
            //                         |  1.m_write_ctx.load
            //                         +->2.read m_buffer
            //                      <= |  3.m_read_index.store
            // 1.m_read_index.load     |
            // 2.write m_buffer   <----+
            // 3.m_write_ctx.store     |
            // 4.m_read_index.load     | #writerが連続する場合を例示
            // 5.write m_buffer   <----+
            // 6.m_write_ctx.store  => |
            //                         |  1.m_write_ctx.load
            //                         +->2.read m_buffer
            //                      <=    3.m_read_index.store
            // .....
            //                     =>: synchronize-with
            //                    -->: happens-before
            auto read_index = m_read_index.load(std::memory_order_acquire);
            if (0 > read_index) {
                break; // canceled
            }

            auto [avail, updated_write_index] =
              this->check_write_available(write_index, read_index, kMaxSize);
            // decltype(write_index) write_index2;   // 'int32_t&& write_index' in gcc/clang
            // decltype(read_end_index) read_end_index2;
            int32_t write_index2;
            int32_t read_end_index2;
            if (0 <= updated_write_index) {
                // flip to back side
                write_index2 = updated_write_index;
                read_end_index2 = write_index;
            } else {
                write_index2 = write_index;
                read_end_index2 = read_end_index;
            }
            if (0 < avail && wantSize <= avail) {
                m_write_im.write_ctx = write_ctx;
                m_write_im.write_index2 = write_index2;
                m_write_im.read_end_index2 = read_end_index2;
                m_write_im.read_index = read_index;
                m_write_im.avail = avail;
                auto* ptr = &m_buffer[write_index2];
                return { ptr, avail };
            }
            if (0 <= updated_write_index) {
                // waitする前に一旦ここで m_write_ctx を更新する
                // read pointerが進む可能性がある
                auto write_ctx2 = encode_ctx(write_index2, read_end_index2);
                bool result = this->update_write_ctx(write_ctx, write_ctx2);
                if (!result) {
                    break; // canceld
                }
                write_ctx = write_ctx2;
                write_index = write_index2;
                read_end_index = read_end_index2;
            }
            ++m_write_stats.insufficient;
            if (0 >= wantSize) { // wantSize==0はnon-blocking呼び出しの意味になる
                return { nullptr, 0 };
            }
#ifdef LIBPT_ENABLE_BUILD_TESTS
            ++m_waiter_count;
#endif
            ++m_write_stats.wait_count;
            m_read_index.wait(read_index, std::memory_order_relaxed);
#ifdef LIBPT_ENABLE_BUILD_TESTS
            --m_waiter_count;
#endif
        }
        return { nullptr, -1 }; // canceled;
    }

    int32_t moveWritePtr(int32_t writtenSize) noexcept
    {
        if (m_write_im.avail < writtenSize) {
            return -1;
        }
        auto read_end_index2 = m_write_im.read_end_index2;
        auto new_write_index = m_write_im.write_index2 + writtenSize; // ここでwrap aroundはしない
        auto new_read_end_index =
          is_front_side(new_write_index, m_write_im.read_index) ? new_write_index : read_end_index2;
        m_write_stats.max_read_end_index =
          (std::max)(m_write_stats.max_read_end_index, read_end_index2);
        auto new_write_ctx = encode_ctx(new_write_index, new_read_end_index);
        // if (new_write_index == m_write_im.write_index2) {
        if (new_write_ctx == m_write_im.write_ctx) {
            PT_DEBUGBREAK();
        }

        auto result = this->update_write_ctx(m_write_im.write_ctx, new_write_ctx);
        if (!result) {
            return -1; // canceled
        }

        m_write_im.avail = 0;
        return writtenSize;
    }

    std::pair<const ElementType*, int32_t> getReadPtr(int32_t wantSize) noexcept
    {
        if (kMaxSize / 2 < wantSize) {
            return { nullptr, -1 };
        }
        auto read_index = m_read_index.load(std::memory_order_relaxed);
        if (0 > read_index) {
            return { nullptr, -1 }; // canceled
        }
        for (;;) {
            auto write_ctx = m_write_ctx.load(std::memory_order_acquire);
            auto [write_index, read_end_index] = decode_ctx(write_ctx);
            if (0 > write_index) {
                break; // canceled
            }

            auto [avail, updated_read_index] =
              check_read_available(write_index, read_end_index, read_index);
            decltype(read_index) read_index2;
            if (0 <= updated_read_index) {
                // flip to front side
                read_index2 = updated_read_index;
            } else {
                read_index2 = read_index;
            }
            if (0 < avail && wantSize <= avail) {
                m_read_im.read_index = read_index;
                m_read_im.read_index2 = read_index2;
                m_read_im.write_index = write_index;
                m_read_im.read_end_index = read_end_index;
                m_read_im.avail = avail;
                const auto* ptr = &m_buffer[read_index2];
                return { ptr, avail };
            }
            if (0 <= updated_read_index) {
                // waitする前に一旦ここで m_read_index を更新する
                // write pointerが進む可能性がある
                bool result = this->update_read_ctx(read_index, read_index2);
                if (!result) {
                    break; // canceld
                }
                read_index = read_index2;
            }
            if (0 >= wantSize) { // wantSize==0はnon-blocking呼び出しの意味になる
                return { nullptr, 0 };
            }
#ifdef LIBPT_ENABLE_BUILD_TESTS
            ++m_waiter_count;
#endif
            m_write_ctx.wait(write_ctx, std::memory_order_relaxed);
#ifdef LIBPT_ENABLE_BUILD_TESTS
            --m_waiter_count;
#endif
        }
        return { nullptr, -1 }; // canceld
    }

    int32_t moveReadPtr(int32_t readSize) noexcept
    {
        if (m_read_im.avail < readSize) {
            return -1;
        }
        auto new_read_index = m_read_im.read_index2 + readSize;
        if (!is_front_side(m_read_im.write_index, m_read_im.read_index2)) {
            if (m_read_im.read_end_index <= new_read_index) {
                // flip to front side
                new_read_index = 0;
            }
        }

        auto result = this->update_read_ctx(m_read_im.read_index, new_read_index);
        if (!result) {
            return -1; // canceled
        }
        m_read_im.avail = 0;
        return readSize;
    }

    void waitUntilEmptyForWriter(void) noexcept
    {
        auto write_ctx = m_write_ctx.load(std::memory_order_relaxed);
        auto [write_index, read_end_index] = decode_ctx(write_ctx);
        if (0 > write_index) {
            return; // canceled
        }
        for (;;) {
            auto read_index = m_read_index.load(std::memory_order_acquire);
            if (0 > read_index) {
                break; // canceled
            }

            auto empty = is_empty(write_index, read_end_index, read_index);
            if (empty) {
                return;
            }

#ifdef LIBPT_ENABLE_BUILD_TESTS
            ++m_waiter_count;
#endif
            m_read_index.wait(read_index, std::memory_order_relaxed);
#ifdef LIBPT_ENABLE_BUILD_TESTS
            --m_waiter_count;
#endif
        }
    }

  protected:
    static std::pair<int32_t, int32_t> decode_ctx(uint64_t encoded) noexcept
    {
        int32_t decoded[2];
        memcpy(decoded, &encoded, sizeof(encoded));
        return { decoded[0], decoded[1] };
    }

    static uint64_t encode_ctx(int32_t x, int32_t y) noexcept
    {
        uint64_t encoded;
        int32_t tmp[2] = { x, y };
        memcpy(&encoded, tmp, sizeof(tmp));
        return encoded;
    }

    /*
     * 状態としては大きく read <= write かどうかでわける
     * read <= write をfront side、read > write をback sideと呼ぶことにする
     * read == writeの場合にfullなのかemptyかの2通りがありえるが、2通りを許容すると
     * lock-free実装が困難になるため read==writeはemptyの場合のみとする
     *
     * [FrontSide]  (r<=w)
     * --------------  -------------- --------------
     * +  | <-w,r      +              +*  <-r
     * |  |            |              |*
     * |  |            |*   <-r       |*
     * |  |            |*             |*
     * |  |            |*             |*
     * |  |            |  | <-w       |*
     * |  |            |  |           |*
     * |  |            |  |           |*
     * |  |            |  |           |*
     * +  V            +  V           +*
     *                                    <-w,end
     *                                    # rがtopの場合はwrap aroundしない
     * [BackSide] (w<r)
     * -------------- -------------- -------------- -
     * +  | <-w       +*             +*             +
     * |  |           |*             |*             |
     * |  |           |  | <-w       |*             |
     * |  |           |  |           |*             |
     * |  V           |  V           |    <-w       |
     * |*   <-r       |*   <-r       |*   <-r       |
     * |*             |*             |*             |
     * |*             |*             |*             |
     * |*   <-end     |    <-end     |    <-end
     * +              +              +
     *                                   # r==wにはしない
     */
    static bool is_front_side(int32_t write_index, int32_t read_index) noexcept
    {
        return read_index <= write_index;
    }

    static std::pair<int32_t, int32_t> check_write_available(int32_t write_index,
                                                             int32_t read_index,
                                                             int32_t maxSize) noexcept
    {
        if (is_front_side(write_index, read_index)) {
            auto avail = maxSize - write_index;
            auto top_area = read_index - 1;
            if (avail < top_area) {
                // flip to back
                return { top_area, /*new_write_index*/ 0 };
            }
            return { avail, /*new_write_index*/ -1 };
        }
        auto avail = read_index - write_index - 1;
        return { avail, /*new_write_index*/ -1 };
    }

    static std::pair<int32_t, int32_t> check_read_available(int32_t write_index,
                                                            int32_t read_end_index,
                                                            int32_t read_index) noexcept
    {
        // writer: backだと思っている(endを更新しない)
        // reader: frontに遷移
        // という場合があるのでfront/backで条件分岐して判定する必要がある
        if (is_front_side(write_index, read_index)) {
            auto avail = write_index - read_index;
            return { avail, /*new_read_index*/ -1 };
        }
        auto avail = read_end_index - read_index;
        PT_ASSERT(0 <= avail);
        if (0 == avail) {
            // flip to front
            avail = write_index;
            return { avail, /*new_read_index*/ 0 };
        }
        return { avail, /*new_read_index*/ -1 };
    }

    bool update_write_ctx(uint64_t currentVal, uint64_t newValue) noexcept
    {
        for (;;) {
            auto expected = currentVal;
            auto success = m_write_ctx.compare_exchange_weak(/*expected*/ expected,
                                                             /*desired*/ newValue,
                                                             std::memory_order_release);
            if (success) {
                break;
            }
            if (0 > int64_t(expected)) {
                // canceled
                return false;
            }
        }
        m_write_ctx.notify_one();
        return true;
    }

    bool update_read_ctx(int32_t currentVal, int32_t newValue) noexcept
    {
        for (;;) {
            auto expected = currentVal;
            auto success = m_read_index.compare_exchange_weak(/*expected*/ expected,
                                                              /*desired*/ newValue,
                                                              std::memory_order_release);
            if (success) {
                break;
            }
            if (0 > expected) {
                // canceled
                return false;
            }
        }
        m_read_index.notify_one();
        return true;
    }

    static bool is_empty(int32_t write_index, int32_t read_end_index, int32_t read_index) noexcept
    {
        if (is_front_side(write_index, read_index)) {
            auto is_empty = (write_index == read_index);
            return is_empty;
        }
        auto is_empty = (read_end_index == read_index);
        return is_empty;
    }

#ifdef LIBPT_ENABLE_BUILD_TESTS
    int32_t waiterCount(void) noexcept { return m_waiter_count.load(); }
    friend class BoundedSpscZeroCopyTest;
#endif
};

} // namespace pf
