# HttpLocalServer · Motion identity

Source of truth: ../AnimationScriptPlan.md, version 3.5.1. Date 2026-11-14.

## Style Prompt

A precise, warm, quietly premium product film about a local HTTP server. Real browser captures are the visual anchor. Large editorial Chinese typography, generous negative space, pale blue paper, thin rules, deep ink and the existing blue/mint logo. Diagrams explain real mechanisms with purposeful movement. Website hosting and application backends share equal identity with file transfer. The film alternates close product details and spacious diagrams; it does not look like a deck of identical feature cards.

## Colors

- Paper / canvas: #F4F7FC
- Ink / primary text: #18324D
- Brand blue / HTTP route: #3C8CF5 (logo endpoint #2158B4)
- Mint / verified content: #92F0DC; use dark ink when text is on mint
- Steel / secondary text: #536B84
- Dark interlude: #101A2B, text #EDF4FC
- Warning: #A45C29 on #FFF1E4; frozen items use diagonal hatching.
- Peer route: #7864B8, dashed. Only after PCDN is explicitly enabled.

The limited extra colors above are semantic product colors, not arbitrary scene palettes.

## Typography

- Chinese: Microsoft YaHei / Microsoft YaHei UI, with the local system font. Export a subset WOFF2 for reproducible local rendering; font files stay in this local workspace.
- Latin / labels: Segoe UI. Terminal: Consolas.
- Hero title: 82–108px at 1920×1080. Supporting copy: 30–38px. Footnote: 24px minimum.
- No gradient text. No tracking on Chinese text. Use tabular numerals for measurements.
- Legal/font assets are production dependencies, not dependencies of HttpLocalServer.

## Layout

- Layout: 1920×1080 landscape / 1080×1920 portrait, 30fps. UHD delivery renders the same HTML layout at deviceScaleFactor 2: 3840×2160 / 2160×3840. Typography, SVG and HTML animation are drawn at UHD pixels, preserving the approved layout.
- Safe area: x=96…1824, y=72…1008. VO subtitle baseline around y=985.
- Scene-content fills frame using flex/grid and padding. Absolute layers are decorative camera frames or diagram paths only.
- Main product shot: large browser capture, 60–75% of frame; do not reduce it to an unreadable thumbnail.
- Alternate full-screen product, split editorial layout, architecture, network diagram and brand typography.
- Use the actual SVG logo from ../resources/branding, referenced by file path.

## Motion

- Deterministic GSAP timelines, paused and registered in window.__timelines.
- Entrances: 0.45–0.8s, power3.out / sine.out / expo.out varied by role.
- Quiet slow camera drift on captures, 1–3% maximum. Keep UI text legible.
- Primary transition: 0.45s crossfade or masked reveal. Accent: 0.5s directional push at chapter boundaries.
- Inter-scene transition owns exit. Keep outgoing content visible until transition starts.
- Mid-scene detail motion: pointers, path drawing, route pulses, tab shifts, block movement, selection and clipping. No arbitrary loops.
- No random or wall-clock animation. No infinite repeats. No autoplaying external webpage inside composition.

## Sound

- Mandarin narration, neutral and unhurried; exact text and timings remain replaceable.
- Original locally synthesized 108 BPM soft-key, pad, bass and restrained percussion score, stereo; no external music or samples.
- Voice dominates the mix. Audio time follows actual generated media.
- Initial UHD voice lift: measured 1.447–2.481 LU versus the original narration stems. Latest v3 raises BGM by 4.321–4.439 LU over that initial mix, while narration changes by only -0.061 to -0.179 LU. Original 6 dB speech ducking stays in the music stem. Master true peak ceiling -1.2 dBTP; actual final mix figures are in assets/audio/delivery-mix-report.json.
- BGM/voice are separate tracks, as are subtitle files.

## What NOT to Do

- No Windows/Apple/Chrome logo replacing the project brand or implying endorsement.
- No cloud-service positioning, fake benchmark, fabricated download completion, infinite throughput or absolute security promise.
- No template-looking repeated equal card grids, neon cyberpunk, code rain, excessive glass or spinning 3D devices.
- No fake live UI made to look like an authenticated recording. Label diagrams and captured frames honestly.
- No personal files, real password, token, private browser profile or existing service modification.
- No claim of real macOS/Win7 verification. Keep PCDN/ZIP/lazy thumbnails/MCP optional labels.
- No full source/app resource bundle in the promotional video runtime; production directory stays separate.

## Production truth

Use the current data/scenes.json timings. The user approved the visual layout but replaced the fixed 480-second pace with continuous editing based on measured narration. Neighboring shots overlap by 0.35 seconds; lead-in and tail are short and words are never cut. Preserve the long-form script as a historical inventory. Source captures are from the current Windows 3.5.1 executable in an isolated demo root. Diagram assets communicate architecture rather than claiming a live measurement. QR codes really decode to https://example.com as a placeholder. The simulated scan must open the actual HttpLocalServer MOBILE FILE MANAGER capture; do not show the example.com website as the destination.
