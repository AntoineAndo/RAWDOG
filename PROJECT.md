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
   paths on a background thread via a `juce::ThreadWithProgressWindow` subclass
   (`ScanThread`), showing a progress bar/status label/Cancel button so the message
   thread (and native menu bar) never blocks during a scan. Verified against a real
   machine: found 44 plugins (2 user-installed VST3+AU pairs, ~40 Apple built-in AUs).
   - **Scans are cached to disk, not repeated on every launch.** `loadCachedPluginList()`/
     `saveCachedPluginListToDisk()` persist `KnownPluginList::createXml()`/
     `recreateFromXml()` to `~/Library/Application Support/PixelBender/KnownPlugins.xml`.
     `MainComponent`'s constructor tries loading that cache first; only a genuine
     first-ever launch (no cache file yet) falls back to a real scan. "Rescan
     Plugins" always does a real scan regardless, and re-saves the cache afterward —
     this is the only way scanning happens after the first launch, per explicit user
     request to stop rescanning automatically on every open.

### UI (all in `MainComponent`, the single content component of the app window)

- **Plugin list** (left column, `juce::ListBox` + custom `PluginListModel`): white
  text on dark background (default JUCE list styling was unreadable), disabled
  (grayed, non-interactive) until an image is loaded. **Double-clicking a row**
  loads that plugin and immediately opens its editor — there's no separate
  "load"/"open editor" buttons anymore, that was a deliberate UX simplification
  after early feedback.
  - **Favourites, tabs, and search** (`FavouritePluginsStore.h` +
    `PluginListModel` additions in `MainComponent.h`/`.cpp`): each row has a
    24px star column (★ filled/yellow when favourited, ☆ outline/grey
    otherwise), hit-tested in `PluginListModel::listBoxItemClicked` and toggled
    via `FavouritePluginsStore` (a `juce::ApplicationProperties`/`PropertiesFile`-
    backed store, newline-joined identifier strings keyed by
    `PluginDescription::createIdentifierString()`, immediate-write so a toggle
    survives a hard quit). A `PluginFilterTabs` (`juce::TabbedButtonBar`, used
    standalone rather than the heavier `TabbedComponent` since there's one
    shared `ListBox` to filter, not separate content pages per tab) offers
    **All / Favourites / By Vendor** — By Vendor groups `cachedTypes` into
    collapsible accordion sections (`DisplayRow`/`displayRows`, rebuilt by
    `rebuildDisplayRows()`, grouped strictly by iterating the already-filtered
    `cachedTypes` so a vendor with zero search matches simply emits no header —
    no extra filtering logic needed). A live `juce::TextEditor` search box
    filters by name/manufacturer/format via `containsIgnoreCase`, combining
    correctly with whichever tab is active. Favourites persist to disk; search
    text and tab selection and per-vendor expand/collapse state are session-only.
- **Resizable panel layout**: the whole window (list, plugin editor panel,
  image preview, waveform) uses `juce::StretchableLayoutManager` +
  `juce::StretchableLayoutResizerBar` for user-draggable dividers — a new
  pattern for this codebase (previously all layout was fixed `Rectangle::removeFrom*`
  carving). Three small container components hold each split's children and
  own their own layout manager + divider: `LeftColumnPanel` (plugin list
  stacked above the plugin editor panel; the divider only exists/renders when
  a panel is open), `RightColumnPanel` (image preview stacked above the
  waveform section), and `WaveformSectionPanel` (a plain, non-resizable
  sub-layout for the waveform + its zoom controls, ported verbatim from the
  old fixed-pixel layout). `MainComponent` itself splits `leftColumn`/`rightColumn`
  horizontally. **Critical correctness rule, easy to get backwards**:
  `setItemLayout()` is called once per manager (seeding min/preferred/max) —
  never on every `resized()`/`layOutComponents()` call, or a user's drag
  adjustment would snap back on every ordinary window resize. The one
  deliberate exception is re-seeding a column's *preferred* size when a
  genuinely new/different plugin panel opens (`LeftColumnPanel::setEditorPanel()`
  re-seeds item 2 only on a real panel change, and `MainComponent::openEditorClicked()`
  re-seeds the outer left/right split to the new plugin's editor width, capped
  at 50% of the window via a *negative* max value — `juce::StretchableLayoutManager`
  interprets negative min/max as a live-recomputed proportion of current total
  size, not a one-time absolute pixel value, which is what makes the 50% cap
  keep working correctly across window resizes without any extra code).
