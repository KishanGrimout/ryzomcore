#include <gtest/gtest.h>

#include <game_share/dynworld/mapgen/noise.h>

TEST(DynworldNoiseTest, PerlinNoiseIsDeterministic)
{
    const float first = noise2d_perlin(12.25f, -3.5f, 91, 4, 0.5f);
    const float second = noise2d_perlin(12.25f, -3.5f, 91, 4, 0.5f);

    EXPECT_FLOAT_EQ(first, second);
}

TEST(DynworldNoiseTest, NoiseSampleSubtractsScalar)
{
    NoiseSample sample;
    sample.value = 3.0f;
    sample.derivative.set(1.0f, 2.0f, 3.0f);

    sample -= 2.0f;

    EXPECT_FLOAT_EQ(sample.value, 1.0f);
    EXPECT_FLOAT_EQ(sample.derivative.x, 1.0f);
    EXPECT_FLOAT_EQ(sample.derivative.y, 2.0f);
    EXPECT_FLOAT_EQ(sample.derivative.z, 3.0f);
}

TEST(DynworldNoiseTest, NoiseSampleProductUsesOriginalValueForDerivative)
{
    NoiseSample sample;
    sample.value = 3.0f;
    sample.derivative.set(1.0f, 2.0f, 3.0f);

    NoiseSample other;
    other.value = 4.0f;
    other.derivative.set(10.0f, 20.0f, 30.0f);

    sample *= other;

    EXPECT_FLOAT_EQ(sample.value, 12.0f);
    EXPECT_FLOAT_EQ(sample.derivative.x, 34.0f);
    EXPECT_FLOAT_EQ(sample.derivative.y, 68.0f);
    EXPECT_FLOAT_EQ(sample.derivative.z, 102.0f);
}
