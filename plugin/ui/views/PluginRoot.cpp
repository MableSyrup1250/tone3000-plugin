#include "PluginRoot.h"

#include "core/Design.h"
#include "core/Help.h"
#include "core/NoDefaultFocus.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
// Faceplate.tsx PLATE_HEIGHT; the toast floats 24px above it.
constexpr int kToastGap = 24;
}  // namespace

PluginRoot::PluginRoot(Services& services)
    : services_(services),
      header_(services),
      hintBar_(services),
      main_(services),
      faceplate_(services),
      toast_(services.toast),
      hintTracker_(services.hints, *this),
      pointerTracker_(services.pointer, *this) {
  setOpaque(true);
  setFocusContainerType(FocusContainerType::keyboardFocusContainer);

  header_.onToggleTuner = [this](bool show) { setTunerShown(show); };
  header_.onStereoToggle = [this](bool stereo) {
    closeTunerThen([&] { services_.chain.setStereoMode(stereo); });
  };
  header_.onUndo = [this] { closeTunerThen([&] { services_.chain.undo(); }); };
  header_.onRedo = [this] { closeTunerThen([&] { services_.chain.redo(); }); };
  header_.presetBar().beforeSave = [this] { closeTunerThen({}); };
  header_.presetBar().beforeLoad = [this] { showChainThen({}); };
  header_.presetBar().onReset = [this] { showChainThen([&] { services_.chain.resetToDefault(); }); };

  banner_.onAction = [this](BannerAction action) { handleBannerAction(action); };
  banner_.onDismiss = [this](const juce::String& id) { services_.banners.dismiss(id); };

  // Network-dependent entry points pass the connection gate first.
  // A plain login lands back on the chain. One sign-in at a time: the
  // header stays live over the sign-in screen, so a second press is not a
  // second browser tab.
  header_.onLogin = [this] {
    if (signInShown()) return;
    services_.connection.requireConnection([this] { services_.session.login(ToneSession::LoginIntent::plain); });
  };
  header_.onLogout = [this] { logout(); };

  // The add / swap browse flows: + and ⇄ open the tone browser through the
  // load flow, which remembers the target slot or block for the pick.
  main_.chainScreen().onSelectTone = [this](ChainSide side, const std::string& id) {
    services_.loadFlow.select(side, id);
  };
  services_.loadFlow.onShowBrowser = [this](bool show) { setBrowserShown(show); };
  // A browse-intent login (a sign-in CTA inside the browser) comes back to
  // the browser.
  services_.session.onAuthenticated = [this] { setBrowserShown(true); };

  header_.onOpenSettings = [this] { openSettings(); };
  main_.chainScreen().onDetailViewChanged = [this] { resetDrawerForPage(); };
  drawerHandle_.onExpansionRequested = [this](bool expanded) {
    if (!inspectionPage()) mainDrawerExpanded_ = expanded;
    setDrawerExpanded(expanded, true);
  };

  addAndMakeVisible(header_);
  addAndMakeVisible(main_);
  addAndMakeVisible(faceplate_);
  addChildComponent(hintBar_);
  addChildComponent(banner_);
  addChildComponent(drawerHandle_);

  // Popovers, toast and modals sit above everything; the layer itself is
  // click-through.
  overlay_.setInterceptsMouseClicks(false, true);
  addAndMakeVisible(overlay_);
  overlay_.addChildComponent(toast_);

  addMouseListener(&focusPolicy_, true);
  services_.hints.addListener(this);
  services_.banners.addListener(this);
  services_.connection.addListener(this);
  services_.updates.addListener(this);
  services_.session.addListener(this);
  hintsVisible_ = services_.hints.enabled();
  updateChromeHeight();
  bannerChanged();
  connectionProblemChanged();
  updateNoticeChanged();
  authFlowChanged();
}

PluginRoot::~PluginRoot() {
  removeMouseListener(&focusPolicy_);
  if (keyWindow_ != nullptr) keyWindow_->removeKeyListener(&focusPolicy_);
  services_.session.onAuthenticated = nullptr;
  services_.loadFlow.onShowBrowser = nullptr;
  if (watchedParent_ != nullptr) watchedParent_->removeComponentListener(this);
  services_.session.removeListener(this);
  services_.updates.removeListener(this);
  services_.connection.removeListener(this);
  services_.banners.removeListener(this);
  services_.hints.removeListener(this);
}

