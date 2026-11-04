#include "mtEffectAmplifier.h"
#include "mtEffectNormalizer.h"
#include "mtEffectDelay.h"
#include "mtEffectEQ.h"
#include "mtEffectChorus.h"
#include "mtEffectFlanger.h"
#include "mtEffectBitcrusher.h"
#include "mtEffectFadeIn.h"
#include "mtEffectFadeOut.h"
#include "mtEffectCompresor.h"
#include "mtEffectLimiter.h"
#include "mtEffectOverdrive.h"
#include "mtEffectModular.h"
#include "ptfx_validation.h"
#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <algorithm>
#include <vector>

// The production global editor effects bind explicit test buffers below. Their
// constructors still need the firmware's ordinary editor-memory base.
static int16_t editorPool[EDIT_BUFF_SIZE * mtEffect::kChannels];
int16_t* externalRamEffectsBuff = editorPool;
// Host implementation of the platform-provided Q15 sine primitive.
int16_t sin_q15(int16_t value)
{
    return std::clamp(std::lround(32768 * std::sin(value * (6.283185307179586 / 32768))), -32768l, 32767l);
}

namespace native_kick
{
using PluginParam = PtfxParamDef;
#define SATURATE_16BIT(value) std::clamp<int32_t>((value), -32768, 32767)
#include "../editor-fx-sdk/effects/analog_kick_gen.c"
#undef SATURATE_16BIT
}

namespace
{
void parameters(mtEffect& fx, std::initializer_list<float> values)
{
    unsigned i = 0;
    for (float value : values) fx.setParameter(value, i++);
}
std::vector<int16_t> run(mtEffect& fx, std::vector<int16_t> input)
{
    constexpr int16_t sentinel = 17381;
    std::vector<int16_t> result(input.size() + 20000, sentinel);
    int16_t* in[2] = {input.data(), input.data()};
    int16_t* out[2] = {result.data(), result.data()};
    fx.bindOffline(in, out, input.size());
    const unsigned expected = fx.getExpectedProcessLength();
    EXPECT_GT(expected, 0);
    EXPECT_LT(expected, result.size());
    if (!expected || expected >= result.size()) return {};
    EXPECT_TRUE(fx.startProcess());
    unsigned completed = 0;
    for (unsigned blocks = 0; fx.getProcessState() && blocks < 10000; ++blocks)
    {
        int32_t count = fx.processBlock();
        EXPECT_GE(count, 0);
        if (count < 0) break;
        completed += count;
    }
    EXPECT_FALSE(fx.getProcessState());
    EXPECT_EQ(completed, expected);
    EXPECT_EQ(result[expected], sentinel);
    result.resize(expected);
    return result;
}
}

TEST(SampleEditorDsp, GainAndSilentNormalizationAreSafe)
{
    parameters(sampleEditorAmplifier, {2});
    EXPECT_EQ(run(sampleEditorAmplifier, {100, -200, 20000}), (std::vector<int16_t>{200, -400, 32767}));
    parameters(sampleEditorNormalizer, {100});
    EXPECT_EQ(run(sampleEditorNormalizer, std::vector<int16_t>(8192)), std::vector<int16_t>(8192));
}
TEST(SampleEditorDsp, DelayReportsAndWritesItsExactTail)
{
    parameters(sampleEditorDelay, {0.5f, 1});
    std::vector<int16_t> input(129); input[0] = 10000;
    auto result = run(sampleEditorDelay, input);
    ASSERT_GT(result.size(), input.size());
    EXPECT_EQ(result[0], 10000);
    EXPECT_EQ(result[44], 5000);
    parameters(sampleEditorDelay, {0.01f, 1});
    auto shortTail = run(sampleEditorDelay, input);
    EXPECT_GE(shortTail.size(), input.size());
}
TEST(SampleEditorDsp, StatefulEffectsResetBetweenChainInstances)
{
    std::vector<int16_t> input(8192 + 129);
    for (unsigned i = 0; i < input.size(); ++i) input[i] = (i * 193) % 24000 - 12000;
    parameters(sampleEditorChorus, {256, 3});
    EXPECT_EQ(run(sampleEditorChorus, input), run(sampleEditorChorus, input));
    parameters(sampleEditorFlanger, {256, 50, 10, 1});
    EXPECT_EQ(run(sampleEditorFlanger, input), run(sampleEditorFlanger, input));
    parameters(sampleEditorFlanger, {128, 100, 8, 5});
    EXPECT_EQ(run(sampleEditorFlanger, input), run(sampleEditorFlanger, input));
    parameters(sampleEditorEQ, {0, 80, 1, 0, 800, 1, 0, 12000, 1});
    EXPECT_EQ(run(sampleEditorEQ, input), run(sampleEditorEQ, input));
    parameters(sampleEditorCompresor, {-12, 4, 10, 50});
    EXPECT_EQ(run(sampleEditorCompresor, input), run(sampleEditorCompresor, input));
    parameters(sampleEditorLimiter, {-6, 3, 300});
    EXPECT_EQ(run(sampleEditorLimiter, input), run(sampleEditorLimiter, input));
}
TEST(SampleEditorDsp, PartialBlocksAndExactBlockBoundariesFinishWithinBounds)
{
    parameters(sampleEditorBitcrusher, {4, 44100});
    parameters(sampleEditorOverdrive, {1, 0});
    for (unsigned length : {1u, 127u, 128u, 8192u, 8193u, 16384u})
    {
        std::vector<int16_t> input(length, 2500);
        EXPECT_EQ(run(sampleEditorBitcrusher, input).size(), length);
        EXPECT_EQ(run(sampleEditorOverdrive, input).size(), length);
        EXPECT_EQ(run(sampleEditorFadeIn, input).size(), length);
        EXPECT_EQ(run(sampleEditorFadeOut, input).size(), length);
        parameters(sampleEditorChorus, {128, 1});
        EXPECT_EQ(run(sampleEditorChorus, input), input);
        parameters(sampleEditorFlanger, {128, 0, 1, 1});
        EXPECT_EQ(run(sampleEditorFlanger, input), input);
    }
}

