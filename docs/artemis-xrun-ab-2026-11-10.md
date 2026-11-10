# Artemis JACK XRUN comparison — 10 November 2026

Keep `665fc2c` and merge the four incoming kiosk commits. Removing CLOBBER
did not resolve the loaded-chain XRUNs at 32 frames. On `192.168.1.121`,
the merged build completed a five-minute, externally stimulated test at
64 frames with zero JACK XRUNs after startup. The device is left on that
configuration, including its persistent boot preference.

This is a steady-playback result, not a claim of completely XRUN-free startup:
the final launch recorded two JACK XRUNs before the measured interval.
32-frame operation with the saved chain remains unqualified.

## Builds and setup

* A: local `665fc2c1411c6c3860b33ddc46108e7eeab5ccfd`, including CLOBBER.
* B: `b9f2dbf` (all four incoming commits), without CLOBBER.
* C: merge of A and B, retaining CLOBBER. Compiled source tree:
  `fc5f79d80595b20d3dae525011631ff3a6e02bd5` (before this report).

All are ARM64 Release builds targeting Cortex-A76, using the existing Debian
13 toolchain and build directory. A was verified up-to-date with Ninja before
copying its binary; B and C were rebuilt. For B, the generated
`T3K_ARTEMIS_JACK_ONLY` JUCE source patch was explicitly removed as well as the
commit itself. Otherwise the configure-time patch would survive the reversal.

The device runs the CS4282P codec, PREEMPT_RT kernel
`6.18.60+rpt-rpi-v8-rt`, native JACK at 48 kHz, two periods, server priority 80
and TONE3000 callback priority 75. The existing Guitarix services remained
running. The XR18 is USB-connected to the test host, with its analogue Aux 1
feeding Artemis Input 1 and Artemis outputs returning on XR18 inputs 7/8.

The stimulus was a 251 Hz sine at 0.05 digital amplitude. An independent JACK
client on Artemis counted server XRUN callbacks and sampled CPU load once per
second. The host recorded both analogue outputs and separately counted its
own XRUNs. No mixer routing changes were needed. Recording analysis ran after
the host capture client stopped.

The saved chain contains two enabled NAM blocks (Fortin TS and EVH 5150) and
an Orange V30 IR, with oversampling, pitch and spread disabled. Identical
embedded DSP state was restored for the loaded-chain comparisons, with
native JACK, capture channel 1 and stereo output.

There were two important configuration traps:

* The originally installed `/usr/bin/TONE3000` predated CLOBBER. Its saved
  configuration selected **ALSA / JACK Audio Connection Kit / 48 frames**,
  rather than native JACK.
* One incoming commit fixes JUCE's handling of `XDG_CONFIG_HOME`. A reads
  `/home/dev/.config/TONE3000`, whereas B/C correctly read
  `/data/artemis/config/TONE3000`. Both locations were explicitly seeded
  with the same state for the loaded-chain tests. The initial A fresh-chain
  run did not restore the user's models and is reported separately.

## Measurements

Counts are new server XRUN callbacks within each measurement window, after
launch, rather than JUCE's estimated over-budget callback count. All external
stimulus/capture runs in this table had **zero host recorder XRUNs**.

| Build / condition | Frames | Seconds | JACK XRUNs | Mean JACK CPU |
| --- | ---: | ---: | ---: | ---: |
| A, fresh empty chain, XR18 stimulus | 32 | 60 | 0 | 2.7% |
| A, saved chain | 32 | 60 | 35 | 32.2% |
| B, saved chain | 32 | 60 | 514 | 69.2% |
| C, saved chain, JACK restarted | 32 | 60 | 482 | 68.5% |
| A, saved chain | 64 | 60 | 1 | 23.2% |
| B, saved chain, early run | 64 | 60 | 2 | 25.9% |
| B, saved chain, settled run | 64 | 180 | 0 | 26.0% |
| C, saved chain, real reboot into 64 | 64 | 300 | **0** | **19.7%** |

The initial 64-frame explorations used `jack_bufsize` with the plugin closed
and then relaunched it. Launchpad correctly rejected a live size inconsistent
with its inherited boot profile; those exploratory 64-frame launches used
the same Launchpad environment directly. C's final run used the persistent
preference, an actual reboot, and the normal Launchpad launch endpoint.
The C32 run also restarted JACK, eliminating a live-resize explanation for
its continuing failures.

The final C64 run's maximum sampled JACK CPU was 24.2%. Both recorded outputs
were finite and unclipped. One-second RMS stayed within 0.044 dB on each
channel across all 300 seconds. The plugin's backend XRUN counter stayed at
its startup total of two throughout the run and subsequent settings checks.

A supplementary attempt to pin B's callback to CPU 2 did not cure its
32-frame failures; that exploratory run is retained in the artifacts but is
not a proposed deployment change. The timing difference between A and the
incoming UI/settings changes has not been bisected. The closely matching B32
and C32 results do not support discarding CLOBBER as the fix.

## First-pass result (superseded by the lockdown below)

* C installed at `/usr/bin/TONE3000`, SHA-256
  `67f94b85085e0f87df87c06fdadeaa6489fac9b232c24e1b8c86611c3c398fdc`.
* Native JACK, 48 kHz, 64 frames, two periods; reboot verified.
* `/data/artemis/config/jack-buffer-size` contains `64`.
* The loaded NAM/IR chain is preserved and the plugin is left open.
* CLOBBER's backend restriction excludes ALSA audio while retaining ALSA MIDI.
  The audio backend selector is hidden with only JACK available. The device
  menu uses JACK's `system` ports.
