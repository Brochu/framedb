# framedb — Design Notes

*Last updated: 2026-08-07*

> **Resuming in a new chat?** Read this whole file first. It contains the current thesis,
> the decisions already made, the reasoning behind them, and — importantly — the ideas that
> were explicitly **rejected** and why. Don't re-litigate section 7.

---

## 1. Current thesis

> Import a GPU frame capture into a normalized SQLite database, so frames can be queried in
> SQL and compared to each other — across frames in one capture, or across builds.

Repo description:

> GPU frame captures → a queryable SQLite database. Diff any two frames, from the same
> capture or across builds.

Pipeline:

```text
RenderDoc capture
       │
       ▼
  Import / normalize
       │
       ▼
   SQLite database
       │
   ┌───┼─────────────┐
   ▼   ▼             ▼
 SQL  Diff        Analysis
   │   │             │
   └───┼─────────────┘
       ▼
      LLM  (last, optional, not the foundation)
```

Later, adapters accrete without touching the core:

```text
RenderDoc ──────┐
GFXReconstruct ─┤
PIX ────────────┤──> Normalizer ──> SQLite
Nsight ─────────┘
```

Universal capture support is an **emergent property**, not the initial goal.

---

## 2. How we got here

The project started as a much broader idea: a *"Universal Graphics Capture IR"* — a semantic
intermediate representation ingesting RenderDoc, PIX, GFXReconstruct, and Nsight, feeding
deterministic tools, visualizers, CI, and LLMs. (Original doc:
`universal_graphics_capture_ir_project_idea.md`, in Downloads.)

That framing was reviewed and substantially narrowed. What survived, what got cut, and why is
the content of sections 3–7.

The one-line reframe:

| Before | After |
|---|---|
| "Universal Graphics IR" | "Normalized graphics capture database" |
| Perf explanation is the demo | Structural diff is the demo |
| Invent a query DSL | Use SQL / SQLite |
| PIX + D3D12 first | RenderDoc first |
| Recover *the* render graph | Derive *observed* structural relationships |
| AI is the point | AI is a late, optional consumer |

---

## 3. Decisions locked in

**Name:** `framedb`. Kept even after frames became plural — the frame is still the atomic unit
of comparison, so "a database of frames" is now *more* accurate than before. `frameshift` is the
pocket alternative if it ever grows a UI and needs a brand.

**Storage:** SQLite. Do **not** design a query DSL. SQL gives deterministic queries, the CLI,
CI checks, and text-to-SQL LLM grounding for free — text-to-SQL over a fixed schema is the most
reliable thing LLMs do.

**First adapter:** RenderDoc, via its Python replay API (or the C++ replay API it wraps, if a
native host is preferred). Not PIX. See section 7.

**Killer app:** structural frame diffing and CI regression gating — *not* performance
explanation. Structural findings are timing-independent, which sidesteps the biggest technical
threat to the project (section 7).

**Frames are first-class and plural from day one.** See section 5.

**Vocabulary discipline:** "capture" and "frame" are different scopes and must be used
rigorously in schema, CLI, and docs. Decide early whether `framedb diff a.db b.db` (no frame
specified) is an error or implies `#0` vs `#0`.

---

## 4. The core technical problem: cross-capture identity

This is the interesting part of the project and the thing worth getting right.

Event #1834 in capture A is meaningless next to event #1834 in capture B. Draws need a **stable
derived identity**, roughly:

```text
DrawIdentity =
    marker path
  + pipeline hash
  + shader hashes (VS/PS/CS/...)
  + render target set
  + bound resource signature
  + possibly mesh/index information
```

So instead of `Event 1834`:

```text
ShadowPass/
  Opaque/
    PSO=8f32...
    VS=91ab...
    PS=17cd...
    RT=[ShadowAtlas]
    DS=[ShadowDepth]
```

Goal: recognize the same conceptual draw across captures even when events were inserted, draw
order changed, unrelated objects were added, or the engine changed its command recording.

