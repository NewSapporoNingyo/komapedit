# Rendering and Navigation Memory

## Scene ownership and caching

- Scene geometry belongs at the maploader/Canvas3D boundary. Avoid GUI-side regeneration or a second parsed route model.
- Keep regular map snapshots and scene geometry lifetimes/revisions distinct. Scene regeneration must not invalidate ordinary geometry accidentally, and ordinary geometry mutation must invalidate dependent scene data.
- Preserve control points until scene generation completes. Earlier cache work showed that moving ordinary geometry state too early can drop event boundaries or leave partial restore state.
- Build marker layout, edit identity, and navigation metadata during scene-cache construction. Visibility toggles should update visibility/index data rather than rebuilding geometry.
- Cache keys must contain every visible input and have an explicit invalidation owner. Profile, Inspector-row, and marker-recipe caches were safe only after exact key/invalidation review.

## Placement and interaction

- Placement track semantics are narrower than broad track aliases. In the current placement path, empty key or `0` can mean own track; `1` remains a real other track. Recheck the exact statement family before reusing this rule.
- Other-track buffers must cover the range needed for sampling; repeaters disappeared before the first explicit other-track point when the first value was not extended over the own-track range.
- Bind row kind and edit ID directly to scene objects/markers. Same-distance matching and lazy lookup can open the wrong Properties/Edit target.
- Keep object hover, timed highlight, selection, and navigation as separate states. Reusing one state caused outlines or table highlights to persist incorrectly.
- Use deferred Inspector/delete requests from context menus. Keep table↔scene and plan↔table highlight/visibility side effects centralized.

## Rendering precision and overlays

- Far-distance flicker is a scene-wide precision/depth issue, not a model-specific defect. Reversed-Z state, depth clear/compare, camera-relative or chunk-local transforms, and fog must be inspected together.
- A correct-looking scene can still expose a wrong overlay statistic. Verify whether a label expects total chunk count, current-frame drawn count, or another field before changing generation.
- Route information depends on current radius/cant, gradient, active speed limit, Section-selected signal speed, and next-station sampling; camera-distance changes must preserve this event lookup.
- Centralize 2D/3D marker icon recipes in `map_marker_visuals`; do not let the canvases drift into separate shapes/colors.

## Model loader and validation

- Assimp remains isolated in `model_loader.dll`; GUI code consumes only the v2 C ABI with matching allocation/free ownership.
- Missing textures or unsupported models must fail gracefully with contextual English warnings.
- Use scene benchmark and camera-transfer headless modes for structural proof, but keep materials, picking, gizmo feel, overlap, label layout, and startup tabs as manual visual checks.
- Strict scene budgets are environment-sensitive. Compare identical parameters and report an unmet gate; a looser smoke run may diagnose correctness but cannot replace the strict result.
