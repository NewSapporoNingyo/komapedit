# Validation and Maintenance Memory

## Current validation shape

- The registered maploader contracts are snapshot, gradient projection, typed edit, and diagnostics. Specialized binary modes such as `signal-glare` are separate from CTest.
- The diagnostics contract depends on ignored local `tests/` fixtures. Confirm availability before treating a clean-checkout failure as a regression.
- `komapedit.exe` is a GUI-subsystem executable. PowerShell proof should use `Start-Process -Wait -WindowStyle Hidden -PassThru`, `--headless-output`, the exit code, and output-file stage markers.
- Headless harnesses that construct `App` may need live ImGui and ImPlot contexts even without rendering. Disable `imgui.ini` writes.
- Select the smallest proof: direct DLL/CTest for contracts, load/open for lifecycle, plan/table for 2D/data interaction, scene/camera for 3D, and statement-family edit modes for writeback.

## Real-route safety

- Historical real-route tests found Include-scale, encoding, scene, and edit issues that small fixtures missed. They are valuable only when the route is explicitly in scope and the test is repeatable.
- Prefer preview and memory Apply. For commit tests, use temporary copies or hash every touched source before/after and verify physical restoration.
- Do not reuse a historical route path as authorization to write to that route.

## Slop-fix evidence

- Audit first and fix only proven issues. Previous successful passes removed duplicated Repeater construction, unused state, unsafe numeric conversion, and incomplete cache behavior; other suspected issues were correctly left unchanged when evidence was insufficient.
- Strict validation uses `KOMAPEDIT_STRICT_WARNINGS=ON`, registered CTests, and affected headless modes. Third-party warnings remain outside self-owned strict targets.
- Count self-owned production code before/after. Exclude tests/docs/vendor/generated content and explain growth with measured value.
- Do not impose arbitrary compatibility-changing caps merely because a theoretical large input exists. Validate the risk and choose a limit only with product/format evidence.
- Keep unrelated failing contracts or manual checks separate from the scoped result.

## Performance evidence

- Compare the same route, parameters, build type, load profile, and environment. Report median and p95 rather than a single favorable run.
- Validate scene stage/count correctness before interpreting frame time; an empty or incomplete scene can look fast.
- Do not lower a strict threshold, change the window, or repeat selectively to manufacture a pass. A looser run can separate correctness from existing debt, but the original gate remains unmet.
- Avoid overlapping timed-out CMake/Ninja builds; a surviving process can own `build.ninja` or the executable and create misleading permission errors.
