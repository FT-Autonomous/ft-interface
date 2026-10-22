#include <gtest/gtest.h>

TEST(ASSITest, OffShowsNone)
{
	bool lights = false;
	EXPECT_FALSE(lights);
}

TEST(ASSITest, ReadyShowsYellow) // Parameters - Test group, Test name
{
	bool yellow_on = false;
	EXPECT_TRUE(yellow_on);
}
