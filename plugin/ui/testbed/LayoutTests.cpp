// Device layout regressions: JUCE may report the 1560 x 720 Artemis panel
// as 780 x 360 logical pixels. Both must retain readable, separate controls.
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <set>
#include <vector>

#include "Drive.h"
#include "Host.h"
#include "services/UiPrefs.h"
#include "views/Faceplate.h"
#include "views/MainScreen.h"
#include "views/PluginHeader.h"
#include "views/SignInScreen.h"
#include "views/block/BlockDetail.h"
#include "views/browser/ToneBrowser.h"
#include "views/settings/SettingsScreen.h"
#include "views/gallery/GalleryTile.h"
#include "views/gallery/StereoPanRail.h"
#include "widgets/DbMeter.h"
#include "widgets/Knob.h"
#include "widgets/RetryLoadBadge.h"
#include "widgets/QrCode.h"

namespace t3k::ui::testbed {
namespace {

bool visible(const juce::Component& component) {
  for (auto* current = &component; current != nullptr; current = current->getParentComponent())
    if (!current->isVisible()) return false;
  return true;
}

void collect(juce::Component& parent, const std::function<bool(juce::Component&)>& accept,
             std::vector<juce::Component*>& found) {
  for (auto* child : parent.getChildren()) {
    if (!visible(*child)) continue;
    if (accept(*child)) found.push_back(child);
    collect(*child, accept, found);
  }
}

juce::String controlName(const juce::Component& component) {
  if (component.getTitle().isNotEmpty()) return component.getTitle();
  if (component.getName().isNotEmpty()) return component.getName();
  return component.getHelpText().upToFirstOccurrenceOf(".", false, false);
}

struct DeviceLayoutTests : juce::UnitTest {
  DeviceLayoutTests() : juce::UnitTest("Artemis compact layout", "ui") {}

  static void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(80); }

  void separateControls(juce::Component& parent, const std::vector<juce::Component*>& controls) {
    const auto bounds = parent.getLocalBounds().toFloat();
    for (size_t i = 0; i < controls.size(); ++i) {
      const auto box = parent.getLocalArea(controls[i], controls[i]->getLocalBounds().toFloat());
      expect(!box.isEmpty() && bounds.contains(box), controlName(*controls[i]) + " remains inside its row");
      for (size_t j = 0; j < i; ++j) {
        const auto other = parent.getLocalArea(controls[j], controls[j]->getLocalBounds().toFloat());
        expect(!box.intersects(other), controlName(*controls[i]) + " overlaps " + controlName(*controls[j]));
      }
    }
  }

