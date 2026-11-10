#include "ArtemisKioskWindow.h"

#if JUCE_LINUX && T3K_ARTEMIS_KIOSK
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#endif

void enforceArtemisKioskWindow(juce::Component& editor) {
#if JUCE_LINUX && T3K_ARTEMIS_KIOSK
  auto* window = dynamic_cast<juce::DocumentWindow*>(editor.getTopLevelComponent());
  if (window == nullptr) return;
  window->setUsingNativeTitleBar(false);
  window->setTitleBarButtonsRequired(0, false);
  window->setTitleBarHeight(0);
  window->setResizable(false, false);
  auto* peer = window->getPeer();
  if (peer == nullptr || !window->isShowing()) return;
  window->setFullScreen(true);
  juce::Desktop::getInstance().setKioskModeComponent(window, false);

  // JUCE's Linux kiosk implementation only sets the bounds. Ask the WM
  // explicitly too: a screen-sized XWayland window can otherwise acquire
  // decorations/placement again on later launches under Labwc.
  if (auto* display = XOpenDisplay(nullptr)) {
    const auto handle = static_cast<::Window>(reinterpret_cast<uintptr_t>(peer->getNativeHandle()));
    const unsigned long hints[] = {2, 0, 0, 0, 0}; // MWM_HINTS_DECORATIONS, no decorations
    const auto motif = XInternAtom(display, "_MOTIF_WM_HINTS", False);
    XChangeProperty(display, handle, motif, motif, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(hints), 5);
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.window = handle;
    event.xclient.message_type = XInternAtom(display, "_NET_WM_STATE", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = 1; // ADD, never toggle persisted state
    event.xclient.data.l[1] = static_cast<long>(XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False));
    event.xclient.data.l[3] = 1; // Application request
    XSendEvent(display, DefaultRootWindow(display), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(display);
    XCloseDisplay(display);
  }
#else
  juce::ignoreUnused(editor);
#endif
}
