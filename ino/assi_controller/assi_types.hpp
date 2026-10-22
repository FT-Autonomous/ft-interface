#ifndef ASSI_TYPES_HPP
#define ASSI_TYPES_HPP

enum class ASState
{
	Off,
	Ready,
	Driving,
	Emergency,
	Finished
};

struct ASSIOutput
{
	bool yellow_on;
	bool blue_on;
	bool buzzer_on;
        bool flashing;
};

#endif