Target diff vocabulary:

```text
UNCHANGED  MOVED  ADDED  REMOVED  MODIFIED  SPLIT  MERGED  UNKNOWN
```

**Identity confidence must be exposed**, since capture-derived semantics are uneven:

```text
Identity confidence: HIGH        Identity confidence: LOW
  ✓ marker path                    ✓ PSO hash
  ✓ object name                    ✓ shader hash
  ✓ PSO hash                       ✗ no markers
  ✓ shader hash                    ✗ no object names
  ✓ render target set
```

### Multi-frame gives this a free test harness

The strongest argument for multi-frame support: frame *N* and frame *N+1* of the same capture are
the same scene with tiny deltas, so they **should** match at ~100%. That's ground truth.

> "My fingerprint matched 11,847 of 11,903 draws — what are those 56?"

Every mismatch is a labeled bug in the identity function. Without it, you're testing against two
builds of a real game where every mismatch is ambiguous (bad fingerprint, or real change?). If
identity can't match frame 100 to frame 101, it will never match build 4471 to 4472.

---

## 5. Schema: two-tier scoping

Not "add a `frame_index` column everywhere." The design is **capture-scoped vs frame-scoped**:

```sql
capture(capture_id, source_tool, api, app_name, ...)
frame(frame_id, capture_id, frame_index, boundary_kind, ...)

-- capture-scoped: survive across frames, same identity throughout
resource(resource_id, capture_id, handle, generation, name, desc...)
shader(shader_id, capture_id, hash, stage, ...)
pipeline(pipeline_id, capture_id, hash, ...)
rt_set(rt_set_id, capture_id, ...)             -- interned
binding_set(binding_set_id, capture_id, ...)   -- interned

-- frame-scoped: the submitted work
event(event_id, frame_id, index_in_frame, kind, marker_path, ...)
draw(event_id, pipeline_id, rt_set_id, binding_set_id, vtx_count, ...)
barrier(event_id, resource_id, state_before, state_after)

-- spans frames explicitly
resource_lifetime(resource_id, created_frame, destroyed_frame)
```

Key property: **single-frame is just N = 1.** The RenderDoc adapter writes one `frame` row and
nothing else in the schema knows or cares. No special case, no migration when GFXReconstruct
(genuinely multi-frame) arrives later.

### Three things that bite

1. **Frame boundaries are tool-defined.** Present is the obvious delimiter until multiple
   swapchains, headless compute with no swapchain at all, or PIX timing captures where "frame"
   is fuzzy. Hence `boundary_kind` ∈ {`present`, `marker`, `whole_capture`}. Record how you
   split; never hardcode present.
2. **Handle recycling.** A destroyed resource's API handle can be reused for an unrelated
   resource. RenderDoc's `ResourceId` handles this already; GFXReconstruct hands you raw
   handles and the normalizer owns generation tracking. Skip this and resource lifetime
   analysis silently merges two different textures.
3. **Interning keeps size sane.** Full per-event pipeline state × 200 frames explodes.
   Consecutive frames are near-identical, so interning RT sets and binding sets as
   capture-scoped rows means 200 frames costs barely more than 1 for the state tables. This is
   why they're separate tables, not inline columns on `draw`.

### CLI shape

A frame handle is `(db, frame_id)`, which makes intra-capture and cross-build diff the *same*
code path:

```text
framedb build capture.rdc  -o 4471.db
framedb build capture.gfxr -o run.db      # imports all frames
framedb frames run.db                     # list frames
framedb diff  run.db#100 run.db#101       # intra-capture
framedb diff  4471.db#0  4472.db#0        # cross-build
framedb query 4471.db "select * from draw where marker_path like 'Shadow/%'"
```

If a db holds exactly one frame, make `#N` optional so the RenderDoc UX stays clean.

---

## 6. What the tool should produce

### Structural diff (the demo)

