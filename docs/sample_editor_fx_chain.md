# Sample-editor FX chain

For everyday controls and patch installation, see the [Sample Editor quick guide](sample_editor.md).

The Sample Editor opens on the waveform, with **Preview, Start, End, Zoom, Undo, Effects, Tools** controls, in that order. The waveform spans the screen width. **Effects** builds a processing chain for the whole sample. Preview runs supported stages live, with parameter edits heard during playback. Apply processes the chain into the current sample slot, and Render to new slot creates a variation.

The Chain view uses the Sample Loader’s browser-and-slots arrangement, followed by parameter editing:

| Column | Purpose |
| --- | --- |
| Effects | Persistent browser of available built-in effects and `.ptfx` effects/generators |
| Chain | Ordered processing stages and any selected generator; context, chain, and destination menus open here |
| Parameter | Parameters for the selected stage |
| Value | Numeric bar or list of choices for the selected parameter |

Effects occupies 3/8 of the screen, Chain and Parameter each occupy 2/8, and Value occupies the final 1/8 at the right edge.

Four native STPalette entries—Character, Movement, Diffusion, and Texture—each expose six algorithms through Type. Each instance runs only its selected Type. Type lists use the Value column; Sync and Tail appear only where relevant. The chain row displays the category and selected Type. Native preview and Render share their DSP and resolved quality profiles. Economy mode is selected when the budget requires it; original Tracker timing estimates still require hardware calibration. A STPalette stage's Insert menu offers Measure CPU, saving a selected-Type report to `/Effects/STPaletteCPU.csv`. See [Native STPalette](../core/effector/stpalette/README.md).

Left/Right moves between columns. Up/Down and the encoder select entries; in Value, the encoder changes the value. Browsing Effects never replaces a chain stage or changes its settings. **Add** appends the browsed effect and keeps focus in the browser, so several effects can be added quickly. Repeated Add creates independent instances. Select a stage in Chain and press the physical **Insert** key to open its context menu for bypass, duplicate, replacement, removal, and chain operations. **Delete** removes the selected chain stage; in Value it restores the parameter default.

The chain toolbar is **Preview, Add, Back, Apply, Actions, Mix, empty, Value**. **Actions** is enabled from every column and opens the menu for the selected chain stage. Press it, or physical **Insert** in Chain, to replace the Chain column with its action menu; Parameter and Value, including their headers and the Value footer, are hidden until the menu closes. The Value button sits directly below its bar on the eighth physical button and shows the selected parameter name with its current value, like Sample Loader's Preview Vol control. Press it to focus value editing. The gray status strip is removed; progress and errors appear in the title.

## Whole-chain Mix

**Mix** shows the chain’s global wet/dry percentage in its toolbar button. Hold Mix while turning the jog wheel or pressing arrows: Up/Right increases it, Down/Left decreases it. Releasing Mix restores normal navigation and parameter editing. Tapping Mix does not change focus, and Mix never replaces the selected parameter in the Value bar. The remaining empty toolbar slot keeps its separator lines, matching Sample Loader and Pattern Fill.

0% is the dry source, 100% is the complete processed chain, and intermediate values use a linear blend after all stages. Mix consumes no stage slot. Live changes ramp over 10 ms without restarting the source or clearing effect history; effects continue running at 0%. Known STPalette resampling latency is compensated on the dry path. Musical delays and modulation remain part of the wet sound. Dry becomes silence after its source ends, while wet tails retain the chain’s output length and are scaled by Mix.

With a generator, dry means that generator’s output before subsequent effects. Live preview retains only a small block/delay history for this path. Prepared preview and Render retain the wet result, read dry audio from the existing source snapshot (or a separate generator snapshot), and blend in bounded foreground steps using shared scratch and the other working buffer. Editing Mix on a prepared result reuses the wet audio; it does not rerun its effects. Declared plugin prepends are aligned during this pass; unreported plugin latency cannot be automatically compensated.

Patches store the chain’s Mix as a percentage. The patch generator accepts an optional `"mix": 50` setting in `patches.json`. The default is 100%.

## Build a sound

1. The current sample is the default source. To generate a new sound, browse a `[Gen]` entry in Effects and press **Use src**. Generators work on empty sample slots too. The current sample is implicit input and appears in neither list. Select a generator and press **Delete** in Chain to remove it and return to sample input.
2. Browse a built-in or `.ptfx` processing effect in Effects and press **Add**. It appears at the end of Chain. A chain supports eight processing stages plus its source.
3. Move Right to Chain, select a stage, then Right to Parameter. Select a parameter and press its button below Value, or move Right again, to edit it with the encoder. **Delete** restores its default. Generator settings include the output length.
4. Focus Chain and open **Actions** to reorder the selected stage with **Move Up / Move Down**, or to bypass, duplicate, replace, or remove stages. Use **Load Patch** or **Save Patch** to reuse chain settings. Duplicate stages have independent settings.
5. Press **Preview** to audition the chain live. Parameter edits take effect on the next 128-frame audio block, without restarting the source. Structural edits (add, bypass, reorder, replace) rebuild and restart the live preview automatically. **Stop** ends auditioning.
6. Press **Apply** to process the chain into the current instrument. The waveform and existing pattern notes for that slot use the edited audio as soon as Apply finishes. Tools uses the same workflow with its **Apply** button.
7. To make a variation, choose **Actions → Render to new slot**, select an empty sample slot, and press **Select**. CE selects the rendered instrument and closes Effects. The source instrument remains available.

**Undo** on the waveform restores the previous sample for the most recent Apply, including its pattern playback. It remains available when switching views within the same project. A failed Apply keeps the draft in the editor and reports the error.

