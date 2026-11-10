#include "ExternalLinks.h"
#include <juce_events/juce_events.h>
#if JUCE_LINUX && T3K_ARTEMIS_KIOSK
#include "StandaloneAudioSettings.h"
#endif

namespace t3k::ui {
bool openExternalLink(const juce::String& url) {
#if JUCE_LINUX && T3K_ARTEMIS_KIOSK
  if (!url.startsWithIgnoreCase("https://") && !url.startsWithIgnoreCase("http://")) return false;
  // Keep the touch UI responsive. An older/offline Launchpad disables the
  // link; it must never fall back to a desktop browser on the appliance.
  return juce::Thread::launch([url] {
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty("url", url);
    int status = 0;
    auto stream = juce::URL("http://127.0.0.1:8764/api/kiosk/open")
        .withPOSTData(juce::JSON::toString(juce::var(body.get())))
        .createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                              .withExtraHeaders("Content-Type: application/json\r\n")
                              .withConnectionTimeoutMs(10000)
                              .withStatusCode(&status));
    const bool opened = stream != nullptr && status == 200
        && static_cast<bool>(juce::JSON::parse(stream->readEntireStreamAsString())["ok"]);
    if (!opened) {
      juce::Logger::writeToLog("[Webkiosk] Link unavailable (HTTP " + juce::String(status) + ")");
      return;
    }
    // Hand off like Back to Launchpad: save the standalone session and quit.
    // Leaving it hidden below the browser would make Launchpad reject the
    // next TONE3000 launch as "already running".
    juce::MessageManager::callAsync([] {
      if (StandaloneAudioSettings::isAvailable())
        if (auto* app = juce::JUCEApplicationBase::getInstance()) app->systemRequestedQuit();
    });
  });
#else
  return juce::URL(url).launchInDefaultBrowser();
#endif
}
}