  void checkLayout(PluginRoot& root, bool stereo, int width, int height) {
    expect(root.getBounds() == juce::Rectangle<int>(0, 0, width, height));
    expect(root.getTransform().isIdentity(), "the panel uses native logical bounds");
    expectEquals(width * 6, height * 13);
    auto* header = drive::find(root, [](juce::Component& c) { return dynamic_cast<PluginHeader*>(&c) != nullptr; });
    auto* plate = drive::find(root, [](juce::Component& c) { return dynamic_cast<Faceplate*>(&c) != nullptr; });
    expect(header != nullptr && plate != nullptr);
    if (!header || !plate) return;
    expectEquals(header->getHeight(), PluginHeader::kDeviceHeight);
    expectEquals(plate->getHeight(), Faceplate::kDeviceHeight);
    expect(root.getLocalBounds().contains(header->getBounds()));
    expect(root.getLocalBounds().contains(plate->getBounds()));

    auto* main = drive::find(root, [](juce::Component& c) { return dynamic_cast<MainScreen*>(&c) != nullptr; });
    expect(main != nullptr);
    if (main) {
      const auto band = root.getLocalArea(main, main->getLocalBounds().toFloat());
      std::vector<juce::Component*> meters, rails;
      collect(*main, [](juce::Component& c) { return dynamic_cast<DbMeter*>(&c) != nullptr; }, meters);
      collect(*main, [](juce::Component& c) { return dynamic_cast<StereoPanRail*>(&c) != nullptr; }, rails);
      expectEquals(static_cast<int>(meters.size()), 2);
      expectEquals(static_cast<int>(rails.size()), stereo ? 1 : 0);
      for (auto* meter : meters)
        expect(band.contains(root.getLocalArea(meter, meter->getLocalBounds().toFloat())),
               "the full input/output meter remains inside the middle band");
      for (auto* rail : rails) {
        const auto railBounds = root.getLocalArea(rail, rail->getLocalBounds().toFloat());
        expect(band.contains(railBounds), "the stereo pan rail stays inside the middle band");
        for (auto* meter : meters)
          expect(!railBounds.intersects(root.getLocalArea(meter, meter->getLocalBounds().toFloat())),
                 "the pan rail stays clear of the meters");
        std::vector<juce::Component*> railControls;
        collect(*rail, [](juce::Component& c) {
          return dynamic_cast<Knob*>(&c) != nullptr || dynamic_cast<juce::Button*>(&c) != nullptr;
        }, railControls);
        separateControls(*rail, railControls);
        for (auto* control : railControls)
          expect(band.contains(root.getLocalArea(control, control->getLocalBounds().toFloat())),
                 controlName(*control) + " remains visible in the scaled pan rail");
      }
    }

    std::vector<juce::Component*> headerPeers;
    for (auto* child : header->getChildren())
      if (visible(*child)) headerPeers.push_back(child);
    separateControls(*header, headerPeers);

    std::vector<juce::Component*> controls;
    collect(*plate, [](juce::Component& c) {
      return dynamic_cast<Knob*>(&c) != nullptr || dynamic_cast<juce::Button*>(&c) != nullptr;
    }, controls);
    separateControls(*plate, controls);
    for (const auto* title : {"Gate", "Pitch", "Bass", "Middle", "Treble", "Input", "Output"}) {
      bool found = false;
      for (const auto* control : controls)
        found |= dynamic_cast<const Knob*>(control) != nullptr && control->getTitle() == title;
      expect(found, juce::String(title) + " is visible in the fully populated faceplate");
    }

    std::vector<juce::Component*> tiles;
    collect(root, [](juce::Component& c) { return dynamic_cast<GalleryTile*>(&c) != nullptr; }, tiles);
    expect(!tiles.empty(), "the chain gallery is visible");
    std::set<int> rows;
    for (size_t i = 0; i < tiles.size(); ++i) {
      const auto box = root.getLocalArea(tiles[i], tiles[i]->getLocalBounds().toFloat());
      // Tiles can scroll horizontally, but must never be clipped vertically
      // by the header, the lower controls, or the other stereo lane.
      expect(box.getY() >= header->getBottom() && box.getBottom() <= plate->getY(),
             "gallery tiles fit between the top and bottom controls");
      expect(box.getWidth() > 0 && box.getWidth() <= (stereo ? 80.0f : 112.0f));
      expectWithinAbsoluteError(box.getWidth(), box.getHeight(), 0.01f);
      rows.insert(juce::roundToInt(box.getY()));
      for (size_t j = 0; j < i; ++j)
        expect(!box.intersects(root.getLocalArea(tiles[j], tiles[j]->getLocalBounds().toFloat())),
               "gallery tiles and stereo rows stay separate");
      std::vector<juce::Component*> actions;
      collect(*tiles[i], [](juce::Component& c) { return dynamic_cast<juce::Button*>(&c) != nullptr; }, actions);
      separateControls(*tiles[i], actions);
    }
    expectEquals(static_cast<int>(rows.size()), stereo ? 2 : 1);
  }

