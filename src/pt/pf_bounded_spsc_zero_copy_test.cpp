/* Copyright (c) 2024 Masaaki Hamada
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#define GTEST_DONT_DEFINE_TEST 1
#include <gtest/gtest.h>

#include "pf_bounded_spsc_zero_copy.h"
#include <random>

namespace pf {

class BoundedSpscZeroCopyTest : public ::testing::Test
{
  public:
    static constexpr auto s_max_size = 64;
    static constexpr auto s_want_size = 16;
    using RingBuffer = pf::BoundedSpscZeroCopy<>;

  public:
    static void setIndex(std::unique_ptr<RingBuffer>& rb,
                         int32_t write_index,
                         int32_t read_end_index,
                         int32_t read_index)
    {
        rb->m_write_ctx.store(RingBuffer::encode_ctx(write_index, read_end_index));
        rb->m_read_index.store(read_index);
    }
    enum
    {
        StateE0,
        StateE1,
        StateF0,
        StateF1,
        StateX0,
        StateX1,
        StateY0,
        StateY1,
    };
    static int getState(std::unique_ptr<RingBuffer>& rb)
    {
        auto write_ctx = rb->m_write_ctx.load(std::memory_order_relaxed);
        auto [write_index, read_end_index] = RingBuffer::decode_ctx(write_ctx);
        auto read_index = rb->m_read_index.load(std::memory_order_relaxed);

        if (read_index <= write_index) {
            // front
            if (write_index == read_index) {
                if (read_index == 0) {
                    return StateE0;
                }
                return StateE1;
            }
            if (read_index == 0) {
                if (write_index == s_max_size) {
                    return StateF0;
                }
                return StateX0;
            }
            return StateX1;
        }

        // back
        if (read_index - 1 == write_index) {
            return StateF1;
        }
        if (write_index == 0) {
            return StateY0;
        }
        return StateY1;
    }

    static int32_t getWaiterCount(std::unique_ptr<RingBuffer>& rb) { return rb->waiterCount(); }

    // 状態                  遷移先(w)  遷移先(r)
    // E0:Empty0 r==0,w==r   F0,X0,
    // E1:Empty1 r!=0,w==r   X1,Y1,F1
    // F0:Full0  r==0,w==end            E1,X1
    // F1:Full1  r!=0,r-1==w            X0,X1,E1
    // X0:Front0 r==0,r<w    F0,X0      E1,X1
    // X1:Front1 r!=0,r<w    X1,Y1,F1   E1,X1
    // Y0:Back0  w==0,w<r    F1,Y1      E0,Y0
    // Y1:Back1  W!=0,w<r    Y1,F1      X0,Y1

    static std::unique_ptr<RingBuffer> makeEmpty0(void)
    {
        auto rb = std::make_unique<RingBuffer>(s_max_size);
        PT_ASSERT(getState(rb) == StateE0);
        return rb;
    }
    static std::unique_ptr<RingBuffer> makeEmpty1(int32_t wAvail = 10)
    {
        auto rb = std::make_unique<RingBuffer>(s_max_size);
        auto w = s_max_size - wAvail;
        auto r_end = 0; // read_end_indexはfront sideではdon't care
        auto r = w;
        setIndex(rb, w, r_end, r);
        PT_ASSERT(getState(rb) == StateE1);
        return rb;
    }
    static std::unique_ptr<RingBuffer> makeFull0()
    {
        // Full0は r_end == s_max_size の場合に限定する
        // r_end < s_max_size だとX0ともみなせてしまうため
        auto rb = std::make_unique<RingBuffer>(s_max_size);
        auto r_end = s_max_size;
        setIndex(rb, r_end, r_end, 0);
        PT_ASSERT(getState(rb) == StateF0);
        return rb;
    }
    static std::unique_ptr<RingBuffer> makeFull1(int32_t wAvail,
                                                 int32_t rAvail = 10,
                                                 int32_t tailRoom = 2)
    {
        PT_ASSERT(wAvail == -1);
        auto rb = std::make_unique<RingBuffer>(s_max_size);
        auto r_end = s_max_size - tailRoom;
        auto r = r_end - rAvail;
        auto w = r - 1;
        PT_ASSERT(0 < r);
        setIndex(rb, w, r_end, r);
        PT_ASSERT(getState(rb) == StateF1);
        return rb;
    }
    static std::unique_ptr<RingBuffer> makeFront0(int32_t wAvail = 10)
    {
        auto rb = std::make_unique<RingBuffer>(s_max_size);
        auto w = s_max_size - wAvail;
        auto r_end = 0;
        auto r = 0;
        setIndex(rb, w, r_end, r);
        PT_ASSERT(getState(rb) == StateX0);
        return rb;
    }
    static std::unique_ptr<RingBuffer> makeFront1(int32_t wAvail = 10, int32_t rAvail = 10)
    {
        auto rb = std::make_unique<RingBuffer>(s_max_size);
        auto w = s_max_size - wAvail;
        auto r_end = 0;
        auto r = w - rAvail;
        PT_ASSERT(0 < r);
        setIndex(rb, w, r_end, r);
        PT_ASSERT(getState(rb) == StateX1);
        return rb;
    }
    static std::unique_ptr<RingBuffer> makeBack0(int32_t wAvail, int32_t rAvail = 10)
    {
        PT_ASSERT(wAvail == -1);
        auto rb = std::make_unique<RingBuffer>(s_max_size);
        auto w = 0;
        auto r_end = s_max_size - 2;
        auto r = r_end - rAvail;
        setIndex(rb, 0, r_end, r);
        PT_ASSERT(getState(rb) == StateY0);
        return rb;
    }
    static std::unique_ptr<RingBuffer> makeBack1(int32_t wAvail = 10, int32_t rAvail = 10)
    {
        auto rb = std::make_unique<RingBuffer>(s_max_size);
        auto r_end = s_max_size - 2;
        auto r = r_end - rAvail;
        auto w = r - 1 - wAvail;
        PT_ASSERT(0 <= w);
        PT_ASSERT(w < r - 1);
        setIndex(rb, w, r_end, r);
        PT_ASSERT(getState(rb) == StateY1);
        return rb;
    }
};

GTEST_TEST_F(BoundedSpscZeroCopyTest, BasicOperation)
{
    struct Reader
    {
        Reader(int32_t rsize0 = 0)
          : rsize(rsize0)
        {
        }
        int32_t rsize;
        const char* p = nullptr;
        int32_t avail = 0;
        bool called = false;
        int32_t operator()(const char* p0, int32_t avail0)
        {
            p = p0;
            avail = avail0;
            called = true;
            return rsize;
        };
    };
    struct Writer
    {
        Writer(int32_t wsize0 = 0)
          : wsize(wsize0)
        {
        }
        int32_t wsize;
        char* p = nullptr;
        int32_t avail = 0;
        bool called = false;
        int32_t operator()(char* p0, int32_t avail0)
        {
            p = p0;
            avail = avail0;
            called = true;
            return wsize;
        };
    };

    auto processReadBuf = [](std::unique_ptr<RingBuffer>& rb, Reader& reader, int32_t wantSize) {
        auto [ptr, size] = rb->getReadPtr(wantSize);
        if (ptr) {
            auto readSize = reader(ptr, size);
            return rb->moveReadPtr(readSize);
        }
        return size;
    };
    auto processWriteBuf = [](std::unique_ptr<RingBuffer>& rb, Writer& writer, int32_t wantSize) {
        auto [ptr, size] = rb->getWritePtr(wantSize);
        if (ptr) {
            auto writtenSize = writer(ptr, size);
            return rb->moveWritePtr(writtenSize);
        }
        return size;
    };

    {
        auto rb = makeEmpty0();
        Reader reader;
        auto ret = processReadBuf(rb, reader, s_max_size / 2 + 1);
        EXPECT_EQ(ret, -1);
        EXPECT_FALSE(reader.called);
    }
    {
        auto rb = makeEmpty0();
        Writer writer;
        auto ret = processWriteBuf(rb, writer, s_max_size / 2 + 1);
        EXPECT_EQ(ret, -1);
        EXPECT_FALSE(writer.called);
    }
    {
        int32_t size = s_max_size / 2;
        auto rb = makeEmpty0();
        Writer writer(size);
        auto ret = processWriteBuf(rb, writer, size);
        EXPECT_EQ(ret, size);
        EXPECT_EQ(writer.avail, s_max_size);

        Reader reader(size);
        ret = processReadBuf(rb, reader, size);
        EXPECT_EQ(ret, size);
        EXPECT_EQ(reader.avail, size);
    }
}

GTEST_TEST_F(BoundedSpscZeroCopyTest, StateTransition)
{
    struct Reader
    {
        Reader(int32_t rsize0 = 0)
          : rsize(rsize0)
        {
        }
        int32_t rsize;
        const char* p = nullptr;
        int32_t num = 0;
        bool called = false;
        int32_t operator()(const char* p0, int32_t avail0)
        {
            p = p0;
            num = avail0;
            called = true;
            return rsize;
        };
    };
    struct Writer
    {
        Writer(int32_t wsize0 = 0)
          : wsize(wsize0)
        {
        }
        int32_t wsize;
        char* p = nullptr;
        int32_t num = 0;
        bool called = false;
        int32_t operator()(char* p0, int32_t avail0)
        {
            p = p0;
            num = avail0;
            called = true;
            return wsize;
        };
    };

    auto processReadBuf = [](std::unique_ptr<RingBuffer>& rb, Reader& reader, int32_t wantSize) {
        auto [ptr, size] = rb->getReadPtr(wantSize);
        if (ptr) {
            auto readSize = reader(ptr, size);
            return rb->moveReadPtr(readSize);
        }
        return size;
    };
    auto processWriteBuf = [](std::unique_ptr<RingBuffer>& rb, Writer& writer, int32_t wantSize) {
        auto [ptr, size] = rb->getWritePtr(wantSize);
        if (ptr) {
            auto writtenSize = writer(ptr, size);
            return rb->moveWritePtr(writtenSize);
        }
        return size;
    };

    //
    // E0
    //
    {
        // E0 write-> F0
        auto rb = makeEmpty0();
        Writer writer(s_max_size);
        auto ret = processWriteBuf(rb, writer, s_max_size / 2);
        EXPECT_EQ(ret, s_max_size);
        EXPECT_TRUE(writer.p != nullptr);
        EXPECT_EQ(writer.num, s_max_size);
        EXPECT_EQ(getState(rb), StateF0);
    }
    {
        // E0 write-> F0 (fail)
        Writer writer(s_max_size);
        auto rb = makeEmpty0();
        auto ret = processWriteBuf(rb, writer, s_max_size / 2 + 1);
        EXPECT_EQ(ret, -1); // この場合は0なくtoo largeエラーになる
        EXPECT_FALSE(writer.called);
    }
    {
        // E0 write-> X0
        int32_t wSize = s_max_size / 2;
        Writer writer(wSize);
        auto rb = makeEmpty0();
        auto ret = processWriteBuf(rb, writer, wSize);
        EXPECT_EQ(ret, wSize);
        EXPECT_TRUE(writer.p != nullptr);
        EXPECT_EQ(writer.num, s_max_size);
        EXPECT_EQ(getState(rb), StateX0);
    }
    {
        // E0 read-> N/A
        auto rb = makeEmpty0();
        Reader reader;
        auto ret = processReadBuf(rb, reader, 0);
        EXPECT_EQ(ret, 0);
        EXPECT_FALSE(reader.called);
        EXPECT_EQ(getState(rb), StateE0);
    }

    //
    // E1
    //
    {
        // E1 write-> X1
        int32_t wAvail = 50; // max_size 64
        int32_t wSize = 10;
        auto rb = makeEmpty1(wAvail);
        Writer writer(wSize);
        auto ret = processWriteBuf(rb, writer, wSize);
        EXPECT_EQ(ret, wSize);
        EXPECT_TRUE(writer.p != nullptr);
        EXPECT_EQ(writer.num, wAvail);
        EXPECT_EQ(getState(rb), StateX1);
    }
    {
        // E1 write-> X1
        // write_indexがmax_sizeになる (X1のテストでカバー)
        int32_t wAvail = 50; // max_size 64
        int32_t wSize = wAvail;
        auto rb = makeEmpty1(wAvail);
        Writer writer(wSize);
        auto ret = processWriteBuf(rb, writer, /*wSize*/ s_max_size / 2);
        EXPECT_EQ(ret, wSize);
        EXPECT_TRUE(writer.p != nullptr);
        EXPECT_EQ(writer.num, wAvail);
        EXPECT_EQ(getState(rb), StateX1);
    }
    {
        // E1 write-> Y1
        // これが起こるのはr==w==max_sizeの場合のみ
        int32_t wAvail = 0;
        int32_t wSize = 10;
        auto rb = makeEmpty1(wAvail);
        Writer writer(wSize);
        auto ret = processWriteBuf(rb, writer, wSize);
        EXPECT_EQ(ret, wSize);
        EXPECT_TRUE(writer.p != nullptr);
        EXPECT_EQ(writer.num, s_max_size - 1);
        EXPECT_EQ(getState(rb), StateY1);
    }
    {
        // E1 write-> F1
        // これが起こるのはr==w==max_sizeの場合のみ
        int32_t wAvail = 0;
        int32_t wSize = s_max_size - 1;
        auto rb = makeEmpty1(wAvail);
        Writer writer(wSize);
        auto ret = processWriteBuf(rb, writer, /*wSize*/ s_max_size / 2);
        EXPECT_EQ(ret, wSize);
        EXPECT_TRUE(writer.p != nullptr);
        EXPECT_EQ(writer.num, s_max_size - 1);
        EXPECT_EQ(getState(rb), StateF1);
    }
    {
        // E1 write-> F1 (fail)
        // これが起こるのはr==w==max_sizeの場合のみ
        // wantSizeの上限をs_max_size/2にしたのでこのテストの意味がない
        int32_t wAvail = 0;
        int32_t wSize = s_max_size;
        auto rb = makeEmpty1(wAvail);
        Writer writer(1);
        auto ret = processWriteBuf(rb, writer, /*wSize*/ s_max_size / 2 + 1);
        EXPECT_EQ(ret, -1);
        EXPECT_FALSE(writer.called);
        EXPECT_EQ(getState(rb), StateE1);
    }
    {
        // E1 read-> N/A
        int32_t wAvail = 10;
        auto rb = makeEmpty1(wAvail);
        Reader reader(1);
        auto ret = processReadBuf(rb, reader, 0);
        EXPECT_EQ(ret, 0);
        EXPECT_FALSE(reader.called);
        EXPECT_EQ(getState(rb), StateE1);
    }

    //
    // F0
    //
    {
        // F0 write-> N/A
        auto rb = makeFull0();
        Writer writer(1);
        auto ret = processWriteBuf(rb, writer, 0);
        EXPECT_EQ(ret, 0);
        EXPECT_FALSE(writer.called);
        EXPECT_EQ(getState(rb), StateF0);
    }
    {
        // F0 read-> E1
        auto rb = makeFull0();
        int32_t rSize = s_max_size;
        Reader reader(rSize);
        auto ret = processReadBuf(rb, reader, /*rSize*/ s_max_size / 2);
        EXPECT_EQ(ret, rSize);
        EXPECT_TRUE(reader.p != nullptr);
        EXPECT_EQ(reader.num, rSize);
        EXPECT_EQ(getState(rb), StateE1);

        // w == r == r_end == max_sizeという特殊な状態 (E1テストでカバー)
    }
    {
        // F0 read-> X1
        auto rb = makeFull0();
        int32_t rSize = 10;
        Reader reader(rSize);
        auto ret = processReadBuf(rb, reader, rSize);
        EXPECT_EQ(ret, rSize);
        EXPECT_TRUE(reader.p != nullptr);
        EXPECT_EQ(reader.num, s_max_size);
        EXPECT_EQ(getState(rb), StateX1);

        // r < w == max_sizeという特殊な状態 (X1テストでカバー)
    }
    {
        // F0 read-> X1 (fail)
        auto rb = makeFull0();
        int32_t rSize = s_max_size + 1;
        Reader reader(1);
        auto ret = processReadBuf(rb, reader, rSize);
        EXPECT_EQ(ret, -1); // too large
        EXPECT_FALSE(reader.called);
        EXPECT_EQ(getState(rb), StateF0);
    }

    //
    // F1
    //
    {
        // F1 write-> N/A
        int32_t rAvail = 10;
        auto rb = makeFull1(/*wAvail*/ -1, rAvail);
        Writer writer(1);
        auto ret = processWriteBuf(rb, writer, 0);
        EXPECT_EQ(ret, 0);
        EXPECT_FALSE(writer.called);
        EXPECT_EQ(getState(rb), StateF1);
    }
    {
        // F1 read-> X0
        int32_t rAvail = 10;
        auto rb = makeFull1(/*wAvail*/ -1, rAvail);
        Reader reader(rAvail);
        auto ret = processReadBuf(rb, reader, rAvail);
        EXPECT_EQ(ret, rAvail);
        EXPECT_TRUE(reader.p != nullptr);
        EXPECT_EQ(reader.num, rAvail);
        EXPECT_EQ(getState(rb), StateX0);
    }
    {
        // F1 read-> X1
        // r=end,r-1=w
        int32_t rAvail = 0;
        int32_t rSize = 10;
        auto rb = makeFull1(/*wAvail*/ -1, rAvail, /*tailRoom*/ 0);
        Reader reader(rSize);
        auto ret = processReadBuf(rb, reader, rSize);
        EXPECT_EQ(ret, rSize);
        EXPECT_TRUE(reader.p != nullptr);
        EXPECT_EQ(reader.num, s_max_size - 1);
        EXPECT_EQ(getState(rb), StateX1);
    }
    {
        // F1 read-> E1
        // r=end,r-1=w
        int32_t rAvail = 0;
        int32_t rSize = s_max_size - 1;
        auto rb = makeFull1(/*wAvail*/ -1, rAvail, /*tailRoom*/ 0);
        Reader reader(rSize);
        auto ret = processReadBuf(rb, reader, /*rSize*/ s_max_size / 2);
        EXPECT_EQ(ret, rSize);
        EXPECT_TRUE(reader.p != nullptr);
        EXPECT_EQ(reader.num, s_max_size - 1);
        EXPECT_EQ(getState(rb), StateE1);
    }
    {
        // F1 read-> E1 (fail)
        // r=end,r-1=w
        int32_t rAvail = 0;
        int32_t rSize = s_max_size;
        auto rb = makeFull1(/*wAvail*/ -1, rAvail, /*tailRoom*/ 0);
        Reader reader(1);
        auto ret = processReadBuf(rb, reader, /*rSize*/ s_max_size / 2 + 1);
        EXPECT_EQ(ret, -1);
        EXPECT_FALSE(reader.called);
        EXPECT_EQ(getState(rb), StateF1);
    }

    //
    // X0
    //
    {
        // X0 write-> F0
        int32_t wAvail = 10;
        int32_t wSize = 10;
        auto rb = makeFront0(wAvail);
        Writer writer(wSize);
        auto ret = processWriteBuf(rb, writer, wSize);
        EXPECT_EQ(ret, wSize);
        EXPECT_TRUE(writer.p != nullptr);
        EXPECT_EQ(writer.num, wAvail);
        EXPECT_EQ(getState(rb), StateF0);
    }
    if (0) {
        // X0 write-> F0 (fail)
        // wait指定不要にしたので意味のないテストになっている
        int32_t wAvail = 10;
        int32_t wSize = 11;
        auto rb = makeFront0(wAvail);
        Writer writer(1);
        auto ret = processWriteBuf(rb, writer, /*wSize*/ s_max_size);
        EXPECT_EQ(ret, -1);
        EXPECT_FALSE(writer.called);
        EXPECT_EQ(getState(rb), StateX0);
    }
    {
        // X0 write-> X0
        int32_t wAvail = 10;
        int32_t wSize = 5;
        auto rb = makeFront0(wAvail);
        Writer writer(wSize);
        auto ret = processWriteBuf(rb, writer, wSize);
        EXPECT_EQ(ret, wSize);
        EXPECT_TRUE(writer.p != nullptr);
        EXPECT_EQ(writer.num, wAvail);
        EXPECT_EQ(getState(rb), StateX0);
    }
    {
        // X0 read-> E1
        int32_t wAvail = 10;
        int32_t rAvail = s_max_size - wAvail;
        int32_t rSize = rAvail;
        auto rb = makeFront0(wAvail);
        Reader reader(rSize);
        auto ret = processReadBuf(rb, reader, /*rSize*/ s_max_size / 2);
        EXPECT_EQ(ret, rSize);
        EXPECT_TRUE(reader.p != nullptr);
        EXPECT_EQ(reader.num, rAvail);
        EXPECT_EQ(getState(rb), StateE1);
    }
    if (0) {
        // X0 read-> E1 (fail)
        int32_t wAvail = 10;
        int32_t rAvail = s_max_size - wAvail;
        int32_t rSize = rAvail + 1;
        auto rb = makeFront0(wAvail);
        Reader reader(1);
        auto ret = processReadBuf(rb, reader, 0);
        EXPECT_EQ(ret, 0);
        EXPECT_FALSE(reader.called);
        EXPECT_EQ(getState(rb), StateX0);
    }
    {
        // X0 read-> X1
        int32_t wAvail = 10;
        int32_t rAvail = s_max_size - wAvail;
        int32_t rSize = 1;
        auto rb = makeFront0(wAvail);
        Reader reader(rSize);
        auto ret = processReadBuf(rb, reader, rSize);
        EXPECT_EQ(ret, rSize);
        EXPECT_TRUE(reader.p != nullptr);
        EXPECT_EQ(reader.num, rAvail);
        EXPECT_EQ(getState(rb), StateX1);
    }

    //
    // X1
    //
    {
        // X1 write-> X1
        int32_t wAvail = 40;
        int32_t wSize = 5;
        int32_t rAvail = 10;
        auto rb = makeFront1(wAvail, rAvail);
        Writer writer(wSize);
        auto ret = processWriteBuf(rb, writer, wSize);
        EXPECT_EQ(ret, wSize);
        EXPECT_TRUE(writer.p != nullptr);
        EXPECT_EQ(writer.num, wAvail);
        EXPECT_EQ(getState(rb), StateX1);
    }
    {
        // X1 write-> X1(write_indexがmax_sizeになる)
        int32_t wAvail = 40;
        int32_t wSize = wAvail;
        int32_t rAvail = 10;
        auto rb = makeFront1(wAvail, rAvail);
        Writer writer(wSize);
        auto ret = processWriteBuf(rb, writer, /*wSize*/ s_max_size / 2);
        EXPECT_EQ(ret, wSize);
        EXPECT_TRUE(writer.p != nullptr);
        EXPECT_EQ(writer.num, wAvail);
        EXPECT_EQ(getState(rb), StateX1);

        // r < w == max_sizeという特殊な状態
        // 次のテストでカバー
    }
    {
        // X1 write-> Y1
        // w == max_size
        int32_t wAvail = 0;
        int32_t rAvail = 20;
        // w_index = s_max_size - wAvail; //64
        // r_index = w_index - rAvail;    //44

        auto rb = makeFront1(wAvail, rAvail);
        int32_t wSize = 10;
        Writer writer(wSize);
        auto ret = processWriteBuf(rb, writer, wSize);
        EXPECT_EQ(ret, wSize);
        // w_index = 0
        // r_index = 44
        int32_t wAvail2 = s_max_size - wAvail - rAvail - 1;
        EXPECT_TRUE(writer.p != nullptr);
        EXPECT_EQ(writer.num, wAvail2);
        // w_index = 10
        // r_index = 44
        EXPECT_EQ(getState(rb), StateY1);
    }
    if (0) {
        // X1 write-> Y1 (fail)
        // w == max_size
        int32_t wAvail = 0;
        int32_t rAvail = 20;
        int32_t wSize = s_max_size - wAvail - rAvail;
        auto rb = makeFront1(wAvail, rAvail);
        Writer writer(1);
        auto ret = processWriteBuf(rb, writer, 0);
        EXPECT_EQ(ret, 0);
        EXPECT_FALSE(writer.called);
        EXPECT_EQ(getState(rb), StateX1);
    }
    {
        // X1 write-> Y1
        int32_t wAvail = 10;
        int32_t rAvail = 10;
        // w_index = s_max_size - wAvail; //54
        // r_index = w_index - rAvail;    //44

        auto rb = makeFront1(wAvail, rAvail);

        int32_t wSize = s_max_size - wAvail - rAvail - 2;
        Writer writer(wSize);
        auto ret = processWriteBuf(rb, writer, /*wSize*/ s_max_size / 2);
        EXPECT_EQ(ret, wSize);
        // w_index = 10
        // r_index = 44
        EXPECT_TRUE(writer.p != nullptr);
        EXPECT_EQ(writer.num, wSize + 1);
        EXPECT_EQ(getState(rb), StateY1);
    }
    {
        // X1 write-> F1
        int32_t wAvail = 10;
        int32_t wSize = 20;
        int32_t rAvail = s_max_size - wAvail - (wSize + 1);
        // w_index = s_max_size - wAvail; //54
        // r_index = wAvail;              //21
        auto rb = makeFront1(wAvail, rAvail);
        Writer writer(wSize);
        auto ret = processWriteBuf(rb, writer, wSize);
        EXPECT_EQ(ret, wSize);
        EXPECT_TRUE(writer.p != nullptr);
        EXPECT_EQ(writer.num, wSize);
        EXPECT_EQ(getState(rb), StateF1);
    }
    if (0) {
        // X1 write-> F1 (fail)
        int32_t wAvail = 10;
        int32_t wSize = 20;
        int32_t rAvail = s_max_size - wAvail - (wSize + 1);
        // w_index = s_max_size - wAvail; //54
        // r_index = wAvail;              //21
        auto rb = makeFront1(wAvail, rAvail);
        Writer writer(1);
        auto ret = processWriteBuf(rb, writer, 0);
        EXPECT_EQ(ret, 0);
        EXPECT_FALSE(writer.called);
    }
    {
        // X1 read-> E1
        int32_t wAvail = 10;
        int32_t rAvail = 10;
        int32_t rSize = rAvail;
        auto rb = makeFront1(wAvail, rAvail);
        Reader reader(rSize);
        auto ret = processReadBuf(rb, reader, rSize);
        EXPECT_EQ(ret, rSize);
        EXPECT_TRUE(reader.p != nullptr);
        EXPECT_EQ(reader.num, rAvail);
        EXPECT_EQ(getState(rb), StateE1);
    }
    if (0) {
        // X1 read-> E1
        int32_t wAvail = 10;
        int32_t rAvail = 10;
        int32_t rSize = rAvail + 1;
        auto rb = makeFront1(wAvail, rAvail);
        Reader reader(1);
        auto ret = processReadBuf(rb, reader, 0);
        EXPECT_EQ(ret, rSize);
        EXPECT_TRUE(reader.p == nullptr);
        EXPECT_EQ(reader.num, 0);
    }
    {
        // X1 read-> X1
        int32_t wAvail = 10;
        int32_t rAvail = 10;
        int32_t rSize = 1;
        auto rb = makeFront1(wAvail, rAvail);
        Reader reader(rSize);
        auto ret = processReadBuf(rb, reader, rSize);
        EXPECT_EQ(ret, rSize);
        EXPECT_TRUE(reader.p != nullptr);
        EXPECT_EQ(reader.num, rAvail);
        EXPECT_EQ(getState(rb), StateX1);
    }

    //
    // Y0
    //
    {
        // Y0 write-> F1
        int32_t rAvail = 20;
        int32_t wAvail = s_max_size - 2 - rAvail - 1; // -2はmakeBack0()のマジックナンバーより
        int32_t wSize = wAvail;
        auto rb = makeBack0(/*wAvail*/ -1, rAvail);
        Writer writer(wSize);
        auto ret = processWriteBuf(rb, writer, /*wSize*/ s_max_size / 2);
        EXPECT_EQ(ret, wSize);
        EXPECT_TRUE(writer.p != nullptr);
        EXPECT_EQ(writer.num, wSize);
        EXPECT_EQ(getState(rb), StateF1);
    }
    if (0) {
        // Y0 write-> F1 (faild)
        int32_t rAvail = 20;
        int32_t wAvail = s_max_size - 2 - rAvail - 1;
        int32_t wSize = wAvail + 1;
        auto rb = makeBack0(/*wAvail*/ -1, rAvail);
        Writer writer(wSize);
        auto ret = processWriteBuf(rb, writer, 0);
        EXPECT_EQ(ret, 0);
        EXPECT_FALSE(writer.called);
        EXPECT_EQ(getState(rb), StateY0);
    }
    {
        // Y0 write-> Y1
        int32_t rAvail = 20;
        int32_t wAvail = s_max_size - 2 - rAvail - 1;
        int32_t wSize = 10;
        auto rb = makeBack0(/*wAvail*/ -1, rAvail);
        Writer writer(wSize);
        auto ret = processWriteBuf(rb, writer, wSize);
        EXPECT_EQ(ret, wSize);
        EXPECT_TRUE(writer.p != nullptr);
        EXPECT_EQ(writer.num, wAvail);
        EXPECT_EQ(getState(rb), StateY1);
    }
    {
        // Y0 read-> E0
        int32_t rAvail = 10;
        // int32_t wAvail=s_max_size - rAvail - 2;
        int32_t rSize = rAvail;
        auto rb = makeBack0(/*wAvail*/ -1, rAvail);
        Reader reader(rSize);
        auto ret = processReadBuf(rb, reader, rSize);
        EXPECT_EQ(ret, rSize);
        EXPECT_TRUE(reader.p != nullptr);
        EXPECT_EQ(reader.num, rAvail);
        EXPECT_EQ(getState(rb), StateE0);
    }
    {
        // Y0 read-> Y0
        int32_t rAvail = 10;
        // int32_t wAvail=s_max_size - rAvail - 2;
        int32_t rSize = 1;
        auto rb = makeBack0(/*wAvail*/ -1, rAvail);
        Reader reader(rSize);
        auto ret = processReadBuf(rb, reader, rSize);
        EXPECT_EQ(ret, rSize);
        EXPECT_TRUE(reader.p != nullptr);
        EXPECT_EQ(reader.num, rAvail);
        EXPECT_EQ(getState(rb), StateY0);
    }

    //
    // Y1
    //
    {
        // Y1 write-> Y1
        int32_t rAvail = 20;
        int32_t wAvail = 20;
        int32_t wSize = 10;
        auto rb = makeBack1(wAvail, rAvail);
        Writer writer(wSize);
        auto ret = processWriteBuf(rb, writer, wSize);
        EXPECT_EQ(ret, wSize);
        EXPECT_TRUE(writer.p != nullptr);
        EXPECT_EQ(writer.num, wAvail);
        EXPECT_EQ(getState(rb), StateY1);
    }
    {
        // Y1 write-> F1
        int32_t rAvail = 20;
        int32_t wAvail = 20;
        int32_t wSize = wAvail;
        auto rb = makeBack1(wAvail, rAvail);
        Writer writer(wSize);
        auto ret = processWriteBuf(rb, writer, wSize);
        EXPECT_EQ(ret, wSize);
        EXPECT_TRUE(writer.p != nullptr);
        EXPECT_EQ(writer.num, wAvail);
        EXPECT_EQ(getState(rb), StateF1);
    }
    if (0) {
        // Y1 write-> F1 (fail)
        int32_t rAvail = 20;
        int32_t wAvail = 20;
        int32_t wSize = wAvail + 1;
        auto rb = makeBack1(wAvail, rAvail);
        Writer writer(1);
        auto ret = processWriteBuf(rb, writer, 0);
        EXPECT_EQ(ret, 0);
        EXPECT_FALSE(writer.called);
        EXPECT_EQ(getState(rb), StateY1);
    }
    {
        // Y1 read-> X0
        int32_t rAvail = 20;
        int32_t rSize = rAvail;
        int32_t wAvail = 20;
        auto rb = makeBack1(wAvail, rAvail);
        Reader reader(rSize);
        auto ret = processReadBuf(rb, reader, rSize);
        EXPECT_EQ(ret, rSize);
        EXPECT_TRUE(reader.p != nullptr);
        EXPECT_EQ(reader.num, rAvail);
        EXPECT_EQ(getState(rb), StateX0);
    }
    {
        // Y1 read-> Y1
        int32_t rAvail = 20;
        int32_t rSize = 10;
        int32_t wAvail = 20;
        auto rb = makeBack1(wAvail, rAvail);
        Reader reader(rSize);
        auto ret = processReadBuf(rb, reader, rSize);
        EXPECT_EQ(ret, rSize);
        EXPECT_TRUE(reader.p != nullptr);
        EXPECT_EQ(reader.num, rAvail);
        EXPECT_EQ(getState(rb), StateY1);
    }
}

