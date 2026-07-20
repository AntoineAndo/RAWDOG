<img width="3840" height="1438" alt="Dog reverb" src="https://github.com/user-attachments/assets/2f5d5c39-19b0-46ce-a725-421be3fc1461" />

# Pixel Bender
## An image editing tool to databend raw image files with audio VST3 plugins

# Features  
## ✂️ Selective glitching 
Select a range on the waveform to modify just part of the image and leave the rest untouched

<img width="300" height="280" alt="laser png" src="https://github.com/user-attachments/assets/7f35ab54-0e2d-4837-b79c-c50fe8e75ca9" />

## 🌈 Per-channel editing
Split into R/G/B lanes and process each one independently
 
<img width="300" height="280" alt="image" src="https://github.com/user-attachments/assets/ffb39757-ecc0-44ed-aff5-58f0a0ac6891" />


## 🎚️Parameter fading 
Ramp a plugin parameter across the selection instead of setting it static

<img width="930" height="401" alt="image" src="https://github.com/user-attachments/assets/a5cf238d-aad9-461c-a176-fef3ed5f9637" />


- ↩️ **Undo/redo**
- 🎛️ **Works with VST3 and AU plugins** 
- 📷 **Supports BMP, PNM, RAF and DNG**
- 🖼️ **Exports to PNG**

## Requirements

- macOS
- Xcode / CMake
- Some VST3 or AU plugins already installed

## Build & run

```bash
cmake -B build -G Xcode
cmake --build build --config Release --target PixelBender
open "build/PixelBender_artefacts/Release/Pixel Bender.app"
```