  void runTest() override {
    const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
    for (const bool stereo : {false, true}) {
      const auto* source = fixtures.find(stereo ? "main-stereo" : "main-mono");
      expect(source != nullptr);
      if (!source) return;
      for (const bool auth : {false, true}) {
        for (const int width : {780, 1560}) {
          const int height = width * 6 / 13;
          beginTest(juce::String(width) + "px " + (stereo ? "stereo" : "mono") + (auth ? " signed in" : " signed out"));
          Scenario scenario = *source;
          scenario.data = source->data.clone();
          auto* properties = scenario.data.getDynamicObject();
          properties->setProperty("deviceViewport", true);
          properties->setProperty("deviceWidth", width);
          properties->setProperty("deviceHeight", height);
          properties->setProperty("auth", auth);
          properties->setProperty("hints", true);
          MockBackend backend(scenario.data);
          ScaledHost host(backend, scenario, fixtures.root);
          auto& root = host.pluginRoot();
          root.services().pointer.sawInput(true);
          root.services().prefs.setBool(UiPrefs::kShowGateControl, true);
          root.services().prefs.setBool(UiPrefs::kShowPitchControl, true);
          for (const auto* id : {"gateEnabled", "pitchEnabled"})
            if (auto* parameter = backend.parameter(id)) parameter->setValueNotifyingHost(1.0f);
          for (const auto* id : {"spreadEnabled", "alignEnabled"})
            if (auto* parameter = backend.parameter(id)) parameter->setValueNotifyingHost(0.0f);
          pump();
          checkLayout(root, stereo, width, height);

          // The advertised and powered image groups have different children;
          // both must fit alongside tone power, balance and the output knob.
          for (const auto* id : {"spreadEnabled", "alignEnabled"})
            if (auto* parameter = backend.parameter(id)) parameter->setValueNotifyingHost(1.0f);
          pump();
          checkLayout(root, stereo, width, height);

          root.services().hints.setEnabled(false);
          pump();
          checkLayout(root, stereo, width, height);
          root.services().hints.setEnabled(true);
          pump();
          checkLayout(root, stereo, width, height);

          if (width == 780 && !stereo && !auth) {
            beginTest("switching a live mono chain to stereo reflows the gallery and controls");
            const auto* stereoScenario = fixtures.find("main-stereo");
            expect(stereoScenario != nullptr);
            if (stereoScenario) {
              backend.setChain(stereoScenario->data["chain"].clone());
              root.services().chain.refresh(true);
              pump();
              checkLayout(root, true, width, height);
            }
          }

          if (width == 780 && stereo && auth) {
            beginTest("a banner and hints leave both compact stereo rows clear");
            const auto* warning = fixtures.find("banner-no-input");
            expect(warning != nullptr);
            if (warning) {
              backend.setDevice(warning->data["device"]);
              pump();
              expect(root.services().banners.active().has_value());
              auto* banner = drive::find(root, [](juce::Component& c) {
                return dynamic_cast<AppBanner*>(&c) != nullptr && visible(c);
              });
              expect(banner != nullptr, "the warning is actually rendered");
              checkLayout(root, stereo, width, height);

              auto failedChain = backend.getChainState(-1).clone();
              auto* blocks = failedChain["chain"].getArray();
              expect(blocks != nullptr && !blocks->isEmpty());
              if (blocks != nullptr && !blocks->isEmpty()) {
                auto* block = blocks->getReference(0).getDynamicObject();
                expect(block != nullptr);
                if (block) {
                  block->setProperty("loadFailed", true);
                  block->setProperty("loaded", false);
                  block->setProperty("modelLoading", false);
                  backend.setChain(failedChain);
                  root.services().chain.refresh(true);
                  pump();
                  auto* retry = drive::find(root, [](juce::Component& c) {
                    return dynamic_cast<RetryLoadBadge*>(&c) != nullptr && visible(c);
                  });
                  expect(retry != nullptr, "the failed download exposes its retry control");
                  checkLayout(root, stereo, width, height);
                }
              }
            }
          }
        }
      }
    }
  }
};

DeviceLayoutTests deviceLayoutTests;

struct DevicePageTests : juce::UnitTest {
  DevicePageTests() : juce::UnitTest("Artemis page layout", "ui") {}