// Modals
juce::Image PluginRoot::snapshotBeneathOverlay(float scale) {
  juce::Image image(juce::Image::ARGB, juce::jmax(1, juce::roundToInt(getWidth() * scale)),
                    juce::jmax(1, juce::roundToInt(getHeight() * scale)), true);
  juce::Graphics g(image);
  g.addTransform(juce::AffineTransform::scale(scale));
  g.fillAll(theme::kBlack);
  for (auto* child : getChildren()) {
    if (child == &overlay_ || !child->isVisible()) continue;
    g.saveState();
    g.setOrigin(child->getPosition());
    child->paintEntireComponent(g, true);
    g.restoreState();
  }
  return image;
}

template <typename Modal, typename... Args>
std::unique_ptr<Modal> PluginRoot::openModal(Args&&... args) {
  auto modal = std::make_unique<Modal>([this](float scale) { return snapshotBeneathOverlay(scale); },
                                       std::forward<Args>(args)...);
  modal->setBounds(getLocalBounds());
  overlay_.addAndMakeVisible(*modal);
  return modal;
}

void PluginRoot::restackModals() {
  if (updateNotice_) updateNotice_->toFront(false);
  if (connectionModal_) connectionModal_->toFront(false);
}

// The sign-in screen is up exactly while the flow is not idle; the screen
// itself follows the phase moves. Posted: the change arrives from inside
// the button handler that started it.
void PluginRoot::authFlowChanged() {
  juce::MessageManager::callAsync([self = juce::Component::SafePointer(this)] {
    if (self == nullptr) return;
    const bool inFlight = self->services_.session.authFlow().phase != ToneSession::AuthFlow::Phase::idle;
    if (inFlight == self->signInShown()) return;
    if (inFlight) {
      self->signIn_ = std::make_unique<SignInScreen>(self->services_);
      // Right above the browser when there is one (it is where a browse
      // sign-in returns to), else above the faceplate; under a tuner,
      // Settings and the overlay.
      juce::Component* below = self->browser_ ? static_cast<juce::Component*>(self->browser_.get())
                                              : static_cast<juce::Component*>(&self->faceplate_);
      self->addChildComponent(*self->signIn_, self->getIndexOfChildComponent(below) + 1);
      self->services_.hints.setHover({});
    } else {
      self->signIn_.reset();
    }
    self->syncTakeovers();
    self->resetDrawerForPage();
    self->resized();
  });
}

// Store changes arrive from inside a modal's own button handler, so the
// modal is rebuilt on the next message-loop turn rather than under its feet.
void PluginRoot::connectionProblemChanged() {
  juce::MessageManager::callAsync([self = juce::Component::SafePointer(this)] {
    if (self == nullptr) return;
    const auto& problem = self->services_.connection.problem();
    self->connectionModal_.reset();
    if (!problem) return;
    auto& s = self->services_;
    auto modal = self->openModal<ConnectionModal>(*problem, s.backend.canOpenDateTimeSettings());
    modal->onRetry = [&s] { s.connection.retry(); };
    modal->onDismiss = [&s] { s.connection.dismiss(); };
    modal->onOpenDateTimeSettings = [&s] { s.backend.openDateTimeSettings(); };
    self->connectionModal_ = std::move(modal);
    self->restackModals();
  });
}

void PluginRoot::updateNoticeChanged() {
  juce::MessageManager::callAsync([self = juce::Component::SafePointer(this)] {
    if (self == nullptr) return;
    const auto& notice = self->services_.updates.notice();
    self->updateNotice_.reset();
    if (!notice) return;
    auto& s = self->services_;
    auto modal = self->openModal<UpdateNotice>(*notice);
    modal->onRemindLater = [&s](int days) { s.updates.remindLater(days); };
    self->updateNotice_ = std::move(modal);
    self->restackModals();
  });
}

void PluginRoot::hintChanged() {
  if (hintsVisible_ == services_.hints.enabled()) return;
  hintsVisible_ = services_.hints.enabled();
  updateChromeHeight();
}

void PluginRoot::updateChromeHeight() {
  const int hintExtra = hintsVisible_ ? design::kHintHeight : 0;
  const int bannerExtra = bannerPhase_ != BannerPhase::hidden ? AppBanner::kHeight : 0;
  hintBar_.setVisible(hintsVisible_);
  if (deviceViewport_)
    resized();
  else
    setSize(design::kWidth, design::kHeight + bannerExtra + hintExtra);
  services_.shell.setExtraContentHeight(bannerExtra + hintExtra, hintExtra);
}

