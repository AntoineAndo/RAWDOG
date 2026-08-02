
<img width="7%" alt="rawdog logo-iOS-Default-1024x1024@1x" src="https://github.com/user-attachments/assets/30b1dcbb-b379-4edd-8c48-6a89afd79a41" align="right" />

# **R**aw **A**udio **W**aveform **D**atabending & **O**utput **G**litcher
An image editing tool to databend image files with audio VST3 plugins



# Features  
## ✂️ Selective glitching 
Select a range on the waveform to modify just part of the image and leave the rest untouched

<img width="300" height="280" alt="laser png" src="https://github.com/user-attachments/assets/7f35ab54-0e2d-4837-b79c-c50fe8e75ca9" />

## 🔗 Effect chaining
Use and tweak multiple effects at the same time

<img width="323" height="222" alt="image" src="https://github.com/user-attachments/assets/ec373411-4cec-4bf9-a3f6-23a3051893a9" />


## 🌈 Per-channel editing
Split into R/G/B/A lanes and process each one independently
 
<img width="300" height="280" alt="image" src="https://github.com/user-attachments/assets/ffb39757-ecc0-44ed-aff5-58f0a0ac6891" />


## 🎚️Parameter fading 
Ramp a plugin parameter across the selection instead of setting it static

<img width="930" height="401" alt="image" src="https://github.com/user-attachments/assets/a5cf238d-aad9-461c-a176-fef3ed5f9637" />


- ↩️ **Undo/redo**
- 🎛️ **Works with VST3 and AU plugins** 
- 📷 **Supports JPEG, PNG, BMP, PNM, RAF and DNG**
- 🖼️ **Exports to PNG**

## Requirements

- macOS
- Xcode / CMake
- Some VST3 or AU plugins already installed

## How to install

There's no pre-built download yet, so build it yourself and drop it into
`/Applications`:

```bash
git clone git@github.com:AntoineAndo/PixelBender.git rawdog
cd rawdog
cmake -B build -G Xcode
cmake --build build --config Release --target RAWDOG
cp -R "build/RAWDOG_artefacts/Release/RAWDOG.app" /Applications/
```

Then launch it from Applications/Spotlight like any other app.

## Build & run (development)

```bash
cmake -B build -G Xcode
cmake --build build --config Release --target RAWDOG
open "build/RAWDOG_artefacts/Release/RAWDOG.app"
```



<img width="3840" height="1438" alt="Dog reverb" src="https://github.com/user-attachments/assets/2f5d5c39-19b0-46ce-a725-421be3fc1461" />