  template <typename T> static T* find(juce::Component& parent) {
    return dynamic_cast<T*>(drive::find(parent, [](juce::Component& c) {
      return dynamic_cast<T*>(&c) != nullptr && visible(c);
    }));
  }

  void runTest() override {
    const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
    for (const int width : {780, 1560}) {
      for (const auto* page : {"signin", "browser", "block", "block-info", "block-eq", "settings", "system"}) {
        const auto id = "artemis-pages-" + juce::String(width) + "-" + page;
        beginTest(id);
        const auto* scenario = fixtures.find(id);
        expect(scenario != nullptr);
        if (!scenario) continue;
        MockBackend backend(scenario->data);
        ScaledHost host(backend, *scenario, fixtures.root);
        auto& root = host.pluginRoot();
        drive::wait(100);
        if (const auto* action = driveFor(scenario->driveId())) (*action)(root, backend);
        drive::wait(100);

        if (auto* signin = find<SignInScreen>(root)) {
          auto* qr = find<QrCode>(*signin);
          expect(qr != nullptr);
          if (qr) {
            const auto cards = signin->getLocalArea(qr->getParentComponent(), qr->getParentComponent()->getLocalBounds());
            expect(signin->getLocalBounds().contains(cards), "both sign-in cards fit above the CPU row");
            expect(qr->getWidth() >= 120, "QR remains readable on the compact display");
          }
        } else if (auto* browser = find<ToneBrowser>(root)) {
          auto* search = find<TextField>(*browser);
          auto* back = drive::byHelpPrefix(*browser, help::text(help::Key::closeToneBrowser));
          expect(search && back, "search and navigation are available");
          if (search && back) {
            const auto box = browser->getLocalArea(search, search->getLocalBounds());
            expect(std::abs(box.getCentreY() - back->getBounds().getCentreY()) <= 1, "search shares the navigation row");
            expect(box.getX() > back->getRight());
          }
          // The filter bar also has a horizontal scroller; choose the result list.
          auto* viewport = dynamic_cast<DragScroller*>(drive::find(*browser, [](juce::Component& c) {
            auto* v = dynamic_cast<DragScroller*>(&c);
            return v && v->getHeight() > FilterBar::kHeight;
          }));
          auto* filters = find<FilterBar>(*browser);
          expect(viewport && filters);
          if (viewport && filters) {
            const int initialHeight = viewport->getHeight();
            if (viewport->getViewedComponent()->getHeight() <= initialHeight + FilterBar::kHeight + 16 + 64) {
              viewport->setViewPosition(0, 100);
              expect(filters->isVisible(), "short lists retain their filters");
              continue;
            }
            viewport->setViewPosition(0, 100);
            expect(!filters->isVisible(), "downward scrolling hides filters");
            expect(viewport->getHeight() > initialHeight, "collapsed filters make more room for tones");
            const int y = viewport->getViewPositionY();
            viewport->setViewPosition(0, y - 2);
            expect(!filters->isVisible(), "small scroll jitter does not flash the header");
            viewport->setViewPosition(0, y - 40);
            expect(filters->isVisible(), "upward scrolling restores filters");
            viewport->setViewPosition(0, 0);
            expectEquals(viewport->getHeight(), initialHeight);
          }
        } else if (auto* detail = find<BlockDetail>(root)) {
          auto* card = find<BlockCard>(*detail);
          auto* viewport = find<DragScroller>(*detail);
          expect(card && viewport);
          if (!card || !viewport) continue;
          const auto cardBox = root.getLocalArea(card, card->getLocalBounds());
          std::vector<juce::Component*> meters;
          collect(root, [](juce::Component& c) { return dynamic_cast<DbMeter*>(&c) != nullptr; }, meters);
          for (auto* meter : meters)
            expect(!cardBox.intersects(root.getLocalArea(meter, meter->getLocalBounds())), "detail stays clear of both meters");
          auto* back = drive::byHelpPrefix(*card, help::text(help::Key::backToChain));
          expect(back != nullptr);
          for (const auto key : {help::Key::eqToggle, help::Key::toneInfo, help::Key::shareTone,
                                 help::Key::swapTone, help::Key::saveToHouston}) {
            auto* button = drive::byHelpPrefix(*card, help::text(key));
            expect(button != nullptr, "inspection action is available");
            if (!button || !back) continue;
            const auto box = card->getLocalArea(button, button->getLocalBounds());
            expect(card->getLocalBounds().contains(box));
            expect(std::abs(box.getCentreY() - back->getBounds().getCentreY()) <= 1, "actions share the Back row");
            expect(!box.intersects(back->getBounds()), "actions do not cover Back");
          }
          viewport->setViewPosition(0, 10000);
          expect(viewport->getViewPositionY() + viewport->getViewHeight() >= card->getBottom(),
                 "the bottom of every inspection view is reachable");
          if (juce::String(page) == "block-eq")
            expectEquals(viewport->getViewPositionY(), 0, "the complete EQ fits with the drawer collapsed");
          else if (width == 780) expect(viewport->getViewPositionY() > 0, "long inspection content can still scroll");
        } else if (auto* settings = find<SettingsScreen>(root)) {
          auto* close = drive::buttonNamed(*settings, "Close settings");
          expect(close != nullptr);
          if (close) {
            const auto box = settings->getLocalArea(close, close->getLocalBounds());
            expectEquals(box.getRight(), width - SettingsScreen::kPadX, "settings uses the full panel width");
          }
          settings->viewport().setViewPosition(0, 10000);
          if (close) expect(settings->getLocalArea(close, close->getLocalBounds()).getY() >= 0,
                            "close remains available after scrolling settings");
        } else {
          expect(false, "expected page was not opened");
        }
      }
    }
  }
};

DevicePageTests devicePageTests;

struct DrawerTests : juce::UnitTest {
  DrawerTests() : juce::UnitTest("Artemis controls drawer", "ui") {}

