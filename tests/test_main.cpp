#include "audio_test_support.h"
#include "filter_variable.h"
#include "fake_devices.h"
#include "elapsedMillis.h"
#include "mtWavetableEnvelopes.h"
#include <gtest/gtest.h>

// Host execution is synchronous, as in the desktop platform.
void enableAudioInterrupt() {}
void disableAudioInterrupt() {}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    AudioStream::initialize_memory(ce::test::audioPool, std::size(ce::test::audioPool));
    AudioFilterStateVariable::initCutoffLookup();
    elapsedMillis::init(&ce::test::testClock());
    elapsedMicros::init(&ce::test::testClock());
    elapsedMillisAudio::init(&ce::test::testClock());
    elapsedMicrosAudio::init(&ce::test::testClock());
    initWavetableEnvelopes();
    return RUN_ALL_TESTS();
}
