# Production log

The creative brief is ../AnimationScriptPlan.md. The approved visual layout is preserved. A later user direction replaces the fixed eight-minute pacing with measured speech and 0.35-second overlapping transitions. Current editions: 319.966667s main (44 shots), 58.3s overview (9), 24.333333s teaser (7), and 34.733333s portrait (8). HyperFrames is pinned to 0.9.44, GSAP to the bundled 3.15.0.

## Assets

- Existing project SVG logo.
- Actual product UI captures from an isolated demo server.
- Original SVG diagrams and motion, explicitly marked as diagrams.
- Chinese paid TTS narration (stella), authorized by the user. Original SAPI fallback retained. Exact spoken text, measured cue durations and honestly estimated subtitle phrase timings retained.
- Original generated music bed and sound cues; no downloaded commercial stock assets.

## Workflow

1. Scaffold with HyperFrames CLI; define DESIGN.md.
2. Build reusable shot modules and scene metadata from the approved plan.
3. Capture real browser states and generate narration.
4. Review hero frames, random-access seeking, transitions and text bounds.
5. Run lint / validate / inspect; render and verify video/audio durations.
6. Publish a local Studio preview and the rendered files in exports/.

## Verified production state

- Product capture: 21 original desktop/mobile PNGs and 4 real VP8 WebM operations; errors=0. The QR code is additional, truly decodes to https://example.com, and is used as a placeholder. The simulated phone opening displays the product's actual MOBILE FILE MANAGER, not the example.com website.
- Technical diagrams: 17 scenes × 2 layouts = 34 static boundary passes and 34 deterministic GSAP seek passes. See assets/diagrams/.
- Integrated composition: review/visual-verification.json checks all 68 current shots for loaded images, JavaScript errors, visible text bounds and non-monotonic seek reproducibility. All pass.
- HyperFrames: review/check-compact-final.json passes lint, runtime, layout, motion, contrast and audio checks, with zero errors. Lint retains composition organization advisories for generated inline scenes/repeated logo assets; these are not claimed to be absent. The separate full craft pass on the prior pacing also had no craft errors.
- Audio: assets/audio/verification.json confirms all four exact durations, 48kHz PCM, no clipping, no truncated speech and no speech overlap. A final pronunciation correction covers 18 shots: H T T P Local Server, English zip, Web Assembly, and explicit initialisms. Small local pitch-preserving adjustments are capped at 1.10×; the scene data, all eight caption files and the other 50 cue WAVs remain byte-identical. Current ordinary main-shot tails are 0.4837–1.1329s; QR retains extra time for interaction. Phrase captions are estimates, not ASR. Independent English clips have auxiliary ASR records, which are not claimed as human listening or phonetic certification.
- Credentials: programmatically supplied from outside this workspace without displaying their values. Exact-match text scans have found zero copies. The most recent scan is recorded in assets/audio/private-data-scan.json.
- Render environment: Windows x64, Node 24.18.0, local Chrome, explicit local FFmpeg/FFprobe paths; native H.264/AAC outputs. Production tools are isolated from the C/C++ application. Versions/hashes in tools/manifest.json.

## Previous 1080p delivery

All four previous 1080p videos rendered, received the corrected pronunciation audio, and passed full decoding. `review/export-verification.json` records that delivery before the UHD audio upgrade. Its hashes describe that earlier state. The retained 1080p files now use the louder stereo mix; `review/audio-refresh-1080p.json` records their unchanged video packet hashes after this additional audio replacement.

| Edition | Duration | Output |
|---|---:|---|
| Detailed | 319.966667s | exports/HttpLocalServer-main-1080p.mp4 |
| Overview | 58.3s | exports/HttpLocalServer-overview.mp4 |
| Teaser | 24.333333s | exports/HttpLocalServer-teaser.mp4 |
| Portrait | 34.733333s | exports/HttpLocalServer-vertical.mp4 |

Each output has an SRT and PNG poster beside it. Rendered contact sheets and 76 representative frames are in review/rendered-*. The actual QR opening frames show the product's mobile file manager. Pronunciation edits leave every shot time and all eight caption files unchanged. The terminology audition is assets/audio/brand/pronunciation-audition.wav.

Studio remains in the background at http://127.0.0.1:3002/#project/AnimationScriptWorkSpace. Render logs are retained per edition. Old pacing drafts are outside exports/, in review/previous-pace/.

## UHD audio and rendering profile

User request: slightly louder narration, background music and 4K delivery. Shot data, transitions, corrected pronunciation and all caption times remain unchanged.