TEST(SampleEditorDsp, RealPtfxGeneratorProcessesEmptySourceThroughModularAdapter)
{
    PtfxHeader header = {};
    header.flags = PTFX_FLAG_GENERATOR;
    header.num_params = 4;
    for (unsigned i = 0; i < 4; ++i) header.params[i] = native_kick::PLUGIN_PARAMS[i];
    PtfxHostApi host = {};
    host.sample_rate = 44100;
    host.sinf = ::sinf; host.expf = ::expf;
    host.rand = [] { return 1000; };
    sampleEditorModular.configurePlugin(header, nullptr, 0, &host,
        native_kick::plugin_init, native_kick::plugin_set_param, native_kick::plugin_process_block);
    sampleEditorModular.setTargetLength(2048);
    auto first = run(sampleEditorModular, {});
    auto second = run(sampleEditorModular, {});
    EXPECT_EQ(first.size(), 2048);
    EXPECT_EQ(first, second); // Initialization resets oscillator and sample cursor.
    EXPECT_TRUE(std::any_of(first.begin(), first.end(), [](int16_t s) { return s != 0; }));
}

TEST(SampleEditorDsp, PtfxRejectsOversizedTailAndInvalidReturnedCounts)
{
    PtfxHeader header = {};
    PtfxHostApi host = {};
    auto init = [](void*, uint32_t, const PtfxHostApi*) { sampleEditorModular.setExtraSamples(0, EDIT_BUFF_SIZE); };
    sampleEditorModular.configurePlugin(header, nullptr, 0, &host, init, nullptr,
        [](const int16_t**, int16_t**, uint32_t n, uint8_t) -> int32_t { return n; });
    int16_t input[8] = {}, output[16] = {};
    int16_t* in[2] = {input, input}; int16_t* out[2] = {output, output};
    sampleEditorModular.bindOffline(in, out, 8);
    EXPECT_FALSE(sampleEditorModular.startProcess());
    sampleEditorModular.configurePlugin(header, nullptr, 0, &host, nullptr, nullptr,
        [](const int16_t**, int16_t**, uint32_t n, uint8_t) -> int32_t { return n + 1; });
    sampleEditorModular.bindOffline(in, out, 8);
    ASSERT_TRUE(sampleEditorModular.startProcess());
    sampleEditorModular.processBlock();
    EXPECT_TRUE(sampleEditorModular.hasProcessingError());
    EXPECT_FALSE(sampleEditorModular.getProcessState());
}

TEST(SampleEditorDsp, StereoChannelsHaveIndependentSamplesAndState)
{
    if (mtEffect::kChannels != 2) GTEST_SKIP() << "Stereo platform only";
    int16_t left[] = {100, -200, 300}, right[] = {-400, 500, -600};
    int16_t leftOut[3] = {}, rightOut[3] = {};
    int16_t* in[2] = {left, right}; int16_t* out[2] = {leftOut, rightOut};
    sampleEditorAmplifier.bindOffline(in, out, 3);
    parameters(sampleEditorAmplifier, {2});
    ASSERT_TRUE(sampleEditorAmplifier.startProcess());
    sampleEditorAmplifier.processBlock();
    EXPECT_EQ(leftOut[0], 200);
    EXPECT_EQ(rightOut[0], -800);
    EXPECT_EQ(leftOut[2], 600);
    EXPECT_EQ(rightOut[2], -1200);
}

TEST(PtfxValidation, RejectsBadLayoutsAndUnterminatedListStrings)
{
    PtfxHeader header = {};
    header.magic = PTFX_MAGIC; header.format_version = 1;
    header.code_offset = sizeof(header); header.code_size = 64;
    ASSERT_TRUE(ptfx::validateHeader(header, sizeof(header) + 64, 128, 128));
    header.num_params = 13;
    EXPECT_FALSE(ptfx::validateHeader(header, sizeof(header) + 64, 128, 128));
    header.num_params = 0; header.bss_size = 100;
    EXPECT_FALSE(ptfx::validateHeader(header, sizeof(header) + 64, 128, 128));
    header.bss_size = 0; header.process_block_offset = 64;
    EXPECT_FALSE(ptfx::validateHeader(header, sizeof(header) + 64, 128, 128));
    header.process_block_offset = 0; header.num_params = 1;
    auto& p = header.params[0]; p.display_type = PARAM_LIST;
    p.step = 1; p.max_val = 1; p.unit[2] = 2;
    ASSERT_TRUE(ptfx::validateHeader(header, sizeof(header) + 64, 128, 128));
    uint8_t code[64]; memset(code, 'a', sizeof(code));
    EXPECT_FALSE(ptfx::validateStrings(header, code));
    code[10] = 0; code[30] = 0;
    EXPECT_TRUE(ptfx::validateStrings(header, code));
}