- **Plugin editor panel** (`MainComponent::PluginEditorPanel`, a nested class in
  `MainComponent.h`): no longer a separate `DocumentWindow` popup — the plugin's
  real editor (`createEditorIfNeeded()`, or `GenericAudioProcessorEditor` as
  fallback for plugins without a custom UI) is embedded in-place, wrapped in a
  `juce::Viewport` (editors vary wildly in size/aspect ratio, so the panel
  scrolls rather than force-resizing the plugin's UI — see the resizable-layout
  bullet above for how the panel's column itself is sized). **"Apply" and
  "Cancel" buttons are docked underneath side by side** (Cancel added after
  Apply-only proved confusing — Cancel discards any unapplied live-preview
  tweaks via `endLivePreviewSession(false)`, mirroring what already happens
  when swapping to a different plugin without applying; both are still the
  only ways to dismiss the panel). While the panel is open, tweaking the plugin's own parameters drives a **live,
  non-destructive preview**: `PluginParameterWatcher` (`PluginParameterWatcher.h`)
  listens via `juce::AudioProcessorListener` and coalesces bursts of parameter
  callbacks (which may arrive off the message thread) through `juce::AsyncUpdater`
  into `MainComponent::refreshLivePreview()`, which reprocesses the working image's
  bytes through `computeProcessedPixelBytes()` and pushes the result into
  `imagePreview`/`waveformView` without touching `workingImage->pixelBytes` or the
  undo stack. `computeProcessedPixelBytes()` calls `plugin.reset()` before each
  pass — a correctness fix: `PluginHost::processWholeBuffer()` never resets a
  plugin's internal DSP state, so re-running it repeatedly against the same source
  bytes (once per knob tweak) would drift for stateful plugins (filters, reverbs)
  without this. Only `applyClicked()` commits the cached `livePreviewBytes` to
  `workingImage` and pushes undo state; swapping/closing the panel any other way
  discards the preview via `endLivePreviewSession(false)`. While a panel is open,
  Load Image / Reset to Original / Rescan Plugins / Undo / Redo are all disabled
  (gated on `pluginEditorPanel != nullptr` in `getMenuForIndex()`/`getCommandInfo()`)
  and re-enable immediately after Apply, since reasoning about those interleaving
  with an uncommitted live session isn't worth the complexity. This was iterated on
  in an earlier, popup-window version of this feature: originally there was no
  in-editor Apply button (users had to close the editor and hit Apply on the main
  window, losing their tweaks in perception even though the plugin instance
  actually kept its parameter state regardless of window visibility), then Apply
  didn't close the window (no feedback that anything happened) — that history is
  why Apply-closes-and-commits remains the one dismiss action today.
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
  - **Resizable/movable selection**: a selection's left/right edges are
    draggable resize handles (small grip marks drawn at each edge, a
    left-right resize mouse cursor on hover, `handleGrabPixels = 6` hit-test
    tolerance), and click-dragging the body of an existing selection (a
    dragging-hand cursor) moves the whole thing while keeping its length —
    both classified in `mouseDown` (`DragMode::resizingLeft/resizingRight/movingSelection`,
    vs. the original `creatingSelection` for a drag on empty waveform) and
    applied in `mouseDrag`. Resize clamps against the fixed opposite edge to
    guarantee at least 1 sample of width (can't invert/collapse the
    selection); move clamps to keep the whole selection within the buffer.
    Every gesture — create, resize, or move — fires `onBeforeSelectionChange`
    once up front, so all three remain a single Undo step and the live
    preview highlight updates during the drag, exactly like creating always did.
- **Selection → image highlight**: `RawImage::toJuceImage()` takes an optional
  `juce::Range<int> highlightByteRange`. **This used to fill every selected
  pixel with a 50% yellow blend**, but that made it impossible to judge a
  plugin's actual live-preview result underneath the tint — it's now a
  **thin (2px) outline around the selection's boundary only**, leaving
  interior pixels completely untinted. The boundary test avoids an
  `O(width × height × thickness²)` per-pixel neighbourhood scan: it
  precomputes each screen row's selected *column* range once
  (`std::vector<juce::Range<int>> rowSelection`, one `getIntersectionWith()`
  call per row, converting a byte-range intersection to a column range via
  integer division by `channels`), then each pixel's border test is an O(1)
  left/right-edge distance check plus an O(`outlineThickness`) loop over
  neighbouring rows for the top/bottom edge — down from
  `O(thickness²)` per pixel to `O(thickness)`, reusing the exact same
  per-pixel BMP/PNM layout loop as before, so the highlight/outline is always
  correctly positioned regardless of format. `toJuceImage()` is now a one-line
  forwarder to `toJuceImageFromBytes(pixelBytes, highlightByteRange)`, which reads
  an explicit `juce::MemoryBlock` instead of always reading `this->pixelBytes` —
  added so the same layout/highlight logic can render an uncommitted live-preview
  buffer (see plugin editor panel above) without ever mutating `pixelBytes` itself.
  `WaveformView::onSelectionChanged` fires on every drag frame and on clear;
  `MainComponent` re-renders the preview (with `resetView=false`) each time —
  routed through `refreshLivePreview()` while a plugin panel is open (so the live
  preview rescopes to the new selection), or `updatePreview()` otherwise. This was
  pulled forward from the original "v2 deferred" list because the mapping turned
  out to be trivial once the toJuceImage() highlight parameter existed.
- **Apply scoping**: if `WaveformView::getSelectionSampleRange()` is non-empty,
  `MainComponent::computeProcessedPixelBytes()` (the shared helper behind both the
  live preview and Apply) copies just that sub-range into a temporary buffer,
  processes it, and copies it back — bytes outside the selection are provably
  untouched (verified via `cmp`/memcmp in a temp harness). No selection ⇒
  whole-buffer apply (original M1 behavior). `applyClicked()` no longer
  recomputes this itself — it commits whatever `livePreviewBytes` was last
  computed by `refreshLivePreview()`, guaranteeing the committed result is
  byte-identical to what was just previewed.
- **Undo/redo**: `std::vector<EditorSnapshot> undoStack`/`redoStack` on
  `MainComponent`, where `EditorSnapshot` bundles `pixelBytes` *and* the waveform
  selection range together — so restoring history restores both the pixels and
  which range was selected, not just the bytes. `pushUndoState()` (called at the
  top of `applyClicked()`/`resetClicked()`, before mutating, plus from
  `WaveformView::onBeforeSelectionChange`) snapshots the current state and clears
  `redoStack`. A selection drag/click counts as exactly one undoable action:
  `WaveformView` fires `onBeforeSelectionChange` once at the start of a gesture
  (`mouseDown`), not per drag frame, so a whole click-drag collapses to a single
  undo entry captured with the pre-gesture state. `undoClicked()`/`redoClicked()`
  swap between the stacks, restoring `pixelBytes` and the selection (via
  `WaveformView::setSelectionSampleRange()`, which does not itself fire
  `onBeforeSelectionChange`). Stack clears on a fresh image load. Cmd+Z / Cmd+Shift+Z
  shortcuts are wired via `juce::ApplicationCommandManager`.
- **Menus**: `MainComponent` implements `juce::MenuBarModel` directly and installs
  itself as the native macOS menu bar (`setMacMainMenu`/`setMacMainMenu(nullptr)` in
  ctor/dtor). Two top-level menus: **File** (Load Image, Export Image, Reset to
  Original, Rescan Plugins — these used to be toolbar buttons, moved to the menu on
  request, which freed up layout space) and **Edit** (Undo, Redo).
  - **Export Image always writes a PNG now**, regardless of whether the image
    was loaded as BMP or PNM — `RawImage::writeToPngFile()` encodes
    `toJuceImage()` (no highlight baked in) via `juce::PNGImageFormat`. This
    replaced the original `writeToFile()`, which wrote the *original* format's
    header+pixel bytes back out verbatim (and the format-matching
    `getExportWildcard()`/`getDefaultExportExtension()` helpers that existed
    solely to keep that verbatim writer's extension honest) — both removed as
    dead code once every export became format-independent PNG.
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
- *(user-requested polish)* — Redo, with undo/redo snapshots now bundling the
  waveform selection alongside pixel bytes; a whole selection drag/click
  collapses to one undo entry via `onBeforeSelectionChange`.
- *(user-requested polish)* — plugin editor embedded in-place (no more popup
  `DocumentWindow`) with live, non-destructive preview as you tweak a
  plugin's own parameters (`PluginParameterWatcher` + `AsyncUpdater` +
  `computeProcessedPixelBytes()`), plus a Cancel button to discard tweaks.
- *(user-requested polish)* — the whole window layout made user-resizable via
  draggable dividers (`juce::StretchableLayoutManager`), replacing fixed
  pixel-based carving.
- *(user-requested polish)* — plugin favourites (★ toggle, persisted), All/
  Favourites/By Vendor tabs, and a live plugin search box.
- *(user-requested polish)* — plugin scan results cached to disk so the app
  doesn't rescan on every launch, only on first-ever run or explicit "Rescan
  Plugins."
- *(user-requested polish)* — waveform selection gained resize handles and
  drag-to-move; the image-preview highlight became a thin outline instead of
  a full yellow tint, so the plugin's actual live-preview result stays
  visible underneath.
- *(user-requested polish)* — Export Image always writes a PNG now, regardless
  of the original loaded format.

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
- **Some plugin editors visually overflow the embedded panel — this is a real
  JUCE/AppKit limitation, not a bug in this codebase, and there is no
  in-app fix.** Confirmed against a real plugin (Apple's `AUBandpass`): editors
  whose UI is a native Cocoa/Metal view get that native `NSView` attached as a
  *sibling of the window's peer content view* by JUCE's native-hosting layer
  (`juce_NSViewComponent_mac.mm`), sized via `ComponentPeer::getAreaCoveredBy()`'s
  full unclipped bounds — it never gets nested inside the `juce::Viewport`'s own
  view hierarchy, so nothing in this app's component tree can clip it. Plugins
  whose editor renders purely through JUCE's own `Graphics` (the generic
  fallback editor, custom-drawn UIs) are unaffected and clip/scroll correctly.
  A real fix would mean patching JUCE's native-view-hosting code itself — out
  of scope. A follow-up attempt to let the user manually shrink an oversized
  editor via +/- zoom buttons (`Component::setTransform()` + a
  `ScaledEditorHolder` wrapper, persisted per-plugin via
  `juce::ApplicationProperties`) was built, tested, and **reverted** — the
  transform only scaled the JUCE-side container, not the native view's actual
  rendered content, so it didn't solve the problem it was meant to solve.
  There is no code for this in the tree; if revisiting, expect the same
  native-hosting root cause to block it again.