GTEST_TEST_F(BoundedSpscZeroCopyTest, BlockingRead)
{
    auto rb = makeEmpty0();

    struct Reader
    {
        Reader(int32_t rsize0 = 0)
          : rsize(rsize0)
        {
        }
        int32_t rsize;
        int32_t operator()(const char* p0, int32_t avail0)
        {
            EXPECT_TRUE(0 < avail0);
            EXPECT_EQ(p0[0], 123);
            return rsize;
        };
    };
    struct Writer
    {
        Writer(int32_t wsize0 = 0)
          : wsize(wsize0)
        {
        }
        int32_t wsize;
        int32_t operator()(char* p0, int32_t avail0)
        {
            EXPECT_TRUE(0 < avail0);
            p0[0] = 123;
            return wsize;
        };
    };

    auto processReadBuf = [](std::unique_ptr<RingBuffer>& rb, Reader& reader, int32_t wantSize) {
        auto [ptr, size] = rb->getReadPtr(wantSize);
        if (ptr) {
            auto readSize = reader(ptr, size);
            return rb->moveReadPtr(readSize);
        }
        return size;
    };
    auto processWriteBuf = [](std::unique_ptr<RingBuffer>& rb, Writer& writer, int32_t wantSize) {
        auto [ptr, size] = rb->getWritePtr(wantSize);
        if (ptr) {
            auto writtenSize = writer(ptr, size);
            return rb->moveWritePtr(writtenSize);
        }
        return size;
    };

    std::thread th([&]() {
        while (0 >= getWaiterCount(rb)) {
            SWITCH_TO_THREAD();
        }
        int32_t wsize = 1;
        Writer writer(wsize);
        auto ret = processWriteBuf(rb, writer, wsize);
        PT_ASSERT(ret == 1);
    });

    int32_t rsize = 1;
    Reader reader(rsize);
    auto ret = processReadBuf(rb, reader, rsize);
    EXPECT_EQ(ret, 1);

    th.join();
}

