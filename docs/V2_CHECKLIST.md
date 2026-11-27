I would focus **NAMplified V2** on three goals. Improve how the rig feels to play, make it easy to record an idea as soon as it comes up, and give guitarists a way to discover and buy complete artist rigs through YouTube. My picks for additional features are **expression pedal control** and **a stereo effect block with selectable placement**.

## Ideas and decisions

**Playing feel, effects, and controls.** For the pitch shifter, I would start with one concrete use case. Drop the guitar one or two semitones virtually and check that fast palm-muted riffs and open chords both stay clear. Focus on latency, clean transients, and minimal audible artifacts. For V2 effects, I would prioritize three sounds you can evaluate yourself and demonstrate in videos: **wide stereo chorus for cleans, stereo delay with tap tempo for leads, and natural-sounding room and plate reverb**. The layout should let you open a rig, see the signal path, and reach the control you want in one click. Give the selected block large controls while keeping the entire chain visible.

**The looper and DI recording serve different purposes.** In the looper, choose **1, 2, or 4 bars**, then set the tempo or sync to the DAW. After an optional count-in, recording starts on beat one and automatically switches to playback at the end of the selected length. Keep the existing free-length mode. For longer ideas, use **Rec DI**. Choose a destination folder once, press the button, and play through the current rig. NAMplified records the dry input as a WAV while you play. It could optionally save a second, synchronized WAV with the processed stereo rig. **Playing the DI back through the rig** would be the most useful addition. Record once, then adjust captures, IRs, and effects with the same take. This complements the existing level-matched A/B comparison.

**Expression pedal.** Right-click a control to enable **MIDI Learn**, move the pedal, set the heel and toe values, and save the assignment with the rig. One pedal should be able to control several parameters at once. An artist could build a lead rig where the pedal raises delay mix and feedback together. An on-screen macro knob would provide the same control for users without a pedal. An analog expression pedal needs a MIDI controller with an expression input or a compatible USB adapter.

**Additional routing.** Allow a stereo effect block to sit **before the amp split, in path A, in path B, or after the mixer**. Amp A can preserve the dry attack while Amp B adds chorus and delay, with the existing Blend control setting how much width to mix in. The same delay placed after the mixer affects **both** amps. Four clearly labeled positions would keep the options easy to scan and build on the existing dual-amp routing.

**YouTube Artist Packs and payouts.** A pack should include at least the **rig heard in the video and the required captures and IRs cleared for distribution**. One click should load the complete rig. I would test **€19** for a small rig collection and **€29** for rigs with backing tracks and isolated guitar tracks. For the first creator, I would set up a dedicated shop listing and share revenue from **all sales of that pack**. That also covers someone who watches the video today and buys the pack next week without using an affiliate link.

As a starting point for negotiations, I would propose a **50/50 split after VAT, actual shop and payment fees, and refunds**, with monthly settlement per pack. The example assumes 19% VAT, total fees of 5% of the gross price, and zero refunds.

| Retail price | After tax and fees | Share per party, rounded |
|---|---:|---:|
| €19 | €15.02 | €7.51 |
| €29 | €22.92 | €11.46 |

At 100 sales, your share would be approximately **€751 or €1,146 before your own costs**. **Pack export in Preset Builder** matters for your return on investment. A second creator should be able to publish a pack with the same software and without requiring a custom plugin build.

## V2 checklist

The numbers show the work order. Playing feel and rig features come first, followed by changes to the shared controls. Layout concepts can be sketched in parallel. DI recording and playback follow one another. Agree on creator terms and distribution rights before the first sale.

| Status | No. | Task | Priority |
|---|---|---|---|
| Open | 1 | **Improve the pitch shifter** — test virtual down-tuning with fast riffs and chords | Core |
| Open | 2 | **Improve and expand the effects** — start with stereo chorus, tap-tempo delay, and room and plate reverb | Core |
| Implemented and tested | 3 | **Make effect placement selectable** — before the split, in path A, in path B, or after the mixer. Use a stereo blend or pan Amp 1 left and Amp 2 right in Dual mode | Recommended addition |
| Open | 4 | **Add expression pedal control and macros** — MIDI Learn, min and max values, multiple parameters per pedal, and assignments saved with the rig | Recommended addition |
| Partially implemented (branding, block selection, A/B levels, centered preset selection, a more compact signal chain, and delay and reverb displays) | 5 | **Refine the controls and layout** — keep the signal chain visible, make blocks directly accessible, and use large controls | Core |
| Implemented and tested on `feat/di-export` | 6 | **Add Rec DI** — save the dry take from the amp and cab A/B comparison as a WAV for optional import into a DAW | Core |
| Open | 7 | **Play recorded DI through the rig** — reuse the same take to adjust settings and compare sounds | Recommended addition |
| Implemented and tested on `feat/looper` | 8 | **Expand the looper** — free length or 1, 2, or 4 bars, BPM, DAW sync, and an optional audible count-in independent of the click. A fixed-length first recording ends in **Finished**. Select **Play** to start playback | Core |
| Open | 9 | **Set creator payout terms** — agree on the revenue split, fees, refunds, usage rights, and payout schedule | Before the first sale |
| Open | 10 | **Launch YouTube Artist Packs** — support complete pack import and export, and test the €19 and €29 offers with one creator | Core for sales |
| Partially implemented on `feat/nam-capture` | 11 | **Create your own NAM captures** — automatically load the official NAM test signal, record the hardware return in standalone, check levels, and export the WAV pair. The Apple Silicon standalone includes Python. The integrated local NAM trainer checks the recording, exports a `.nam` file, and can load it into Amp A. A/B comparison against the hardware is still open | Addition |
