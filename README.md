# NovaHQ 3DI/3DO/PAK to OBJ Converter

Visit [https://novahq.net](https://novahq.net) for more on NovaLogic file formats and tools.

**Related research notes**:

- [NovaLogic 3DI File Format](https://github.com/Novahq-net/NovaResearch/blob/main/Shared/3DI%20File%20Format.md)
- [NovaLogic 3DO File Format](https://github.com/Novahq-net/NovaResearch/blob/main/Shared/3DO%20File%20Format.md)
- [NovaLogic PAK File Format](https://github.com/Novahq-net/NovaResearch/blob/main/Shared/PAK%20File%20Format.md)
- [NovaLogic OCF File Format](https://github.com/Novahq-net/NovaResearch/blob/main/Shared/OCF%20File%20Format.md)
- [NovaLogic AI File Format](https://github.com/Novahq-net/NovaResearch/blob/main/Shared/AI%20File%20Format.md)

## Nova3di

Nova3di converts NovaLogic's 3D model formats (.3DI, .3DO, .PAK) to Wavefront OBJ files. It supports all NovaLogic titles and formats, including composite formats like OCF and AI. I knew nothing of 3D modeling or these file formats before starting this project. I just wanted an easier way to view models outside of the game using a modern format. I learned that NovaLogic's model formats are more complex and nuanced than I anticipated, with many special cases and variations across games. Even when titles share a format, the engines apply it differently.

I've reviewed around 20,000 models across the various games and made a decent attempt at supporting all the features and edge cases I found, but there may still be some unsupported features or inaccuracies in the output. Feel free to open an issue if you find something that looks wrong. Before reporting, please confirm the model is actually used in-game - NovaLogic shipped many leftover or test assets that were never textured or fully set up. Screenshots of the model in-game and in the viewer, along with the original model file, are very helpful for troubleshooting.

Since this project started as an exporter to simply view models, the converter post-processes textures (alpha bleeding, colour-key conversion, mode-specific blending) so they render correctly in viewers. The converter tries to preserve the original appearance as much as possible, but it may not be perfect in all viewers. If you are interested in modding or using the models in a game engine, use the `--raw` option to skip all texture conversion to preserve the original textures. I found a lot of viewers don't support *.PCX textures, so you'll need to convert them after export to view them.

## Supported Formats

**Note:** For GPM/3DI3 formats, check out the [OpenNova](https://github.com/opennova-net/opennova) project for more in-depth format support. Nova3di's GPM/3DI3 support is primarily intended for viewing models / simple exports and currently does not support all the enhanced features of those formats.

| Format | Extension | Games |
|--------|-----------|-------|
| 3DI v2-v5 | .3DI | Delta Force |
| 3DI v7 | .3DI | Armored Fist 3 (mission editor previews) |
| 3DI v8 | .3DI | Delta Force 2 |
| 3DI v10 | .3DI | Land Warrior, Task Force Dagger |
| GPM/GPS/GPP | .3DI | Black Hawk Down, Comanche 4 |
| 3DI3 | .3DI | Joint Operations, Delta Force: Xtreme, Delta Force: Xtreme 2 |
| 3DO | .3DO | Armored Fist 3, Comanche 3 Gold |
| 3DPK | .PAK | Tachyon: The Fringe, F-16 Multirole Fighter, MiG-29 Fulcrum, F-22 Lightning 3, F-22 Raptor |
| OCF | .OCF | Tachyon: The Fringe (composite) |
| AI | .AI | Armored Fist 3, Comanche 3 Gold (composite) |

## Usage

```
Nova3di.exe <file>                   Convert a single file
Nova3di.exe <directory>              Batch convert all models in a directory
Nova3di.exe <input> --out=<dir>      Send output to a specific directory
```

NovaLogic games store textures in two ways: embedded and external. "Embedded" means textures that are stored directly within the model file. "External" means textures that are stored as separate files alongside the model files, but still contained within a PFF archive. 

The simplest setup is to download [NovaPFF](https://novahq.net/files.php?ID=863), extract all the game's PFF archives into a single folder, and run Nova3di across that folder. Embedded textures are extracted automatically, external textures resolve as long as they sit alongside the model files.

The converter auto-detects the format from the file's magic bytes. Double-clicking the exe with no arguments opens a file dialog. In batch mode, it finds all supported model files in the given directory. Pass any of --3di, --3do, --pak, --ocf, or --ai to restrict which formats are processed.

### Examples

```
Nova3di.exe soldier.3di
Nova3di.exe vehicle.3di --out=C:\output --collision --hardpoints
Nova3di.exe spaceship.pak --out=D:\models
Nova3di.exe spaceship.pak --extract
Nova3di.exe C:\games\game\assets --raw
Nova3di.exe C:\games\game\assets --effects
Nova3di.exe C:\games\game\assets --ai --out=D:\af3_obj
```

### Options

| Option | Description |
|--------|-------------|
| `--out=<dir>` | Output directory (default: exe directory) |
| `--collision` | Export collision mesh as a separate OBJ (GPM/3DI3) |
| `--hardpoints` | Export attachment points as a text file (3DI/3DI3) |
| `--extract` | Extract embedded 3DOs + textures (PAK/OCF) |
| `--metadata` | Write `#nova:` metadata comments in MTL (3DI only) |
| `--effects` | Include animated effect materials (shadows, effects) |
| `--lods` | Write one OBJ per LOD level (default: highest LOD only) |
| `--raw` | No texture conversion (still decompresses BFC1/LZP1 wrappers) |
| `--env=N` | Environment texture binding (mixed support) |
| `--lod=N` | LOD selector (mixed support) |
| `--3di` / `--3do` / `--pak` / `--ocf` / `--ai` | Batch convert by file extension |
| `--log` | Write warnings/errors to `Nova3di.log` |

### Output

| File/Path | Description |
|------|-------------|
| `<name>.obj` | Wavefront OBJ geometry |
| `<name>.mtl` | Material library referencing the textures |
| `*.tga`, `*.pcx`, `*.dds` | Extracted or copied textures |
| `<name>_collision.obj` | Collision mesh (`--collision`) |
| `<name>_hardpoints.txt` | Hardpoint positions and names (`--hardpoints`) |
| `<model>_<ENV>/` | Per-environment composed OBJs (AI) |

### Viewing Models
The exported models were compared to their in-game variants for accuracy using [F3D](https://github.com/f3d-app/f3d) and [Blender](https://www.blender.org/). Not all viewers support all features. I've tried my best to make sure models work in Blender. If you see missing textures, try a different viewer or use one of the two listed above.

***Recommended F3D viewer settings:***
```json
[
  {
    "options": {
      "filename": true,
      "tone-mapping": true,
      "background-color": "0.8,0.8,0.8",
      "anti-aliasing": "fxaa",
      "axes-grid":false,
      "grid": "no",
      "grid-absolute": false,
      "interaction-style":"trackball",
      "hdri-ambient": true,
      "blending": "ddp",
      "light-intensity":4.0,
      "hdri-skybox":false,
      "roughness":1.0,
      "dpi-aware": true,
      "notifications": true
    }
  }
]
```

## Building

**Requirements**: Visual Studio 2022 with the **C++ Desktop Development** workload (v145 toolset, Win32/x86). No external dependencies.

- **VSCode**: Press **Ctrl+Shift+B** with the C/C++ extension installed.
- **Developer Command Prompt**: `msbuild Nova3di.vcxproj /p:Configuration=Release /p:Platform=Win32`
- **Visual Studio**: Open `Nova3di.vcxproj` and build the Release configuration for Win32.