// Banner choreography
void PluginRoot::bannerChanged() {
  const auto& active = services_.banners.active();
  if (active) {
    switch (bannerPhase_) {
      case BannerPhase::hidden:
        // From cold the window has to grow first; show once it has.
        bannerPhase_ = BannerPhase::waiting;
        banner_.setSpec(*active);
        updateChromeHeight();
        if (viewportFits()) {
          bannerShow();
        } else {
          bannerWait_.start(kBannerWaitMs, [this] { bannerShow(); });
        }
        break;
      case BannerPhase::waiting:
      case BannerPhase::shown:
        // Rule swaps render directly.
        banner_.setSpec(*active);
        break;
    }
    return;
  }
  if (bannerPhase_ == BannerPhase::hidden) return;
  bannerWait_.cancel();
  bannerPhase_ = BannerPhase::hidden;
  banner_.setVisible(false);
  updateChromeHeight();
}

bool PluginRoot::viewportFits() const {
  // The viewport is the parent the shell scales us into; it fits once it is
  // at least the banner-inclusive box at the current scale (2px tolerance
  // for rounding). No parent yet: the check reruns when one arrives.
  const auto* parent = getParentComponent();
  if (parent == nullptr) return false;
  const float verticalScale = getTransform().mat11;
  return parent->getHeight() >= designHeight() * verticalScale - 2;
}

void PluginRoot::componentMovedOrResized(juce::Component&, bool, bool) {
  if (bannerPhase_ == BannerPhase::waiting && viewportFits()) bannerShow();
}

void PluginRoot::parentHierarchyChanged() {
  if (auto* top = getTopLevelComponent(); top != this && top != keyWindow_.getComponent()) {
    if (keyWindow_ != nullptr) keyWindow_->removeKeyListener(&focusPolicy_);
    keyWindow_ = top;
    top->addKeyListener(&focusPolicy_);
  }
  auto* parent = getParentComponent();
  if (parent == watchedParent_) return;
  if (watchedParent_ != nullptr) watchedParent_->removeComponentListener(this);
  watchedParent_ = parent;
  if (watchedParent_ != nullptr) watchedParent_->addComponentListener(this);
  componentMovedOrResized(*this, false, true);
}

// Keyboard focus (see the header): JUCE's tab order, with no default.
std::unique_ptr<juce::ComponentTraverser> PluginRoot::createKeyboardFocusTraverser() {
  return std::make_unique<NoDefaultFocus>();
}

bool PluginRoot::FocusPolicy::keyPressed(const juce::KeyPress& key, juce::Component*) {
  if (!key.isKeyCode(juce::KeyPress::tabKey)) return false;
  // Focus resting on the window itself (a standalone DocumentWindow takes
  // it when the OS activates it) is nothing focused as far as the UI goes.
  auto* focused = juce::Component::getCurrentlyFocusedComponent();
  if (focused != nullptr && root_.isParentOf(focused)) return false;
  const auto order = juce::KeyboardFocusTraverser().getAllComponents(&root_);
  if (order.empty()) return false;
  (key.getModifiers().isShiftDown() ? order.back() : order.front())->grabKeyboardFocus();
  return true;
}

void PluginRoot::FocusPolicy::mouseDown(const juce::MouseEvent& e) {
  // Runs after the pressed component's own mouseDown, so a click that gave
  // focus (a text field) has already done so: keep focus when it sits on the
  // pressed component's line of ancestry either way (an editor inside its
  // field, a row inside its popover).
  auto* focused = juce::Component::getCurrentlyFocusedComponent();
  auto* pressed = e.eventComponent;
  if (focused == nullptr || pressed == nullptr || !root_.isParentOf(focused)) return;
  if (focused == pressed || focused->isParentOf(pressed) || pressed->isParentOf(focused)) return;
  focused->giveAwayKeyboardFocus();
}

bool PluginRoot::keyPressed(const juce::KeyPress& key) {
  if (key != juce::KeyPress::escapeKey) return false;
  auto* focused = getCurrentlyFocusedComponent();
  if (focused == nullptr || !isParentOf(focused)) return false;
  focused->giveAwayKeyboardFocus();
  return true;
}