- `data/delivery.json`: 3840×2160 landscape / 2160×3840 portrait, 30fps. HyperFrames draws the original HTML at DPR 2. This is a new source render, not an upscale of the completed 1080p MP4.
- 21 actual PNG captures were recaptured at DPR 2, with the same 1600×900 / 390×844 CSS viewports. Output PNGs are 3200×1800 / 780×1688. Capture completed with zero errors. Original product executable SHA matches the previous capture. `capture-retina-manifest.json` and updated `capture-manifest.json` record provenance. Previous PNGs and manifest are retained in `assets/capture-before-retina/`.
- Four actual interaction recordings retain their original 1600×900 VP8 sources. They are scaled inside the UHD composition; this workflow does not claim those source videos are native 4K.
- `scripts/audio_delivery.py` uses the already corrected narration stems, with a high pass and gentle compression. It raises narration by 2.269 LU main / 2.481 LU overview / 1.447 LU teaser / 1.880 LU portrait, subject to the master true-peak ceiling. No new TTS requests or pronunciation edits.
- Original 108 BPM Fmaj7 / Dm9 / Bbmaj7 / Cadd9 score: oscillator soft keys, pad, bass and restrained seeded percussion; no external recordings or samples. Stereo music aims for -27 LUFS in gaps and is ducked 6 dB during speech. Master loudness ranges from -14.998 to -13.988 LUFS; 4× oversampled true peak is -1.2 dBTP. Full measured results and unchanged timing hashes: `assets/audio/delivery-mix-report.json`.
- Delivery audio is 48 kHz stereo AAC at 320 kbps. Final remux copies video packets and installs the exact current master. `scripts/refresh_audio.py` records video packet hashes before and after.
- GPU H.264 target bitrate 35 Mbps. `scripts/hf_compat.mjs` makes the HyperFrames 0.9.44 NVENC help parser accept legacy FFmpeg enum names without numeric values. The existing FFmpeg 4.2.2 supports legacy preset names but not modern `p4`; actual HyperFrames delivery encoding selects `fast`. The compatibility patch is applied automatically by `hf.mjs`, and changes only production tooling.
- Source checks after the asset upgrade: `review/check-4k-source-summary.json` has zero errors in runtime, layout, motion, contrast and audio. 50 previously reviewed organizational lint advisories remain. Audio verification passes all 68 corrected cue hashes, full-length stems and 119 captions.

The UHD exports use `*-4K.mp4` names, preserving the older 1080p videos. `scripts/verify_exports.py` is the UHD release check: full decode, dimensions, frame rate, durations, stereo stream, encoded loudness/true peak, current-master correlation and representative UHD frames. Results are written to `review/export-verification-4k.json`.

## Music balance revision v3 and poster set

The user found the initial UHD background music too quiet. `scripts/audio_rebalance.py` applies +4.5 dB to the v2 music stem and a local offline peak envelope, preserving the centered narration. Measured BGM gain is +4.439 / +4.405 / +4.410 / +4.321 LU (main / overview / teaser / portrait); voice only changes -0.061 / -0.095 / -0.090 / -0.179 LU. Every exact PCM duration and all captions remain unchanged. The latest delivery-mix-report replaces v2 metrics; the previous report and stems are retained in before-music-rebalance/. Final video audio is remuxed after rendering; both 4K and retained 1080p outputs receive v3. No new TTS calls.

`PromotionalPosters/` contains nine independent 2160px square posters, full 6480px nine-grid PNG/JPG, a lightweight preview, editable HTML/CSS and a reproducible Chromium renderer. It uses actual captures and the approved brand palette. Automated checks cover text bounds, art/footer separation, resources and output dimensions; the assembled grid is visually reviewed. The project's read-only cleanup inventory is generated in the repository root with each actual candidate path, size, rebuild cost and retained source/data notes. No files are deleted.

Final v3 UHD verification: all four videos passed full decode, H.264/AAC stream checks, expected UHD dimensions and 30fps, exact A/V durations and representative frame extraction. Encoded true peaks are below -1.1 dBTP in all editions, with current-master audio correlation above 0.999. Main export is 332,292,743 bytes, 319.966667s, 3840×2160. Poster output checks pass for all nine images and the 6480px grid; the ZIP contains exactly the nine PNGs with matching hashes. The last root cleanup inventory is generated after completion; the atomic render transaction and Windows render work folder were automatically removed by HyperFrames. Studio remains available at the documented local URL.
