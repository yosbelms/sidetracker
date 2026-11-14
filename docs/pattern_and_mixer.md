# Pattern and mixer

## Pattern view indicator

In the four-track Pattern view, press **Note** to select the Note column. Press **Note** again to toggle the last two columns. **`[FX]`** in the title bar means **FX1 / FX2**. **`[mV]`** means **Micro-move / Volume (Velocity)**. Tracker CE already has the toggle. SideTracker adds the title-bar indicator, shown with **Config → General → Pattern info → Additional info**.

## Render fader bypass

Turn on **Config → Project Settings → Render fader bypass** to render sample tracks at unity (0 dB). The setting is **Off** by default and saves with the project. It applies to **Render Pattern** and **Render Selection**, including post-fader delay and reverb sends. Instrument levels, per-step volume and velocity, panning, mutes, FX, and synth levels still apply. Playback, song exports, and stem exports use the saved mixer levels.

## Master and track meter readouts

The top right of the **Master** screen shows the live digital output peak in **dBFS**. On **Track Mixer 2/3** (called **Sample Track Mixer 2/3** on Tracker+ and Mini), the number above each track meter shows its averaged signal level in dB. The dB values along the bottom show the fader gain settings.

**Master 1/3** shows a single FFT curve above **Delay**, **Reverb**, **Saturation**, and **Limiter**, with frequency ticks from **50 Hz** to **20 kHz**. The curve uses a fixed level scale and follows the regular digital master after the software effects and limiter. **Space**, **Bass Boost**, **EQ**, and headphone **Volume** act downstream. [Master audio and export routing](master_audio_routing.md)


## Master Saturation and Perf FX Mix

On **Master 1/3**, press **F4** to select a saturation **Mode** or adjust **Drive** from −12 to +12 dB in 0.5 dB steps. Press Delete to reset the focused control or **F8** to go back. The project saves both settings.

On **Global Mixer 3/3**, the **Perf FX Mix** fader reaches the other faders’ 0 dB mark at 100%. Hold **F5** to adjust the mix with the jog wheel or arrows. Press Delete while holding F5 to return to 100%.
