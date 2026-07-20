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

1. **`RawImage`** (`RawImage.h/.cpp`) — loads/saves **24-bit uncompressed BMP**,
   **raw PNM (P5/P6)**, and loads **PNG** (any depth/colour type JUCE's own decoder
   handles, normalised to 8-bit RGB or RGBA) and **JPEG** (always 3-channel RGB —
   JPEG has no alpha channel). On load it splits the file into two
   `juce::MemoryBlock`s: `headerBytes` and `pixelBytes` (the only thing a plugin is
   ever allowed to touch). BMP parsing follows `bfOffBits` from the file header
   rather than assuming a fixed 54 bytes, so it's correct even with unusual DIB
   header variants. `toJuceImage()` renders the current `pixelBytes` back into a
   displayable `juce::Image`, handling BMP's bottom-up-row/BGR-order quirks vs
   PNM/PNG/JPEG's top-down/RGB(A). It always renders plain — the selection
   highlight is drawn separately, as a line overlay by the viewing component (see
   "Selection → image highlight" below).
   - TIFF is **not** supported — deliberately deferred (see Deferred Work). BMP/PNM
     were chosen because their headers are simple/fixed-size, so "protected region"
     is trivial to define correctly, unlike TIFF's IFD/strip-offset structure.
   - **PNG loading** (`RawImage::loadPng()`) decodes via `juce::ImageFileFormat`
     (the same codec `writeToPngFile()` already used for export) and re-packs the
     result into a fresh interleaved RGB or RGBA buffer — `headerBytes` stays empty,
     since PNG's chunked/DEFLATE-compressed layout has no simple fixed-offset
     "protected region" the way BMP/PNM have (same reasoning as the TIFF deferral
     above). Whether the source actually has an alpha channel is read directly from
     the file's IHDR colour-type byte (a fixed offset, not general chunk-walking) —
     deliberately not from the decoded `juce::Image`'s own `hasAlphaChannel()`, which
     macOS's `CoreGraphicsImageType` silently reports as true for every decoded
     image regardless of the source. A loaded PNG with real alpha is `Channel::alpha`,
     a 4th channel plane alongside red/green/blue — `RawImage::hasChannelPlanes()`
     now covers 3- *or* 4-channel images, and `hasAlphaChannel()` is the single
     source of truth the UI gates the 4th waveform lane on (hidden for every other
     format/case) — it deliberately also checks `getFormat() == Format::png`, not
     just `channels == 4`, since a BMP header-edited to `biBitCount == 32`
     (`validateBmpHeaderFields()` only warns on this, never blocks it) also
     re-derives `channels` to 4 without that being real alpha. `writePixelRows()`
     (behind `toJuceImage()`/export) must premultiply before writing into
     `juce::PixelARGB` — that class's storage is documented as premultiplied, and
     its `setARGB()` is a raw setter that does no premultiply math itself, so
     writing straight RGBA through it un-premultiplies a second time everywhere
     downstream (PNG export, on-screen compositing) without the explicit
     `premultiply()` call after `setARGB()`. Known, accepted precision tradeoff
     (distinct from the premultiply bug above, which is a correctness fix, not a
     tradeoff): an RGBA source's colour channels (not alpha itself) round-trip
     through `juce::Image::ARGB`'s premultiplied 8-bit storage on *load*, which
     loses up to ±1-2/255 at low alpha — not worth a hand-rolled non-premultiplied
     PNG scanline decoder for this.
   - **JPEG loading** (`RawImage::loadJpeg()`) is the same decode-and-repack shape
     as PNG loading, minus the alpha-detection step (JPEG never has alpha, so this
     is always 3-channel RGB) — both share `packImageToInterleavedBytes()`, the
     pixel-by-pixel `juce::Image::BitmapData::getPixelColour()` packing loop
     factored out once both formats needed it. Dispatch in `loadFromFile()` sniffs
     the SOI marker (`0xFF 0xD8`, present at the start of every JPEG variant —
     JFIF, EXIF, etc.) the same way PNG's signature and BMP's `"BM"` are sniffed.
   - **Fujifilm RAF and Adobe DNG camera-raw files *are* supported — but not by
     `RawImage` itself.** `RawCameraConverter.h/.cpp` sniffs for these formats
     (RAF's literal `"FUJIFILMCCD-RAW"` magic; DNG's/any TIFF's `II*\0`/`MM\0*`
     magic — DNG is TIFF-based, and confirming "specifically DNG" vs. "any
     TIFF-based raw" would need the exact IFD-tag parsing this project avoids
     project-wide, so this converter opportunistically also handles other
     TIFF-based camera raws) and, before `RawImage::loadFromFile()` ever runs,
     decodes the file via macOS's system ImageIO/`CGImageSource` API (backed by
     `RawCamera.bundle`, the same decoder Preview.app/QuickLook use) and writes
     the result out as a synthesized 24-bit BMP to a `juce::TemporaryFile`,
     which is then handed to the same unmodified `loadFromFile()` every other
     BMP goes through. `RawImage`'s own parsing has zero knowledge of raw
     formats. This gives the camera's/OS's demosaiced, colour-processed RGB
     image — not the raw sensor mosaic bytes — with no exposure/white-balance
     control; RAW format coverage depends on the installed `RawCamera.bundle`
     version. Wired into `MainComponent::loadImageClicked()`
     (`MainComponentImageIO.cpp`), which widens its FileChooser wildcard and
     detects/converts before the normal load path.
   - **`headerBytes` is no longer fully opaque for BMP.** It originally was —
     "the protected region from the original Audacity technique, kept byte-for-byte
     untouched." Two features since punched through that: BMP header editing (below)
     lets a user rewrite the 5 structural fields that drive this app's own decoding,
     and BMP bit-depth conversion (below) discards the original header outright and
     synthesizes a fresh one. `headerBytes` is still never touched for PNM, and for
     BMP it's still never touched by the plugin-apply pipeline — only by these two
     deliberate, user-initiated rewrite paths.

   - **BMP header editing** (`getBmpHeaderFields()`, `validateBmpHeaderFields()`,
     `applyBmpHeaderFields()`, `restoreSnapshot()`, `moveHeaderPixelBoundary()`; UI in
     `HeaderEditorPanel.h`/`MainComponentHeaderEditor.cpp`; "Edit Header..." in the
     File menu). Exposes all 16 documented `BITMAPFILEHEADER`+`BITMAPINFOHEADER`
     fields for **read-only display** (`BmpHeaderFields`, parsed fresh from
     `headerBytes` on every call — no cached state to drift), but only the 5 that
     actually drive this app's own decode/render logic are **editable**
     (`BmpEditableHeaderFields`: `bfOffBits`/`biWidth`/`biHeight`/`biBitCount`/
     `biCompression`). Validation is two-tier: blocking errors for genuine crash/UB
     risk (bad dimensions, an offset that would truncate the 54-byte header
     `applyBmpHeaderFields()` is about to serialize), warnings-only for "this will
     look wrong but won't crash" (non-24-bit depth, non-zero compression) — this is
     a *glitch-art tool*, so letting the header lie on purpose is often the point.
     Editing `bfOffBits` moves the `headerBytes`/`pixelBytes` boundary via
     `moveHeaderPixelBoundary()`, re-labelling bytes between the two blocks (growing
     the offset pulls former leading pixel bytes into the header, and vice versa)
     without mutating any byte's content — `headerBytes.size() + pixelBytes.size()`
     is conserved, so grow-then-shrink-back round-trips losslessly. The panel follows
     the same live-preview/Apply/Cancel shape as the plugin editor (below): edits
     apply live to a scratch `RawImage` copy for preview, only committing to the real
     `workingImage` (and pushing an undo entry) on Apply.
   - **Per-channel plane API** (`Channel` enum, `hasChannelPlanes()`,
     `getChannelPlane()`, `applyChannelBytes()`, `previewWithChannelBytes()`,
     `setPixelBytes()`; UI in `WaveformSplitPanel.h` + the `WaveformSectionPanel`/
     `RightColumnPanel` split-toggle extension). Lets a 3- or 4-channel chunky image
     (BMP 24-bit, PNM P6, or PNG — `channels == 3 || channels == 4`) be viewed and
     edited **per color channel** instead of only as one interleaved byte stream.
     `getChannelPlane(Channel)` lazily deinterleaves `pixelBytes` into up to 4
     one-byte-per-pixel buffers in visual top-down row-major order (index `i` ==
     pixel `(i % width, i / width)` — deliberately *not* `pixelBytes`' own possibly-
     bottom-up/padded row order, so a plane's layout doesn't depend on `bottomUp`),
     cached and invalidated via a `planesDirty` flag rather than recomputed on every
     edit — deinterleaving a 1080p image is a few-ms linear pass, cheap once but
     wasteful if paid on every edit whether or not split view is even open.
     `applyChannelBytes()` re-interleaves one edited channel back into `pixelBytes`
     and updates *only that channel's* cache entry, leaving the others valid — the
     entire point of the lazy design. `setPixelBytes()` is a new explicit setter
     replacing a direct `pixelBytes = ...` field assignment that used to exist in
     `endLivePreviewSession()`, so plane-cache invalidation can never be silently
     bypassed. Editing is **fully per-channel**: a selection on one channel's
     waveform lane can drive Apply, processing just that channel's plane through the
     plugin and re-interleaving the result — see `MainComponent::SelectionScope`/
     `getCurrentSelectionScope()` below. The image-preview highlight outline uses a
     second `toJuceImage()`/`toJuceImageFromBytes()` overload for this case, colored
     to match the active channel (red/green/blue/white-for-alpha) instead of the
     fixed yellow used for a whole-buffer/interleaved selection. The 4th (alpha)
     lane only ever appears for a loaded PNG with a real alpha channel
     (`hasAlphaChannel()`) — hidden, not just empty, for every other case.
   - **Non-24bpp BMP support via load-time conversion** (`loadBmp()`'s
     `isPalette`/`is32Rgb`/`is32Bitfields` paths). Rather than teaching every
     downstream subsystem (waveform, channel planes, header editing, the apply
     pipeline) about other bit depths, 1bpp/4bpp/8bpp (palette-indexed) and 32bpp
     (`BI_RGB` implicit BGRX, or `BI_BITFIELDS` explicit per-channel bitmasks) are
     **expanded into an ordinary 24-bit BGR buffer once, at load time** — everything
     downstream then sees exactly what it would for a native 24-bit file, with zero
     special-casing. Palette formats look the color table up (immediately after the
     40-byte `BITMAPINFOHEADER`, `biClrUsed` or `2^bitCount` entries × 4 bytes BGRX,
     defaulted to a full-size black-filled table so an out-of-range index can never
     go out of bounds); `BI_BITFIELDS` extracts each channel generally via its mask's
     lowest set bit (shift) and popcount (bit width), normalizing to 8 bits rather
     than assuming byte-aligned masks. Because the original header no longer
     describes the expanded pixel data, it's discarded and a fresh, self-consistent
     54-byte header is synthesized (`buildBmp24Header()`) instead of being patched.
     Only the classic 40-byte `BITMAPINFOHEADER` is supported for the palette/mask
     lookup offset — a file using the larger `BITMAPV4HEADER`/`V5HEADER` variants
     (common from modern screenshot tools for `BI_BITFIELDS`) is rejected with a
     clear error rather than silently misreading unrelated bytes as a palette/masks.
     **1bpp/4bpp were deliberately not given native (unconverted) support** — packing
     multiple pixels per byte fundamentally conflicts with this app's whole
     "byte is the smallest independently-glitchable unit" model (waveform selection,
     `SampleFormat`, channel planes, header-editing offsets all assume it) — a
     one-byte glitch would flip up to 8 *unrelated* pixels to arbitrary palette
     colors at once, producing blocky corruption rather than the smooth
     channel-level artifacts the rest of this tool aims for. Converting first sidesteps
     this entirely: there's no sub-byte anything left once expanded. **16bpp is not
     supported** (not implemented, no format-detection path exists for it).