void PluginRoot::bannerShow() {
  bannerWait_.cancel();
  bannerPhase_ = BannerPhase::shown;
  banner_.setVisible(true);
  resized();
  if (const auto& spec = services_.banners.active()) {
    juce::String text;
    for (const auto& run : spec->content) text << run.text;
    help::announce(text);
  }
}

void PluginRoot::handleBannerAction(BannerAction action) {
  switch (action) {
    case BannerAction::openSettings:
      openSettings(SettingsScreen::Tab::system);
      break;
    case BannerAction::switchToAsio:
      services_.audioDevice.setDeviceType("ASIO");
      break;
    case BannerAction::openMicSettings:
      services_.audioDevice.openMicSettings();
      break;
  }
}

// Takeovers
void PluginRoot::setTunerShown(bool shown) {
  if (shown == tunerShown()) return;
  if (shown) {
    tuner_ = std::make_unique<TunerView>(services_);
    tuner_->onClose = [this] { setTunerShown(false); };
    addAndMakeVisible(*tuner_);
    overlay_.toFront(false);  // popovers and the toast stay above the takeover
  } else {
    tuner_.reset();
  }
  syncTakeovers();
  resetDrawerForPage();
  header_.setTunerShown(shown);
  resized();
}

void PluginRoot::setBrowserShown(bool shown) {
  if (shown == browserShown()) return;
  if (shown) {
    browser_ = std::make_unique<ToneBrowser>(services_);
    // Closing without picking abandons any pending swap / insert target.
    browser_->onClose = [this] {
      services_.loadFlow.clearPendingTargets();
      setBrowserShown(false);
    };
    // The browser's sign-in CTAs run the login flow and return to this
    // same browser.
    browser_->onSignIn = [this] {
      if (signInShown()) return;
      services_.connection.requireConnection([this] { services_.session.login(ToneSession::LoginIntent::browse); });
    };
    // Right above the faceplate: under a tuner, Settings and the overlay.
    addChildComponent(*browser_, getIndexOfChildComponent(&faceplate_) + 1);
  } else {
    browser_.reset();
  }
  syncTakeovers();
  resetDrawerForPage();
  resized();
}

// What a takeover covers is hidden, not left painting underneath: the meters
// tick at 30 Hz and would otherwise repaint for nothing.
void PluginRoot::syncTakeovers() {
  const bool tuner = tunerShown(), browser = browserShown(), signIn = signInShown();
  const bool column = browser || signIn;  // something covers the whole column
  main_.setVisible(!tuner && !column);
  faceplate_.setVisible(deviceViewport_ ? drawerTween_.value() > 0 : tuner || !column);
  if (browser_) browser_->setVisible(!tuner && !signIn);
  if (signIn_) signIn_->setVisible(!tuner);
}

bool PluginRoot::inspectionPage() {
  return !tunerShown() && (browserShown() || signInShown() || main_.chainScreen().detailShown());
}

void PluginRoot::resetDrawerForPage() {
  if (deviceViewport_) setDrawerExpanded(!inspectionPage() && mainDrawerExpanded_, false);
}

void PluginRoot::setDrawerExpanded(bool expanded, bool animate) {
  drawerHandle_.setExpanded(expanded);
  if (animate) drawerTween_.animateTo(expanded ? 1.0f : 0.0f, 160);
  else drawerTween_.snap(expanded ? 1.0f : 0.0f);
}

void PluginRoot::closeTunerThen(const std::function<void()>& fn) {
  setTunerShown(false);
  if (fn) fn();
}

void PluginRoot::showChainThen(const std::function<void()>& fn) {
  setTunerShown(false);
  if (browserShown()) {
    services_.loadFlow.clearPendingTargets();
    setBrowserShown(false);
  }
  main_.chainScreen().returnToGallery();
  if (fn) fn();
}

void PluginRoot::logout() {
  services_.loadFlow.clearPendingTargets();
  setBrowserShown(false);
  services_.session.logout();
}

// Layout
void PluginRoot::paint(juce::Graphics& g) { g.fillAll(theme::kBlack); }

void PluginRoot::setDeviceViewport(juce::Rectangle<int> bounds) {
  const bool first = !deviceViewport_;
  deviceViewport_ = true;
  header_.setDeviceViewport(true);
  faceplate_.setDeviceViewport(true);
  main_.chainScreen().gallery().setDeviceViewport(true);
  setTransform({});
  setBounds(bounds);
  if (first) resetDrawerForPage();
  resized();
}

