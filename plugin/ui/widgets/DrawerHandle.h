#pragma once

#include <functional>

#include "Clickable.h"

namespace t3k::ui {

// A tap toggles the drawer; an upward/downward swipe opens/closes it.
// Decide the swipe on release so relayout cannot move the handle out from
// under the finger while JUCE is still delivering the gesture.
class DrawerHandle : public Clickable {
public:
  static constexpr int kHeight = 24, kWidth = 180;
  DrawerHandle();
  void setExpanded(bool expanded);
  std::function<void(bool)> onExpansionRequested;

  void paintButton(juce::Graphics&, bool highlighted, bool down) override;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseDrag(const juce::MouseEvent&) override;
  void mouseUp(const juce::MouseEvent&) override;

private:
  bool expanded_ = true, swiped_ = false;
  juce::Point<int> pressScreen_;
};

}  // namespace t3k::ui
