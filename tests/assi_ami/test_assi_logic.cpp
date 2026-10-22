#include <gtest/gtest.h>

#include "assi_types.hpp"
#include "assi_controller.hpp"

TEST(ASSITest, OffShowsNone)  // Parameters - Test group, Test name
{
	ASState state = ASState::Off;
	ASSIOutput output {
                .yellow_on = true,
                .blue_on = true,
                .buzzer_on = true,
                .flashing = true
        };

        assi_light_toggle(state, output);

	EXPECT_FALSE(output.blue_on);
	EXPECT_FALSE(output.yellow_on);
        EXPECT_FALSE(output.buzzer_on);
        EXPECT_FALSE(output.flashing);
}

// Check buzzer states for the states below later on
TEST(ASSITest, ReadyShowsYellow)
{
	ASState state = ASState::Ready;
	ASSIOutput output {
                .yellow_on = false,
                .blue_on = true,
                .buzzer_on = false,
                .flashing = true
        };

        assi_light_toggle(state, output);

	EXPECT_TRUE(output.yellow_on);
	EXPECT_FALSE(output.blue_on);
	EXPECT_FALSE(output.flashing);
}

TEST(ASSITest, DrivingShowsFlashingYellow)
{
	ASState state = ASState::Driving;
	ASSIOutput output {
                .yellow_on = false,
                .blue_on = true,
                .buzzer_on = false,
                .flashing = false
        };

        assi_light_toggle(state, output);

	EXPECT_TRUE(output.yellow_on);
	EXPECT_FALSE(output.blue_on);
	EXPECT_TRUE(output.flashing);
}

TEST(ASSITest, FinishedShowsBlue)
{
	ASState state = ASState::Finished;
	ASSIOutput output {
                .yellow_on = true,
                .blue_on = false,
                .buzzer_on = false,
                .flashing = true
        };

        assi_light_toggle(state, output);

	EXPECT_TRUE(output.blue_on);
	EXPECT_FALSE(output.yellow_on);
	EXPECT_FALSE(output.flashing);
}

TEST(ASSITest, EmergencyShowsFlashingBlue)
{
	ASState state = ASState::Emergency;
	ASSIOutput output {
                .yellow_on = true,
                .blue_on = false,
                .buzzer_on = false,
                .flashing = false
        };

        assi_light_toggle(state, output);

	EXPECT_TRUE(output.blue_on);
	EXPECT_FALSE(output.yellow_on);
	EXPECT_TRUE(output.flashing);
}