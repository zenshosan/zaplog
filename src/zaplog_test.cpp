#include <iostream>
#include <zaplog.hpp>

#define GTEST_DONT_DEFINE_TEST 1
#include <gtest/gtest.h>

GTEST_TEST(MyLibraryTest1, Function1Test)
{
    zaplog::Zaplog zaplog;
    EXPECT_EQ(zaplog.get_number(), 6);
}

class MyLibraryTest2 : public ::testing::Test
{
  protected:
    void SetUp() override { ; }
    zaplog::Zaplog m_zaplog;
};

GTEST_TEST_F(MyLibraryTest2, Function1Test2)
{
    EXPECT_EQ(m_zaplog.get_number(), 6);
}
