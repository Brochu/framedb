# framedb — first concrete slice

## Context

`doc/origin.md` and `doc/DESIGN-NOTES.md` contain a well-developed thesis but zero code. The
repo is docs + LICENSE + `.gitignore`. The goal of this plan is to get from "a good idea with
locked-in decisions" to "a `.db` file you can run SQL against," without drifting into the
half-built-universal-tool failure mode that `DESIGN-NOTES.md` §12 names as the project's
primary risk.

The broader motivation matters for scoping: this is an attempt to push graphics/real-time
tooling toward normalized, generally-accessible data instead of per-vendor walled gardens. Even
if `framedb` itself doesn't land, building it should tune attention toward where the real
tooling gaps are. That argues for finishing a narrow thing and learning from it, not for
breadth.

**Decisions taken for this plan** (confirmed with the user):

| Decision | Choice |
| --- | --- |
| First adapter | RenderDoc, as locked in by `DESIGN-NOTES.md` §3 |
| Stack | C-style C++17, matching existing project conventions |
| Scope | Falsification experiment → schema v0 → `build` → `query`. Diff comes after. |
| License | Relicense LGPL-2.1 → MIT |

---

## Verified environment facts

Checked directly, not assumed. Worth recording — `DESIGN-NOTES.md` asks future sessions to
read it first, and several of its assumptions have now been measured.

- **RenderDoc 1.45.0 installed at `C:\RenderDoc`** (not `Program Files`). `renderdoc.dll` is
  27 MB and present.
- **The C++ replay API is viable.** A byte-scan of `renderdoc.dll`'s export table confirms
  `RENDERDOC_InitialiseReplay`, `RENDERDOC_ShutdownReplay`, `RENDERDOC_OpenCaptureFile`,
  `RENDERDOC_GetVersionString`, and critically `RENDERDOC_AllocArrayMem` /
  `RENDERDOC_FreeArrayMem` — the hooks that let `rdcarray`/`rdcstr` allocate safely across the
  DLL boundary. This is what makes a non-Python consumer practical.
- **Only `renderdoc_app.h` ships in the install** — that is the *in-application* capture API,
  not the replay API. The replay headers must be fetched from the RenderDoc GitHub repo at tag
  `v1.45`, path `renderdoc/api/replay/`.
- **RenderDoc's embedded Python is 3.6** (`python36.dll`, `python36.zip`). The local Python is
  3.14, so the Python replay module was never importable here regardless. C++ is the right call.
- No import library ships for `renderdoc.dll`. One must be generated (see Milestone 0).
- `sqlite3.exe` is already at `C:\sqlite-tools\sqlite3.exe` — handy for poking at output.
- The user has D3D12 apps of their own to capture (`Explore-Ray`, `Raytracing-Bench`,
  `ModelViewerDx12` — the first two already emit `.rdi` files).

### Corrections to `doc/DESIGN-NOTES.md`

Two claims in the notes are wrong or outdated and should be fixed in the doc as part of this
work, so a future session doesn't act on them:

1. **§7 "PIX-first" rejection rests on a false premise.** It states `.wpix` is proprietary and
   undocumented and that "WinPixEventRuntime writes markers, it doesn't read captures." The
   **PIX Analysis API** (`WinPixCaptureReplay.dll`) reads captures programmatically and ships
   *official pure-Python ctypes bindings*. It is already vendored and working in the sibling
   project `C:\Users\alexb\Documents\pix-mcp` against a 468 MB `.wpix`, with a prototype
   (`list_draws.py`) that already walks queues → events → `ParentIndex` breadcrumbs → PSO
   tracking. RenderDoc-first still stands — it's public, redistributable, cross-API and
   cross-platform, which is what the "break the walled garden" thesis actually needs — but it
   should stand on *those* reasons, not on "PIX has no programmatic access."
