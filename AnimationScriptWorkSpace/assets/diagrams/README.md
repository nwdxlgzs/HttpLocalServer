# Technical scenes

Original HTML/CSS/SVG technical diagrams for the HttpLocalServer film. They are explanatory illustrations, not live network recordings. The brand image is the project's own `assets/logo.svg`.

`js/technical.js` exposes `HLSTechnical.render(scene)` and `HLSTechnical.animate(timeline, root, start, duration, scene)`. Pass the original shot ID in `scene.id`. Supported IDs: S24, S25, S27–S31, S34–S43. Entrances belong to the module's `.tech-enter` elements; the parent composition may animate the outer module container without applying competing tweens to those elements. All timelines are supplied by the caller. Motion is finite and deterministic.

## Evidence

- S24–S31: current README, `docs/server-and-transfers.md`, and `docs/workspace-app.md`; active dependencies, rather than arbitrary matching names, are frozen.
- S34–S36: `docs/workspace-app.md`, `docs/quickjs-wasm.md`, and `examples/workspace-app/server.qjs`. S35 is an explicitly labeled simplified demonstration of real `hls` imports, WAMR instantiation and `fs.writeText`. It assumes the allowed results directory already exists and write permission has been granted.
- S37: `docs/mcp.md`; the illustrated `list_files`, `file_type`, `read_text`, and `download_url` are actual read-only tools.
- S38–S39: `docs/development.md`, `docs/server-and-transfers.md`, and `docs/workspace-app.md`. Cache cells and worker slots are conceptual, not utilization measurements.
- S40: current PCDN documentation. PCDN is optional; ordinary downloading remains the default. A page may disable sending for that page session.
- S41: delivered Windows/Linux artifacts and verification reports. macOS is source adaptation only; Windows 7 has no current real-machine validation claim.
- S42: `docs/verification/script-performance-v351-windows.json`, also summarized in `docs/verification/release-v351.json` and `AnimationScriptPlan.md` lines 1150–1152. The bars share a zero baseline and scale. Measured before/after successful RPS: 3909.49/4899.58, 1158.17/2601.44, 412.27/443.33. This is a single short diagnostic per workload, IPv4 HTTP loopback, 8 clients, 3 seconds, fixed script and worker budgets, warmed ZIP resource. It is not an observed LAN capacity or sustained-load claim.

## Local visual review

`review.html` provides the static most-visible state of one scene. Use `?s=S42` or run `review('S42')` in the page. `review_layout.py` uses an isolated headless installed Chrome process, Playwright and Pillow to regenerate final-state screenshots and a text/container boundary report.

- Landscape: 1920×1080 canvas; 1640×590 stage.
- Portrait: 1080×1920 canvas; 940×1150 stage.
- `layout-report.json`: all 34 static scene/ratio combinations; zero stage overflow or clipping findings after fixes.
- `contact-landscape.jpg`, `contact-portrait.jpg`: all technical hero frames.
- `animation-report.json`: all 34 scene/ratio combinations pass repeated non-monotonic GSAP seeking (9→0→6→1→9) with identical final styles and geometry; all detail tweens finish within the 10-second review budget. `review_animation.py` uses the pinned local `assets/vendor/gsap.min.js` by default, with an optional explicit path argument.

The full HyperFrames composition still owns transition, narration synchronization, captions, timeline validation and final rendering.