```text
Build 4471                         Build 4472
──────────                         ──────────
ShadowPass                         ShadowPass
  128 draws                          256 draws       ← +128
  PSO A, PSO B                       PSO A, PSO B
                                     PSO C           ← NEW
LightingPass                       LightingPass
  412 draws                          412 draws
  3 barriers                         7 barriers      ← +4
PostProcess                        PostProcess
  HDR10A2                            RGBA16F         ← FORMAT CHANGE
```

All timing-independent. No AI required.

### CI regression fingerprints

Store a capture DB per build, run deterministic analysis:

```text
Draw count: +13.2%    Barrier count: +41    PSO count: +7
Shader permutations: +3    RT format changes: 1
New resources: 17     Removed resources: 2
```

With policies:

```text
FAIL if draw_count > baseline * 1.10
WARN if barrier_count > baseline * 1.20
FAIL if unexpected_render_target_format_change
```

This is the reason a normalized representation earns its existence — stronger than the AI case.

### Analyses only multi-frame can do

- **PSO created after frame 0** → runtime shader compilation → a top cause of shipped-game
  stutter, and invisible in a single-frame capture.
- Resources recreated per frame instead of pooled.
- Draw-count drift across frames; per-frame variance identifying *which* frame hitched.

### Structural relationships (not "the render graph")

Derivable: `resource → writer`, `resource → reader`, `resource → transition`,
`event → pipeline`, `event → shader`, `event → marker hierarchy`.

Phrase it as **observed/inferred structure**, never as "this is the engine's render graph."

---

## 7. Explicitly rejected — do not re-propose

**Perf explanation as the headline feature.** RenderDoc's per-event GPU duration comes from a
serialized re-replay with timestamps around each event: it flushes, kills overlap, destroys
async compute, and does not reflect the real frame. The one tool with trustworthy timing (PIX
timing captures) is the one with no programmatic access. Perf explanation is a late,
heavily-hedged feature at best.

**"Why did async compute overlap disappear?"** Structurally unanswerable from a RenderDoc
capture. Cut from scope.

**Rich engine semantics** (`Mesh: Rock_07`, `Material: Terrain`). Not in a capture. What's
actually there is debug markers (PIX events / `vkCmdBeginDebugUtilsLabel`) and `SetName` object
names — quality entirely dependent on engine discipline, often stripped in shipped builds. The
semantic layer is really *marker hierarchy + usage inference + surviving names*. Design the
schema around that, and expose confidence (section 4).

**PIX-first / D3D12-first.** `.wpix` is proprietary and undocumented; WinPixEventRuntime writes
markers, it doesn't read captures. Actual difficulty order: **RenderDoc → GFXReconstruct →
Nsight → PIX.** Start with RenderDoc even though D3D12 is the familiar API — RenderDoc captures
D3D12 fine.

**Calling it an "IR."** LLVM IR won because compilers *emit* into it. Nobody will emit into
this; every producer is a capture tool with its own model. It's a normalized import format and
analysis database. The "IR" framing invites "so who adopts it?" and the honest answer is
"nobody upstream, ever."

**Aliasing analysis as an early feature.** Any engine sophisticated enough to care already has a
transient resource allocator doing this at graph-compile time with information the capture
doesn't contain (heap layout, committed vs placed, allocator constraints). You'd be telling
people what they already know, with less information.

**A custom query DSL.** SQLite. See section 3.

**An adapter plugin system before the second adapter exists.** Guiding principle: *add
flexibility where retrofitting is global (frame scoping — do it now); skip it where retrofitting
is local (plugin system — wait).*

---

## 8. "Doesn't the RenderDoc Python API already do this?"

Asked and answered: it's the **data source, not the product**. It's an accessor API on a
stateful cursor —

```python
ctrl.SetFrameEvent(eid, False)
state = ctrl.GetD3D12PipelineState()
```

— giving `GetRootActions()` (renamed from drawcalls), per-event pipeline state,
`GetUsage(resourceId)`, shader reflection/disassembly, `FetchCounters([GPUDuration])`, texture
and buffer lists. `renderdoccmd python script.py` runs headless, so batch use is supported.

