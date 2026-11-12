# Sample-editor FX chain proposal

Status: proposal for review. No firmware implementation is included.

## Recommendation

Add an offline sound-design workflow to the Sample Editor:

**Source → ordered processing effects → processed preview → render to a new sample slot.**

The source can be the current sample or a `.ptfx` sound/wavetable generator. Processing stages can be built-in effects or `.ptfx` effects. Users can reorder, bypass, duplicate, and edit stages before committing the result.

The first release should process whole samples, support up to eight processing stages plus one source, and render into an empty sample slot. Eight is a proposed UI/chain limit, not a measured hardware limit. The design should reuse bounded working memory regardless of stage count.

The result is ordinary sample audio. The chain adds no continuing FX processing during song playback once rendered.

## Intended experience

Two representative workflows:

| Starting point | Source | Processing chain | Result |
| --- | --- | --- | --- |
| Recorded drum hit | Current sample | EQ → overdrive → compressor | A new processed drum sample; original remains available |
| Empty sample slot | Analog Kick Gen `.ptfx` | EQ → bitcrusher → limiter | A generated and processed kick, ready to use as an instrument |

1. Open **Effects** from the Sample Editor. Existing individual edit operations remain available.
2. Choose **Current Sample** or **Generator** as the source. Generator mode works on empty slots. An empty slot initially opens the source chooser.
3. Add processing effects and adjust their parameters using the existing parameter-control style.
4. Choose **Preview**. CE prepares the audio, shows progress, and plays the completed result. It does not run the chain continuously while encoders move.
5. Change parameters or order, then preview again. The interface marks the previous result as out of date immediately after a change.
6. Choose **Render** and an empty destination slot. CE commits the prepared result, selects the new instrument, and closes the active chain session.

If Render is requested with an out-of-date result, prepare it first. If the result is current, commit those exact bytes without rerunning DSP. This matters for generators that use random noise.

After rendering, retain the chain as **Last Used**, but do not attach it automatically to the new sample. Reopening Effects starts from the new sample with an empty processing list. This prevents accidental double processing.

## Source and stage behavior

### Source

The source is a separate row at the top of the chain. There is exactly one source; generators are offered in the source browser and transforming effects in the processing browser.

| Source | Settings | Behavior |
| --- | --- | --- |
| Current Sample | Current instrument; whole sample in the first release | Snapshot the current confirmed editor audio, including edits already made in this session |
| `.ptfx` generator | Plugin parameters; host target length; supported generator/wavetable settings | Generate a fresh source, then feed it through the processing stages |

A generator replaces the source rather than appearing midway through a chain. Mixing multiple generators or replacing audio midway would need additional routing and is outside this proposal's first release.

Changing the source preserves the processing-stage settings and invalidates the prepared result. Original/Processed audition is available for sample sources. A generator source has no original sample to compare against; the first release offers processed audition only.

The existing `GENERATOR`, `WAVETABLE`, `STEREO`, `CONVERT_TO_MONO`, and `IMX_ONLY` flags must remain meaningful. `EFFECT` is currently the zero/default flag, so classification must check whether the generator bit is set rather than testing for a nonzero effect bit.

### Processing stages

Each stage stores its effect identity, parameter values, and bypass state. Duplicate stages have independent settings, including repeated instances of the same `.ptfx` file.

Execute stages strictly in displayed order. A bypassed stage passes its input through without DSP or length/channel changes. An empty chain is valid for copying a sample or rendering a generator alone.

Initial built-in candidates are gain, EQ, overdrive, bitcrusher, chorus, flanger, compressor, limiter, delay, normalize, and fades. Admit each after checking its reset, bounds, and completion behavior. Whole-sample analysis effects such as normalize are supported by complete stage passes rather than requiring every effect to stream through a block pipeline.

Trim/cut and reverse can continue as ordinary editor operations initially. Timestretch and wavetable smoothing can join the chain after their length and metadata behavior is validated. Existing standalone functions remain usable throughout.

### Length, channels, and metadata

Run each stage over the full output of its predecessor, including any added tail. A downstream limiter or fade therefore also processes the delay tail.

- Use each effect's existing length behavior initially. Display the resulting duration; do not silently truncate audio or add a new generic tail algorithm.
- Check the next output size before writing it. Length extensions requested during plugin initialization must be checked after initialization and before processing begins.
- Reject an oversized stage with its name and required/available capacity. Keep the draft settings and source intact.
- Respect mono processing on Original Tracker and the supported stereo behavior on Tracker+/Mini. A mono-conversion stage updates the channel layout passed downstream.
- Carry generator/wavetable and channel metadata alongside audio. Commit it once at the end, after validating that subsequent effects leave it meaningful. Do not run instrument-mutating plugin commit hooks between stages.
- Preserve valid instrument settings when copying a sample source. Clamp sample/loop/slice positions to the output and validate mappings; do not leave positions outside the new sample. Generator sources start with appropriate defaults and their supported generator metadata.