GTEST_TEST_F(BoundedSpscZeroCopyTest, BlockingWrite)
{
    auto rb = makeEmpty0();

    struct Reader
    {
        Reader(int32_t rsize0 = 0)
          : rsize(rsize0)
        {
        }
        int32_t rsize;
        int32_t operator()(const char* p0, int32_t avail0)
        {
            EXPECT_TRUE(0 < avail0);
            for (int i = 0; i < rsize; ++i) {
                PT_ASSERT(p0[i] == i + 10);
            }
            return rsize;
        };
    };
    struct Writer
    {
        Writer(int32_t wsize0 = 0)
          : wsize(wsize0)
        {
        }
        int32_t wsize;
        int32_t operator()(char* p0, int32_t avail0)
        {
            EXPECT_TRUE(0 < avail0);
            for (int i = 0; i < wsize; ++i) {
                p0[i] = char(i + 10);
            }
            return wsize;
        };
    };

    auto processReadBuf = [](std::unique_ptr<RingBuffer>& rb, Reader& reader, int32_t wantSize) {
        auto [ptr, size] = rb->getReadPtr(wantSize);
        if (ptr) {
            auto readSize = reader(ptr, size);
            return rb->moveReadPtr(readSize);
        }
        return size;
    };
    auto processWriteBuf = [](std::unique_ptr<RingBuffer>& rb, Writer& writer, int32_t wantSize) {
        auto [ptr, size] = rb->getWritePtr(wantSize);
        if (ptr) {
            auto writtenSize = writer(ptr, size);
            return rb->moveWritePtr(writtenSize);
        }
        return size;
    };

    const int32_t rsize = s_max_size / 2;

    std::thread reader([&]() {
        while (0 >= getWaiterCount(rb)) {
            std::this_thread::yield();
        }
        // #1
        Reader reader(rsize);
        auto ret = processReadBuf(rb, reader, rsize);
        PT_ASSERT(ret == rsize);

        // #2
        ret = processReadBuf(rb, reader, rsize);
        PT_ASSERT(ret == rsize);

        // #3
        ret = processReadBuf(rb, reader, rsize);
        PT_ASSERT(ret == rsize);
    });

    // #1
    Writer writer(rsize);
    auto ret = processWriteBuf(rb, writer, rsize);
    EXPECT_EQ(ret, rsize);

    // #2
    ret = processWriteBuf(rb, writer, rsize);
    EXPECT_EQ(ret, rsize);

    // #3
    ret = processWriteBuf(rb, writer, rsize);
    EXPECT_EQ(ret, rsize);

    reader.join();
}