  struct BackendWithEq : MockBackend {
    using MockBackend::MockBackend;
    int editedBand = -1;
    bool setBlockEqBand(const std::string&, int band, const juce::var&) override {
      editedBand = band;
      return true;
    }
  };

  void runTest() override {
    const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
    for (const int width : {780, 1560}) {
      beginTest(juce::String(width) + "px drawer tap, swipe, page defaults and EQ hit targets");
      const auto* scenario = fixtures.find("artemis-pages-" + juce::String(width) + "-block");
      expect(scenario != nullptr);
      if (!scenario) continue;
      auto data = scenario->data.clone();
      if (auto* blocks = data["chain"]["chain"].getArray())
        for (auto& block : *blocks)
          if (auto* eq = block["params"]["eq"].getDynamicObject()) eq->setProperty("enabled", true);
      BackendWithEq backend(data);
      ScaledHost host(backend, *scenario, fixtures.root);
      juce::DocumentWindow window("drawer test", juce::Colours::black, 0);
      window.setContentNonOwned(&host, true);
      window.setVisible(true);
      drive::wait(100);
      auto& root = host.pluginRoot();
      auto* handle = DevicePageTests::find<DrawerHandle>(root);
      auto* main = DevicePageTests::find<MainScreen>(root);
      auto* plate = drive::find(root, [](juce::Component& c) { return dynamic_cast<Faceplate*>(&c) != nullptr; });
      auto* hints = drive::find(root, [](juce::Component& c) { return dynamic_cast<HintBar*>(&c) != nullptr; });
      auto* peer = window.getPeer();
      expect(handle && main && plate && hints && peer);
      if (!handle || !main || !plate || !hints || !peer) continue;
      const auto check = [&](bool open) {
        expect(handle->getToggleState() == open, "handle reflects the drawer state");
        expect(plate->isVisible() == open, "controls follow the drawer state");
        expect(hints->isVisible() == (open && root.services().hints.enabled()), "CPU and hints follow the drawer state");
        expect(root.getLocalBounds().contains(handle->getBounds()), "the handle remains reachable");
        expectEquals(main->getBottom(), handle->getY(), "the main pane ends above the handle");
        if (open) expectEquals(plate->getY(), handle->getBottom());
      };
      check(false);  // entering any block inspection starts collapsed
      const int closedHeight = main->getHeight();
      drive::click(root, *handle);
      drive::wait(250);
      check(true);
      expectEquals(closedHeight - main->getHeight(), Faceplate::kDeviceHeight + design::kHintHeight);
      root.services().chain.refresh(true);
      root.setDeviceViewport(root.getBounds());
      drive::wait(100);
      check(true);  // polling / resizing must not undo the user's choice

      juce::int64 time = juce::Time::currentTimeMillis();
      const auto pointer = [&](juce::Point<float> point, bool down) {
        peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, point,
                              down ? juce::ModifierKeys::leftButtonModifier : juce::ModifierKeys(),
                              0.0f, 0.0f, ++time);
        drive::wait(15);
      };
      const auto swipe = [&](int dy) {
        const auto start = window.getLocalPoint(handle, handle->getLocalBounds().getCentre()).toFloat();
        pointer(start, false);
        pointer(start, true);
        pointer(start.translated(0, static_cast<float>(dy)), true);
        pointer(start.translated(0, static_cast<float>(dy)), false);
        drive::wait(250);
      };
      swipe(50);
      check(false);
      swipe(-50);
      check(true);

      drive::clickByHelp(root, help::text(help::Key::eqToggle));
      drive::wait(100);
      check(false);  // EQ automatically makes room even after a manual open
      auto* eqView = DevicePageTests::find<BlockEqView>(root);
      expect(eqView != nullptr);
      if (eqView) {
        const auto box = root.getLocalArea(eqView, eqView->getLocalBounds());
        expect(box.getBottom() <= handle->getY(), "all EQ bands and labels fit above the drawer");
        for (auto* layer : eqView->getChildren()) {
          const auto bounds = eqView->getLocalArea(layer, layer->getLocalBounds().toFloat());
          expectWithinAbsoluteError(bounds.getWidth(), static_cast<float>(eqView->getWidth()), 0.1f);
          expectWithinAbsoluteError(bounds.getHeight(), static_cast<float>(eqView->getHeight()), 0.1f);
        }
        const auto lastBand = window.getLocalPoint(eqView, juce::Point<int>(eqView->getWidth() * 9 / 10,
                                                                          eqView->getHeight() / 3)).toFloat();
        pointer(lastBand, false);
        pointer(lastBand, true);
        pointer(lastBand, false);
        expectEquals(backend.editedBand, 5, "the rightmost fader edits band six after scaling");
      }
      drive::click(root, *handle);
      drive::wait(250);
      drive::clickByHelp(root, help::text(help::Key::toneInfo));
      drive::wait(100);
      check(false);

      drive::clickByHelp(root, help::text(help::Key::backToChain));
      drive::wait(100);
      check(true);  // return to the chain restores its original preference
      drive::click(root, *handle);
      drive::wait(250);
      check(false);
      root.setBrowserShown(true);
      drive::wait(100);
      expect(!handle->getToggleState());
      root.setBrowserShown(false);
      check(false);  // preserve a deliberately hidden chain drawer
      root.services().hints.setEnabled(false);
      drive::click(root, *handle);
      drive::wait(250);
      check(true);  // opening never re-enables the user's disabled info bar
      root.openSettings();
      expect(!handle->isVisible(), "settings owns the full screen");
      root.closeSettings();
      drive::wait(100);
      check(true);
      window.setVisible(false);
    }
  }
};

DrawerTests drawerTests;

}  // namespace
}  // namespace t3k::ui::testbed
