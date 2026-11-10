#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Apply the window-manager state after JUCE has mapped its standalone peer.
void enforceArtemisKioskWindow(juce::Component& editor);
