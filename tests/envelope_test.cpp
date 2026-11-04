#include "fake_devices.h"
#include "mtEnvelopeGenerator.h"
#include "mtWavetableEnvelopes.h"
#include <gtest/gtest.h>
#include <memory>

using namespace ce::test;

class EnvelopeTest : public testing::Test
{
  protected:
    void SetUp() override
    {
        testClock().timeUs = 0;
        env.reset(new envelopeGenerator);
        params = envelopeGenerator::s_defaultEnvelope;
        params.enable = 1;
        env->begin(&testClock());
        env->setIsRandom(false);
        env->init(&params);
    }
    float advance(uint32_t us) { testClock().advanceUs(us); return env->getOut(); }
    envelopeGenerator::strEnv params{};
    std::unique_ptr<envelopeGenerator> env;
};

TEST_F(EnvelopeTest, DisabledEnvelopeOutputsZero)
{
    params.enable = 0;
    env->start();
    EXPECT_FLOAT_EQ(advance(100000), 0.0f);
}

TEST_F(EnvelopeTest, DelayHoldsTheEnvelopeAtZero)
{
    params.delay = 10;
    params.attack = 10;
    env->start();
    EXPECT_FLOAT_EQ(advance(9000), 0.0f);
    EXPECT_FLOAT_EQ(advance(1000), 0.0f);
    EXPECT_NEAR(advance(5000), 0.5f, 0.01f);
}

TEST_F(EnvelopeTest, AttackRisesToTheConfiguredPeak)
{
    params.attack = 10;
    env->start();
    EXPECT_FLOAT_EQ(env->getOut(), 0.0f);
    EXPECT_NEAR(advance(5000), 0.5f, 0.01f);
    EXPECT_NEAR(advance(5000), 1.0f, 0.01f);
}

TEST_F(EnvelopeTest, ReleaseDecaysAndThenFinishes)
{
    params.release = 10;
    env->start();
    advance(1);
    advance(1);  // Finish zero-length hold and decay.
    EXPECT_NEAR(env->getOut(), 1.0f, 0.01f);
    env->stop();
    env->getOut();
    EXPECT_NEAR(advance(5000), 0.5f, 0.01f);
    EXPECT_NEAR(advance(6000), 0.0f, 0.01f);
    EXPECT_FALSE(env->isKeyPressed());
}

TEST_F(EnvelopeTest, KillToZeroImmediatelyClearsTheOutput)
{
    env->start();
    advance(1);
    advance(1);
    env->killToZero();
    EXPECT_FLOAT_EQ(env->getLastOut(), 0.0f);
    EXPECT_FLOAT_EQ(advance(1000), 0.0f);
    EXPECT_FALSE(env->isKeyPressed());
}

TEST(WavetableEnvelopes, WarpCoordinatesRoundTripAcrossTheirSupportedRange)
{
    for (int x = -7; x <= 7; ++x)
        for (int y = -7; y <= 7; ++y)
        {
            const uint8_t packed = packWarpXY(x, y);
            EXPECT_EQ(unpackWarpX(packed), x);
            EXPECT_EQ(unpackWarpY(packed), y);
        }
}

TEST(WavetableEnvelopes, FactoryShapesStayInTheirUnipolarRange)
{
    for (int shape = 0; shape < kWtEnvFactoryCount; ++shape)
        for (int position = 0; position < kWtEnvLength; ++position)
        {
            const float value = evalWavetableEnvelopeFloat(float(position) / 127, shape, 0);
            ASSERT_GE(value, 0.0f);
            ASSERT_LE(value, 1.0f);
        }
}

TEST(WavetableEnvelopes, BellShapesFinishAtZero)
{
    for (int shape = 16; shape < 24; ++shape)
        EXPECT_EQ(g_wtEnvelopeBank[shape][kWtEnvLength - 1], 0) << shape;
}
