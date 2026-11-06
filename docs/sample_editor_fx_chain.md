# Sample-editor FX chain

The Sample Editor opens on the waveform, with **Preview, Start, End, Zoom, Undo, Chain, Tools** controls, in that order. The waveform spans the screen width. The original effects list is replaced by **Chain**, which prepares a complete sound before saving it into a new sample slot. Preview runs supported stages live, with parameter edits heard during playback. Render processes the complete chain into a new ordinary sample.

The Chain view uses the Sample Loader’s browser-and-slots arrangement, followed by parameter editing:

| Column | Purpose |
| --- | --- |
| Effects | Persistent browser of available built-in effects and `.ptfx` effects/generators |
| Chain | Ordered processing stages and any selected generator; context, recipe, and destination menus open here |
| Parameter | Parameters for the selected stage |
| Value | Numeric bar or list of choices for the selected parameter |

Effects occupies 3/8 of the screen, Chain and Parameter each occupy 2/8, and Value occupies the final 1/8 at the right edge.

Left/Right moves between columns. Up/Down and the encoder select entries; in Value, the encoder changes the value. Browsing Effects never replaces a chain stage or changes its settings. **Add** appends the browsed effect and keeps focus in the browser, so several effects can be added quickly. Repeated Add creates independent instances. Select a stage in Chain and press the physical **Insert** key to open its context menu for bypass, duplicate, replacement, removal, and recipe operations. **Delete** removes the selected chain stage; in Value it restores the parameter default.

The chain toolbar is **Preview, Add, Move Up, Move Down, Back, Render, empty, Value**. The Value button sits directly below its bar on the eighth physical button and shows the selected parameter name with its current value, like Sample Loader's Preview Vol control. Press it to focus value editing. The gray status strip is removed; progress and errors appear in the title.

## Build a sound

1. The current sample is the default source. To generate a new sound, browse a `[Gen]` entry in Effects and press **Use src**. Generators work on empty sample slots too. The current sample is implicit input and appears in neither list. Select a generator and press **Delete** in Chain to remove it and return to sample input.
2. Browse a built-in or `.ptfx` processing effect in Effects and press **Add**. It appears at the end of Chain. A chain supports eight processing stages plus its source.
3. Move Right to Chain, select a stage, then Right to Parameter. Select a parameter and press its button below Value, or move Right again, to edit it with the encoder. **Delete** restores its default. Generator settings include the output length.
4. **Move Up / Move Down** reorder the selected stage directly. Use physical **Insert** in Chain for bypass, duplication, replacement, and recipes. Duplicate stages have independent settings.
5. Press **Preview** to audition the chain live. Parameter edits take effect on the next 128-frame audio block, without restarting the source. Structural edits (add, bypass, reorder, replace) rebuild and restart the live preview automatically. **Stop** ends auditioning.
6. Press **Render** to process the full chain using its current settings, select an empty sample slot, and press **Select**. CE saves the prepared audio, selects the new instrument, and closes the chain. The original sample remains available.

Live preview reads the immutable sample snapshot once before starting, then processes blocks in the audio graph; it does not render the full chain first. Playback is one-shot, including supported tails. Render is a separate complete pass, so randomized generators can produce a different take. A prepared preview remains reusable by Render until its settings change.

**Back** returns immediately to the waveform without confirmation and keeps the constructed chain. Main-view **Preview** plays through that draft, and reopening Chain restores it. Physical view buttons such as Master stop auditioning, restore the source buffers, and switch views; the draft remains available for the same sample until restart or rendering. Rendering closes the active chain and starts the next session with an empty processing list, preventing accidental double processing. The most recently rendered settings remain available as **Last Used** until restart.

## Sources, effects, and recipes

Generators are visible in Effects alongside processing plugins, marked `[Gen]`. When selected with **Use src**, they occupy the source row. They generate new audio rather than transforming an incoming sample. They can be followed by built-in effects and `.ptfx` effects in any supported order. All stages process the complete output of the preceding stage, including added tails.

The chain browser contains amplifier, overdrive, delay, bitcrusher, chorus, flanger, EQ, limiter, and compressor. **Sample Editor → Tools** contains Crop, Reverse, Normalizer, Fade In, Fade Out, WT Smoother, Timestretch Beat, and Timestretch Note in a left-hand column matching the chain browser. Tools use the current waveform selection; the Chain processes the whole sample. Normalizer opens its target-level control before applying; Fade In and Fade Out apply directly. Existing recipes containing these three effects remain supported.

Press physical **Insert** while focused on Chain and choose **Load Recipe** to load a saved chain. **Save Recipe** writes one of 16 recipe slots under `/Effects/Chains/Chain01.ptchain` through `Chain16.ptchain`. Recipes store settings and plugin fingerprints, not audio. A Current Sample recipe uses the sample selected when loaded. Loading a recipe replaces the current draft settings; saving into a numbered slot replaces that recipe.

If a plugin is missing or its contents changed, re-add it, remove the stage, or explicitly bypass it. A missing generator must be replaced. Recompiling a plugin changes its fingerprint even if the filename stays the same.

## Live and prepared preview

The nine built-in effects in the chain browser support live preview. Older recipes containing normalization or fades remain loadable and render correctly. Each stage has independent DSP history, including repeated effects. PTFX effects and generators use separate copies of their executable code, BSS, and scratch memory; callbacks run on the audio thread. Plugin UI metadata stays in the editor's separate shared loader.

Normalization in an older recipe needs whole-input analysis, and plugins that prepend audio need a shifted upstream timeline. These chains automatically use **Prepared preview**. Chains that cannot fit private live state also use prepared processing; no effect is silently bypassed. Render remains available for these chains. Changes to a prepared preview need another Preview.

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
- ARM `.ptfx` binaries execute on the device. Desktop can add/configure plugins and save recipes using validated metadata; requesting preview/render with an active plugin displays **PTFX: Tracker only**. Native generator/adapter tests cover the plugin processing contract separately.

Save the project normally to retain rendered instruments. Recipe saving is independent of project saving.