2. **§4's "free test harness" doesn't exist as described.** Frame *N* vs *N+1* inside one
   capture presumes multi-frame captures. A `.rdc` holds exactly one frame, and PIX GPU
   captures are single-frame too. The real ground truth is **two captures taken back-to-back of
   the same static scene** — which is a better test anyway, since it exercises the actual
   cross-capture code path rather than a special case. The `frame` table stays (it costs
   nothing now and avoids a global retrofit when GFXReconstruct arrives), but the validation
   story changes.

---

## Repo layout and code conventions

Follows `Grav-85`, which is the most evolved of the reference projects (`ForgeProj`,
`Raytracing-Bench`, `Grav-85`): MSBuild `config.xml` with named targets rather than a flat
`build.bat`, unity translation unit, `.hpp`/`.cpp` pairs, module prefix. Prefix here is `fdb_`.
Development happens in VS Code with the C/C++ extension, but the command-line build stays
first-class — `config.xml` and `compile.bat` are the source of truth, and `tasks.json` just
shells out to them so there is only ever one build path to debug.

```
framedb/
  config.xml              msbuild targets: framedb, sqlite, clean, all
  compile.bat             msbuild -nologo config.xml -t:%*
  .vscode/
    c_cpp_properties.json include paths + msvc mode for IntelliSense
    tasks.json            invokes compile.bat
    launch.json           debug framedb.exe
  build/                  .obj output (gitignored)
  bin/                    framedb.exe (gitignored)
  libs/
    renderdoc/            api/replay/*.h vendored, pinned to v1.45
    sqlite/               sqlite3.c + sqlite3.h amalgamation
    renderdoc.lib         generated import lib (see Milestone 0)
  shared/
    shared_types.hpp      u8/u32/u64/etc., matching Grav-85
  src/
    ~framedb.cpp          includes every .cpp below
    fdb_main.cpp          CLI dispatch: build | query | frames
    fdb_arena.cpp/.hpp    mem_arena, lifted from Grav-85's qg_memory
    fdb_db.cpp/.hpp       sqlite open/exec, schema creation, prepared-stmt wrappers
    fdb_schema.cpp/.hpp   schema v0 DDL as a string literal + version check
    fdb_rd_adapter.cpp    RenderDoc replay → normalized rows
    fdb_dxbc.cpp/.hpp     DXBC/DXIL container walk → HASH part
  doc/                    existing notes (to be amended)
```

Style follows the existing projects by default — plain structs and free functions taking `T *`
first, `#pragma once`, `u8`/`u64` typedefs, `assert(false && "message")` for invariants,
module-prefixed names — but treat that as the starting point rather than a rule to defend. Where
the standard library is clearly the simpler answer (a `std::unordered_map` for an interning
table, say) it's fine to just use it; nothing in this project is performance-critical enough to
justify hand-rolling containers on principle. The pieces genuinely worth writing by hand are the
ones where the data model matters, not the ones where a container would do.

Compile flags from `Grav-85`: `-nologo -c -std:c++17 -EHsc -Zi -MD -TP`, separate `link.exe`
step. **Note `-MD`, not `Raytracing-Bench`'s `-MT`** — the dynamic CRT is the right choice here
given the process loads `renderdoc.dll` and its dependency chain.

**Arena allocation is a natural fit and worth leaning on.** Import is a batch process: allocate
one large `mem_arena` up front, build every row and lookup table inside it, write to SQLite, tear
the whole thing down at once. Nothing needs individual frees, which removes an entire category of
bookkeeping from the importer.

**One friction point worth planning around.** RenderDoc's replay headers are template-heavy
(`rdcarray<T>`, `rdcstr`, `rdcpair`). Confine them to `fdb_rd_adapter.cpp` as the *only*
translation unit that includes RenderDoc headers, and translate at that boundary into plain
`fdb_*` structs. Everything downstream then works on ordinary data, independent of any capture
tool's types. This is good adapter isolation regardless, and it's what makes the second adapter
cheap later.

