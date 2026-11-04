# Sample-editor FX chain

Open **Sample Editor → Chain** to prepare a complete sound before saving it into a new sample slot. The chain runs offline. Once rendered, the result plays as an ordinary sample.

## Build a sound

1. Choose the **Source** row. Use the current sample, or choose a `.ptfx` sound/wavetable generator from `/Effects`. Empty sample slots open the generator browser.
2. Press **Add** to append a built-in or `.ptfx` processing effect. A chain supports eight processing stages plus its source.
3. Select a stage and press **Edit**. Up/Down selects a parameter; the encoder changes its value. **Reset** restores its default. Generator settings include the output length.
4. Use **Actions** to bypass, move, duplicate, or remove a stage. Duplicate stages have independent settings.
5. Press **Preview** to prepare and audition the result. Parameter changes require another Preview. **Original** auditions the original sample when the source is an existing sample; it preserves the prepared processed result.
6. Press **Render**, select an empty sample slot, and press **Select**. CE saves the exact prepared audio, selects the new instrument, and closes the chain. The original sample remains available.

Render can also prepare the audio if Preview has not been run. A current preview is committed without regenerating it, so a randomized generator's saved sound matches what was auditioned.

Use **Back → Keep Editing / Discard Chain** to leave a draft. Rendering closes the active chain and starts the next session with an empty processing list, preventing accidental double processing. The most recently rendered settings remain available as **Last Used** until restart.

## Sources, effects, and recipes

Generators occupy the source row. They generate new audio rather than transforming an incoming sample. They can be followed by built-in effects and `.ptfx` effects in any supported order. All stages process the complete output of the preceding stage, including added tails.

The first version includes normalize, amplifier, overdrive, delay, bitcrusher, chorus, flanger, EQ, limiter, compressor, fade in, and fade out. Crop, reverse, timestretch, and wavetable smoothing remain available through the existing individual-effect workflow.

**Recipes** loads a saved chain. **Actions → Save Recipe** writes one of 16 recipe slots under `/Effects/Chains/Chain01.ptchain` through `Chain16.ptchain`. Recipes store settings and plugin fingerprints, not audio. A Current Sample recipe uses the sample selected when loaded. Loading a recipe replaces the current draft settings; saving into a numbered slot replaces that recipe.

If a plugin is missing or its contents changed, re-add it, remove the stage, or explicitly bypass it. A missing generator must be replaced. Recompiling a plugin changes its fingerprint even if the filename stays the same.

## Limits and behavior

- Processing covers the whole sample. The editor's existing individual effects still support selections.
- Chain processing uses the editor's existing length limit. An intermediate stage or delay tail that exceeds that limit is rejected.
- Original Tracker processes mono. Tracker+/Mini retain supported stereo processing and mono conversion. A plugin without stereo support requires a mono input.
- Wavetable generators use complete 2048-frame windows for host length adjustment. Final wavetable audio must fit its instrument's window size.
- Length-changing chains reset loop/slice playback metadata to a one-shot result when there is no valid general mapping. Wavetable sources retain wavetable mode when the output has complete windows.
- Preview and rendering use shared editor memory and require stopped song playback. Keep the SD card inserted: CE stores a temporary source snapshot under `Workspace/FxChainTemp` so repeated previews can rebuild safely without retaining audio for every stage.
- **FX Chain: RAM full** means resident sample audio overlaps the editor's working region. Free sample RAM before opening the chain. Rendering also checks that a new instrument's resident data fits outside that region.
- Built-in analysis and DSP run in bounded work steps. Native `.ptfx` callbacks must return promptly; the host cannot interrupt a callback while it is executing.
- ARM `.ptfx` binaries execute on the device. The desktop simulator rejects executing them; native generator/adapter tests cover the plugin processing contract separately.

Save the project normally to retain rendered instruments. Recipe saving is independent of project saving.