* The actual buffer dropdown was inspected: only 32 and 64 are offered.
  Selecting 32 wrote `32` to the preference while `jack_bufsize` remained 64
  and the plugin PID was unchanged. Selecting 64 restored the preference;
  active size and process remained unchanged. The caption shows active 64.

64 frames is 1.333 ms per JACK period at 48 kHz; this is not a measurement of
total analogue round-trip latency. JACK shares the size with Houston/Guitarix.
The repository's fallback remains 32; this device's tested preference is 64.

Validation: Release standalone builds passed; `build/tests/test_audio_buffer.py`
passed all five tests; `build/tests/test_audio_service.sh` passed; staged and
unstaged whitespace checks passed. The optional Xvfb/Openbox kiosk script
could not run because this host lacks `xwininfo` and `openbox`. Fullscreen,
settings and the buffer selector were inspected on the real device instead.

## Evidence and rollback

Full artifacts, including binaries and their hashes, scripts, per-run JSON,
raw two-channel NPY recordings, logs and screenshots, are retained at:

`/home/booky/artemis-os/build/output/tone3000-xrun-ab-20261110/`

The portable measurement summary is in the adjacent
`artemis-xrun-ab-2026-11-10.json`. The test scripts in the artifact directory
document the exact stimulus and monitoring implementation; their temporary
paths and SSH control socket must be updated when rerunning them.

The device backup is `/data/artemis/t3k-xrun-ab/original/`, including the
original executable (`TONE3000-binary`), original persistent settings
(`TONE3000/`) and original `jack-buffer-size`. Restore those with TONE3000
closed, then reboot to restore the previous audio profile. No firmware was
flashed and no XR18 configuration was changed. The original local plugin commit is also
retained on branch `backup/pre-xrun-ab-665fc2c`.

## 64-frame / Lite lockdown follow-up

The Artemis build now refuses to activate JACK audio unless the actual server
buffer is 64 frames. The plugin buffer selector is read-only, and its backend
cannot write the shared boot preference. NAM A2 size is forced to Lite in the
engine, machine default, block setter and preset/session restoration. The
Full/Lite selector and per-block size option are removed on Artemis; desktop
builds retain those options.

Launchpad checks the active boot and saved preference before launching. At 32 it
shows Cancel or “Use 64 and restart”; at active 64 with a pending return to 32 it
asks before saving 64 again. Cancel makes no write, launch or reboot request.
The dialog prominently identifies Houston's shared latency change: at 48 kHz,
a period changes from 0.67 to 1.33 ms (+0.67 ms), and the two-period output
buffer from 1.33 to 2.67 ms (+1.33 ms). These are buffer durations, not a measured
analogue round trip. The image fallback remains 32; missing or invalid saved
preferences are reported as that next-boot fallback.

Validation:

- Real device boot at 32: the Launchpad launch API returned HTTP 409 without
  starting the plugin. A direct executable launch did not activate JACK audio
  or call `prepareToPlay` (the UI remained available, with zero audio CPU).
- Saved 64 through the Launchpad API and rebooted through its power API:
  active and saved settings both read 64; normal app launch returned success.
- Release standalone build and two processor/engine regression tests passed.
  A Full-size block restored from a desktop-style state becomes Lite; setters,
  subsequent save/reopen and direct engine size requests remain Lite.
- Launchpad: 36 relevant Python tests and 26 UI unit tests passed. Browser
  tests at 780x360, 800x480 and 1560x720 covered refusal, consent, failed
  saves/restarts, pending reboot, next-boot launch, unavailable settings,
  keyboard navigation and visible warning/refusal controls.

During deployment, restarting `artemis-kiosk.service` left orphaned compositor
and supervisor sessions because the PAM session lives outside the service's
control group. This caused visible cycling and extra audio load. The deployment
was rolled back, and the user's power cycle cleared the orphaned sessions.
The subsequent deployment used Launchpad's `/api/system/power` action
`restart-kiosk`, which shuts down only Launchpad and lets its existing supervisor
restart it. The compositor PID stayed unchanged and one session remained.
Both subsequent full audio-profile reboots completed normally. Do not use a
service restart to reload Launchpad on this device.

Follow-up artifacts are in
`/home/booky/artemis-os/build/output/tone3000-lockdown-20261110/`.
The pre-follow-up deployment backup is `/data/artemis/t3k-lockdown/original/`
(Launchpad archive, plugin binary and saved plugin settings).

The first 300-second follow-up recorded zero device/capture XRUNs (JACK CPU
mean 19.43%, maximum 23.01%). Its analogue returns were silent for the first
approximately 58 seconds, then stable. The cause of that initial silence was
not established, so it is not counted as five minutes of continuous audio.
A second measurement explicitly waits for audible returns before starting
its timer; both recordings are retained in the artifact directory.

The second run detected audible returns after 1.01 seconds and then captured
300.03 seconds: **0 device XRUNs, 0 capture XRUNs**, JACK CPU mean **19.72%**,
maximum **21.84%**. Both outputs remained audible throughout (minimum
one-second RMS -28.03/-27.90 dBFS; spans 0.253 dB), with no clipped or non-finite
samples. The plugin counter also stayed at zero from this boot's startup.
Portable results: `artemis-lockdown-2026-11-10.json`.

Final binary SHA-256:
`16d384deb4eff90f95a9be9d6bae913d9e2f87681d104b0055599a2e5c2371f9`.
Launchpad commit: `c4180ca`. The device remains on native JACK 48 kHz / 64 frames /
two periods, with the original two-NAM-plus-IR chain open and one desktop session.