**`arena_off` + generation is already the answer to a known problem.** `DESIGN-NOTES.md` §5
point 2 flags handle recycling — a destroyed resource's API handle can be reused for an
unrelated resource, and missing that silently merges two different textures in lifetime
analysis. `Grav-85`'s `arena_off { off, gen }` with generation-validated access is exactly that
pattern. RenderDoc's `ResourceId` already handles recycling internally so v0 doesn't strictly
need it, but the `resource.native_id` + generation column should be designed in now, because
GFXReconstruct hands over raw handles and the normalizer will own the tracking.

`.gitignore` needs `build/`, `bin/`, `*.db`, `*.rdc`, `libs/*.lib`, `libs/*.obj` added — the
current one is a generic C/C++ template with none of these.

---

## Milestone 0 — Falsification experiment

`DESIGN-NOTES.md` §10 makes this the gate before committing to anything, and it stays the gate.
The whole project's pitch is *extract once, query forever*; if extraction is already fast and
small, the persistence argument collapses and the project narrows to identity + diff only.

**Setup steps (one-time, and the parts most likely to eat an evening):**

1. Clone RenderDoc at tag `v1.45`, copy `renderdoc/api/replay/*.h` into `libs/renderdoc/`.
   Needed headers: `renderdoc_replay.h`, `apidefs.h`, `rdcarray.h`, `rdcstr.h`, `rdcpair.h`,
   `resourceid.h`, `basic_types.h`, `capture_options.h`, `control_types.h`, `data_types.h`,
   `replay_enums.h`, `shader_types.h`, `pipestate.h`, `structured_data.h`, `stringise.h`.
   Pin the version — these are internal headers with no ABI guarantee across releases.
2. Generate the missing import library from a Developer PowerShell:
   ```
   dumpbin /exports C:\RenderDoc\renderdoc.dll > rd_exports.txt
   ```
   Hand-write a `renderdoc.def` with `EXPORTS` listing the handful of `RENDERDOC_*` entry
   points actually used, then:
   ```
   lib /def:renderdoc.def /out:libs\renderdoc.lib /machine:x64
   ```
   Linking against this (rather than `LoadLibrary` + `GetProcAddress`) matters because the
   `rdcarray` headers reference `RENDERDOC_AllocArrayMem`/`FreeArrayMem` as ordinary extern
   symbols — they must resolve at link time or every array operation breaks.
3. Drop the SQLite amalgamation (`sqlite3.c`, `sqlite3.h`) into `libs/sqlite/` and give it its
   own `config.xml` target. It must compile as **C** (`-TC`), which conflicts with the project's
   default `-TP` — hence a separate `Exec` rather than adding it to the unity file. Compile once,
   link `build\sqlite3.obj` thereafter.
4. Note that `renderdoc.dll` must be on `PATH` or beside the exe at run time.

**The experiment itself** — a single throwaway `main` that:

- `RENDERDOC_InitialiseReplay` → `RENDERDOC_OpenCaptureFile` → `OpenFile(path, "rdc", nullptr)`
  → `OpenCapture(ReplayOptions{}, nullptr)`.
- Walks `IReplayController::GetRootActions()` depth-first, maintaining a marker stack from
  actions flagged `ActionFlags::PushMarker` to build `marker_path`.
- For each draw/dispatch action: `SetFrameEvent(eventId, false)`, then `GetPipelineState()` —
  the API-agnostic `PipeState`, not `GetD3D12PipelineState()` — and pull the PSO `ResourceId`,
  per-stage shader `ResourceId`s, `GetOutputTargets()`, `GetDepthTarget()`.
- Dumps one line per action to a file.

**Measure three numbers and write them down:**

| Number | Why it matters |
| --- | --- |
| `OpenCapture` wall time | Fixed cost every consumer pays without a cached `.db` |
| Per-`SetFrameEvent` cost × action count | The core of the extract-once argument |
| Dump size vs `.rdc` size | The compression/CI-artifact story |