2. **`SampleFormat.h`** — the byte↔float bridge. **Fixed 8-bit PCM**, with a choice
   of mapping selected per-image via `SampleFormat::Mode`/`RawImage::getSampleMode()`/
   `setSampleMode()` (defaulting to bipolar): **bipolar** (`float = (byte-128)/128`,
   byte 128 = silence — correct for real audio) or **unipolar** (`float = byte/255`,
   byte 0 = silence — correct for image intensity, see the bipolar/unipolar
   milestone entry below for why). Still only 8-bit, though — multi-byte sample
   formats remain the single biggest piece of unfinished scope, see M3 below. A
   `WaveformView.h` comment and this doc both flag the assumption "sample index ==
   byte index," which breaks the moment a multi-byte sample format is added.

3. **`PluginHost`** (`PluginHost.h/.cpp`) — thin wrapper: instantiate an
   `AudioPluginInstance` from a `PluginDescription` (via
   `AudioPluginFormatManager::createPluginInstance`, the synchronous API — JUCE 8
   also has an async variant, not used here), then `processWholeBuffer()` runs a
   mono `AudioBuffer<float>` through it in `blockSize`-sized chunks, upmixing to
   however many channels the plugin actually wants (`jmax(2, in, out)`) by
   duplicating the mono signal, then reading channel 0 back out.
   - `processWholeBuffer()` also takes an optional `beforeBlock` callback
     (default `nullptr`, preserving the old behavior exactly), invoked with
     each block's 0-based starting sample offset right before that block is
     processed — the hook parameter automation (see the UI section below) uses
     to sweep a plugin parameter's value across the buffer instead of holding
     it static for the whole pass.

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
   - **Crash recovery via dead man's pedal.** Some third-party plugins (observed:
     MeldaProduction VST3s — a race between the plugin's internal `OnTimer()` and
     JUCE destroying the probed instance mid-scan) crash the whole app when
     `PluginDirectoryScanner` probes them. `ScanThread` passes a real dead man's
     pedal file (`~/Library/Application Support/PixelBender/DeadMansPedal.txt`,
     not `juce::File()`) to the scanner, so a crash mid-probe is recorded there;
     the next scan sees the still-present entry and blacklists that plugin instead
     of re-probing it — turning an infinite crash-loop-on-rescan into a one-time
     skip. Skipped plugins are diffed from `KnownPluginList`'s blacklist
     before/after the scan (not `PluginDirectoryScanner::getFailedFiles()`, which
     deliberately excludes dead-man's-pedal skips) and surfaced via
     `PluginScanner::getLastSkippedCrashers()` in the post-scan status message.

### UI (`MainComponent`, the single content component of the app window, plus its
extracted collaborators)

`MainComponent.h`/`.cpp` used to hold everything below directly (611/645 lines,
mixing the plugin list, the menu bar, plugin-hosting lifecycle, image I/O, and the
resizable-layout panel classes in one file) — it's since been split by concern,
purely as a structural refactor with no behavior change:
- **`PluginListModel.h/.cpp`** — the plugin list box model (vendor grouping,
  favourites, search), unchanged, just no longer nested inside `MainComponent`.
- **`MainMenuModel.h/.cpp`** — the native File/Edit menu bar (`juce::MenuBarModel`),
  talking back to `MainComponent` through a small `Callbacks` struct of
  `std::function`s (state queries + action callbacks + a `populateEditMenu` hook so
  `MainMenuModel` never needs to know `MainComponent`'s `undoCommand`/`redoCommand`
  IDs). `MainComponent` now owns a `MainMenuModel menuModel` member instead of
  implementing `juce::MenuBarModel` itself, and calls `menuModel.menuItemsChanged()`
  wherever it used to call `menuItemsChanged()` on itself.
- **`PluginEditorPanel.h`, `LeftColumnPanel.h`, `WaveformSectionPanel.h`,
  `RightColumnPanel.h`** — the nested layout/panel classes described below, each
  now its own header (all still fully inline, as they were before extraction — none
  needed a `.cpp`). Pure lift-and-shift: no external file referenced these by name,
  so there were no call sites to update.
- `MainComponent`'s own remaining logic is still one class, but its method
  *definitions* are now spread across three `.cpp` files by concern rather than one
  645-line file: `MainComponent.cpp` (ctor/dtor, `resized()`, plugin-scan
  orchestration, undo/redo, `ApplicationCommandTarget`), `MainComponentPluginEditor.cpp`
  (plugin hosting + live-preview lifecycle — `loadAndOpenPlugin` through
  `endLivePreviewSession`), and `MainComponentImageIO.cpp` (image load/export +
  preview/waveform refresh glue). This was a deliberate lighter-weight choice over
  extracting the plugin-hosting lifecycle into its own owning class: that logic is
  the most state-entangled part of the app (undo stack, live-preview bytes, waveform
  selection, and image bytes all interact directly), so splitting *definitions*
  without introducing new ownership/callback plumbing was the lower-risk move.

