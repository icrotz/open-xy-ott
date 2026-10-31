# OTT (`ott`)

An OTT-style **3-band upward + downward multiband compressor** for the Teenage
Engineering **OP-XY**, built with the [Open-XY SDK](https://github.com/etcetc000/open-xy).
It splits the signal into low / mid / high, squeezes each band toward a target
loudness from *both* sides — pulling loud parts down **and** pushing quiet parts
up — and glues the result back together. The effect that makes everything sound
bigger, denser, and more “finished.”

## How to use

Load **OTT** as an effect. It works best as an **insert on a track or bus run
fully wet** (turn the dry signal down) so you hear the compressed result, not a
blend. Four controls on the FX page, three more on the alternate layer:

| Encoder | Default (page 1) | Alternate (press encoder) |
| --- | --- | --- |
| 1 | **Depth** | **Low** band amount |
| 2 | **Time** | **Mid** band amount |
| 3 | **In Gain** | **High** band amount |
| 4 | **Out Gain** | — |

- **Depth** — how hard the multiband compression works. `0%` = bypass (unity,
  transparent); crank it for the aggressive, in-your-face OTT sound.
- **Time** — scales the attack/release of every band. Low = snappy and
  pumping; high = smooth and relaxed.
- **In Gain** — level into the compressor (0.5 = unity). More input drives it
  harder.
- **Out Gain** — make-up / output level after compression (0.5 = unity).
- **Low / Mid / High** — press an encoder to reach these; each dials how much
  that band is compressed, so you can go heavy on highs and gentle on lows (or
  any balance). Pull one to zero to leave that band alone.

## Concept

A normal compressor turns **down** everything above a threshold. OTT does that
**per frequency band**, and it also does the opposite — **upward** compression
that turns *up* everything below a second threshold. The loud gets quieter, the
quiet gets louder, so the whole band is squeezed toward a middle loudness. Do
that independently on lows, mids, and highs and the sound becomes dense,
sustained, and detailed: reverb tails bloom, quiet textures surface, transients
flatten into a wall.

How the signal flows:

1. **Crossover split.** The input is divided into three bands with **4th-order
   Linkwitz-Riley crossovers at 88.3 Hz and 2.5 kHz**. LR crossovers sum back
   together *flat* — with Depth at 0 the output is within ±0.01 dB of the input,
   no phasey notches at the crossover points. (The low band gets a matching
   allpass so all three bands stay phase-aligned.)
2. **Per-band detection.** Each band tracks its own level with a stereo-linked
   peak envelope, using **per-band attack/release times** (lows react slower,
   highs faster — like the real thing), scaled by the **Time** knob.
3. **Upward + downward gain.** Each band applies a compression curve with
   separate up/down thresholds and ratios — near-**limiting downward** on loud
   material, gentle **~4:1 upward** on quiet material — plus per-band make-up
   gain. **Depth** blends the whole curve in from unity.
4. **Recombine** the three bands and apply **Out Gain**.

All the expensive math (the dB-domain compression curves, the filter
coefficients) is **precomputed offline and baked into lookup tables**, so the
running effect does no logarithms, exponentials, or division per sample — just
multiply-accumulates and table lookups. That’s what keeps a three-band,
nine-biquad compressor viable on the OP-XY.

> Inspired by the behaviour of Xfer Records’ OTT. This is an original
> implementation with that character, **not an emulation** of any specific
> plugin.

## Example usage

- **Instant “produced” drums** — insert on a drum bus, **Depth ~60–80%**, Time
  low for pump. Rooms and tails come forward, hits sound glued and loud.
- **Fat synth / bass** — moderate Depth, back the **Low** band off so the
  sub stays controlled while the mids and highs get dense and aggressive.
- **Lo-fi texture / ambience** — high Depth on a pad or field recording pulls
  up every quiet detail and hiss into a thick, saturated bed.
- **Vocal thickener** — gentle Depth, lean on the **Mid**/**High** bands for
  presence and consistency; ease **Low** so plosives don’t balloon.
- **Parallel-style glue** — if it feels too crushed, lower **Depth** (or the
  per-band amounts) rather than mixing dry back in; the crossover is only
  phase-flat on the wet path.

Watch out: high Depth is a big volume and density change. Ride **Out Gain** to
match levels, and trust your ears — OTT is easy to overdo, which is also,
famously, the point.

## Contribute

The machine lives at the repository root: DSP in `fx.cpp` / `core.h`, the control
layout in `definition.json`, and the baked crossover + gain tables in
`ott_tables.h`. **Don’t hand-edit the header** — regenerate it:

```sh
python tools/gen_ott.py > ott_tables.h
```

`tools/gen_ott.py` is where the musical tuning lives (crossover frequencies,
per-band thresholds/ratios/times, knee, make-up). Agent/contributor notes are in
[AGENTS.md](AGENTS.md).

Keep the [Open-XY SDK](https://github.com/etcetc000/open-xy) beside this repo as
`../open-xy` (branch `staging`) and run the host sound tests:

```sh
python ../open-xy/sdk.py test --machine .
```

Effects have no browser preview — audition on hardware after building firmware
(SDK [build](https://github.com/etcetc000/open-xy/blob/staging/docs/build.md) /
[profiling](https://github.com/etcetc000/open-xy/blob/staging/docs/profiling.md)
guides), and keep firmware/stock content **outside** this checkout. This effect
is CPU-heavy (nine biquads per channel for the LR4 crossovers); the host CPU
estimate is only a guide — listen for dropouts on dense material, and a real
on-device profiling run is required before catalog submission. PRs welcome:
alternative crossover points, an RMS-detection option, or a lighter crossover
mode are all worthwhile — add matching tests in `tests/`.

## Credits

- Built on the [Open-XY SDK](https://github.com/etcetc000/open-xy) by the Open-XY
  authors.
- Concept inspired by **Xfer Records OTT**. The DSP was cross-checked against the
  open-source [Sephonix/sps-dsp](https://github.com/Sephonix/sps-dsp) OTT-style
  compressor (GPL-3.0) as a reference for crossover topology and the per-band
  compression defaults; no source was copied.
- Crossover and envelope filters follow the **Robert Bristow-Johnson “Audio EQ
  Cookbook”** and standard Linkwitz-Riley design.
- Created by [@icrotz](https://github.com/icrotz) with Claude (Claude Code).
- Licensed under [LICENSE](LICENSE).