**Decision rule.** Minutes at realistic event counts → the premise holds and you have the first
real number for the README. Seconds with a small dump → say so plainly in the notes and narrow
the project to identity + diff, where the value is cross-capture rather than persistence.

**Also worth timing here, cheaply:** `ICaptureFile::GetStructuredData()`, which returns the
capture's serialised chunk tree *without* a replay device. If it's fast, there may be a
GPU-free extraction path for purely structural data — which would be a significant CI
advantage, since it drops the "needs a GPU that can replay this capture" dependency. Not part
of v0, but the measurement is nearly free while the harness is open.

---

## Milestone 1 — Schema v0

Two-tier capture/frame scoping per `DESIGN-NOTES.md` §5. `fdb_schema.cpp` holds the DDL as a
string literal plus a `schema_version` check; there is no migration machinery in v0 — a version
mismatch is a clean error telling you to rebuild the `.db`.

**Capture-scoped** (identity stable across frames):

```sql
capture(capture_id, source_tool, source_version, api, driver,
        capture_file, imported_at, framedb_version, schema_version)
frame(frame_id, capture_id, frame_index, boundary_kind)   -- CHECK IN ('present','marker','whole_capture')

resource(resource_id, capture_id, native_id, generation, name, name_is_auto, kind,
         format, width, height, depth, mips, array_size, samples,
         UNIQUE(capture_id, native_id, generation))
shader(shader_id, capture_id, native_id, stage, entry_point, encoding,
       container_hash, bytecode_len, UNIQUE(capture_id, native_id))
pipeline(pipeline_id, capture_id, native_id, kind, UNIQUE(capture_id, native_id))
pipeline_shader(pipeline_id, stage, shader_id, PRIMARY KEY(pipeline_id, stage))

-- interned; the reason these are tables and not inline columns on draw (§5)
rt_set(rt_set_id, capture_id, signature, UNIQUE(capture_id, signature))
rt_set_member(rt_set_id, slot, resource_id, is_depth)
binding_set(binding_set_id, capture_id, signature, UNIQUE(capture_id, signature))
binding(binding_set_id, stage, bind_type, space, slot, resource_id)
```

**Frame-scoped** (the submitted work):

```sql
event(event_id, frame_id, index_in_frame, native_event_id, kind,
      marker_path, parent_event_id, name)
draw(event_id, pipeline_id, rt_set_id, binding_set_id,
     num_indices, num_instances, dispatch_x, dispatch_y, dispatch_z)
resource_usage(event_id, resource_id, usage)   -- from IReplayController::GetUsage()
```

Notes on specific choices:

- `native_event_id` is the RenderDoc `eventId`, kept purely so a query result can be navigated
  back to the capture in the RenderDoc UI. It is *not* an identity key — §4 is explicit that
  event IDs are meaningless across captures.
- `resource.generation` is always `0` for the RenderDoc adapter, whose `ResourceId` already
  disambiguates recycled handles. The column exists now because adding it later is a global
  retrofit across every FK — the same reasoning §5 uses to justify frame scoping on day one.
- `signature` on the interned sets is a deterministic text encoding of the members (sorted,
  stable), used as the interning key. It also makes "did the RT set change" a string compare in
  SQL, which the diff will want.
- `pipeline_shader` as a join table rather than `vs_shader_id`/`ps_shader_id`/... columns —
  handles mesh/amplification/raytracing stages without schema churn, and keeps the table
  API-agnostic for the eventual Vulkan path.
- **No `barrier` table in v0.** How barriers/transitions surface through the replay API needs
  investigation (they are not plain entries in the action list the way draws are). This is the
  single most valuable addition immediately after v0, because "3 barriers → 7 barriers" is a
  headline signal in the §6 diff mock-up. Flagged, not silently dropped.
- **No per-row provenance in v0.** Every field written by the RenderDoc adapter comes directly
  from the capture, so a provenance column would be a constant. Add it when the first genuinely
  inferred field exists, and record the reasoning in the notes so §15 of `origin.md` doesn't
  look forgotten.

