#include "DrawerHandle.h"

#include "core/Fonts.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

DrawerHandle::DrawerHandle() {
  setSize(kWidth, kHeight);
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  setToggleable(true);
  onClick = [this] {
    if (!swiped_ && onExpansionRequested) onExpansionRequested(!expanded_);
  };
  setExpanded(true);
}

void DrawerHandle::setExpanded(bool expanded) {
  expanded_ = expanded;
  setName(expanded ? "Hide controls and CPU" : "Show controls and CPU");
  setHelpText("Controls and CPU: tap to toggle; swipe up to open or down to close.");
  setToggleState(expanded, juce::dontSendNotification);
  repaint();
}

void DrawerHandle::paintButton(juce::Graphics& g, bool highlighted, bool down) {
  const auto colour = highlighted || down ? theme::kWhite : theme::kGray;
  g.setColour(theme::kPanelBg);
  g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1, 1), 10);
  g.setColour(colour);
  g.fillRoundedRectangle(14, 10, 32, 3, 1.5f);
  paint::text(g, "CONTROLS", {56, 0, 90, getHeight()}, Fonts::mono(11), colour);
  Icons::draw(g, expanded_ ? Icon::ChevronDown : Icon::ChevronUp, {154, 5, 14, 14}, colour);
}

void DrawerHandle::mouseDown(const juce::MouseEvent& e) {
  pressScreen_ = e.getScreenPosition();
  swiped_ = false;
  Clickable::mouseDown(e);
}

void DrawerHandle::mouseDrag(const juce::MouseEvent& e) {
  const auto delta = e.getScreenPosition() - pressScreen_;
  if (std::abs(delta.y) >= 12 && std::abs(delta.y) > std::abs(delta.x)) swiped_ = true;
  Clickable::mouseDrag(e);
}

void DrawerHandle::mouseUp(const juce::MouseEvent& e) {
  const int dy = e.getScreenPosition().y - pressScreen_.y;
  Clickable::mouseUp(e);  // onClick ignores a completed swipe
  if (swiped_ && onExpansionRequested) onExpansionRequested(dy < 0);
  swiped_ = false;
}

}  // namespace t3k::ui
