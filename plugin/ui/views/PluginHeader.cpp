#include "core/ExternalLinks.h"
#include "PluginHeader.h"

#include "core/Brand.h"
#include "core/CustomIcons.h"
#include "core/Design.h"
#include "core/Help.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/Clickable.h"

namespace t3k::ui {

namespace {
constexpr int kPadX = 24;
constexpr int kGroupGap = 40;  // between header items
constexpr int kPairGap = 16;   // tight pairs (undo/redo)
constexpr int kLogoWidth = 160;
constexpr int kLogoHeight = 24;  // 160 * 32 / 210, rounded like the browser
constexpr int kDevicePadX = 12;
constexpr int kDeviceGap = 8;
constexpr int kDevicePairGap = 4;
constexpr int kDeviceLogoWidth = 128;
}  // namespace

// The wordmark links to tone3000.com.
class PluginHeader::LogoLink : public Clickable {
public:
  LogoLink() : Clickable({}) {
    setTitle("TONE3000");  // tone3000.com, to a screen reader
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    onClick = [] { openExternalLink("https://www.tone3000.com"); };
  }
  void paintButton(juce::Graphics& g, bool, bool) override {
    Brand::drawLogo(g, getLocalBounds().toFloat());
  }
};

PluginHeader::PluginHeader(Services& services)
    : services_(services),
      logo_(std::make_unique<LogoLink>()),
      presetBar_(services),
      tuner_(custom_icons::kTuningFork, 28, 18) {
  addAndMakeVisible(*logo_);
  artemisExit_.setHelpText("Back to Launchpad");
  artemisExit_.setTitle("Back to Launchpad");
  artemisExit_.onClick = [] {
    juce::MessageManager::callAsync([] {
      if (auto* app = juce::JUCEApplicationBase::getInstance()) app->systemRequestedQuit();
    });
  };
  addChildComponent(artemisExit_);
  addAndMakeVisible(presetBar_);

  stereo_.onToggle = [this](bool stereo) {
    if (onStereoToggle) onStereoToggle(stereo);
  };
  addAndMakeVisible(stereo_);

  tuner_.setHelpText(help::text(help::Key::tuner));
  tuner_.setFillWhenActive(true);
  tuner_.onClick = [this] {
    if (onToggleTuner) onToggleTuner(!tunerShown_);
  };
  addAndMakeVisible(tuner_);

  undo_.setHelpText(help::text(help::Key::undo));
  undo_.onClick = [this] {
    if (onUndo) onUndo();
  };
  redo_.setHelpText(help::text(help::Key::redo));
  redo_.onClick = [this] {
    if (onRedo) onRedo();
  };
  addAndMakeVisible(undo_);
  addAndMakeVisible(redo_);

  account_.onOpenSettings = [this] {
    if (onOpenSettings) onOpenSettings();
  };
  account_.onLogin = [this] {
    if (onLogin) onLogin();
  };
  account_.onLogout = [this] {
    if (onLogout) onLogout();
  };
  addAndMakeVisible(account_);

  services_.chain.addListener(this);
  services_.session.addListener(this);
  sessionChanged();
  chainChanged(services_.chain.state());
  setTunerShown(false);
  setSize(design::kWidth, kHeight);
}

PluginHeader::~PluginHeader() {
  services_.session.removeListener(this);
  services_.chain.removeListener(this);
}

void PluginHeader::sessionChanged() {
  const auto& session = services_.session;
  account_.setAuthenticated(session.authenticated());
  const auto user = session.user();
  const auto url = user ? user->avatarUrl : juce::String();
  if (url == avatarUrl_ && (url.isNotEmpty() || !session.authenticated())) return;
  avatarUrl_ = url;
  account_.setAvatar(services_.images, url);
}

void PluginHeader::setTunerShown(bool shown) {
  tunerShown_ = shown;
  // Lit + HIGHLIGHT fill while the tuner is up; the icon itself stays white.
  tuner_.setActive(true);
  tuner_.setFillWhenActive(shown);
}

void PluginHeader::setDeviceViewport(bool enabled) {
  if (deviceViewport_ == enabled) return;
  deviceViewport_ = enabled;
  artemisExit_.setVisible(enabled);
  resized();
}

void PluginHeader::chainChanged(const ChainState& state) {
  undo_.setEnabled(state.canUndo);
  redo_.setEnabled(state.canRedo);
  stereo_.setStereoEnabled(state.stereoEnabled);
}

void PluginHeader::paint(juce::Graphics& g) {
  g.fillAll(theme::kBlack);
  paint::hairlineH(g, 0, static_cast<float>(getWidth()), static_cast<float>(getHeight() - 1),
                   theme::kBorder);
}

void PluginHeader::resized() {
  // The 1px bottom border is inside the header; items centre in the remaining
  // content row like the flex container did.
  const int padX = deviceViewport_ ? kDevicePadX : kPadX;
  const int groupGap = deviceViewport_ ? kDeviceGap : kGroupGap;
  const int pairGap = deviceViewport_ ? kDevicePairGap : kPairGap;
  auto area = getLocalBounds().withTrimmedBottom(1).reduced(padX, 0);
  // 31.5 in the browser; rounding up puts even-height boxes where the
  // browser's anti-aliased edges land.
  const int cy = (area.getHeight() + 1) / 2;
  auto place = [&](juce::Component& c, int right) {
    c.setBounds(right - c.getWidth(), cy - c.getHeight() / 2, c.getWidth(), c.getHeight());
    return c.getX();
  };

  // Right group, laid out from the right edge: account · undo/redo · tuner ·
  // stereo. Artemis keeps the touch targets and tightens only the gaps.
  int x = place(account_, area.getRight()) - groupGap;
  x = place(redo_, x) - pairGap;
  x = place(undo_, x) - groupGap;
  x = place(tuner_, x) - groupGap;
  x = place(stereo_, x) - groupGap;

  int logoX = area.getX();
  if (deviceViewport_) {
    artemisExit_.setTopLeftPosition(logoX, cy - artemisExit_.getHeight() / 2);
    logoX += artemisExit_.getWidth() + kDeviceGap;
    // Reserve the full preset selector and every action before allocating
    // the wordmark. Presets sit beside the brand, with any spare width
    // between them and the right action group, so their hit areas cannot
    // drift beneath the logo on a scaled display.
    const int availableLogoWidth = x - presetBar_.getWidth() - groupGap - logoX;
    const int logoWidth = juce::jlimit(1, kDeviceLogoWidth, availableLogoWidth);
    const int logoHeight = juce::roundToInt(static_cast<float>(kLogoHeight) * logoWidth / kLogoWidth);
    logo_->setBounds(logoX, cy - logoHeight / 2, logoWidth, logoHeight);
    presetBar_.setTopLeftPosition(logo_->getRight() + groupGap, cy - presetBar_.getHeight() / 2);
  } else {
    logo_->setBounds(logoX, cy - kLogoHeight / 2, kLogoWidth, kLogoHeight);
    place(presetBar_, x);
  }
}

}  // namespace t3k::ui