- **Plugin list** (left column, `juce::ListBox` + custom `PluginListModel`): white
  text on dark background (default JUCE list styling was unreadable), disabled
  (grayed, non-interactive) until an image is loaded. **Double-clicking a row**
  loads that plugin and immediately opens its editor — there's no separate
  "load"/"open editor" buttons anymore, that was a deliberate UX simplification
  after early feedback.
  - **Favourites, tabs, and search** (`FavouritePluginsStore.h` +
    `PluginListModel.h/.cpp`): each row has a
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
- **Plugin editor panel** (`PluginEditorPanel.h` — its own header now, see the file
  layout note at the top of this section): no longer a separate `DocumentWindow`
  popup — the plugin's
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
- **Parameter automation / fade in-out ramps** (`ParameterAutomation.h`,
  `ParameterAutomationPanel.h`; a new "Editor"/"Automation" tab strip at the top
  of `PluginEditorPanel`). Born from a real limitation: applying a plugin to a
  selection was all-or-nothing — static parameter values for the whole scope, a
  hard cut at the selection's edges. This lets one or more of the plugin's own
  parameters ramp from an initial value to a target value across the scope
  instead, so an edit can fade in/out rather than snap on/off. Deliberately
  scoped to the single currently-loaded plugin (no multi-plugin chaining — see
  Deferred below) and to *this* image's selection/byte-position axis, not a
  separate animation timeline — an earlier direction (an Ableton-style
  automation graph driving a rendered frame sequence exported as an animated
  GIF) was explored and abandoned once the actual need turned out to be "smooth
  the edges of an existing Apply," not "export a new artifact."
  - **Data model** (`ParameterAutomation.h`, header-only): a `RampSegment` is
    `{ startFraction, endFraction, initialValue, targetValue, easing }` — the
    first two are 0..1 fractions of whichever scope is being processed (the
    selection sub-range, or the whole buffer when there's no selection), so a
    ramp automatically rescales if the selection is resized, rather than being
    pinned to an absolute duration. A `ParameterAutomation` bundles a target
    `parameterIndex`, an `originalValue` (captured when the automation is
    added, restored if it's removed — see gotcha below), and an ordered list
    of `RampSegment`s. `evaluateAt(timeMs, totalScopeMs)` holds at the first
    segment's `initialValue` before it starts, interpolates (with one of 4
    `Easing` curves: linear/easeIn/easeOut/easeInOut) inside a segment, holds
    at the *previous* segment's `targetValue` in any gap between segments, and
    holds at the last segment's `targetValue` after it ends — this is what
    turns "one fade-in segment + one fade-out segment" into a fade-in/sustain/
    fade-out envelope without needing a separate "sustain" concept.
  - **Evaluated per audio block, not once per pass.** `PluginHost::
    processWholeBuffer()`'s `beforeBlock` hook (above) is where
    `MainComponent::computeProcessedPixelBytes()` evaluates every active ramp
    and writes it via `AudioProcessorParameter::setValueNotifyingHost()` — so a
    parameter can genuinely sweep across the selection instead of the plugin
    holding one static value for the whole pass. `totalScopeMs` (the fraction
    denominator) is computed once per call from whichever buffer is about to
    be processed (selection length or whole-buffer length), so both the
    selection-scoped and whole-buffer branches share one `beforeBlock` closure.
  - **Feedback-loop guard.** Ramp-driven `setValueNotifyingHost()` calls are
    programmatic, but `PluginParameterWatcher` (attached for live-preview
    refresh) can't tell that apart from a real knob-drag — left unguarded, each
    ramp-driven write would fire the watcher, which schedules another
    `refreshLivePreview()`, which reprocesses the same deterministic ramps and
    ends on the same call, forever. Originally guarded by `ScopedWatcherPause`,
    which fully detached the watcher for the duration of the whole (then-
    synchronous, message-thread) processing call — safe at the time because
    the message thread was blocked for that entire call, so no real concurrent
    gesture could be missed. Once processing moved to `LivePreviewWorker`'s
    background thread (see "Live-preview performance" below), a real knob-drag
    can genuinely happen *concurrently* on the message thread while a ramp
    evaluates on the worker thread, and a full detach would have wrongly
    swallowed it. Replaced with `PluginParameterWatcher::ScopedSelfWriteSuppression`,
    a `thread_local` depth counter checked at the top of
    `audioProcessorParameterChanged()`/`audioProcessorChanged()`, wrapped only
    around the two `setValueNotifyingHost()` calls inside the ramp-evaluation
    `beforeBlock` lambda (now running on the worker thread). Since ramp writes
    and real gestures now run on genuinely different threads, a per-thread flag
    correctly distinguishes "my own automated write" from a real gesture with
    no detach/reattach dance — and as a side effect, removes the old
    reentrancy risk entirely (a worker-thread ramp write can no longer trigger
    `onPluginParametersChanged` at all, on any thread).
  - **UI** (`ParameterAutomationPanel.h`): a small `TabbedButtonBar` ("Editor" /
    "Automation") added to `PluginEditorPanel` swaps its `Viewport` between the
    native plugin editor and this panel — Apply/Cancel stay docked at the
    bottom regardless of which tab is showing, since Apply always commits
    whatever the live preview currently reflects. "+ Add automated parameter"
    opens a popup of the plugin's own parameters (excluding ones already
    added); each automated parameter gets a block with a "Remove parameter"
    button and a "+ segment" button. Each segment row is two-tiered so its
    range control gets the full row width: a `juce::Slider::TwoValueHorizontal`
    range slider on top (with a custom per-instance `LookAndFeel_V4` override —
    `SegmentRow::RangeSliderLookAndFeel`, applied only to that one `Slider` via
    `setLookAndFeel()`, not installed globally — drawing the two handles in
    distinct colors, since JUCE's default two-value-slider drawing uses the
    same `thumbColourId` for both, differing only by pointer direction) plus a
    live "X–Y%" readout, then initial value / target value / easing / remove
    underneath. `ParameterAutomationPanel` owns the
    `std::vector<ParameterAutomation>` directly (the single source of truth —
    `MainComponent` reads it via `PluginEditorPanel::getParameterRamps()`
    rather than keeping its own copy), so ramps are automatically discarded
    whenever the panel is (Apply or Cancel). Edits are debounced through the
    same `AsyncUpdater`/`triggerAsyncUpdate()` idiom already used for
    parameter-change bursts and selection-drag, so a burst of field edits
    collapses to one live-preview refresh per event-loop turn rather than one
    per keystroke.
  - **Gotcha found via manual testing and fixed**: removing an automated
    parameter used to leave the image visibly unchanged, because nothing ever
    reset the plugin's actual parameter value — it just stayed at whatever the
    last ramp evaluation had driven it to. Fixed by capturing `originalValue`
    when the automation is added (`param->getValue()`) and restoring it via
    `setValueNotifyingHost()` in `removeAutomation()` before erasing.
  - **Another gotcha found via manual testing and fixed**: the numeric fields
    were configured (`setInputRestrictions`/`setText`/`setTooltip`) but never
    actually added as child components (`addAndMakeVisible()`) — they were
    silently never painted or interactive, while the easing `ComboBox` and
    remove `TextButton` (which *were* added) rendered fine. A reminder that
    configuring a JUCE component isn't the same as parenting it.
  - **Expected, not buggy**: because a ramp's `initialValue`/`targetValue`
    fully override the parameter for the segment's span, the plugin's own
    embedded editor knob will visibly move as a live preview recomputes — the
    same way a fader moves under DAW automation. Manually tweaking a knob
    that's also under active automation has no lasting effect: the next
    live-preview refresh immediately overrides it again via the ramp.
- **Header editor panel** (`HeaderEditorPanel.h`/`MainComponentHeaderEditor.cpp`,
  opened via File → "Edit Header...", BMP only). Shares `PluginEditorPanel`'s
  embedded-panel-with-Apply/Cancel shape and `leftColumn`'s single panel slot
  (`LeftColumnPanel::setEditorPanel()`), so the two are mutually exclusive for
  free — you can't have both open at once. 5 editable fields (see `RawImage`'s
  BMP-header-editing entry above) plus 11 read-only informational fields, laid
  out in a scrollable `juce::Viewport`. Every edit re-renders a scratch `RawImage`
  copy live (image preview + waveform), with a warning label (amber, non-blocking)
  and error label (red, blocks Apply) reflecting `validateBmpHeaderFields()`'s
  two-tier result. While open, Load Image/Reset/Rescan/Undo/Redo/the plugin list
  are all disabled (same `updatePluginListEnablement()` gating pattern as the
  plugin editor), and split-channel mode is force-disabled and locked — a header
  edit can change `biBitCount` away from 24 mid-edit, which would make
  `hasChannelPlanes()` false, and header edits have no per-channel meaning at all.
