# ofxPlotterKit

Optional UI kit for [ofxPlotter](https://github.com/ofxyz/ofxPlotter): reusable
Dear ImGui windows and panels for plotter workflows.

`ofxPlotter` stays UI-free; this addon hosts the windows and integrates with the
[ofxKit](https://github.com/ofkitty/ofxKit) runtime (docking, viewports, panel
registry) — mirroring the `ofxGrbl` / `ofxGrblKit` split.

## Windows

- `plotter::kit::PlotterPrintPreviewPanel` — G-code print preview: FBO viewport
  with pan/zoom (`ofkitty::View2DState`), transport controls, G0 travel-path
  overlay, per-move-type colors. Usable standalone (`draw()`) or embedded in an
  ofxKit `ViewportInstance` (`drawScene()` / `drawHeader()`).
- `plotter::kit::PlotterZonesPanel` — machine zone editor/inspector. Registry
  and selection are supplied via setters/callbacks.

## Dependencies

`ofxPlotter`, `ofxGrbl`, `ofxKit`