GTEST_TEST_F(BoundedSpscZeroCopyTest, CancelRead)
{
    auto rb = makeEmpty0();

    struct Reader
    {
        Reader(int32_t rsize0 = 0)
          : rsize(rsize0)
        {
        }
        int32_t rsize;
        int32_t operator()(const char* p0, int32_t avail0)
        {
            EXPECT_TRUE(0 < avail0);
            for (int i = 0; i < rsize; ++i) {
                PT_ASSERT(p0[i] == i + 10);
            }
            return rsize;
        };
    };

    auto processReadBuf = [](std::unique_ptr<RingBuffer>& rb, Reader& reader, int32_t wantSize) {
        auto [ptr, size] = rb->getReadPtr(wantSize);
        if (ptr) {
            auto readSize = reader(ptr, size);
            return rb->moveReadPtr(readSize);
        }
        return size;
    };

    std::thread th([&]() {
        while (0 >= getWaiterCount(rb)) {
            std::this_thread::yield();
        }
        rb->cancel();
    });

    char* p = nullptr;
    int32_t rsize = 1;
    Reader reader(rsize);
    auto ret = processReadBuf(rb, reader, rsize);
    EXPECT_EQ(ret, -1);

    th.join();
}

GTEST_TEST_F(BoundedSpscZeroCopyTest, CancelWrite)
{
    auto rb = makeEmpty0();

    struct Writer
    {
        Writer(int32_t wsize0 = 0)
          : wsize(wsize0)
        {
        }
        int32_t wsize;
        int32_t operator()(char* p0, int32_t avail0)
        {
            EXPECT_TRUE(0 < avail0);
            for (int i = 0; i < wsize; ++i) {
                p0[i] = char(i + 10);
            }
            return wsize;
        };
    };

    auto processWriteBuf = [](std::unique_ptr<RingBuffer>& rb, Writer& writer, int32_t wantSize) {
        auto [ptr, size] = rb->getWritePtr(wantSize);
        if (ptr) {
            auto writtenSize = writer(ptr, size);
            return rb->moveWritePtr(writtenSize);
        }
        return size;
    };

    std::thread th([&]() {
        while (0 >= getWaiterCount(rb)) {
            std::this_thread::yield();
        }
        rb->cancel();
    });

    char* p = nullptr;
    const int32_t rsize = s_max_size / 3;
    // #1
    Writer writer(rsize);
    auto ret = processWriteBuf(rb, writer, rsize);
    EXPECT_EQ(ret, rsize);

    // #2
    ret = processWriteBuf(rb, writer, rsize);
    EXPECT_EQ(ret, rsize);

    // #3
    ret = processWriteBuf(rb, writer, rsize);
    EXPECT_EQ(ret, rsize);

    // #4
    ret = processWriteBuf(rb, writer, rsize);
    EXPECT_EQ(ret, -1);

    th.join();
}

