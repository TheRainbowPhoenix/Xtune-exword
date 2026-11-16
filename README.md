# Xtune

Xtune is an overclocking and benchmarking utility for CASIO calculators with SuperH 3/4 MPU.

## Features

> Note: Ftune only has PLL (1-4x), memory latency tuning and benchmarks due to SH3 MPU design.

* Real-time frequency monitoring — view CPU, SuperHyway, bus, and peripheral frequencies live
* Frequency fine-tuning — adjust frequencies with FLL (450-2047x), PLL (1-64x) and dividers
* Memory latency tuning — adjust wait cycles of RAM, ROM and even LCD controller
* Memory tests — verify RAM and ROM integrity
* Benchmarks — CPU/memory scores, FPS counter and Dhrystone

## Releases

Xtune comes in 2 flavors: Legacy and gint. New features will appear on gint version first.

Legacy | Calculators | Remarks
--- | --- | ---
[Ftune](https://git.planet-casio.com/CalcLoverHK/Xtune/wiki/Ftune) | fx-9860G(II) SH3, Graph 75/85/95 SH3 |
[Ftune2](https://git.planet-casio.com/CalcLoverHK/Xtune/wiki/Ftune2) | fx-9860GII SH4A, Graph 75/95 SH4A |
[Ftune3](https://git.planet-casio.com/CalcLoverHK/Xtune/wiki/Ftune3) | fx-9750/9860GIII, Graph 35+EII | Compatible with models supported by Ftune2
[Ptune2](https://git.planet-casio.com/CalcLoverHK/Xtune/wiki/Ptune2) | fx-CG10/20 |
[Ptune3](https://git.planet-casio.com/CalcLoverHK/Xtune/wiki/Ptune3) | fx-CG50, Graph 90+E |

gint | Calculators | Remarks
--- | --- | ---
Ftune4 (WIP) | All models supported by Ftune2/3 | Does not support SH3 models at the moment as [gint's getkey crashes in overclocked SH3 calculators](https://git.planet-casio.com/Lephenixnoir/gint/pulls/50)
Ptune4 | All models supported by Ptune2/3, fx-CG100, fx-1AU Graph, Graph Math+ | Comes with unified and model-specific versions
CPtune4 | fx-CP400(+E), fx-CG500 |

## License

Legacy and gint releases are licensed under [GPLv2+](__legacy__/LICENSE.md) and [MIT/expat](LICENSE.md) licenses respectively.

## EXTune on EX-word (experimental)

The `exword` branch builds an initial **EXTune** (.d01) port using
[gint-exword](https://github.com/TheRainbowPhoenix/gint-exword),
[JustUI-exword](https://github.com/TheRainbowPhoenix/JustUI-exword), and
[fxsdk-exword](https://github.com/TheRainbowPhoenix/fxsdk-exword).
The build and packaging steps mirror EXWord-Explorer's GitHub Actions CI.

This first stage provides clock frequency and CPG/BSC register inspection,
a local integer benchmark, and a 528x320 EX-word interface with keyboard
navigation and touch tabs. **This is not yet an overclocking build.**
Ptune's calculator-specific PLL presets, memory timings, power controls,
and configuration writes are deliberately excluded. The reported clock rates
use gint's generic SH7305 formula and still need validation on EX-word
hardware. Never flash calculator Ptune profiles to an EX-word.

The `.d01` is packaged under application ID `EXTUN` for `ja` and
`cn` using the same HTML metadata source as EXWord-Explorer.

### Build

Install the toolchain from `fxsdk-exword` branch
`fix/exword-data-load-alignment`, `elf2d01`, the
`feature/exword-runtime-bridge` branch of `gint-exword`,
and `JustUI-exword` (branch `dev`). Then:

```sh
cmake -S . -B build-exword \
  -DCMAKE_TOOLCHAIN_FILE=/tmp/fxsdk-exword/lib/cmake/fxsdk/EXWORD.cmake \
  '-DCMAKE_PREFIX_PATH=/tmp/gint-exword;/tmp/justui-exword' \
  '-DCMAKE_MODULE_PATH=/tmp/gint-exword/lib/cmake/fxsdk;/tmp/justui-exword/lib/cmake/fxsdk;/tmp/fxsdk-exword/lib/cmake/fxsdk' \
  -DEXWORD_METADATA_SOURCE=/tmp/exword-metadata
cmake --build build-exword --parallel 2
```

Outputs: `build-exword/extune00.d01`,
`build-exword/packages/ja/EXTUN/` and
`build-exword/packages/cn/EXTUN/`. See
`.github/workflows/exword.yml` for the full reproducible setup.

**Hardware status:** builds should be treated as untested on physical
EX-word until confirmed; measure oscillator and bus timing before enabling
any clock register writes.

### CN startup diagnostics (2026-11-16)

The initial CN build could immediately return to the dictionary menu without
ever painting a screen. A successful CI build does not prove device startup.

The new pipeline builds **two separate CN/JA applications**:

- `packages/cn/EXPRB/` — `EXTune Probe`, a minimal gint-only renderer
  with **no JustUI, clock-register reads, or timer initialization**.
  It should visibly show "PASS: the EX-word module reached main()." and
  remain on-screen until ESC is pressed.
- `packages/cn/EXTUN/` — `EXTune`. It first displays a **boot checkpoint**,
  prior to JustUI setup, clock snapshots, and benchmark timers. Press EXE to
  enter the About page. Navigate LEFT/RIGHT to test other pages one at a time.
  Clocks, registers, and benchmark are not accessed during initial startup.

For a device that still exits instantly, first run **EXTune Probe**.
If the probe fails too, investigate the EX-word launch/package/gint runtime.
If the probe works but EXTune has no checkpoint, inspect JustUI's
pre-main constructors or other linked drivers.
If the checkpoint works but a subsequent page fails, the corresponding
UI, clock or profiling path is responsible. No overclock writes are enabled.

### EX-word D01 filename compatibility

The first CN builds returned straight to the menu. Comparing their packages to
EXWord Explorer revealed a loader naming inconsistency: previous filenames
were mixed-case (`EXTune.d01`) and the probe exceeded 8 ASCII filename
characters. Explorer uses an eight-character lowercase filename and a
matching six-character identifier in the D01 header.

This revision uses `extune00.d01` (header `extune`) and `extprb00.d01`
(header `extprb`). CI verifies D01 headers, ELF entrypoints, packaged files
and the Japanese/Chinese `addin` metadata. This is a hypothesis, not proof
of a hardware fix. Completely replace the old EXTUN/EXPRB installation folders;
do not rename a D01 file independently of its metadata. Test EXPRB first.
