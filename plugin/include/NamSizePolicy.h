#pragma once

// Artemis has CPU headroom for the Lite tier only. Apply this at every
// state boundary as well as in NamEngine, including presets from desktop.
inline double supportedNamSize(double requested) noexcept {
#if T3K_ARTEMIS_KIOSK
  (void) requested;
  return 0.0;
#else
  return requested < 0.0 ? 0.0 : requested > 1.0 ? 1.0 : requested;
#endif
}