void PluginRoot::openSettings(SettingsScreen::Tab tab) {
  if (settings_ != nullptr) {
    settings_->setTab(tab);
    return;
  }
  settings_ = std::make_unique<SettingsScreen>(services_, tab);
  settings_->onClose = [this] { closeSettings(); };
  settings_->setBounds(getLocalBounds());
  // Above the content column, below the overlay layer.
  addChildComponent(*settings_);
  settings_->toBehind(&overlay_);
  settings_->setVisible(true);
  drawerHandle_.setVisible(false);
  services_.hints.setHover({});
}

void PluginRoot::closeSettings() {
  if (settings_ == nullptr) return;
  // Deferred: the close click comes from a button inside the screen.
  juce::MessageManager::callAsync([safe = juce::Component::SafePointer<PluginRoot>(this)] {
    if (safe != nullptr) {
      safe->settings_.reset();
      safe->resized();
    }
  });
}

void PluginRoot::resized() {
  overlay_.setBounds(getLocalBounds());
  if (settings_ != nullptr) settings_->setBounds(getLocalBounds());
  for (auto* modal : {static_cast<ModalLayer*>(updateNotice_.get()), static_cast<ModalLayer*>(connectionModal_.get())})
    if (modal != nullptr) modal->setBounds(getLocalBounds());

  // The banner strip, then the content column at its full height; while
  // the window has the strip's space but the banner isn't shown yet, the
  // gap at the bottom is black on black.
  const int slotH = bannerPhase_ == BannerPhase::shown ? AppBanner::kHeight : 0;
  banner_.setBounds(0, 0, getWidth(), AppBanner::kHeight);
  const int hintH = hintsVisible_ ? design::kHintHeight : 0;
  const int columnH = deviceViewport_ ? getHeight() - slotH : design::kHeight + hintH;
  auto column = juce::Rectangle<int>(0, slotH, getWidth(), columnH);
  if (deviceViewport_) {
    header_.setBounds(column.removeFromTop(PluginHeader::kDeviceHeight));
    const int extent = Faceplate::kDeviceHeight + hintH;
    const int drawerHeight = juce::roundToInt(extent * drawerTween_.value());
    const int drawerTop = column.getBottom() - drawerHeight;
    faceplate_.setBounds(0, drawerTop, getWidth(), Faceplate::kDeviceHeight);
    hintBar_.setBounds(0, drawerTop + Faceplate::kDeviceHeight, getWidth(), hintH);
    faceplate_.setVisible(drawerHeight > 0);
    hintBar_.setVisible(hintsVisible_ && drawerHeight > 0);
    column.removeFromBottom(drawerHeight + DrawerHandle::kHeight);
    drawerHandle_.setBounds((getWidth() - DrawerHandle::kWidth) / 2, column.getBottom(),
                            DrawerHandle::kWidth, DrawerHandle::kHeight);
    drawerHandle_.setVisible(settings_ == nullptr);
    if (browser_) browser_->setBounds(column);
    if (signIn_) signIn_->setBounds(column);
    main_.setBounds(column);
    if (tuner_) tuner_->setBounds(column);
    // Status messages remain above the handle, even when CPU/hints are hidden.
    toast_.setBottomOffset(drawerHeight + DrawerHandle::kHeight + kToastGap);
    return;
  }
  if (hintsVisible_) hintBar_.setBounds(column.removeFromBottom(hintH));
  header_.setBounds(column.removeFromTop(deviceViewport_ ? PluginHeader::kDeviceHeight : PluginHeader::kHeight));
  if (browser_) browser_->setBounds(column);  // the rest, faceplate included
  if (signIn_) signIn_->setBounds(column);
  faceplate_.setBounds(column.removeFromBottom(deviceViewport_ ? Faceplate::kDeviceHeight : Faceplate::kHeight));
  main_.setBounds(column);
  if (tuner_) tuner_->setBounds(column);

  // The toast floats above the faceplate, measured from the overlay's bottom.
  const int belowColumn = getHeight() - (slotH + columnH);
  toast_.setBottomOffset(belowColumn + faceplate_.getHeight() + hintH + kToastGap);
}

}  // namespace t3k::ui