GTEST_TEST_F(BoundedSpscZeroCopyTest, Random)
{
    struct Reader
    {
        Reader(int32_t rsize0, int n0)
          : rsize(rsize0)
          , n(n0)
        {
        }
        int32_t rsize;
        int32_t n;
        int32_t operator()(const char* p0, int32_t avail0)
        {
            EXPECT_TRUE(0 < avail0);
            for (int i = 0; i < rsize; ++i) {
                PT_ASSERT(p0[i] == (char)(i + n));
            }
            return rsize;
        };
    };
    struct Writer
    {
        Writer(int32_t wsize0, int n0)
          : wsize(wsize0)
          , n(n0)
        {
        }
        int32_t wsize;
        int32_t n;
        int32_t operator()(char* p0, int32_t avail0)
        {
            EXPECT_TRUE(0 < avail0);
            for (int i = 0; i < wsize; ++i) {
                p0[i] = char(i + n);
            }
            return wsize;
        };
    };

    auto processReadBuf = [](std::unique_ptr<RingBuffer>& rb, Reader& reader, int32_t wantSize) {
        auto [ptr, size] = rb->getReadPtr(wantSize);
        if (ptr) {
            auto readSize = reader(ptr, size);
            return rb->moveReadPtr(readSize);
        }
        return size;
    };
    auto processWriteBuf = [](std::unique_ptr<RingBuffer>& rb, Writer& writer, int32_t wantSize) {
        auto [ptr, size] = rb->getWritePtr(wantSize);
        if (ptr) {
            auto writtenSize = writer(ptr, size);
            return rb->moveWritePtr(writtenSize);
        }
        return size;
    };

    volatile uint32_t seed = 4646;
    std::mt19937 mt(seed);
    std::uniform_int_distribution<int> dist(1, 32);

    const size_t s_sizeListLength = 10000;
    std::vector<int> sizeList(s_sizeListLength);
    std::generate(sizeList.begin(), sizeList.end(), [&]() { return dist(mt); });

    auto rb = makeEmpty0();

    const int loopNum = 1000000;
    auto producer = [&]() {
        for (int i = 0; i < loopNum; ++i) {
            auto sz = sizeList[i % sizeList.size()];
            Writer writer(sz, i);
            auto ret = processWriteBuf(rb, writer, sz);
            PT_ASSERT(ret == sz);
        }
    };
    auto consumer = [&]() {
        for (int i = 0; i < loopNum; ++i) {
            auto sz = sizeList[i % sizeList.size()];
            Reader reader(sz, i);
            auto ret = processReadBuf(rb, reader, sz);
            PT_ASSERT(ret == sz);
        }
    };

    std::thread c([&]() { producer(); });
    std::thread p([&]() { consumer(); });

    p.join();
    c.join();
}

} // namespace pf