Live preview reads the immutable sample snapshot once before starting, then processes blocks in the audio graph; it does not render the full chain first. Playback is one-shot, including supported tails. Apply and Render can run a separate complete pass, so randomized generators can produce a different take. Apply and Render can reuse a prepared preview until its settings change.

**Back** returns immediately to the waveform without confirmation and keeps the constructed chain. Main-view **Preview** plays through that draft, and reopening Chain restores it. Physical view buttons such as Master stop auditioning, restore the source buffers, and switch views; the draft remains available for the same sample until restart, Apply, or Render. Applying or rendering closes the active chain and starts the next session with an empty processing list, preventing accidental double processing. The most recently applied or rendered settings remain available as **Last Used** until restart.

## Sources and effects

Generators are visible in Effects alongside processing plugins, marked `[Gen]`. When selected with **Use src**, they occupy the source row. They generate new audio rather than transforming an incoming sample. They can be followed by built-in effects and `.ptfx` effects in any supported order. All stages process the complete output of the preceding stage, including added tails.

The chain browser contains amplifier, overdrive, delay, bitcrusher, chorus, flanger, EQ, limiter, and compressor. **Sample Editor → Tools** contains Crop, Reverse, Normalizer, Fade In, Fade Out, WT Smoother, Timestretch Beat, and Timestretch Note in a left-hand column matching the chain browser. Tools use the current waveform selection; the Chain processes the whole sample. Select any tool to open **Preview**, **Back**, and **Apply**. Preview plays the resulting sample, including the audio outside the selection. Back returns to the waveform without saving the tool output. Apply saves the result in the current slot. Tools with settings display their parameter controls on the same page.

## Patches

Open **Actions** or press physical **Insert** while focused on Chain. **Save Patch** saves the chain settings, including source, stage order, parameters, bypass, Mix, and any resolved quality profiles. Choose a numbered destination such as `Patch01` or replace a listed patch. **Load Patch** replaces the working chain with the saved settings. A patch with the current sample as its source uses the selected sample.

The browser lists up to 32 `.patch` files in `/Effects/Patches/` and displays their filenames without extensions. The format is PTPR v1. To reuse a PTPR v1 `.ptpreset` file, copy it to `/Effects/Patches/` and change its extension to `.patch`.

The [STPalette showcase pack](../patches/stpalette/README.md) provides ready-to-load patches covering all 24 native algorithms, with descriptive filenames and a guide to their settings.

To generate the SD files and the ZIP, run this from the repository root:

```sh
make sd
```

The generator reads `patches/stpalette/patches.json` and validates the settings with the native effects registry. It stages files such as `build/SD/Effects/Patches/Worn Cassette.patch` and creates `build/STPaletteShowcase.zip`. The portable core library builds automatically.

Copy the pack’s `Effects` folder to the SD-card root, preserving any existing files with the same names before replacing them. Open **Load Patch** and select the patch by name.

If a plugin is missing or its contents changed, re-add it, remove the stage, or explicitly bypass it. A missing generator must be replaced. Recompiling a plugin changes its fingerprint even if the filename stays the same.

## Live and prepared preview

The nine built-in effects in the chain browser support live preview. The runner also processes normalization and fades. Each stage has independent DSP history, including repeated effects. PTFX effects and generators use separate copies of their executable code, BSS, and scratch memory; callbacks run on the audio thread. Plugin UI metadata stays in the editor's separate shared loader.

Normalization in a chain needs whole-input analysis, and plugins that prepend audio need a shifted upstream timeline. These chains automatically use **Prepared preview**. Chains that cannot fit private live state also use prepared processing; no effect is silently bypassed. Render remains available for these chains. Changes to a prepared preview need another Preview.

Length changes that outgrow a live delay's history, or switch a plugin into prepending, rebuild preview automatically. Ordinary parameter changes preserve DSP history and playback position. Existing PTFX v1 plugins must process 128-frame blocks and finish each callback promptly to work live. CE validates their returned lengths but cannot preempt plugin code; a plugin that blocks or takes too long requires offline use. CPU headroom still depends on the device and the chosen chain.

## Limits and behavior

- Processing covers the whole sample. The editor's existing individual effects still support selections.
- Chain processing uses the editor's existing length limit. An intermediate stage or delay tail that exceeds that limit is rejected.
- Original Tracker processes mono. Tracker+/Mini retain supported stereo processing and mono conversion. A plugin without stereo support requires a mono input.
- Wavetable generators use complete 2048-frame windows for host length adjustment. Final wavetable audio must fit its instrument's window size.
- Length-changing chains reset loop/slice playback metadata to a one-shot result when there is no valid general mapping. Wavetable sources retain wavetable mode when the output has complete windows.
- Preview and rendering use shared editor memory and require stopped song playback. Keep the SD card inserted: CE stores a temporary source snapshot under `Workspace/FxChainTemp` so repeated previews can rebuild safely without retaining audio for every stage.
- **FX Chain: RAM full** means resident sample audio overlaps the editor's working region. Free sample RAM before opening the chain. Rendering also checks that a new instrument's resident data fits outside that region.
- Rendering and whole-input analysis run in bounded foreground work steps. Live DSP runs in the audio callback, with no filesystem access or allocation in its processing path.
- ARM `.ptfx` binaries execute on the device. Desktop can add/configure plugins and save patches using validated metadata; requesting preview/render with an active plugin displays **PTFX: Tracker only**. Native generator/adapter tests cover the plugin processing contract separately.

Save the project normally to retain rendered instruments. Patches are saved independently of the project.