Indices to create up front: `event(frame_id, index_in_frame)`, `event(marker_path)`,
`draw(pipeline_id)`, `resource_usage(resource_id)`, `resource_usage(event_id)`.

---

## Milestone 2 — `framedb build`

`framedb build capture.rdc -o out.db`

`fdb_rd_adapter.cpp` performs one pass over the capture and streams rows into SQLite. Structure:

1. **Open + capture row.** Populate `capture` from `RENDERDOC_GetVersionString()`,
   `ICaptureFile::DriverName()`, and `IReplayController::GetAPIProperties()` for `api`.
2. **One `frame` row**, `frame_index = 0`, `boundary_kind = 'present'`. Per §5 this is the
   whole point of the two-tier design: single-frame is just N = 1 and nothing downstream knows.
3. **Resources up front.** `GetResources()` for names and `autogeneratedName`, joined with
   `GetTextures()` / `GetBuffers()` for descriptors. Insert all `resource` rows before events so
   later inserts are pure FK lookups against an in-memory `ResourceId → resource_id` map.
4. **Action tree walk.** Depth-first over `GetRootActions()` with a marker stack; emit an
   `event` row per action, `draw` row per draw/dispatch. Marker actions become `event` rows with
   `kind='marker'` so the hierarchy is queryable rather than only encoded in `marker_path`.
5. **Per-event state.** `SetFrameEvent` then `GetPipelineState()`; intern the RT set and binding
   set, resolve or insert the pipeline and its shaders.
6. **Shader hashing.** `ShaderReflection::rawBytes` → `fdb_dxbc.cpp` walks the DirectX container
   to the `HASH` part and reads the 16-byte digest. This is ~50 lines and needs no crypto —
   DXC/FXC write the digest at compile time. **Also store the flag field alongside the digest**:
   `/Zsb` vs `/Zss` builds produce different digests for identical shaders, so the diff must be
   able to detect that flags differ and fall back rather than report a false change. This is
   already worked out in `pix-mcp/.claude/brainstorm.md` and transfers directly.
7. **Usage.** `GetUsage(ResourceId)` per resource → `resource_usage` rows. Cheaper than
   per-event inspection and gives the read/write relationships §6 calls "observed structure."

Performance shape that matters given the C++ choice: wrap the entire import in a single
transaction, reuse prepared statements across rows, and set `PRAGMA journal_mode=OFF` /
`synchronous=OFF` during import only. Interning tables are keyed on `ResourceId` and on set
signatures. The dominant cost will almost certainly be `SetFrameEvent` inside RenderDoc rather
than anything on our side — Milestone 0's numbers settle it, and if SQLite does turn out to
matter, batching inserts is the lever.

---

## Milestone 3 — `framedb query` and `framedb frames`

Deliberately thin, per §3's "do not design a query DSL."

- `framedb query out.db "SELECT ..."` — open, `sqlite3_prepare_v2`, print rows as aligned text,
  `--csv` for piping. No query rewriting, no dialect of our own.
- `framedb frames out.db` — list frames; exists so the `db#N` handle syntax from §5 has
  something to enumerate before diff arrives.
- Ship a handful of `CREATE VIEW`s in the schema so the useful questions are one word long, e.g.
  a `v_draw` view joining `event`/`draw`/`pipeline`/`rt_set` so `marker_path`, PSO hash and
  render targets are visible without hand-writing the join every time.

`DESIGN-NOTES.md` §5 leaves open whether `framedb diff a.db b.db` with no frame specified is an
error or implies `#0`. Not needed until diff exists — but decide it when `frames` lands, since
that's where the `#N` parser gets written.

---

## Housekeeping

- Replace `LICENSE` (LGPL-2.1) with MIT. Apache-2.0 is the swap if a patent grant is ever wanted
  for studio adoption; MIT is the lower-friction default for a tool this size.
