# Official BVE route-format baseline

Verified against the official BVE Trainsim pages on 2026-08-13. The live official pages remain normative; this file is a compact routing aid, signature inventory, and review checklist rather than a replacement specification.

## Contents

- [Official sources and scope](#official-sources-and-scope)
- [File-level grammar](#file-level-grammar)
- [Map language invariants](#map-language-invariants)
- [Official Map statement signatures](#official-map-statement-signatures)
- [Resource-list schemas](#resource-list-schemas)
- [Other-train schema](#other-train-schema)
- [Scenario schema](#scenario-schema)
- [Cross-file checks](#cross-file-checks)

## Official sources and scope

| Format | Official source | Covered role |
| --- | --- | --- |
| Map | [Map file](https://bvets.net/jp/edit/formats/route/map.html) | Route statements, expressions, distance, and Include |
| Structure List | [Structure list](https://bvets.net/jp/edit/formats/route/structure.html) | Structure keys and model paths |
| Signal Aspects List | [Signal aspects list](https://bvets.net/jp/edit/formats/route/signal.html) | Aspect keys, indexed structures, and glare rows |
| Sound List | [Sound list](https://bvets.net/jp/edit/formats/route/sound.html) | Sound keys, WAV paths, and buffer counts |
| Other-train | [Other-train file](https://bvets.net/jp/edit/formats/route/train.html) | Other-train structures and 3D sound regions |
| Scenario | [Scenario file](https://bvets.net/jp/edit/formats/scenario.html) | Route/vehicle entry selection and display metadata |

This baseline covers only these six supplied official pages. For linked formats such as Station List or vehicle files, retrieve the corresponding official specification before changing their grammar. The Map page defines `Station.Load` and `Station[...].Put`, but it does not define the Station List row schema.

Official-page badges such as `NEW` and annotations such as `[old]` describe status; they are not source tokens. Project forms such as `Legacy.*` are compatibility syntax unless an official page explicitly says otherwise.

## File-level grammar

For all six formats, place the documented format string first. An optional encoding name follows a colon with no whitespace around the colon; omission means UTF-8. Do not assume that the set of encodings accepted by current komapedit is the full set an official BVE implementation may declare.

| Format | Exact format string | Body model | Official comments |
| --- | --- | --- | --- |
| Map | `BveTs Map 2.02` | Semicolon-terminated statements | `#` or `//` to end of line |
| Structure List | `BveTs Structure List 2.00` | Comma-separated rows | `#` to end of line |
| Signal Aspects List | `BveTs Signal Aspects List 2.00` | Variable-width comma-separated rows | `#` to end of line |
| Sound List | `BveTs Sound List 2.00` | Two- or three-column comma-separated rows | `#` to end of line |
| Other-train | `BveTs Train 1.01` | INI-like sections and `key = value` entries | `#` or `;` to end of line |
| Scenario | `BveTs Scenario 2.00` | Top-level `key = value` entries | `#` or `;` to end of line |

Do not transfer CSV quoting, escaping, whitespace, duplicate-key, case, or unknown-field behavior from a general parser library unless the relevant official page documents it or established project compatibility explicitly requires it.

## Map language invariants

- Terminate every statement with `;`; allow multiple statements on one line and documented whitespace, including line breaks, between lexical tokens.
- Treat Map element/function names case-insensitively. Do not automatically extend that rule to resource key values or other file formats.
- Use only the documented shapes `Element.Function(...)`, `Element[key].Function(...)`, and `Element[key].NestedElement.Function(...)`.
- Delimit strings with single quotes.
- Form variable names with `$` followed by ASCII letters, digits, or underscore. Assign one numeric or string value/expression in a standalone statement; use variables only in assignments, arguments, and keys.
- Support only the documented arithmetic operators `+`, `-`, `*`, `/`, and `%`, parentheses, and unary signs. `+` also concatenates strings using the official coercion behavior. Do not add logical/comparison, increment/decrement, or compound-assignment operators.
- Recognize the documented math calls `abs(value)`, `atan2(y, x)`, `ceil(value)`, `cos(value)`, `exp(value)`, `floor(value)`, `log(value)`, `pow(x, y)`, `rand(value)`, `rand()`, `sin(value)`, and `sqrt(value)` with exactly those arities.
- Treat a standalone number, numeric-valued variable, or numeric expression as a distance statement. Require a nonnegative real result. Use the predefined `distance` variable without `$` for the previous current distance.
- Insert another Map with `include 'relative-path';`. Require the included Map to have its own header; do not insert that header into the parent statement stream.
- Consult the official argument description for every touched statement's types, units, signs, ranges, sentinels, interpolation rules, and ordering prerequisites. Do not derive them from parameter names.

## Official Map statement signatures

The list below inventories official shapes visible on the Map page at the verification date. Re-open the relevant live entry before implementation.

### Own track and other tracks

```text
Curve.SetGauge(value)                         [old: Curve.Gauge(value)]
Curve.SetCenter(x)
Curve.SetFunction(id)
Curve.BeginTransition()
Curve.Begin(radius, cant)                     [old: Curve.BeginCircular(radius, cant)]
Curve.Begin(radius)
Curve.End()
Curve.Interpolate(radius, cant)
Curve.Interpolate(radius)
Curve.Interpolate()
Curve.Change(radius)

Gradient.BeginTransition()
Gradient.Begin(gradient)                      [old: Gradient.BeginConst(gradient)]
Gradient.End()
Gradient.Interpolate(gradient)
Gradient.Interpolate()

Track[trackKey].X.Interpolate(x, radius)
Track[trackKey].X.Interpolate(x)
Track[trackKey].X.Interpolate()
Track[trackKey].Y.Interpolate(y, radius)
Track[trackKey].Y.Interpolate(y)
Track[trackKey].Y.Interpolate()
Track[trackKey].Position(x, y, radiusH, radiusV)
Track[trackKey].Position(x, y, radiusH)
Track[trackKey].Position(x, y)
Track[trackKey].Cant.SetGauge(gauge)           [old: Track[trackKey].Gauge(gauge)]
Track[trackKey].Cant.SetCenter(x)
Track[trackKey].Cant.SetFunction(id)
Track[trackKey].Cant.BeginTransition()
Track[trackKey].Cant.Begin(cant)
Track[trackKey].Cant.End()
Track[trackKey].Cant.Interpolate(cant)         [old: Track[trackKey].Cant(cant)]
Track[trackKey].Cant.Interpolate()
```

### Structures, repeaters, stations, sections, and signals

```text
Structure.Load(filePath)
Structure[structureKey].Put(trackKey, x, y, z, rx, ry, rz, tilt, span)
Structure[structureKey].Put0(trackKey, tilt, span)
Structure[structureKey].PutBetween(trackKey1, trackKey2, flag)
Structure[structureKey].PutBetween(trackKey1, trackKey2)

Repeater[repeaterKey].Begin(trackKey, x, y, z, rx, ry, rz, tilt, span,
                            interval, structureKey1, ..., structureKeyN)
Repeater[repeaterKey].Begin0(trackKey, tilt, span, interval,
                             structureKey1, ..., structureKeyN)
Repeater[repeaterKey].End()
Background.Change(structureKey)

Station.Load(filePath)
Station[stationKey].Put(door, margin1, margin2)

Section.Begin(signal0, signal1, ..., signalN)  [old: Section.BeginNew(...)]
Section.SetSpeedLimit(v0, v1, ..., vn)         [old: Signal.SpeedLimit(...)]
Signal.Load(filePath)
Signal[signalAspectKey].Put(section, trackKey, x, y)
Signal[signalAspectKey].Put(section, trackKey, x, y, z, rx, ry, rz, tilt, span)
Beacon.Put(type, section, sendData)
SpeedLimit.Begin(v)
SpeedLimit.End()
```

Do not interpret `...` as accepting an empty variable-length list. Verify the minimum documented cardinality and each value's semantics on the live page before coding or testing it.

### Timing, environment, sound, and other trains

```text
PreTrain.Pass(time)
PreTrain.Pass(second)

Light.Ambient(red, green, blue)
Light.Diffuse(red, green, blue)
Light.Direction(pitch, yaw)
Fog.Interpolate(density, red, green, blue)     [old: Fog.Set(density, red, green, blue)]
Fog.Interpolate(density)
Fog.Interpolate()
DrawDistance.Change(value)
CabIlluminance.Interpolate(value)              [old: CabIlluminance.Set(value)]
CabIlluminance.Interpolate()
Irregularity.Change(x, y, r, lx, ly, lr)
Adhesion.Change(a)
Adhesion.Change(a, b, c)

Sound.Load(filePath)
Sound[soundKey].Play()
Sound3D.Load(filePath)
Sound3D[soundKey].Put(x, y)
RollingNoise.Change(index)
FlangeNoise.Change(index)
JointNoise.Play(index)

Train.Add(trainKey, filePath, trackKey, direction)
Train[trainKey].Load(filePath, trackKey, direction)
Train[trainKey].Enable(time)
Train[trainKey].Enable(second)
Train[trainKey].Stop(decelerate, stopTime, accelerate, speed)
```

High-risk semantic constraints include load-before-use relationships, Begin/End or transition ordering, variable-length argument lists, `null` signal speed entries, short/full `Signal.Put` forms, `Put`/`Put0` and `Begin`/`Begin0` equivalence, per-file relative paths, time-string versus seconds overloads, one-time light statements, color and illuminance ranges, direction/door/tilt enumerations, and units/sign conventions. Verify the relevant official entry rather than relying on this reminder list.

## Resource-list schemas

### Structure List

Use exactly two documented columns:

1. arbitrary structure name used by Map, Signal Aspects List, and Other-train files;
2. structure-file path relative to the list file.

### Signal Aspects List

- Put the signal aspect name in column 1 and one or more Structure List keys in subsequent columns.
- Map the subsequent column order to the signal indexes used by Map section statements.
- Treat a blank column 1 as the glare definition for the aspect on the preceding row; preserve that adjacency and blank field.

### Sound List

Use `sound name, relative WAV path, optional buffer count`. The documented buffer-count default is `1`. Keep the eager-load memory implications of buffer counts separate from syntax validity.

## Other-train schema

Use the exact header and INI-like section/key grammar. The official page documents these sections and keys:

| Section | Keys | Essential constraints |
| --- | --- | --- |
| `Structure` | `Key`, `Distance`, `Span`, `Z` | `Key` references a Structure List name; distance/geometry values use the other-train origin and documented front/rear orientation |
| `Sound3d` | `Key`, `Distance1`, `Distance2`, `Function` | `Key` references a Sound List name; `Function` is `Stationary`, `Rolling`, `Acceleration`, or `Deceleration` |

Allow only one `Acceleration` and one `Deceleration` source per other train; the official page permits multiple `Stationary` and `Rolling` sources. Verify repeated-section and unknown-key behavior rather than assuming generic INI semantics.

## Scenario schema

Recognize only the documented top-level keys when implementing official behavior:

```text
Title
Route
RouteTitle
Vehicle
VehicleTitle
Author
Image
Comment
```

`Route` references a Map path relative to the Scenario file. `Vehicle` similarly references a vehicle file. Each can be a single path or a `|`-separated weighted choice whose terms use `path * weight`; an omitted weight defaults to `1`. `Image` is also a relative path. Do not confuse semicolon comments here with Map statement terminators.

## Cross-file checks

- Resolve every relative path from the owning file documented by the official format, not from the process working directory.
- Verify that `Structure.Load`, `Signal.Load`, `Sound.Load`, `Sound3D.Load`, `Train.Add`, and `Train[...].Load` connect to the correct file family.
- Verify Structure keys used by Map placement/repeaters/backgrounds, Signal aspect rows, and Other-train structures against the loaded Structure List.
- Verify Signal aspect keys and structure-index order against `Signal[...].Put`, `Section.Begin`, and `Section.SetSpeedLimit` semantics.
- Verify Sound keys used by Map and Other-train sound placement against the loaded Sound List.
- Verify Scenario `Route` choices point to Map files with their own valid headers. Vehicle-file grammar is outside this six-page baseline.
- Preserve source order wherever row adjacency or positional indexing carries meaning; do not replace it with unordered-map iteration during writeback.