The first release processes whole samples. Selection processing is a later extension: transform one selected region through the chain, splice the result once, and define tail insertion plus loop/slice relocation explicitly. The existing single-effect selection workflow remains available meanwhile.

## Controls and screen layout

A proposed layout, to be fitted to each device's existing display and controls:

```text
FX CHAIN                         3 / 8
Source   Analog Kick Gen
01       EQ
02       Overdrive
03       Limiter

Result: Needs preview

Add      Edit      Actions      Preview      Render
```

The source row opens its own settings. Selecting an effect opens its parameters. An Actions menu contains Bypass, Move Up/Down, Duplicate, Remove, Load Patch, and Save Patch; this avoids fitting every operation onto a dedicated soft key. Exact button assignments need a separate screen-layout review on Original, Mini, and Tracker+.

Use visible states: **Needs preview**, **Preparing**, **Ready**, and **Failed**. During preparation show the current stage, stage count, progress, and Cancel. Do not display an inaccurate overall time estimate for analysis passes or plugin loading.

Changing a parameter marks the result out of date without immediately rerendering. Preview is an explicit action in the first release. Stop sample audition before overwriting its playback buffer. Original/Processed comparisons switch safely with a short transition or restart; they need not be phase-aligned live crossfades.

Render targets empty slots in the first release. Replacing the current sample and multi-step undo are later additions. Users can discard an unneeded new instrument through the existing instrument workflow. Leaving a draft offers Keep Editing or Discard. Saving a patch is a separate action.

## Patches and persistence

Save reusable chains as versioned files on the SD card, using `.patch` files. Store them under `/Effects/Patches/`.

A patch stores the chain’s source mode, optional generator identity/settings, ordered effect identities, parameter values, bypass states, and host generation-length settings. A Current Sample patch binds to the sample selected when loaded. The file contains the settings only.

Use stable built-in IDs. For plugins, store a path plus a content fingerprint and parameter-layout information. A renamed, missing, or changed plugin must be resolved explicitly, rather than substituting another effect or guessing parameter compatibility. Unsupported effects stay visible in the draft and block rendering until resolved, removed, or explicitly bypassed. An unresolved generator blocks generation.

Chains initially store parameter values according to the matching effect version/layout. Introducing parameter IDs across plugin revisions can be a later SDK change. A chain alone does not guarantee that a randomized generator recreates identical audio; rendered samples remain the authoritative saved sound.

No project-format change is required for the first release. Saving a chain does not save the instrument or project, and rendering follows the existing project sample-save behavior.

## Processing and memory design

### Sequential offline execution

Use a chain controller that owns the draft, source snapshot, working-buffer roles, processing state, and final result. Existing effects provide DSP through adapters. Chain execution must not call the existing Apply operation after every stage: its buffer swapping and undo behavior represent an editor commit, not a private intermediate pass.

Render one complete stage at a time, then pass its output to the next. This accommodates analysis passes, length-changing effects, and the current plugin loader. Every stage is initialized afresh and receives its stored parameters before processing.

For `.ptfx`, load the stage's binary into the shared executable area, reset scratch/state, restore that stage's parameters, process its complete input, then release its use of the loader. Repeat for the next stage. Two uses of the same plugin are independent sequential executions.

This approach should work with the existing plugin ABI; it does not require several live instances to coexist. It still requires verifying that initialization/reloading resets all relevant plugin state. New capabilities or an ABI revision should be introduced only if a concrete incompatibility requires them.

### Preserve the source without allocating one buffer per stage

The current editor has confirmed and processed areas per channel. Alternating those two areas through several passes can overwrite the original; their existing one-step undo cannot preserve the entire chain's source automatically.

Recommended baseline:

1. Create an immutable temporary SD snapshot of the confirmed source and required metadata before processing. For generator mode, retain the generator settings; no input-audio snapshot is needed.
2. Alternate two bounded working areas for stage input/output. Retain only the completed final result for audition and Render.
3. Rebuild from the source snapshot or generator when settings change. Do not keep full audio caches for every stage in the first release.
4. Keep the source snapshot until the session is committed or discarded. Do not assume an original WAV on disk contains the current unsaved editor edits.