- Amend `doc/DESIGN-NOTES.md`: fix §7's PIX premise and §4's test-harness claim per the
  Corrections section above, and add the measured Milestone 0 numbers. The file explicitly tells
  future sessions to trust it, so leaving known-wrong statements in place is the expensive
  option.
- Update `README.md` with build instructions once `config.xml` works (`compile.bat framedb`,
  run from a Developer PowerShell so `cl`/`link`/`lib` are on `PATH`).

---

## Verification

Each milestone has a concrete end-state, not a "looks right":

1. **Milestone 0.** Three numbers written into `doc/DESIGN-NOTES.md`, and an explicit
   go/narrow decision recorded against the §10 decision rule.
2. **Milestone 2.** Build a `.db` from a capture of `Raytracing-Bench` or `Explore-Ray` (both
   already produce `.rdi` files, so capturing them is a known-good path). Then:
   - `sqlite3 out.db "SELECT count(*) FROM draw"` matches RenderDoc's own event-browser count
     for the same capture. **This is the correctness check that matters** — it catches action
     tree walk bugs (double-counting markers, missing `ExecuteIndirect` children) that nothing
     else will.
   - Every `draw` row has a non-null `pipeline_id`. Unresolved PSOs mean the state query is
     wrong; the equivalent counter in `list_draws.py` exists precisely because this fails
     silently otherwise.
   - Spot-check three `marker_path` values against the RenderDoc UI hierarchy.
   - `.db` size and build wall time recorded for the README.
3. **Milestone 3.** These queries return sensible rows on a real capture:
   ```sql
   SELECT marker_path, count(*) FROM v_draw GROUP BY marker_path ORDER BY 2 DESC;
   SELECT * FROM resource WHERE resource_id NOT IN (SELECT resource_id FROM resource_usage);
   SELECT stage, count(DISTINCT container_hash) FROM shader GROUP BY stage;
   ```
   The second one — resources that exist but are never used — is the first thing framedb can
   answer that a RenderDoc session genuinely cannot, and is worth confirming by hand.
4. **Cross-capture smoke test.** Build two `.db` files from two back-to-back captures of the
   same static scene. They should have near-identical draw counts and identical shader hash
   sets. This is the ground truth described in the Corrections section, and it is the input the
   identity function will be developed against next.

---

## Explicitly out of scope for this plan

Named so they don't creep in: the identity function and `framedb diff` (next, and the actual
demo); CI fingerprints and policies; barrier extraction (first addition after v0); visualization;
LLM/text-to-SQL; the GFXReconstruct adapter; any adapter plugin system. `DESIGN-NOTES.md` §7's
existing rejections — perf explanation as the headline, rich engine semantics, early aliasing
analysis, a custom query DSL — all still stand and are not revisited here.

---

## Open questions to resolve while building

- Do the v1.45 replay headers compile cleanly under `-std:c++17 -EHsc -MD -TP`? Vendored
  template headers are the likely first friction point; prefer `#pragma warning(push/disable)`
  around the includes over weakening the project's flags. They also need to be reachable from
  `c_cpp_properties.json` or IntelliSense will red-squiggle the whole adapter.
- Confirm `-MD` interoperates with `renderdoc.dll`'s CRT. The `RENDERDOC_AllocArrayMem` /
  `FreeArrayMem` exports exist precisely so array memory is allocated and freed on the same side
  of the boundary, so this should hold — but it is cheap to verify early and expensive to
  discover late.
- What does `GetPipelineState()` (generic `PipeState`) actually surface for D3D12 versus
  `GetD3D12PipelineState()`? §8 calls the common abstraction "thin." If it's too thin for the RT
  set and bound resources, fall back to the D3D12-specific state for v0 and treat normalization
  as the adapter's job — but prefer the generic path wherever it suffices, since it's what makes
  Vulkan cheap later.
- How do barriers/resource transitions surface through the replay API at all?
- Is `GetStructuredData()` fast enough to be a GPU-free structural extraction path?