- **Split-channel waveform** (`WaveformSplitPanel.h`, the `WaveformSectionPanel`/
  `RightColumnPanel` toggle-button extension). A `splitModeToggle` button next to
  the existing zoom controls swaps the single interleaved `WaveformView` for 3
  side-by-side lanes (`std::array<WaveformView, 3> channelWaveformViews`, indexed
  by `RawImage::Channel`), each fed that channel's deinterleaved plane via the
  exact same `WaveformView` API used for the interleaved lane (`setBuffer`/
  `updateSampleRange`/selection) — the component itself needed zero changes,
  composition (3 existing instances in a new dumb container) was the whole trick.
  Only one lane has an active selection at a time: each lane's
  `onBeforeSelectionChange` records itself as `MainComponent::activeSelectionChannel`
  and clears the other two via `setSelectionSampleRange({})` (not
  `clearSelection()`, which would itself fire `onBeforeSelectionChange` and
  recurse into `pushUndoState()` for the "losing" lane). Zoom/scroll stay synced
  across all 4 views from a single driver (`MainComponent`'s slider/scrollbar
  callbacks fan out to every view — `WaveformView` has no scroll/zoom gesture of
  its own) — zoom is a ratio so it "just works" regardless of each view's own
  sample count (a channel plane has `width×height` samples vs. the interleaved
  buffer's `width×height×3`), scroll position is converted to a fraction of the
  primary view's sample count and reapplied to each view's own count. The toggle
  is disabled whenever `! workingImage->hasChannelPlanes()` (grayscale PNM, or a
  BMP header-edited to a non-24-bit depth) or the header editor is open — both
  wired into `updatePluginListEnablement()`.
- **`ZoomableImageView`** (`ZoomableImageView.h/.cpp`) — the image preview. Mouse
  wheel/two-finger trackpad scroll pans (JUCE reports trackpad swipes as wheel
  deltas — see `mouseWheelMove`), pinch gesture zooms centered on the cursor
  (standard "zoom to point" math: capture the image-space point under the
  cursor, change scale, recompute offset so that same point stays under the
  cursor), double-click resets to fit-the-viewport. `mouseDrag` is currently a
  no-op (click-drag does **not** pan — panning is wheel/trackpad-only per
  above), reserved for a future feature. **Clicking the preview while no image
  is loaded triggers the same "Load Image..." flow as the File menu**
  (`onClickWithNoImage` callback, fired from `mouseDown` only when
  `! image.isValid()`, wired to `MainComponent::loadImageClicked()`) — the
  placeholder text ("Click to load an image") advertises this; the guard means
  it can never misfire once a real image is loaded. Takes a `resetView` bool on `setImage()` — `true` only on a
  genuinely new image (load/Reset), `false` on in-place refreshes (Apply, selection
  highlight redraw) so the user's zoom/pan isn't yanked out from under them
  mid-workflow. This same `resetView` pattern is used in `WaveformView` for the
  identical reason — **it's a recurring design idiom in this codebase**: any view
  that redraws frequently for cosmetic reasons (selection highlight, apply-in-place)
  must not reset user-adjusted view state unless the underlying data's *identity*
  (not just its bytes) genuinely changed.
  - **Render cache, so a highlight-only repaint stays cheap regardless of
    image size.** `paint()` used to call `g.drawImageTransformed(image, ...)`
    unconditionally on *every* repaint — including the ones the selection
    highlight overlay (above) triggers on every waveform-drag frame — even
    though `image` itself hadn't changed. On macOS this always resamples/
    composites the image's *own full pixel dimensions* regardless of the
    current clip region (confirmed via `CoreGraphicsContext::drawImage`'s
    source), so for a large photo (this app now loads 24MP+ RAF/DNG
    conversions) that per-repaint cost was real and was the actual cause of
    slow selection-dragging that survived the highlight-overlay change.
    Fixed with a manually-managed `cachedRender` — a `juce::Image` sized to
    the *viewport* (`getWidth()×getHeight()`), not the source image's own
    dimensions — rendered once via `ensureCachedRenderUpToDate()` and then
    just blitted directly (`g.drawImageAt()`) on every repaint; regenerated
    only when `image`/`scale`/`offset` actually change (`setImage()`,
    `fitToView()`, `mouseWheelMove()`, `applyZoom()`), never when only
    `setHighlightLines()` is called. Deliberately not
    `Component::setBufferedToImage()` — confirmed via JUCE source that a
    buffered *parent's* cache still gets invalidated by a *child's* repaint
    bubbling up, so nesting the overlay as a child wouldn't have helped;
    this manual cache sidesteps that entirely by staying a single component.
- **`WaveformView`** (`WaveformView.h/.cpp`) — renders the pixel buffer as a
  min/max-per-column waveform. Click-drag selects a **sample range**, which (given
  the current fixed 8-bit-PCM format) is numerically identical to a **byte range**
  in whichever buffer was passed to `setBuffer()` — for the interleaved (non-split)
  waveform that's `RawImage::getVisualOrderedPixelBytes()` (visual top-down, unpadded
  order, **not** `pixelBytes` directly — see the row-flip bug fix in Milestones
  below for why), for a split lane it's `getChannelPlane()`. This sample-index-as-
  byte-index equivalence will need revisiting when M3 lands. Has its own vertical zoom (a pure display-amplitude
  multiplier, clipped to ±1 — doesn't touch underlying data) and horizontal
  zoom+scroll (a `juce::Slider` + `juce::ScrollBar`, wired via
  `WaveformView::onViewChanged` callback so `MainComponent::syncScrollBarToView()`
  keeps the scrollbar's thumb size/position in sync with the view window).
  - **Render cache, for the same reason `ZoomableImageView` got one.**
    `mouseDrag()` calls `repaint()` unconditionally on every raw mouse-move
    event during a selection drag — unlike the image-preview path, this one
    was never debounced through an `AsyncUpdater`. `paint()`'s per-column
    min/max scan costs `O(viewLengthSamples)` total (one full pass over every
    *visible* sample, which defaults to the *entire buffer* until the user
    zooms in horizontally) — for a large image's tens of millions of samples,
    that's a synchronous full-buffer scan on every mouse-move, since the
    trace itself never actually depends on the selection. Fixed identically
    to `ZoomableImageView`: `ensureCachedTraceUpToDate()` renders the trace
    (and the "no buffer" placeholder text) into a viewport-sized `cachedTrace`
    image once, `paint()` just blits it (`g.drawImageAt()`) and then draws
    the separate, already-cheap selection rect/grip-marks on top — the trace
    only regenerates when `waveformData`/view/zoom/sample-mode actually
    change (`setBuffer()`, `updateSampleRange()`, `setHorizontalZoom()`,
    `setViewStart()`, `setVerticalZoom()`, `setSampleMode()`), never on a
    plain `mouseDrag()` selection move. A throwaway timing harness measured
    ~264ms/repaint before this fix (on a 50M-sample synthetic buffer, fully
    zoomed out) vs. ~0.04ms/repaint after — a ~7000x difference, and the
    actual dominant cost behind "dragging a selection is slow," bigger than
    the `ZoomableImageView` fix above. Same class, instantiated 4 times
    (the main waveform + 3 per-channel split lanes) — each gets its own
    independent cache, no special-casing needed.
  - **Bug fixed: `paint()`'s per-column min/max was seeded from a hardcoded
    `0.0f`** (silence) instead of the first real sample in that column's range.
    This silently acted as an implicit floor/ceiling: whenever every sample in a
    displayed column sat on one side of zero — which is common for a single
    channel's plane (e.g. a region with none of that color reads as a long run
    of full-scale-*negative* bytes, not silence, per the byte↔float mapping
    below), and was essentially never true for the original interleaved BGR
    byte stream, which is why this went unnoticed until the per-channel
    split-waveform feature made it obvious — the opposite extreme incorrectly
    reported as exactly 0 instead of tracking the true (still one-sided)
    min/max, making a uniformly-quiet region's trace look like it only reached
    the centre line rather than the real extreme. Found via a user report
    ("there's still a signal in the red channel after the red square") that
    was traced end-to-end (`RawImage::getChannelPlane()` → `SampleFormat::
    bytesToBuffer()`, both confirmed correct against the user's actual file via
    a temporary diagnostic harness) before landing on `paint()` itself as the
    actual culprit. Fixed by seeding `minV`/`maxV` from `samples[startSample]`.
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
- **Selection → image highlight**: originally baked directly into the
  rendered `juce::Image`'s pixel data (first a 50% yellow blend over every
  selected pixel, then a 4-sided outline, then top/bottom-only marker
  lines — see Milestones below for that lineage) — **all of that is gone
  now.** The highlight is drawn as a cheap **line overlay by
  `ZoomableImageView` itself**, on top of a plain rendered image, never
  touching pixel data at all. `RawImage::toJuceImage()`/
  `toJuceImageFromBytes()` lost their highlight parameter entirely — always
  a plain render — and `RawImage::computeHighlightOverlay(juce::Range<int>
  highlightByteRange)` (plus `computeChannelHighlightOverlay()` for a
  channel-scoped selection) returns an `optional<HighlightOverlay>` — two
  row indices (`topRow`/`bottomRow`) — computed via pure `O(1)` integer
  arithmetic (`start/rowBytes`, `(end-1)/rowBytes`; literally "which row does
  this fraction of the buffer fall on"), never a loop over `height`. Each
  line spans the *full image width* rather than just the boundary row's own
  intersected columns — a deliberate simplification (a partial first/last
  row's line reads as "here's the selection's vertical extent," not a
  precise per-column boundary) — so `HighlightOverlay`'s column fields are
  always `0`/`width` on both lines. `MainComponent::updateHighlightOverlay()`
  turns that into two `juce::Line<float>` in image-space coordinates and
  calls `ZoomableImageView::setHighlightLines()`, which `paint()` transforms
  through the same `AffineTransform` it already uses to draw the image
  (`getImageToScreenTransform()`, extracted for reuse) and draws with
  `Graphics::drawLine()` at a constant on-screen thickness — so the marker
  stays clearly visible at any zoom level, unlike a baked-in outline whose
  thickness was fixed in image pixels. `ZoomableImageView` deliberately
  knows nothing about `RawImage`/selections — just image-space line
  coordinates and a colour — keeping it generic/reusable.

  This is a bigger win than the highlight-rendering optimizations that
  preceded it (both now deleted, along with `renderWithRowSelection()`
  entirely): `MainComponent::handleAsyncUpdate()` — the debounced handler
  for every waveform drag frame — used to call `updatePreview()` unconditionally,
  which rebuilds the *entire* `width×height` image from raw bytes on every
  frame, even though a plain selection drag with no plugin panel open never
  changes `pixelBytes` at all. It now calls `updateHighlightOverlay()`
  directly in that case — no image rebuild whatsoever, any image size. The
  one case that doesn't get faster: with a plugin panel open,
  `refreshLivePreview()` always reprocesses the selection through the
  plugin on every drag frame regardless (no memoization exists there) — the
  image content genuinely changes then, independent of how the highlight
  itself is drawn.
  - **Channel-scoped highlight**: `computeChannelHighlightOverlay()` takes a
    plane-sample range (`getChannelPlane()`'s coordinate space) instead of an
    interleaved byte range — simpler than the interleaved case, since a
    channel plane is already stored in visual top-down row-major order, so
    each row's span is a direct arithmetic slice with no
    `rowStride`/`bottomUp`/`channels` adjustment needed.
    `MainComponent::updateHighlightOverlay()` picks the overlay colour
    (red/green/blue vs. the whole-buffer yellow) from `SelectionScope::channel` —
    the same information `RawImage` used to receive as an explicit `Channel`
    parameter just to choose a colour, now resolved entirely on the caller
    side since `RawImage` no longer renders anything highlight-related at all.
  - **Live-preview performance**: on a large image (e.g. full HD, ~6.2MB of pixel
    bytes), the naive version of the above made dragging a selection while a
    plugin panel was open feel frozen — every mouse-move frame re-ran the plugin
    and re-converted the *whole* pixel buffer to float and back, even for a tiny
    selection. Two independent fixes, both preserving exact byte output:
    (1) `MainComponent` privately inherits `juce::AsyncUpdater`; `onSelectionChanged`
    now calls `triggerAsyncUpdate()` instead of recomputing directly, coalescing a
    burst of drag frames into at most one recompute per event-loop turn via
    `handleAsyncUpdate()` — the same debounce idiom `PluginParameterWatcher`
    already used for parameter-change bursts. (2) the recompute (see
    `LivePreviewWorker` below) no longer float-converts the untouched majority of
    the buffer: bytes outside the selection are a plain byte copy (they're
    provably unchanged, per Apply scoping below), and only the selected
    sub-range pays the float round-trip — `WaveformView::updateSampleRange()`
    (a new method, distinct from `setBuffer()`) writes just that sub-range into
    the waveform's existing buffer rather than reconstructing the whole thing.
  - **Dragging a plugin's own knob was still laggy even after the above** —
    every parameter change ran the plugin's `processBlock()` pass synchronously
    *on the message thread* (coalesced via `PluginParameterWatcher`'s
    `AsyncUpdater` to at most one queued recompute at a time, but each one
    still fully blocked the thread painting the knob while it ran), so a drag
    looked like a slideshow instead of a glide. A cheaper first attempt — a
    reentrancy guard in `PluginParameterWatcher`, on the theory that some
    native Cocoa/AU plugin editors pump the run loop from inside their own
    drag-tracking loop and re-enter `handleAsyncUpdate()` — was tried and
    confirmed (by the user, manually) *not* to fix the actual lag: the
    dominant case is just the plain sequential one, one full blocking
    recompute per drag tick, no reentrancy involved. Fixed properly by moving
    the heavy work off the message thread entirely: **`LivePreviewWorker`**
    (`LivePreviewWorker.h`/`.cpp`), a dedicated `juce::Thread` with a one-slot
    "latest request wins" mailbox (`juce::CriticalSection` + `juce::WaitableEvent`) —
    `submit()` always overwrites whatever hadn't started yet rather than
    queuing, since only the most recent parameter/selection state is ever
    worth computing. This is more aligned with JUCE's own expected usage, not
    less: `juce_AudioProcessor.h`'s header comments explicitly describe
    `processBlock()`/`reset()` as callbacks "the audio thread" makes, distinct
    from the message thread, and recommend `AsyncUpdater`-style hand-off to
    reach the UI from inside them.
    - What used to be `MainComponent::computeProcessedPixelBytes()` is now a
      free function inside `LivePreviewWorker.cpp`, running entirely on the
      worker thread — it only ever touches the `AudioPluginInstance` and plain
      `juce::MemoryBlock`/`AudioBuffer` values, never `workingImage`/`RawImage`
      directly, since `RawImage`'s lazy render/plane caches have no locking and
      are not thread-safe.
    - **Source-buffer caching, not per-tick copying.** While the plugin panel
      is open, `updatePluginListEnablement()` already disables Load Image/
      Reset/Undo/Redo, so `workingImage->pixelBytes` is provably immutable for
      the whole session — only the selection, channel scope, ramps, and plugin
      parameters can change. `MainComponent::getOrBuildLivePreviewSource()`
      exploits this: it lazily builds and caches (`cachedWholeBufferSource`/
      `cachedChannelSource`, cleared in `endLivePreviewSession()`) an immutable
      `shared_ptr<const juce::MemoryBlock>` snapshot per scope, shared (not
      copied) across every request in the session — safe across threads since
      nothing mutates it after construction.
    - **Staleness via an epoch counter, not per-job cancellation.**
      `MainComponent::livePreviewEpoch` is bumped in `endLivePreviewSession()`
      (both the commit and discard branches) and echoed on every
      `LivePreviewWorker::Request`/`Result`. `applyLivePreviewResult()` (the
      relocated tail of the old `refreshLivePreview()` — same image/waveform-
      update logic, now driven by `LivePreviewWorker::onResultReady` instead of
      running inline) drops any result whose epoch doesn't match, so a
      background pass that outlives its session (Apply/Cancel/plugin swap
      already happened) is silently discarded rather than misapplied. It also
      routes on the *result's own* echoed channel/selection rather than a
      freshly-queried scope, since the live selection may have moved again
      since that particular request was submitted.
    - **Three distinct flush operations, not one generic "flush"**, since
      call sites want different things: `submit()` (every live-preview tick,
      non-blocking), `discardPending()` (Cancel/`endLivePreviewSession(false)`
      — clears only the not-yet-started request; deliberately does *not* wait
      for one already in flight, since Cancel never touches the plugin
      instance, so a still-in-flight pass finishing in the background is
      harmless and its result is just stale), and `waitUntilIdle()` (`applyClicked()`,
      replacing the old `if (livePreviewBytes.isEmpty())` safety net entirely —
      blocks until nothing is in flight or pending, applying every result
      produced along the way, so `livePreviewBytes` is guaranteed fresh before
      committing — the same brief-blocking tradeoff `handleUpdateNowIfNeeded()`
      already made for the selection-drag debounce, just extended to cover the
      worker). `loadAndOpenPlugin()` also calls `waitUntilIdle()` before
      `currentPlugin->releaseResources()`, since the plugin instance is
      genuinely destroyed there and must not race a worker still inside
      `processBlock()` on it. `~MainComponent()` calls the analogous
      `livePreviewWorker.shutdown()` (full `stopThread()`, not just idle-
      draining) before the same `releaseResources()` call.
    - **Gotcha found and fixed via manual testing**: the earlier reentrancy-
      guard attempt (above) was reverted once confirmed ineffective, rather
      than left in alongside the real fix — its rationale (guarding against
      recursive heavy recompute) no longer applied once `handleAsyncUpdate()`
      just submits a cheap request, so keeping it would have left a guard with
      no remaining reason to exist.
    - **Second gotcha, found via manual testing after the above shipped**:
      dragging a knob was smooth for roughly the first half-second, then
      degraded again — worse the faster the drag. Decoupling *compute* wasn't
      the whole fix: applying a result still re-renders the image on the
      message thread, and `RawImage::toJuceImageFromBytes()` (the no-selection
      path — there's no unchanged sub-range to scope a render to when the
      whole buffer was just processed) is an *uncached*, full `width*height`
      repaint on every call. With compute no longer gating how often a fresh
      result appeared, the worker could feed results to the message thread
      faster than it could render them back-to-back with any idle time —
      recreating the exact same message-thread-saturation symptom the whole
      fix was meant to solve, just moved from "compute" to "render." Fixed by
      throttling *delivery*, not just compute: `LivePreviewWorker` privately
      inherits `juce::Timer` as well as `juce::Thread` now, ticking at a fixed
      `deliveryRateHz` (60) that pulls and delivers only the latest available
      result, discarding how many the worker actually finished in between —
      the worker still always computes towards the freshest request as fast as
      it can, but the message thread only ever pays the render cost at a rate
      no faster than the display can show, regardless. This also simplified
      the code: the previous per-job `juce::MessageManager::callAsync` push
      (plus the `alive`/`shared_ptr<atomic<bool>>` dead-object guard protecting
      it) was removed entirely — a `juce::Timer` callback's lifetime safety is
      handled by JUCE itself (`stopTimer()` guarantees no more callbacks), so
      once delivery became a poll instead of a push there was nothing left for
      that guard to protect.
    - **RESOLVED (July 2026) — the remaining knob-drag stutter was diagnosed
      with real measurements and fixed in three further steps.** All numbers
      below were measured with temporary `DBG()` timing on a 6240×4160 test
      image (77,875,200 pixel bytes = samples), Debug build; the
      instrumentation was removed after the user confirmed smoothness, per
      this doc's measure → confirm → remove convention.
      - **Cheap compute wins** (first pass, prior session): `blockSize` bumped
        512 → 4096 (`MainComponent.h`/`LivePreviewWorker.h`) —
        `processWholeBuffer()`'s cost was dominated by per-block overhead
        (~5.2µs/block near-constant), so this cut block-boundary crossings
        ~8x; the accepted tradeoff is ~8x coarser parameter-automation ramp
        resolution, still thousands of steps across a typical selection.
        `SampleFormat::bufferToBytes()`'s double-clamp was fused into one
        (verified byte-identical via a throwaway harness, deleted after
        passing). The waveform's redundant second `bytesToBuffer()` pass was
        deduplicated — the worker computes the post-plugin float buffer once
        into `LivePreviewWorker::Result::waveformSamples`.
      - **The entire render moved to the worker thread** (`LivePreviewWorker::
        renderResult()`): image composition (`previewWith*Bytes()` +
        `toJuceImageFromBytes()`/`...Scoped()`) and the waveform float
        conversion now run right after compute on the worker, wrapped in
        `JUCE_AUTORELEASEPOOL`; `applyLivePreviewResult()` became a cheap
        hand-off (~1-2ms) of `Result::renderedImage`/`waveformSamples`. The
        two safety questions flagged in the earlier hand-off were researched
        first, not assumed: (a) macOS `juce::Image` construction/writing goes
        through `CoreGraphicsPixelData`, which is a private in-memory
        `CGBitmapContextCreate` bitmap with no window/AppKit/main-thread
        dependency (confirmed against JUCE source); (b) the `RawImage`
        render-cache invariant was closed by call-site audit — `splitModeToggle`
        and Export are now also disabled while the plugin panel is open (both
        could previously touch the render caches on the message thread
        concurrently with the worker), and `endLivePreviewSession()` calls
        `waitUntilIdle()` unconditionally so no in-flight render can outlive
        the session. Verified with a second throwaway harness (Apply-scoping
        invariant, pixel-identical scoped-vs-full renders, waveform sample
        math; deleted after passing).
      - **The final, dominant residual — the paint caches — was the actual
        stutter.** Each delivered result unconditionally invalidated
        `WaveformView`'s `cachedTrace` and `ZoomableImageView`'s
        `cachedRender`, whose rebuilds run synchronously inside `paint()` on
        the message thread. Measured: the trace rebuild's per-column min/max
        scan is O(viewLengthSamples) — **~406ms per rebuild** fully zoomed out
        on the 77.9M-sample buffer, recurring every ~440ms for the whole drag
        (the message thread spent ~430 of every ~440ms there); the image
        viewport resample added ~21–28ms. A 500Hz message-thread stall
        watchdog cross-correlated by timestamp showed **zero unattributed
        stalls** — ruling out plugin-internal lock contention between the
        plugin's editor and the worker's `processBlock()`. (This also
        explained the "smooth at first, then stutters" symptom: the first
        ~165ms+ of a drag has no delivered result yet, so nothing heavy runs
        on the message thread until deliveries begin.) Fixed by:
        1. **`WaveformPeaks.h`** — an exact min/max bucket peak cache
           (`samplesPerBucket = 512`, the standard audio-editor technique,
           single level), written as pure free functions over plain arrays so
           a CLI harness can verify them without a GUI. `WaveformView` keeps
           `peakMins`/`peakMaxs` in sync inside `setBuffer()`/
           `updateSampleRange()` (both now take an optional precomputed
           `WaveformPeaks::Partial`), and `ensureCachedTraceUpToDate()`'s
           per-column scan aggregates fully-covered buckets + raw-scans the
           partial head/tail — **byte-identical output to the old raw scan**
           (proven with a throwaway harness across buffer sizes, view
           windows, and splice scenarios; deleted after passing). Trace
           rebuild: ~406ms → **~1.4–3.7ms**. The display-only setters
           (zoom/pan/vertical zoom/sample mode) never touch peaks, so their
           previously-just-as-expensive rebuilds got the same speedup free.
        2. **Worker-side peak precompute** — `renderResult()` also fills
           `Result::waveformPeaks` (only buckets *fully contained* in the
           changed range, absolute bucket alignment from the selection start;
           the partial edge buckets are recomputed by the view from the
           already-spliced data). Without this, a no-selection delivery would
           pay an O(numSamples) peak rebuild on the message thread per
           delivery — the same shape of cost the worker exists to absorb.
        3. **Session-scoped fast resample** — `ZoomableImageView::
           setFastResampling()` switches the viewport cache rebuild to
           nearest-neighbour (`lowResamplingQuality` → `kCGInterpolationNone`
           on macOS) while a plugin panel is open; toggled in
           `openEditorClicked()`/`endLivePreviewSession()`, self-invalidating,
           full quality returns on session end.
      - **Busy spinner**: since a preview pass on a large image legitimately
        takes a worker-cycle (~165ms+) to land, the preview intentionally lags
        the knob even though the UI stays smooth. `BusySpinner`
        (`BusySpinner.h`, sitting left of the status label in
        `RightColumnPanel`'s status strip) polls `LivePreviewWorker::isBusy()`
        at ~30Hz, showing a small rotating arc while a pass is in flight or
        queued and hiding itself (with a few-tick linger to avoid flicker
        between back-to-back passes) when idle.
      - **"Spinner overstays after a tweak" — investigated, measured, and
        deliberately left as-is.** The user reported the spinner staying ~3x
        longer than the visible preview change after a knob gesture. Measured
        (temporary per-notification/submit/pass/delivery logging, removed
        after): the plugin fires dozens of parameter notifications per gesture
        with *genuinely drifting values* — no identical-value trailing
        re-sends, no non-parameter `audioProcessorChanged` events — so a
        submit-time dedup fingerprint (designed and reviewed, never shipped)
        would have skipped nothing. The trailing spinner time is the honest
        queue tail of the one-slot mailbox: on release, the in-flight pass
        (computing an already-stale value) finishes, then exactly one more
        pass runs for the true released value. The preview *looks* settled a
        pass earlier because adjacent values render near-identically, but the
        spinner stopping is precisely the "preview now shows your final
        setting" signal. A tail-shortening optimization (abort the stale
        in-flight pass between blocks once the gesture goes quiet — a
        `shouldAbort` hook in `PluginHost::processWholeBuffer()` gated on
        pending-request + ~50ms of notification silence, to avoid starving
        preview updates mid-drag) was designed but dropped as unnecessary once
        the user confirmed the behavior feels fine. If this ever comes back,
        start from that design — and do NOT reach for dedup; the data already
        refuted it.
  - **Image loading is asynchronous** (`loadImageClicked()`,
    `MainComponentImageIO.cpp`): the whole heavy path — RAW camera conversion
    (`RawCameraConverter`, ImageIO decode + BMP write: seconds for camera
    files), `RawImage::loadFromFile()`, the copy to `workingImage`, warming
    the plain-render cache, the whole-buffer `bytesToBuffer()` and
    `WaveformPeaks` build — runs on `MainComponent::imageLoaderPool` (a
    1-thread `juce::ThreadPool`; one-shot serialized jobs, so no
    LivePreviewWorker-style mailbox needed), inside `JUCE_AUTORELEASEPOOL`.
    It previously ran synchronously in the FileChooser callback and cost a
    measured ~1.9s message-thread stall on a ~78MB image, freezing the UI and
    the spinner. The message-thread completion (via `MessageManager::callAsync`)
    is a cheap install; `finishImageLoad()` is the shared success/failure tail.
    Key invariants:
    - `imageLoadInProgress` (message-thread-only bool) gates every
      image-mutating entry point while a load is in flight: the File menu
      (via `MainMenuModel`'s busy callback), undo/redo keyboard commands
      (`getCommandInfo`), the plugin list (`updatePluginListEnablement()`'s
      `listInteractive`), the split toggle, and `loadImageClicked()` itself
      (also reachable via `imagePreview.onClickWithNoImage`). Loads already
      can't start during a plugin/header session, so an install can never
      land into one (asserted in the completion).
    - Completion liveness needs **two** guards: `Component::SafePointer`
      (covers use-after-full-destruction) *plus* `imageLoadAliveToken`
      (a `shared_ptr`/`weak_ptr` pair reset first thing in `~MainComponent()`)
      — SafePointer only nulls in `~Component`, which runs *after* members
      are destroyed, so a queued completion could otherwise dispatch into
      partially-destroyed members if a plugin destructor pumps the run loop
      during teardown (the same hazard the destructor already defends
      against for `previewBusySpinner`).
    - The spinner's feed is `livePreviewWorker.isBusy() || imageLoadInProgress`,
      so it covers loads too, and the status bar shows "Loading <name>…".
    - **Bug fixed in passing**: loading a new image with split-channel mode on
      used to leave the three channel lanes showing the *previous* image's
      planes (the old synchronous path never refreshed them either). Split
      mode is now forced off on every successful load — the same "new image
      resets view state" semantic as the sample-mode reset next to it.
  - **Scoped image *render*, on top of the scoped DSP above.** Even with the
    plugin/float-conversion work scoped to the selection, `refreshLivePreview()`
    still called `toJuceImageFromBytes(livePreviewBytes)` — a full per-pixel
    BGR/RGB-extraction render of *every* pixel — on every single plugin
    parameter tweak, even though only the selected rows' bytes actually
    differ from the already-committed `pixelBytes`. Fixed with a third
    `RawImage` lazy cache (`cachedPlainImage`/`plainImageDirty`, same idiom
    and same invalidation sites as `channelPlanes`/`visualOrderedPixelBytes`
    — every place `pixelBytes` itself is mutated) backing `toJuceImage()`,
    plus a new `toJuceImageFromBytesScoped(bytesToRender, firstRow, lastRow)`
    that copies the cached plain render and re-renders only rows
    `[firstRow, lastRow]` — reusing the exact row range already computed for
    the selection-highlight overlay (`computeHighlightOverlay()`/
    `computeChannelHighlightOverlay()`), so no new geometry logic was needed.
    `refreshLivePreview()` uses this scoped path whenever a selection is
    active, falling back to the full render only when there's no selection
    (whole-buffer apply — no "unchanged outside a sub-range" guarantee to
    exploit there). **A real correctness landmine, found via JUCE source
    research before writing any code, not by trial and error**: `juce::Image`
    is a reference-counted COW handle, and opening a writable
    `juce::Image::BitmapData` on a copy does **not** automatically duplicate
    a shared pixel buffer first — without an explicit
    `result.duplicateIfShared()` call before writing, the "scoped" write
    would silently corrupt the shared cached original. `duplicateIfShared()`
    does a real full-buffer `memcpy`, so this isn't literally free for the
    untouched rows — but a raw memcpy is far cheaper than the per-pixel
    render loop it replaces. Verified via a throwaway harness (built, run,
    deleted): re-rendering `toJuceImage()` after a scoped call returned
    byte-identical pixels to before it (proving the cache wasn't corrupted),
    and a ~25x speedup on a 2000×1500 image re-rendering only 51 of 1500 rows.
- **Apply scoping**: if `WaveformView::getSelectionSampleRange()` is non-empty,
  `MainComponent::computeProcessedPixelBytes()` (the shared helper behind both the
  live preview and Apply) copies just that sub-range into a temporary buffer,
  processes it, and copies it back — bytes outside the selection are provably
  untouched (verified via `cmp`/memcmp in a temp harness). No selection ⇒
  whole-buffer apply (original M1 behavior). `applyClicked()` no longer
  recomputes this itself — it commits whatever `livePreviewBytes` was last
  computed by `refreshLivePreview()`, guaranteeing the committed result is
  byte-identical to what was just previewed.
  - **Channel-scoped variant**: `computeProcessedPixelBytes()` takes an optional
    `RawImage::Channel` — when set, its source/destination is
    `workingImage->getChannelPlane(channel)` (a deinterleaved copy) instead of
    `getVisualOrderedPixelBytes()` (the whole-buffer analogue — see the row-flip
    bug fix in Milestones below), same selection-scoping logic either way. Which
    scope is "the"
    current one — a channel-plane selection (split-waveform mode, a lane has an
    active selection) or the plain whole-buffer selection — is resolved once via
    `MainComponent::SelectionScope`/`getCurrentSelectionScope()`, consumed by
    `computeProcessedPixelBytes()`, `refreshLivePreview()`, `updatePreview()`
    (for highlight color), and undo/redo (below). Commit branches the same way:
    `endLivePreviewSession()` calls `workingImage->applyChannelBytes()` for a
    channel-scoped commit (preserving the other two channels' plane caches) or
    `workingImage->applyVisualOrderedBytes()` otherwise (splicing back into
    `pixelBytes`' real, possibly bottom-up/padded layout).
  - **Parameter ramps** (see the Parameter automation entry above) evaluate
    inside this same per-block loop via `PluginHost::processWholeBuffer()`'s
    `beforeBlock` hook, so a selection-scoped Apply can fade a parameter across
    the scope instead of holding it static — same selection-scoping logic
    either way, no separate code path.
- **Undo/redo**: `std::vector<EditorSnapshot> undoStack`/`redoStack` on
  `MainComponent`, where `EditorSnapshot` bundles `headerBytes`, `pixelBytes`, the
  waveform selection range, *and* (since the split-channel feature) which channel
  — if any — owned that selection, so restoring history restores the pixels, the
  header, which range was selected, and whether it was a channel-scoped selection,
  not just the bytes. `pushUndoState()` (called at the top of
  `applyClicked()`/`resetClicked()`/`applyHeaderEditClicked()`, before mutating,
  plus from every waveform lane's `onBeforeSelectionChange`) snapshots the current
  state via `getCurrentSelectionScope()` and clears `redoStack`. A selection
  drag/click counts as exactly one undoable action: each `WaveformView` fires
  `onBeforeSelectionChange` once at the start of a gesture (`mouseDown`), not per
  drag frame, so a whole click-drag collapses to a single undo entry captured with
  the pre-gesture state. `undoClicked()`/`redoClicked()` swap between the stacks,
  restore `headerBytes`/`pixelBytes` via `RawImage::restoreSnapshot()` (which
  re-derives BMP geometry from the restored header), then restore the selection via
  `MainComponent::restoreSelectionScope()` — which also flips the split-mode toggle
  on/off to match whether the entry has a channel, so the restored selection is
  actually visible rather than landing on a hidden panel. Stack clears on a fresh
  image load. Cmd+Z / Cmd+Shift+Z shortcuts are wired via
  `juce::ApplicationCommandManager`, deactivated while either the plugin editor or
  the header editor panel is open.
- **Menus**: `MainMenuModel` (`MainMenuModel.h/.cpp`) implements `juce::MenuBarModel`
  and is installed as the native macOS menu bar (`setMacMainMenu(&menuModel)`/
  `setMacMainMenu(nullptr)` in `MainComponent`'s ctor/dtor). Two top-level menus:
  **File** (Load Image, Export Image, Reset to
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
- *(structural refactor, no behavior change)* — `MainComponent.h`/`.cpp` (was
  611/645 lines, mixing plugin-list/menu-bar/hosting/image-IO/layout concerns
  in one file) split into `PluginListModel`, `MainMenuModel`, four layout/panel
  header classes, and three `MainComponent*.cpp` files by concern — see the UI
  section above for the new file layout and rationale.
- *(performance fix)* — live preview on a large image no longer feels frozen
  while dragging a waveform selection: drag-triggered recomputes are now
  debounced (`juce::AsyncUpdater`, same idiom as parameter-change coalescing),
  and byte↔float conversion in `computeProcessedPixelBytes()`/`refreshLivePreview()`
  is scoped to the selected sub-range instead of the whole buffer — see the
  "Live-preview performance" note above.
- *(user-requested polish)* — clicking the empty image preview now opens the
  same "Load Image..." dialog as the File menu, instead of doing nothing.
- *(user-requested feature)* — BMP header editing: all 16 documented
  `BITMAPFILEHEADER`+`BITMAPINFOHEADER` fields readable, the 5 that drive this
  app's own decoding editable with live preview and two-tier (blocking/warning)
  validation, via a new "Edit Header..." panel. Inverts the previous "headerBytes
  is fully protected" invariant for BMP specifically — see `RawImage`'s
  entry above for why that's still safe.
- *(user-requested feature)* — per-channel split waveform: view and edit each
  color channel's byte stream as its own waveform lane (toggle next to the
  existing zoom controls), fully editable — Apply can be scoped to a single
  channel, re-interleaving the result back — with a color-matched highlight
  outline on the image preview.
- *(user-requested feature)* — non-24bpp BMP support (1/4/8/32bpp) via
  conversion to a full 24-bit buffer at load time, so no other subsystem needs
  to know about other bit depths. Prompted by a real load failure on a 4bpp
  file; 1bpp/4bpp were deliberately *not* given native (unconverted) support —
  see `RawImage`'s entry above for why that would fight this app's whole
  byte-addressable model.
- *(user-requested feature)* — **bipolar/unipolar sample-mode toggle**, fixing
  the byte↔float encoding mismatch described at length in earlier revisions of
  this doc: `SampleFormat.h`'s bipolar mapping (`float = (byte-128)/128`) is
  correct for real audio but wrong for image intensity, where byte 0 ("no
  colour") should mean silence, not a full-scale-negative signal — a
  gain-reducing plugin effect would otherwise paradoxically *create* visible
  content in channels/regions that had none. Added `SampleFormat::Mode`
  (bipolar/unipolar) as a parameter on `bytesToBuffer()`/`bufferToBytes()`,
  stored per-image on `RawImage` (`getSampleMode()`/`setSampleMode()`,
  defaulting to bipolar), and threaded through every call site (image load,
  plugin apply/live-preview, header-edit live preview, per-channel split
  waveforms). `WaveformView::paint()` now renders unipolar data bottom-anchored
  using the full lane height instead of assuming a signal centred on zero.
  Surfaced as a `ComboBox` in a new header strip above the image preview
  (`RightColumnPanel`), visible only while the plugin editor panel is open;
  changing it re-renders the live preview and waveform(s) immediately via
  `refreshLivePreview()`. Deliberately a session-level view setting, not part
  of undo/redo — resets to bipolar on every new image load.
- *(bug fix)* — **the interleaved (non-split) waveform was row-flipped for
  bottom-up BMPs.** Its sample index was the raw `pixelBytes` byte offset
  directly; a standard BMP stores rows bottom-up (byte 0 = the image's
  *bottom* row), so selecting the end of the waveform highlighted/glitched
  the *top* of the image instead of the bottom — surprising, and inconsistent
  with the split-channel waveform, which already normalizes to visual
  top-down order via `RawImage::getChannelPlane()` (built for the split
  feature specifically to avoid this). Fixed by generalizing that same
  pattern to the whole interleaved buffer: `RawImage::getVisualOrderedPixelBytes()`/
  `applyVisualOrderedBytes()`/`previewWithVisualOrderedBytes()` mirror
  `getChannelPlane()`/`applyChannelBytes()`/`previewWithChannelBytes()` but for
  the still-interleaved (not deinterleaved-per-channel) whole buffer, cached
  the same way (`visualOrderDirty`, invalidated at every site `planesDirty`
  already was, plus `applyChannelBytes()` — a channel edit changes the real
  interleaved bytes too, so it now invalidates the whole-buffer cache as well
  as its own channel-plane one). `toJuceImageFromBytes()`'s `highlightByteRange`
  parameter's contract changed project-wide from "a raw `pixelBytes` offset"
  to "a visual top-down, unpadded offset" — matching what the channel-plane
  highlight overload already used, and what every interleaved waveform call
  site (`updateWaveform()`, `computeProcessedPixelBytes()`,
  `refreshHeaderLivePreview()`) now builds its buffer from. Because the
  live-preview/Apply pipeline's edited result is now in visual-order space but
  the image preview needs pixelBytes' real (possibly bottom-up/padded) layout
  to render colours, `MainComponent` gained a `livePreviewVisualOrderBytes`
  field (parallel to the existing `livePreviewChannelPlaneBytes`) so the
  "edited source" and "rendered/committed" buffers can differ, same split the
  channel-scoped path already had. **Behaviour change**: selecting the start of
  the interleaved waveform now highlights/glitches the *top* of the image and
  the end the *bottom* (previously reversed) — any workflow built around the
  old mapping will see different, now-correct, results for the same numeric
  selection. BMP row-padding bytes are also no longer part of the interleaved
  waveform/selection at all (stripped by the reorder), consistent with how
  channel planes already exclude them; PNM is unaffected (never bottom-up,
  never padded).
- *(user-requested feature)* — **parameter automation (fade in/out ramps)**:
  one or more of a plugin's own parameters can ramp from an initial to a
  target value across a selection (or the whole buffer), with easing, via a
  new "Automation" tab on the plugin editor panel — smooths what used to be a
  hard on/off cut at a selection's edges. Two directions were explored and
  abandoned first: multi-plugin chaining, and an Ableton-style
  automation-graph-driving-a-rendered-frame-sequence idea exported as an
  animated GIF — both replaced by this narrower, actually-needed feature once
  discussed further. See the Parameter automation UI entry above for the full
  design (data model, per-block evaluation, the watcher feedback-loop guard,
  the two-value range-slider UI, and two bugs found via manual testing and
  fixed: a stuck parameter value on removal, and numeric fields configured but
  never actually added as child components).
- *(performance fix)* — **plugin-knob live preview decoupled from the message
  thread**: dragging a plugin's own embedded knob no longer blocks the thread
  painting it. `LivePreviewWorker` runs the plugin `processBlock()` pass on a
  dedicated background thread with "latest request wins" coalescing, instead
  of synchronously on the message thread once per parameter change — plus a
  second fix once compute-alone turned out to be insufficient: *delivering* a
  result back to the message thread is also throttled to a fixed rate
  (`juce::Timer` at 60Hz) instead of pushed on every completed pass, since the
  render itself is real, uncached work that could otherwise saturate the
  message thread just as badly as the old compute-on-message-thread design
  did. See the "Live-preview performance" entry above for the full design
  (source-buffer caching, the epoch staleness check, the three flush
  operations, the `thread_local` feedback-loop guard that replaced
  `ScopedWatcherPause`, and the throttled-delivery timer).

## What's NOT done yet (planned)

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
  one-plugin-at-a-time (apply, inspect, apply again manually). Parameter
  automation (ramps) was deliberately scoped to the single currently-loaded
  plugin for this same reason — see the Parameter automation UI entry above.
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
- **Converting a non-24bpp BMP discards its original palette/bit-depth
  permanently** — there's no "export back to 4bpp indexed" path, by design
  (export was already PNG-only for every format before this, so this isn't a new
  regression, just worth knowing: the databending happens on the *expanded*
  24-bit buffer, not the file's literal original bytes).
- **16bpp BMP is not supported at all** — no format-detection path exists for it
  (1/4/8/32bpp are converted to 24-bit at load; 24bpp loads natively). Would need
  the same "expand to 24-bit at load" treatment; not implemented because it
  hasn't come up yet.
- **BMP conversion only supports the classic 40-byte `BITMAPINFOHEADER`** for
  locating a palette or `BI_BITFIELDS` masks — a file using the larger
  `BITMAPV4HEADER`/`V5HEADER` variants (108/124 bytes, common from modern
  screenshot tools for `BI_BITFIELDS`) is rejected with a clear error rather than
  silently misreading unrelated header bytes as a palette/masks. This mirrors a
  limitation that already existed everywhere else in this codebase (header
  parsing has only ever assumed the 40-byte header), not a new one.