Temporary files should be created and closed with checked I/O, have a session-specific identity, and be cleaned up safely after cancellation or a later restart. SD space or write failures must be reported before relying on the snapshot. Peak working RAM should depend on the largest intermediate audio plus the active effect's state, rather than the sum of every stage's audio and scratch requirements.

This trades SD access and render time for predictable RAM use. The exact buffer map must be checked on all platforms before implementation; editor, sample playback, SD streaming, and `.ptfx` code/scratch can share or overlap resources. Do not treat the shared scratch area as an extra free audio buffer.

### Scheduling and commit

Keep processing outside the audio callback. Use bounded work and return regularly to the UI; analysis scans and plugin setup need attention as well as process blocks. Cancel takes effect at the next safe host work boundary. Arbitrary native plugin code cannot be forcibly interrupted safely through the current ABI.

For the first release, stop sample audition and song playback before taking shared resources for preparation. Explain this behavior in the UI. Concurrent playback can be explored after ownership and timing are measured.

Cancellation or a failed stage never commits intermediate output. The previous preview may have been overwritten by working passes; mark it unavailable and allow rebuilding from the retained source rather than promising to preserve a second cached result.

Publish a new instrument only after the final audio, destination capacity, and metadata are valid. On a failed write or allocation, roll back destination creation and retain the draft. Commit the exact prepared audio, including generator randomness, without another render pass.

## First-release scope and delivery order

| Step | Deliverable | Completion condition |
| --- | --- | --- |
| 1 | Chain/session model and offline runner | Whole-sample sequential stages, source protection, bounded buffers, failure/cancel handling |
| 2 | Built-in stage adapters and generator/plugin sources | Repeated stages reset independently; generator into empty slot; lengths/channels validated |
| 3 | Chain screen, parameters, audition, and new-slot render | User can complete both example workflows; Render matches preview exactly |
| 4 | Patch save/load and validation | Round trips preserve settings; missing/changed plugins are handled explicitly |
| 5 | Original/Mini/Tracker+ validation | Resource ownership, maximum lengths, UI responsiveness, and render timing verified |

All five steps make up the proposed first release. Live master/track chains, song-selection bounce, automatic processing while encoders move, per-stage audio caching, sample replacement, selection processing, and multiple generator mixing are follow-up work.

## Validation and acceptance criteria

- A one-stage built-in chain matches the equivalent standalone effect for approved deterministic fixtures, or any intentional difference is documented.
- Effect order changes output where expected. Bypass is an exact pass-through. Duplicate instances retain independent parameters and reset history correctly.
- Built-in and `.ptfx` stages can be interleaved, including repeated uses of one plugin. Generators work on empty slots and feed downstream effects.
- Rendered audio matches the prepared preview bytes, including randomized generators. Parameter edits invalidate the result and cannot commit stale audio.
- Source bytes and instrument metadata remain intact during draft editing, preview, cancellation, and failures. New-slot creation has a defined rollback path.
- Test zero-length/silent audio, analysis effects, maximum output length, expanding tails, stereo/mono transitions, generator/wavetable metadata, unsupported plugins, SD-full/write failures, and project/instrument changes during a session.
- Measure total preparation time, longest UI work interval, peak RAM, temporary storage, and cancellation response on hardware. Slot count alone is not a performance benchmark.
- Confirm that ordinary song playback after rendering does not execute the chain.

## Code grounding

The proposal is based on these existing components, not an assumption that CE already has a general chain host:

- [`mtEffect.h`](../core/effector/mtEffect.h) and [`mtEffect.cpp`](../core/effector/mtEffect.cpp): confirmed/processed buffers, selections, staged processing, Apply, and one-step Undo.
- [`fSampleEdit.cpp`](../core/interface/sampleEditor/fSampleEdit.cpp): current parameter, process, preview, and commit workflow.
- [`mtSampleEditorEngine.cpp`](../core/effector/mtSampleEditorEngine.cpp): built-in effect catalog and parameter descriptions.
- [`mtEffectModular.cpp`](../core/effector/mtEffectModular.cpp), [`ptfx_loader.cpp`](../core/effector/ptfx_loader.cpp), and [`tracker_plugin.h`](../core/effector/tracker_plugin.h): generator mode, plugin initialization, shared code/scratch, channel/length flags, and block processing.
- [`analog_kick_gen.c`](../editor-fx-sdk/effects/analog_kick_gen.c): an existing generator with randomized click audio, motivating exact preview-to-render reuse.

Product precedents are M8's selection-to-sample workflow, SP-404MKII's resampling workflow, and Move's consistent effect choices across contexts. Those are workflow references; no DSP throughput or achievable CE slot count is inferred from their specifications.
