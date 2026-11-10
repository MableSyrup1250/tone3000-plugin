#!/usr/bin/env bash
set -euo pipefail

# Configure only. A full plugin compile is deliberately outside this test.
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
case "$(uname -m)" in aarch64|arm64) ;; *) echo 'ARM64 host required' >&2; exit 77 ;; esac
build_dir="${ARTEMIS_TONE3000_CONFIGURE_TEST_DIR:-$root/../../build/work/tone3000-plugin/build-arm64-juce-9.0.3}"
old_juce="$root/libs/juce"
isolated_juce="$root/libs/juce-9.0.3"
legacy_cache="$root/../../build/work/tone3000-plugin/build-arm64/_deps/juce-build/tools/CMakeCache.txt"
legacy_cache_hash=""
if [ -f "$legacy_cache" ]; then legacy_cache_hash="$(sha256sum "$legacy_cache")"; fi

fingerprint() {
  if [ -d "$old_juce/.git" ]; then
    git -C "$old_juce" rev-parse HEAD
    git -C "$old_juce" status --porcelain=v1 --untracked-files=no
    git -C "$old_juce" stash list
  fi
}
before="$(fingerprint)"

mkdir -p "$build_dir"
configure=(cmake -S "$root" -B "$build_dir" -G Ninja
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_TOOLCHAIN_FILE="$root/cmake/linux-toolchain.cmake"
  -DCMAKE_C_FLAGS_RELEASE='-O3 -DNDEBUG -mcpu=cortex-a76 -mtune=cortex-a76'
  -DCMAKE_CXX_FLAGS_RELEASE='-O3 -DNDEBUG -mcpu=cortex-a76 -mtune=cortex-a76'
  -DT3K_ARTEMIS_KIOSK=ON
  -DBUILD_AAX=OFF -DBUILD_LV2=OFF -DBUILD_CLAP=OFF)
for pass in first second; do
  if ! "${configure[@]}" > "$build_dir/artemis-juce-$pass.log" 2>&1; then
    tail -60 "$build_dir/artemis-juce-$pass.log" >&2
    exit 1
  fi
done

grep -qxF "JUCE_SOURCE_DIR:STATIC=$isolated_juce" "$build_dir/CMakeCache.txt"
[ "$(git -C "$isolated_juce" rev-parse HEAD)" = "$(git -C "$isolated_juce" rev-parse '9.0.3^{commit}')" ]
grep -qF 'T3K_AUDIO_DEFAULT_SETUP' "$isolated_juce/modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp"
grep -qF 'T3K_ALSA_CLOSE_STUCK_TIMEOUT' "$isolated_juce/modules/juce_audio_devices/native/juce_ALSA_linux.cpp"
grep -qF 'T3K_X11_HINT_OVERFLOW' "$isolated_juce/modules/juce_gui_basics/native/juce_XWindowSystem_linux.cpp"
grep -qF 'T3K_ARTEMIS_KIOSK_WINDOW' "$isolated_juce/modules/juce_audio_plugin_client/juce_audio_plugin_client_Standalone.cpp"
grep -qF 'T3K_ARTEMIS_XDG_CONFIG' "$isolated_juce/modules/juce_core/native/juce_Files_linux.cpp"
grep -qF 'window->setUsingNativeTitleBar (false)' "$isolated_juce/modules/juce_audio_plugin_client/juce_audio_plugin_client_Standalone.cpp"
grep -qF 'mainWindow->setFullScreen (true)' "$isolated_juce/modules/juce_audio_plugin_client/juce_audio_plugin_client_Standalone.cpp"
[ "$(fingerprint)" = "$before" ] || {
  echo 'configure changed the old JUCE checkout' >&2
  exit 1
}
if [ -n "$legacy_cache_hash" ]; then
  [ "$(sha256sum "$legacy_cache")" = "$legacy_cache_hash" ] || {
    echo 'configure changed the old JUCE build cache' >&2
    exit 1
  }
fi
grep -qF 'Configuring done' "$build_dir/artemis-juce-second.log"
echo 'Artemis JUCE 9.0.3 configure and reconfigure tests passed'