What it structurally is *not*:

1. **A query engine.** Any analytical question means walking events and calling
   `SetFrameEvent` on each, which replays state up to that event. Minutes, not milliseconds, and
   paid again every run.
2. **Multi-capture aware.** One `ReplayController`, one capture. No cross-capture anything.
   This is a deliberate upstream design decision, not a gap that might close next release.
3. **Persistent.** Every run re-replays a multi-GB capture, and needs a compatible RenderDoc, a
   GPU that can replay it, and often a comparable driver. A 20 MB `.db` has none of those
   dependencies and can be a CI build artifact. For the CI story this *is* the whole thing.
4. **Normalized.** The useful detail is in `D3D12State` vs `VKState`, structured completely
   differently; the common `PipeState` abstraction is thin.
5. **Relational.** "Resources written under `Shadow/*` and never read anywhere" is a join across
   usage, the action tree, marker hierarchy, and per-event state — hand-rolled and recomputed
   every invocation.

Analogy: the API is `libclang`. Nobody greps a codebase by re-parsing per query; you build an
index once. Or: `perf script` can dump everything, and that didn't make Perfetto's trace
processor pointless.

**Reframed: the API answers "what is the state at event N?" framedb answers "which N?"**

Honest limit: for one capture, one question, right now — a 40-line Python script beats the
database. Value is proportional to (repeated queries) × (cross-capture) × (persistence). The
API existing is *good news*: the foundation is a supported interface, not a reverse-engineered
file format.

---

## 9. Prior art to study

- **Perfetto trace processor** — trace → relational → SQL → UI. Proof the model works, and a
  warning that someone could extend it instead.
- **Android GPU Inspector (AGI)** — Google, Vulkan-focused, partially this idea. Worth a day
  understanding why it didn't take over.
- **GFXReconstruct** — documented-ish binary call stream, genuinely multi-frame (captures a
  frame range delimited by present). No timing. The natural second adapter.

---

## 10. Immediate next step

**The falsification experiment.** One evening, before committing to anything:

Write the extraction loop over a real capture — walk `GetRootActions()`, `SetFrameEvent` each
one, pull PSO/shader hashes, RT set, marker path — dump to JSON. **Time it and measure output
size.**

- Runs in seconds, small dump → the caching premise is weak; project narrows to identity +
  diff only (still worth doing, but persistence isn't the pitch).
- Takes minutes (expected at realistic event counts) → extract-once-query-forever is proven
  empirically, and you have the first real number for the README.

Either way you end the evening knowing whether the thing is real, with the extraction pass
half-written.

### Then, in order

1. Schema v0 with capture/frame two-tier scoping (section 5).
2. RenderDoc adapter → `framedb build`.
3. `framedb query` (thin SQLite wrapper).
4. Identity function + `framedb diff`, validated against consecutive frames (section 4).
5. CI fingerprints and policies (section 6).
6. Visualization.
7. LLM (text-to-SQL over the fixed schema).
8. Second adapter — GFXReconstruct.

---

## 11. Open questions

- Which fields make identity stable in practice, and what's the measured match rate on
  consecutive frames of a real capture?
- Where exactly to draw the frame boundary per adapter; how to represent captures with no
  present at all.
- How much of `D3D12State` / `VKState` to normalize vs. keep as provenance blobs.
- Provenance representation: how to record "this came from the capture" vs. "this was inferred,
  confidence medium."
- Multi-queue / async compute modeling — worth schema space now, or later?
- Does `framedb diff a.db b.db` with no frame specified error, or imply `#0`?
- Extraction cost per 1k events — the number that decides section 10.

---

## 12. Scope discipline

The failure mode for this project is a half-built "universal" tool abandoned at phase 3. A
finished narrow tool beats an abandoned ambitious one — for the community and for a
staff-level interview conversation.

Ship **RenderDoc → SQLite → diff** as a complete standalone thing. Let it become universal only
if people actually use it.
