#include "chain_test_helpers.h"
#include "NamEngine.h"
#include "NAM/get_dsp.h"

#include <gtest/gtest.h>

TEST(NamSizePolicyTest, RestoredFullBlockAndSetterRespectDevicePolicy) {
  ChainTestProcessor proc;
#if T3K_ARTEMIS_KIOSK
  proc.setNamSlimSizeDefault(1.0);
  EXPECT_DOUBLE_EQ(proc.getNamSlimSizeDefault(), 0.0);
#endif
  proc.setPlayConfigDetails(2, 2, 48000.0, 64);
  proc.prepareToPlay(48000.0, 64);
  juce::ValueTree snapshot("ChainSnapshot");
  juce::ValueTree lane("ChainBlocks");
  auto block = makeNamBlockTree("full-from-desktop", 1, 100);
  block.setProperty("slimSize", 1.0, nullptr);
  lane.appendChild(block, nullptr);
  snapshot.appendChild(lane, nullptr);
  proc.restoreFromTree(snapshot);
  ASSERT_TRUE(waitForChainLoaded(proc));
#if T3K_ARTEMIS_KIOSK
  constexpr double expected = 0.0;
#else
  constexpr double expected = 1.0;
#endif
  auto size = [&] {
    auto state = proc.getChainState(-1);
    return static_cast<double>(state["chain"][0]["params"]["slimSize"]);
  };
  EXPECT_DOUBLE_EQ(size(), expected);
  EXPECT_TRUE(proc.setBlockSlimSize("full-from-desktop", 1.0));
  EXPECT_DOUBLE_EQ(size(), expected);
  // A subsequent save/reopen must retain the supported tier.
  juce::MemoryBlock saved;
  proc.getStateInformation(saved);
  proc.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  ASSERT_TRUE(waitForChainLoaded(proc));
  EXPECT_DOUBLE_EQ(size(), expected);
}

TEST(NamSizePolicyTest, EngineCannotSelectFullOnArtemis) {
  juce::MemoryBlock bytes;
  ASSERT_TRUE(testFile("a2-amp-test.nam").loadFileAsData(bytes));
  const auto* begin = static_cast<const char*>(bytes.getData());
  const auto config = nlohmann::json::parse(begin, begin + bytes.getSize());
  std::vector<std::unique_ptr<nam::DSP>> models;
  models.push_back(nam::get_dsp(config));
  NamEngine engine(std::move(models), 1, 1);
  engine.setSlimmableSize(1.0);
  engine.prepare(64);
#if T3K_ARTEMIS_KIOSK
  EXPECT_DOUBLE_EQ(engine.getSlimmableSize(), 0.0);
#else
  EXPECT_DOUBLE_EQ(engine.getSlimmableSize(), 1.0);
#endif
}
