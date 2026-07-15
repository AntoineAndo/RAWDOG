# Pixel Bender — Project Handoff

## What this is

A macOS desktop app for "databending" — treating an image's raw pixel bytes as an
audio buffer, running that buffer through real VST3/AudioUnit plugins, and writing
the glitched bytes back out as a still-valid image. This is the same trick people do
manually in Audacity (import an image as raw µ-law audio, apply effects, export raw),
but built as a dedicated tool: browse installed plugins, tweak them with their real
UI, select a specific byte range to glitch instead of the whole image, and preview
the result live.

Repo: `~/Documents/Projects/pixel-bender` (git, `main` branch, JUCE as a pinned
submodule at `JUCE/`).

## Stack and why

- **JUCE 8.0.14** (git submodule, pinned to the `8.0.14` tag, not `develop`) — the
  standard C++ framework for hosting VST/AU plugins with a real GUI. Chosen over a
  Python prototype (e.g. `pedalboard`) because we need live plugin editor windows,
  not just offline processing.
- **CMake** (JUCE's native CMake integration, no `.jucer`/Projucer file) — plain-text
  project, diffable, no GUI project generator step.
- **macOS only**, VST3 + AudioUnit hosting (`JUCE_PLUGINHOST_VST3=1`,
  `JUCE_PLUGINHOST_AU=1`). No Windows/VST2 support — VST2 SDK is deprecated and
  wasn't worth the licensing hassle for this project.
- Target: `PixelBender` (a `juce_add_gui_app`). Building produces
  `build/PixelBender_artefacts/Debug/Pixel Bender.app`.

## Build & run

```bash
cmake -B build -G Xcode          # configure (only needed after CMakeLists.txt changes)
cmake --build build --config Debug --target PixelBender
open "build/PixelBender_artefacts/Debug/Pixel Bender.app"
```

To see stdout/DBG output live, run the binary directly instead of `open`:
```bash
"build/PixelBender_artefacts/Debug/Pixel Bender.app/Contents/MacOS/Pixel Bender"
```

No test suite exists. Verification so far has been manual (the human tester) plus
several **temporary CLI harnesses** (`Source/VerifyM1.cpp`, `VerifyM2.cpp`,
`VerifyHighlight.cpp`) that were added to `CMakeLists.txt`, built, run once to prove
a specific behavior via `cmp`/pixel-diff checks, then deleted along with their
CMakeLists entries. None of them exist in the tree today — if you need to verify a
new byte-level invariant (e.g. "only the selected range changed", "header bytes
untouched"), recreate this pattern rather than trying to test through the GUI.

## Architecture

### The core databending pipeline

1. **`RawImage`** (`RawImage.h/.cpp`) — loads/saves **24-bit uncompressed BMP** and
   **raw PNM (P5/P6)**. On load it splits the file into two `juce::MemoryBlock`s:
   `headerBytes` (kept byte-for-byte untouched — this is the "protected region" from
   the original Audacity technique, whatever a real image viewer needs to parse the
   file) and `pixelBytes` (the only thing a plugin is ever allowed to touch). BMP
   parsing follows `bfOffBits` from the file header rather than assuming a fixed 54
   bytes, so it's correct even with unusual DIB header variants. `toJuceImage()`
   renders the current `pixelBytes` back into a displayable `juce::Image`, handling
   BMP's bottom-up-row/BGR-order quirks vs PNM's top-down/RGB. It also accepts an
   optional highlight byte-range (see below).
   - TIFF is **not** supported — deliberately deferred (see Deferred Work). BMP/PNM
     were chosen because their headers are simple/fixed-size, so "protected region"
     is trivial to define correctly, unlike TIFF's IFD/strip-offset structure.

2. **`SampleFormat.h`** — the byte↔float bridge. **Currently hardcoded to 8-bit
   unsigned PCM**: `float = (byte - 128) / 128`. This is the single biggest piece of
   unfinished scope — see M3 below. A `WaveformView.h` comment and this doc both
   flag the assumption "sample index == byte index," which breaks the moment a
   multi-byte sample format is added.

3. **`PluginHost`** (`PluginHost.h/.cpp`) — thin wrapper: instantiate an
   `AudioPluginInstance` from a `PluginDescription` (via
   `AudioPluginFormatManager::createPluginInstance`, the synchronous API — JUCE 8
   also has an async variant, not used here), then `processWholeBuffer()` runs a
   mono `AudioBuffer<float>` through it in `blockSize`-sized chunks, upmixing to
   however many channels the plugin actually wants (`jmax(2, in, out)`) by
   duplicating the mono signal, then reading channel 0 back out.

4. **`PluginScanner`** (`PluginScanner.h/.cpp`) — wraps
   `AudioPluginFormatManager` + `KnownPluginList`, scans the default VST3/AU search
   paths. Verified against a real machine: found 44 plugins (2 user-installed VST3+AU
   pairs, ~40 Apple built-in AUs).

### UI (all in `MainComponent`, the single content component of the app window)

- **Plugin list** (left column, `juce::ListBox` + custom `PluginListModel`): white
  text on dark background (default JUCE list styling was unreadable), disabled
  (grayed, non-interactive) until an image is loaded. **Double-clicking a row**
  loads that plugin and immediately opens its editor — there's no separate
  "load"/"open editor" buttons anymore, that was a deliberate UX simplification
  after early feedback.
- **Plugin editor window** (`MainComponent::PluginWindow` /
  `EditorWithApplyButton`, nested classes in `MainComponent.h`): a `DocumentWindow`
  wrapping the plugin's real editor (`createEditorIfNeeded()`, or
  `GenericAudioProcessorEditor` as fallback for plugins without a custom UI), with
  an **"Apply" button docked underneath**. Clicking Apply runs the effect and closes
  the editor window — this was also iterated on: originally there was no in-editor
  Apply button (users had to close the editor and hit Apply on the main window,
  losing their tweaks in perception even though the plugin instance actually kept
  its parameter state regardless of window visibility), then Apply didn't close the
  window (no feedback that anything happened). Current behavior is the result of
  two rounds of user feedback.
- **`ZoomableImageView`** (`ZoomableImageView.h/.cpp`) — the image preview. Mouse
  wheel zooms centered on the cursor (standard "zoom to point" math: capture the
  image-space point under the cursor, change scale, recompute offset so that same
  point stays under the cursor), click-drag pans, double-click resets to
  fit-the-viewport. Takes a `resetView` bool on `setImage()` — `true` only on a
  genuinely new image (load/Reset), `false` on in-place refreshes (Apply, selection
  highlight redraw) so the user's zoom/pan isn't yanked out from under them
  mid-workflow. This same `resetView` pattern is used in `WaveformView` for the
  identical reason — **it's a recurring design idiom in this codebase**: any view
  that redraws frequently for cosmetic reasons (selection highlight, apply-in-place)
  must not reset user-adjusted view state unless the underlying data's *identity*
  (not just its bytes) genuinely changed.
- **`WaveformView`** (`WaveformView.h/.cpp`) — renders the pixel buffer as a
  min/max-per-column waveform. Click-drag selects a **sample range** (which, given
  the current fixed 8-bit-PCM format, is numerically identical to a **byte range**
  in `pixelBytes` — this equivalence is called out in comments and will need
  revisiting when M3 lands). Has its own vertical zoom (a pure display-amplitude
  multiplier, clipped to ±1 — doesn't touch underlying data) and horizontal
  zoom+scroll (a `juce::Slider` + `juce::ScrollBar`, wired via
  `WaveformView::onViewChanged` callback so `MainComponent::syncScrollBarToView()`
  keeps the scrollbar's thumb size/position in sync with the view window).
  - Horizontal zoom was added after user feedback that vertical zoom alone
    couldn't make the waveform legible: image byte data tends to sit in big flat
    runs near the amplitude extremes (bright/dark regions), so vertical zoom just
    clips harder without revealing structure. Horizontal zoom/scroll (narrowing the
    visible sample window) is what actually helps you find precise selection
    boundaries.
- **Selection → image highlight**: `RawImage::toJuceImage()` takes an optional
  `juce::Range<int> highlightByteRange` and tints matching pixels toward yellow
  (`Colour::interpolatedWith(yellow, 0.5f)`), reusing the exact same per-pixel loop
  that already handles BMP/PNM layout differences — so the highlight is always
  correctly positioned regardless of format. `WaveformView::onSelectionChanged`
  fires on every drag frame and on clear, and `MainComponent` re-renders the
  preview (with `resetView=false`) each time. This was pulled forward from the
  original "v2 deferred" list because the mapping turned out to be trivial once the
  toJuceImage() highlight parameter existed.
- **Apply scoping**: if `WaveformView::getSelectionSampleRange()` is non-empty,
  `MainComponent::applyClicked()` copies just that sub-range into a temporary
  buffer, processes it, and copies it back — bytes outside the selection are
  provably untouched (verified via `cmp`/memcmp in a temp harness). No selection ⇒
  whole-buffer apply (original M1 behavior).
- **Undo**: a plain `std::vector<juce::MemoryBlock> undoStack` on `MainComponent`.
  `pushUndoState()` (called at the top of both `applyClicked()` and
  `resetClicked()`, before mutating) pushes a copy of the current `pixelBytes`.
  `undoClicked()` pops and restores. Linear, no redo — wasn't asked for. Stack
  clears on a fresh image load. **No keyboard shortcut** — explicitly requested by
  the user to skip that for now.
- **Menus**: `MainComponent` implements `juce::MenuBarModel` directly and installs
  itself as the native macOS menu bar (`setMacMainMenu`/`setMacMainMenu(nullptr)` in
  ctor/dtor). Two top-level menus: **File** (Load Image, Export Image, Reset to
  Original, Rescan Plugins — these used to be toolbar buttons, moved to the menu on
  request, which freed up layout space) and **Edit** (Undo).
  - **Gotcha already hit and fixed**: the native macOS menu does *not* reliably
    re-invoke `getMenuForIndex()` just because a submenu is reopened. Item
    enablement (grayed-out state) goes stale after the first render unless you
    explicitly call `MenuBarModel::menuItemsChanged()` whenever state that affects
    a menu item changes. This is called after `pushUndoState()`, after `undoClicked()`,
    and after a new image loads. If you add more menu items with dynamic
    enablement, you **must** remember this call or they'll silently never update —
    this was debugged via temporary `DBG()` logging that proved
    `getMenuForIndex(Edit)` was invoked exactly once at startup and never again
    despite the user reopening the menu repeatedly after Apply.
- **Window**: resizable (`setResizable(true, true)`, 700×500 minimum) with native
  macOS fullscreen support — the green traffic-light button only appears when a
  window has both the maximize-button style flag and is resizable, which is why
  both had to be set together (`Main.cpp`).

## Milestones shipped (see git log for exact commits)

- **M0** — CMake+JUCE scaffold, plugin scanning/listing only.
- **M1** — Image⇄audio bridge, whole-buffer apply, static preview. Fixed 8-bit PCM.
- **M2** — Waveform view, click-drag time-range selection, scoped apply.
- *(unplanned, pulled forward from "v2 deferred")* — pixel-highlight overlay for
  the current selection.
- *(user-requested polish, not in original milestone plan)* — resizable/fullscreen
  window, zoomable/pannable image preview, File/Edit native menu bar, Undo.

## What's NOT done yet (planned)

From the original incremental plan (`~/.claude/plans/quirky-hugging-scroll.md` if
still present on this machine — otherwise this section is the source of truth now):

- **M3 — sample-format dropdown.** Replace the hardcoded 8-bit-unsigned-PCM
  assumption in `SampleFormat.h` with a dropdown offering µ-law, A-law, 8-bit,
  16-bit signed/unsigned, and byte order — matching Audacity's raw-import dialog.
  **This is the most architecturally significant remaining task**: once a sample
  format can be >1 byte, the "sample index == byte index" assumption baked into
  `WaveformView`'s selection range, `RawImage`'s highlight range, and
  `MainComponent::applyClicked()`'s byte-range copy all need a conversion layer
  (samples × bytesPerSample = byte offset). Search for the comment "map 1:1 onto
  byte offsets" in `WaveformView.h` and similar notes in `RawImage.h` — those are
  the spots that assume the current fixed format.
- **M4 — real spectrogram.** Replace/augment the waveform with an STFT-based
  time×frequency heatmap (`juce::dsp::FFT` is already linked in via
  `juce::juce_dsp`, unused so far). Same time-range selection semantics as the
  waveform. This was the original "spectrum at the bottom" vision from the very
  first conversation about this project.

## Deferred to a hypothetical v2 (explicitly out of scope, per original plan)

- **Frequency-band selection** (isolate a band within a time range, not just full-
  spectrum time selection) — needs STFT split/recombine, real DSP complexity.
- **Plugin chaining/rack** (multiple plugins in sequence) — current UX is
  one-plugin-at-a-time (apply, inspect, apply again manually).
- **TIFF support** — only worth adding once a real TIFF library (e.g. libtiff)
  manages the fragile IFD/strip-offset header, exposing just a pixel-data pointer
  to the same pipeline that already treats header-vs-pixel-bytes as protected vs.
  bendable.
- **Presets, batch processing.**
- Pixel-highlight overlay was *also* on this deferred list originally but got
  built already (see above) — the sample-index→pixel-index mapping turned out to
  be simple enough to pull forward.

## Known rough edges / things a future agent should be aware of

- `createEditorIfNeeded()` is deprecated in JUCE 8 in favor of
  `createEditorAndMakeActive()` — there's a standing compiler warning about this in
  `MainComponent.cpp`. Not fixed because the replacement changes ownership
  semantics slightly and wasn't worth the risk mid-feature-work; worth revisiting.
- `RawImage`'s `MemoryBlock::replaceWith()` calls are also flagged deprecated by
  JUCE 8 (prefers `replaceAll`) — same story, not yet addressed, cosmetic only.
- No automated tests exist. The verification pattern used throughout (temporary
  console-app CMake targets that exercise the same production classes headlessly,
  assert byte-level invariants via `memcmp`, then get deleted) is documented above
  under Build & run — reuse it for anything that needs proof beyond visual
  inspection, since driving the GUI programmatically (mouse drag, menu clicks,
  screenshots) was not available in this environment (no screen-recording
  permission was granted to the terminal).
- The plugin list, waveform controls, and Export/Reset menu items are all
  disabled/enabled based on `workingImage != nullptr` — if you add a new
  image-dependent control, wire it into `updatePluginListEnablement()` (misnamed
  now — it does more than the plugin list) or a similarly named successor.
- `sampleRate` (44100.0) and `blockSize` (512) in `MainComponent` are fixed
  constants chosen to match typical Audacity raw-import defaults for consistency
  with the manual technique — not derived from anything, since there's no real
  "audio" here, just bytes being pushed through a plugin's `processBlock`.
