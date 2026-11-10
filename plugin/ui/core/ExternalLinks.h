#pragma once

#include <juce_core/juce_core.h>

namespace t3k::ui {
// Artemis hands web navigation to Launchpad; other builds use the OS browser.
bool openExternalLink(const juce::String& url);
}
