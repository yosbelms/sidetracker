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
TEST(SampleEditorDsp, NormalizationAnalyzesNonSilentInputOnEveryPass)
{
    parameters(sampleEditorNormalizer, {50});
    const std::vector<int16_t> input{100, -200, 100};
    const auto first = run(sampleEditorNormalizer, input);
    ASSERT_EQ(first.size(), 3);
    EXPECT_GT(first[0], 8000);
    EXPECT_LT(first[1], -16000);
    EXPECT_EQ(run(sampleEditorNormalizer, input), first);
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

#include "SampleFxRealtime.h"
#include "mtSampleEditorEngine.h"

namespace
{
struct NoLivePlugins : samplefx::RealtimePluginLoader
{
    PtfxHostApi api = {};
    bool load(const samplefx::Node&, samplefx::RealtimePlugin&, samplefx::RealtimeArena&) override { return false; }
    const PtfxHostApi* hostApi() override { return &api; }
};
samplefx::Node liveNode(uint8_t id, std::initializer_list<float> values = {})
{
    samplefx::Node node; node.effect = id; node.count = values.size();
    std::copy(values.begin(), values.end(), node.params);
    return node;
}
std::vector<int16_t> liveOutput(samplefx::Recipe recipe, std::vector<int16_t>& input)
{
    samplefx::RealtimeChain chain;
    NoLivePlugins loader;
    std::vector<uint8_t> arena(2 * 1024 * 1024);
    samplefx::Audio source{{input.data(), input.data()}, uint32_t(input.size()), 1};
    EXPECT_TRUE(chain.prepare(recipe, source, arena.data(), arena.size(), loader, 1000000)) << chain.error();
    std::vector<int16_t> result;
    int16_t left[128], right[128];
    while (!chain.finished())
    {
        unsigned count = chain.process(left, right, 128);
        EXPECT_FALSE(chain.hasFailed()) << chain.error();
        if (!count) break;
        result.insert(result.end(), left, left + count);
        EXPECT_TRUE(std::equal(left, left + count, right));
    }
    return result;
}
}

TEST(SampleFxRealtime, MatchesOfflineDspIncludingPartialBlocks)
{
    std::vector<int16_t> source(8192 + 131);
    for (unsigned i = 0; i < source.size(); ++i) source[i] = int(i * 193 % 24000) - 12000;
    struct Case { mtEffect* fx; samplefx::Node node; } cases[] = {
        {&sampleEditorAmplifier, liveNode(editorEffectAmplifier, {1.5f})},
        {&sampleEditorOverdrive, liveNode(editorEffectOverdrive, {0.7f, 0})},
        {&sampleEditorBitcrusher, liveNode(editorEffectBitcrusher, {9, 7000})},
        {&sampleEditorChorus, liveNode(editorEffectChorus, {256, 3})},
        {&sampleEditorFlanger, liveNode(editorEffectFlanger, {256, 50, 4, 1})},
        {&sampleEditorEQ, liveNode(editorEffectEQ, {2, 80, 1, -3, 800, 1, 1, 12000, 1})},
        {&sampleEditorCompresor, liveNode(editorEffectCompresor, {-12, 4, 10, 50})},
        {&sampleEditorLimiter, liveNode(editorEffectLimiter, {-6, 3, 300})},
        {&sampleEditorFadeIn, liveNode(editorEffectFadeIn)},
        {&sampleEditorFadeOut, liveNode(editorEffectFadeOut)},
        {&sampleEditorDelay, liveNode(editorEffectDelay, {0.5f, 1})},
    };
    for (auto& entry : cases)
    {
        SCOPED_TRACE(unsigned(entry.node.effect));
        for (unsigned p = 0; p < entry.node.count; ++p)
        { float v = entry.node.params[p]; entry.fx->setParameter(v, p); }
        auto expected = run(*entry.fx, source);
        samplefx::Recipe recipe; ASSERT_TRUE(recipe.add(entry.node));
        EXPECT_EQ(liveOutput(recipe, source), expected);
    }
}

TEST(SampleFxRealtime, RepeatedStatefulStagesAreIndependent)
{
    std::vector<int16_t> source(1027);
    for (unsigned i = 0; i < source.size(); ++i) source[i] = int(i * 119 % 18000) - 9000;
    parameters(sampleEditorChorus, {256, 3});
    auto expected = run(sampleEditorChorus, source);
    parameters(sampleEditorChorus, {512, 5});
    expected = run(sampleEditorChorus, expected);
    samplefx::Recipe recipe;
    recipe.add(liveNode(editorEffectChorus, {256, 3}));
    recipe.add(liveNode(editorEffectChorus, {512, 5}));
    EXPECT_EQ(liveOutput(recipe, source), expected);
}

TEST(SampleFxRealtime, ParameterEditChangesNextBlockWithoutRestartingSource)
{
    std::vector<int16_t> input(1024);
    for (unsigned i = 0; i < input.size(); ++i) input[i] = 1000 + i;
    std::vector<uint8_t> arena(8192);
    NoLivePlugins loader; samplefx::RealtimeChain chain; samplefx::Recipe recipe;
    recipe.add(liveNode(editorEffectAmplifier, {1}));
    recipe.add(liveNode(editorEffectAmplifier, {2}));
    samplefx::Audio source{{input.data(), input.data()}, uint32_t(input.size()), 1};
    ASSERT_TRUE(chain.prepare(recipe, source, arena.data(), arena.size(), loader, input.size()));
    int16_t left[128], right[128];
    ASSERT_EQ(chain.process(left, right, 128), 128);
    EXPECT_EQ(left[0], 2000);
    recipe.stages[1].params[0] = 3;
    chain.setParameters(recipe);
    ASSERT_EQ(chain.process(left, right, 128), 128);
    EXPECT_EQ(left[0], input[128] * 3);
    EXPECT_EQ(chain.played(), 256);
    EXPECT_EQ(recipe.stages[0].params[0], 1);
    EXPECT_EQ(input[128], 1128);
}

TEST(SampleFxRealtime, DelayTailsFlowIntoFollowingStagesAndBypass)
{
    std::vector<int16_t> source(131, 0); source[0] = 10000;
    parameters(sampleEditorDelay, {0.5f, 1});
    auto expected = run(sampleEditorDelay, source);
    parameters(sampleEditorFadeOut, {});
    expected = run(sampleEditorFadeOut, expected);
    samplefx::Recipe recipe;
    recipe.add(liveNode(editorEffectDelay, {0.5f, 1}));
    auto bypass = liveNode(editorEffectAmplifier, {5}); bypass.bypass = true;
    recipe.add(bypass);
    recipe.add(liveNode(editorEffectFadeOut));
    EXPECT_EQ(liveOutput(recipe, source), expected);
}

TEST(SampleFxRealtime, AnalysisAndMemoryLimitsFailBeforePlayback)
{
    std::vector<int16_t> input(1000, 2000);
    uint8_t arena[128]; NoLivePlugins loader; samplefx::RealtimeChain chain;
    samplefx::Audio source{{input.data(), input.data()}, uint32_t(input.size()), 1};
    samplefx::Recipe recipe; recipe.add(liveNode(editorEffectNormalizer, {100}));
    EXPECT_FALSE(chain.prepare(recipe, source, arena, sizeof(arena), loader, 10000));
    EXPECT_NE(std::string(chain.error()).find("analysis"), std::string::npos);
    recipe.stages[0] = liveNode(editorEffectChorus, {256, 3});
    EXPECT_FALSE(chain.prepare(recipe, source, arena, sizeof(arena), loader, 10000));
    EXPECT_EQ(input[0], 2000);
}

TEST(SampleFxRealtime, StereoChannelsKeepIndependentHistory)
{
    if (mtEffect::kChannels < 2) GTEST_SKIP() << "Stereo-only platform";
    std::vector<int16_t> left(300, 0), right(300, 0); left[0] = 12000;
    std::vector<uint8_t> arena(32000);
    NoLivePlugins loader; samplefx::RealtimeChain chain; samplefx::Recipe recipe;
    recipe.add(liveNode(editorEffectChorus, {256, 3}));
    samplefx::Audio source{{left.data(), right.data()}, uint32_t(left.size()), 2};
    ASSERT_TRUE(chain.prepare(recipe, source, arena.data(), arena.size(), loader, 1000));
    int16_t outLeft[128], outRight[128]; bool heardLeft = false;
    while (!chain.finished())
    {
        unsigned count = chain.process(outLeft, outRight, 128);
        ASSERT_GT(count, 0);
        for (unsigned i = 0; i < count; ++i) { heardLeft |= outLeft[i] != 0; EXPECT_EQ(outRight[i], 0); }
    }
    EXPECT_TRUE(heardLeft);
}

namespace
{
// Two native function banks stand in for separate relocated PTFX code/BSS
// images. State itself is allocated through the production live arena.
struct NativeLiveState { float gain; unsigned position; const PtfxHostApi* host; };
template<unsigned Bank> struct NativeLivePlugin
{
    static inline NativeLiveState* state;
    static void init(void* scratch, uint32_t, const PtfxHostApi* host)
    { state = new (scratch) NativeLiveState{1, 0, host}; }
    static void parameter(uint8_t index, float value)
    { if (!index) state->gain = value; }
    static int32_t process(const int16_t** in, int16_t** out, uint32_t frames, uint8_t channels)
    {
        for (unsigned i = 0; i < frames; ++i)
            for (unsigned c = 0; c < channels; ++c)
                out[c][i] = in[c] ? in[c][i] * state->gain : (state->position + i) * state->gain;
        state->position += frames;
        return frames;
    }
};
struct NativeLiveLoader : NoLivePlugins
{
    unsigned loaded = 0;
    bool load(const samplefx::Node& node, samplefx::RealtimePlugin& plugin, samplefx::RealtimeArena& arena) override
    {
        plugin.header.flags = node.flags; plugin.header.num_params = node.count;
        plugin.header.scratch_size = sizeof(NativeLiveState);
        plugin.header.params[0].min_val = 0; plugin.header.params[0].max_val = 5;
        plugin.scratch = arena.allocate(plugin.header.scratch_size);
        if (loaded++ == 0)
        { plugin.init = NativeLivePlugin<0>::init; plugin.parameter = NativeLivePlugin<0>::parameter; plugin.process = NativeLivePlugin<0>::process; }
        else
        { plugin.init = NativeLivePlugin<1>::init; plugin.parameter = NativeLivePlugin<1>::parameter; plugin.process = NativeLivePlugin<1>::process; }
        return plugin.scratch != nullptr;
    }
};
}

TEST(SampleFxRealtime, GeneratorAndProcessingPluginHaveIndependentState)
{
    NativeLiveLoader loader; samplefx::RealtimeChain chain; samplefx::Recipe recipe;
    recipe.source = liveNode(samplefx::kPlugin, {2}); recipe.source.flags = PTFX_FLAG_GENERATOR;
    recipe.generatorFrames = 301;
    recipe.add(liveNode(samplefx::kPlugin, {3}));
    uint8_t arena[1024]; samplefx::Audio empty;
    ASSERT_TRUE(chain.prepare(recipe, empty, arena, sizeof(arena), loader, 1000)) << chain.error();
    EXPECT_NE(NativeLivePlugin<0>::state, NativeLivePlugin<1>::state);
    int16_t left[128], right[128];
    ASSERT_EQ(chain.process(left, right, 128), 128);
    EXPECT_EQ(left[100], 600);
    recipe.stages[0].params[0] = 4; chain.setParameters(recipe);
    ASSERT_EQ(chain.process(left, right, 128), 128);
    EXPECT_EQ(left[0], 128 * 8);
    EXPECT_EQ(NativeLivePlugin<0>::state->gain, 2);
    ASSERT_EQ(chain.process(left, right, 128), 45);
    EXPECT_EQ(chain.played(), 301);
    EXPECT_TRUE(chain.finished());
}

namespace
{
void prependInit(void* scratch, uint32_t bytes, const PtfxHostApi* host)
{
    NativeLivePlugin<0>::init(scratch, bytes, host);
    host->set_extra_samples(4, 0);
}
int32_t invalidLiveProcess(const int16_t**, int16_t**, uint32_t frames, uint8_t) { return frames + 1; }
struct SpecialLiveLoader : NativeLiveLoader
{
    bool prepend = false;
    bool load(const samplefx::Node& node, samplefx::RealtimePlugin& plugin, samplefx::RealtimeArena& arena) override
    {
        if (!NativeLiveLoader::load(node, plugin, arena)) return false;
        if (prepend) plugin.init = prependInit;
        else plugin.process = invalidLiveProcess;
        return true;
    }
};
}
TEST(SampleFxRealtime, PrependedAudioUsesPreparedPreviewAndInvalidPluginOutputStops)
{
    samplefx::RealtimeChain chain; samplefx::Recipe recipe;
    recipe.source = liveNode(samplefx::kPlugin, {1}); recipe.source.flags = PTFX_FLAG_GENERATOR;
    uint8_t arena[1024]; samplefx::Audio empty;
    SpecialLiveLoader prepend; prepend.prepend = true;
    EXPECT_FALSE(chain.prepare(recipe, empty, arena, sizeof(arena), prepend, 100000));
    EXPECT_NE(std::string(chain.error()).find("inserts audio"), std::string::npos);
    SpecialLiveLoader invalid;
    ASSERT_TRUE(chain.prepare(recipe, empty, arena, sizeof(arena), invalid, 100000));
    int16_t left[128], right[128];
    EXPECT_EQ(chain.process(left, right, 128), 0);
    EXPECT_TRUE(chain.hasFailed());
    EXPECT_TRUE(chain.finished());
}

#include "SampleFxPreview.h"
#include "audio_test_support.h"
TEST(SampleFxRealtime, PreviewAudioStreamRoutesStereoAndStopsWithoutLeakingBlocks)
{
    static SampleFxPreview preview;
    static ce::test::BlockSink leftSink, rightSink;
    AudioConnection left(&preview, 0, &leftSink, 0), right(&preview, 1, &rightSink, 0);
    samplefx::Recipe recipe; recipe.add(liveNode(editorEffectAmplifier, {2}));
    std::vector<int16_t> input(131, 3000); std::vector<uint8_t> arena(8192);
    samplefx::Audio source{{input.data(), input.data()}, uint32_t(input.size()), 1};
    NoLivePlugins loader; samplefx::RealtimeChain chain;
    ASSERT_TRUE(chain.prepare(recipe, source, arena.data(), arena.size(), loader, 1000));
    const auto blocksBefore = AudioStream::memory_used;
    preview.start(&chain);
    preview.update(); leftSink.update(); rightSink.update();
    EXPECT_TRUE(leftSink.received); EXPECT_TRUE(rightSink.received);
    EXPECT_EQ(leftSink.samples[0], 6000); EXPECT_EQ(rightSink.samples[0], 6000);
    EXPECT_TRUE(preview.isPlaying());
    preview.update(); leftSink.update(); rightSink.update();
    EXPECT_FALSE(preview.isPlaying());
    EXPECT_EQ(leftSink.samples[2], 6000); EXPECT_EQ(leftSink.samples[3], 0);
    EXPECT_EQ(AudioStream::memory_used, blocksBefore);
    ASSERT_TRUE(chain.prepare(recipe, source, arena.data(), arena.size(), loader, 1000));
    preview.start(&chain); preview.stop(); preview.update(); leftSink.update();
    EXPECT_FALSE(leftSink.received);
}
