# Sample Editor

- **Waveform** — Use **Start**, **End**, and **Zoom** to set and inspect a selection. **Tools** opens selection edits such as Crop, Normalizer, and fades. Use **Preview** to audition, **Back** to leave, and **Apply** to update the current sample slot. **Undo** on the waveform restores the previous audio after Apply.
- **Effects** — Build a whole-sample chain of up to eight stages. Browse **Effects**, press **Add**, then select a stage in **Chain** and edit its **Parameter** and **Value**. For a generator marked `[Gen]`, press **Use src**. Use **Actions** to reorder, bypass, or remove stages. Hold **Mix** and turn the jog wheel or use the arrows to blend the whole chain.
- **Preview and apply** — **Back** keeps the draft, so waveform **Preview** still plays through it. **Apply** updates the current slot, and existing notes using that slot play the edited audio. Use **Actions → Render to new slot** for a separate sample. If realtime preview exceeds the audio budget, **Apply** is still available.

## ST effects

**ST Character**, **ST Movement**, **ST Diffusion**, and **ST Texture** are native effects in the Effects browser. Each has six algorithms under **Type**. Add a category, choose a Type, then adjust **Amount**, **Macro**, or **Drift**. **Sync** and **Tail** appear for Types that use them.

## Presets

Use **Effects → Actions → Save Preset** or **Load Preset** to reuse a chain. A preset stores the source choice, stage order, settings, and Mix. A preset that uses the current sample works with whichever sample is open. Presets live on the SD card in `/Effects/Presets/` as `.ptpreset` files.

Run `python3 tools/generate_stpalette_presets.py` from the repository root to write 20 showcase presets to `build/STPaletteShowcase/Effects/Presets/` and package them in `build/STPaletteShowcase.zip`. Copy the generated `Effects` folder to the root of the Tracker SD card or desktop virtual SD card. Load a preset by name, such as **Worn Cassette** or **Granular Warp**.

[Showcase preset list](../presets/stpalette/README.md#presets) · [Detailed chain guide](sample_editor_fx_chain.md) · [Live and sequenced Perf FX](perf_fx.md)
