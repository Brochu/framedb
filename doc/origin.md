# Universal Graphics Capture IR

## Project concept

Build a universal, queryable intermediate representation (IR) for graphics captures.

The system would ingest captures or execution traces from tools such as RenderDoc, PIX, GFXReconstruct, Nsight, and potentially vendor profiling tools, and translate their observable GPU work into a common semantic representation.

The long-term goal is **not simply an "AI for RenderDoc"**. The more interesting project is a graphics knowledge and analysis layer that can be consumed by:

- deterministic analysis tools
- query languages
- visualizers
- CI/regression systems
- custom graphics tooling
- LLMs and other AI systems

The AI component would sit on top of useful infrastructure rather than being the infrastructure itself.

---

# 1. Where the idea came from

The original discussion started from the observation that modern version-control systems can sometimes force developers to spend too much attention on the meta-management of a project rather than the project itself.

That led to a broader design principle:

> The tools should adapt to the programmer rather than requiring the programmer to constantly manage the tools.

Jujutsu (JJ) was discussed as an example of this philosophy. Rather than treating conflicts and history manipulation as interruptions that require immediate manual management, JJ makes more of the underlying state first-class and allows the user to continue working.

That same philosophy naturally applies to graphics tooling.

As a graphics programmer, a large amount of time can be spent navigating captures, searching for the relevant event, correlating resources, understanding barriers, comparing frames, and mentally reconstructing why the GPU is doing what it is doing.

The interesting question is therefore:

> Can graphics tooling make the underlying information accessible enough that the programmer can ask about the problem instead of manually managing the debugging process?

---

# 2. Why LLMs are interesting for graphics debugging

The most promising use of LLMs here is probably **not generating graphics code**.

Instead, the LLM can act as a reasoning and investigation layer.

A graphics capture already contains enormous amounts of useful data:

- draw calls
- dispatches
- resource transitions
- resource bindings
- pipeline state
- shaders
- timing information
- synchronization
- queue activity
- memory information
- dependencies

The difficult part is turning that information into an explanation.

An engineer might look at a frame and ask:

- Why is this pass taking 2.8 ms?
- Why did this frame become slower?
- Why does this barrier exist?
- Could these two resources alias?
- Why is occupancy low?
- Why did async compute overlap disappear?
- What changed between these two captures?
- Which three things are most worth investigating?

These are reasoning questions rather than simple data-retrieval questions.

An LLM is potentially very good at this kind of synthesis, **provided that it is grounded in deterministic data from the capture**.

---

# 3. Why GUI automation is probably the wrong abstraction

RenderDoc and PIX are primarily designed as interactive GUI tools.

One possible approach would be to let an LLM operate them like a human:

```text
Open capture
  -> select event
  -> open pipeline state
  -> inspect shader
  -> inspect resources
  -> navigate to barrier
  -> repeat
```

This is possible in principle, but it is probably the wrong architecture.

GUI automation is:

- fragile
- difficult to maintain
- dependent on UI layout
- inefficient
- overly procedural

The LLM would spend its time navigating the tool instead of reasoning about the graphics workload.

A better architecture is:

```text
Capture
   |
   v
Programmatic extraction
   |
   v
Structured graphics representation
   |
   v
Query / analysis engine
   |
   v
LLM
```

The LLM should ask the system for information rather than click around a GUI to discover it.

---

# 4. RenderDoc and programmatic access

RenderDoc is interesting because it already has programmatic interfaces that expose information about captures.

Conceptually, instead of asking an automated agent to click through the event browser, the system could expose operations such as:

```text
GetDrawCall(event)
GetPipelineState(event)
GetShaderReflection(shader)
GetBoundResources(event)
GetResourceHistory(resource)
GetTiming(event)
```

The exact API and available information would need to be investigated as part of the project.

The important architectural idea is:

> RenderDoc should be treated as a capture source, not as the interface the LLM has to operate.

---

# 5. PIX

PIX is particularly interesting for a D3D12-first implementation because it provides rich information about:

- D3D12 execution
- events
- timing
- GPU work
- resource state
- pipeline state
- shader information
- synchronization

A major research/engineering question would be determining what information can be accessed programmatically and how it can be normalized into the common representation.

The project should avoid depending on GUI automation if possible.

---

# 6. GFXReconstruct

GFXReconstruct is particularly interesting because its model is closer to a captured/replayed command stream.

Instead of primarily thinking in terms of a human-oriented debugging GUI, its data is much closer to:

```text
vkCmdBindPipeline
vkCmdBindDescriptorSets
vkCmdDrawIndexed
vkCmdPipelineBarrier
vkCmdDispatch
...
```

That makes it attractive as a programmatic input source.

The important distinction is:

> RenderDoc/PIX are excellent human debugging environments, while GFXReconstruct is closer to a machine-readable representation of graphics execution.

This makes GFXReconstruct worth studying even if the first implementation ultimately starts with D3D12.

---

# 7. The key realization: build an IR

The most important idea from the discussion was to stop thinking about:

> "How can an LLM understand a RenderDoc capture?"

and instead think about:

> "What is the right intermediate representation for captured graphics execution?"

The project could therefore look like:

```text
RenderDoc ─────┐
PIX ───────────┤
GFXReconstruct ┤
Nsight ────────┤
Other sources ┘
       |
       v
Capture adapters
       |
       v
Universal Graphics IR
       |
       +---- Query engine
       +---- Analysis engine
       +---- Visualization
       +---- CI / regression tools
       +---- LLM
```

This is much more interesting than an "AI plugin" because the IR itself can become useful infrastructure.

---

# 8. Why the IR should be semantic

An initial temptation would be to make the IR a normalized Vulkan/D3D12 command stream.

For example:

```text
vkCmdPipelineBarrier
vkCmdBindPipeline
vkCmdDrawIndexed
```

or:

```text
D3D12_RESOURCE_BARRIER
DrawIndexedInstanced
```

That would be useful, but it would still be too close to the APIs.

The more interesting abstraction is semantic.

Instead of:

```text
vkCmdPipelineBarrier(...)
```

the IR could represent:

```text
Barrier

Producer:
    Shadow Pass

Consumer:
    Lighting Pass

Resource:
    ShadowAtlas

Reason:
    Write -> Read dependency
```

Similarly, instead of merely representing:

```text
vkCmdDrawIndexed(...)
```

the semantic layer could expose:

```text
Draw

Mesh:
    Rock_07

Material:
    Terrain

Vertex count:
    5800

Pixel count:
    2.1M

Duration:
    0.48 ms

Pipeline:
    DeferredOpaque
```

The original API information should still be retained as provenance.

---

# 9. The render-graph connection

This is one of the most interesting aspects of the project.

Modern graphics engines often organize rendering through a render graph.

For example:

```text
             +-------------+
             | Shadow Pass |
             +------+------+
                    |
                ShadowAtlas
                    |
             +------v------+
             |  Lighting   |
             +------+------+
                    |
                 HDRColor
                    |
             +------v------+
             | Postprocess |
             +-------------+
```

The engine knows this graph explicitly.

However, a capture may only show the consequences of that graph:

```text
Create resource
Transition resource
Begin render pass
Bind render target
Draw
Draw
Transition resource
Bind SRV
Dispatch
...
```

This leads to the idea of **recovering an inferred graph from the execution stream**.

Conceptually:

```text
Engine Render Graph
        |
        v
   API / compiler
        |
        v
  Command Stream
        |
        v
     Capture
        |
        v
 Inferred / Observed Graph
```

The recovered graph would not necessarily be identical to the engine's original graph.

Different engine implementations can produce equivalent command streams.

Therefore the system should call it an **observed or inferred graph**, not automatically claim that it is the original engine render graph.

---

# 10. Multiple graphs should exist

The IR should probably expose several related graph views.

## Execution graph

What physically executed and in what order:

```text
Draw A -> Draw B -> Dispatch C
```

## Resource dependency graph

Which passes produce and consume resources:

```text
Shadow Pass
    |
    | writes
    v
ShadowAtlas
    |
    | reads
    v
Lighting Pass
```

## Synchronization graph

Where synchronization is required:

```text
Write ShadowAtlas
       |
       v
    Barrier
       |
       v
Read ShadowAtlas
```

## Resource lifetime graph

When resources exist and are used:

```text
Create
  |
  +-- Write
  +-- Read
  +-- Read
  |
Destroy
```

This is useful for detecting aliasing opportunities.

## Pipeline/shader graph

Relationships between:

- passes
- draws
- PSOs
- shaders
- root signatures / descriptor layouts
- resources
- pipeline state

The same capture can therefore be viewed through multiple graph structures.

---

# 11. Resource aliasing

Resource aliasing is a particularly good example of why reconstructing the graph is valuable.

Suppose the capture reveals:

```text
Texture A
Lifetime: [Pass 12, Pass 18]
Size: 32 MB

Texture B
Lifetime: [Pass 20, Pass 31]
Size: 28 MB
```

If their lifetimes do not overlap, they may potentially share physical storage.

The tool could report:

```text
Potential aliasing opportunity:
    Texture A
    Texture B

Non-overlapping lifetime:
    yes

Potential saving:
    approximately 28 MB
```

This is no longer simply displaying the capture.

The tool is **deriving knowledge from the capture**.

---

# 12. Engine graph versus observed graph

A particularly powerful future capability would be to compare the engine's actual render graph with the graph inferred from the capture.

Conceptually:

```text
Engine graph
     |
     | expected
     v
Observed capture graph
     |
     v
GPU execution
```

Potential findings could include:

- The engine considers two passes independent, but the capture contains synchronization between them.
- The engine expects two resources to alias, but their observed lifetimes overlap.
- A render-graph resource exists but never contributes to the frame.
- Unexpected barriers appear between otherwise independent passes.
- A dependency exists that the engine's higher-level representation does not make obvious.
- A supposedly transient resource has an unexpectedly long lifetime.

This turns the tool into something closer to a **rendering architecture validation system**.

---

# 13. Query language

Once the IR exists, a query language becomes extremely useful.

It could be SQL-like:

```sql
SELECT draws
WHERE duration > 0.5ms;
```

Or:

```sql
SELECT resources
WHERE never_read;
```

Or:

```sql
SELECT resources
WHERE lifetimes_do_not_overlap
  AND allocations_are_separate;
```

Or:

```sql
SELECT barriers
WHERE synchronization_cost > threshold;
```

The exact syntax is not important initially.

The important architecture is:

```text
Natural-language question
        |
        v
       LLM
        |
        v
     Query
        |
        v
Deterministic query engine
        |
        v
     Evidence
        |
        v
       LLM
        |
        v
Explanation
```

This keeps the model grounded.

---

# 14. The LLM should reason over evidence

For example:

User:

> Why is the lighting pass expensive?

The system could retrieve:

```text
Lighting Pass:
    GPU duration: 2.8 ms
    Draw count: 412
    Pixel count: 14.2M
    Shader: LightingPS
    Texture reads: 9
    Barrier count: 3
    Occupancy: 42%
```

The LLM can then produce something like:

> The strongest evidence points toward fragment workload and memory latency rather than synchronization. The pass processes approximately 14.2M pixels and the shader performs nine texture reads. Occupancy is only 42%, which may limit the GPU's ability to hide texture latency. I would investigate the lighting shader and its texture access pattern before focusing on the three barriers.

The important property is that the model is **synthesizing evidence**, not inventing GPU state.

---

# 15. Provenance and confidence

Because graph reconstruction is inference, the IR should track where information came from.

For example:

```text
Observation:
    source = capture
    confidence = high
```

versus:

```text
Inference:
    relationship = inferred dependency
    confidence = medium
    evidence =
        resource transition
        + write/read history
```

This is especially important for AI.

The LLM should be able to distinguish:

> "The capture shows..."

from:

> "Based on the observed resource usage, it appears..."

That makes the system much more trustworthy.

---

# 16. Architecture

A possible architecture:

```text
                 +------------------+
                 | RenderDoc        |
                 +--------+---------+
                          |
                 +--------v---------+
                 | PIX              |
                 +--------+---------+
                          |
                 +--------v---------+
                 | GFXReconstruct   |
                 +--------+---------+
                          |
                 +--------v---------+
                 | Other adapters   |
                 +--------+---------+
                          |
                          v
                 +------------------+
                 | Capture adapters |
                 +--------+---------+
                          |
                          v
                 +------------------+
                 | Universal IR     |
                 |                  |
                 | Execution        |
                 | Resources        |
                 | Dependencies     |
                 | Synchronization  |
                 | Lifetimes        |
                 | Shaders          |
                 | Timing           |
                 | Provenance       |
                 +--------+---------+
                          |
              +-----------+-----------+
              |           |           |
              v           v           v
           Queries    Analyses    Visualizer
              \           |           /
               +----------+----------+
                          |
                          v
                         LLM
```

The center of the system is the IR.

The LLM is one consumer.

---

# 17. Potential analyses

The system could eventually support:

- Finding the most expensive passes.
- Finding the most expensive draws or dispatches.
- Comparing two captures.
- Explaining performance regressions.
- Finding suspicious barriers.
- Finding unnecessary synchronization.
- Finding resource lifetime overlap.
- Finding aliasing opportunities.
- Tracing producer/consumer chains.
- Explaining why a barrier exists.
- Correlating shaders, PSOs, and resources.
- Finding unexpected pipeline changes.
- Finding shader permutation changes.
- Finding resources that appear unused.
- Finding unexpectedly long-lived resources.
- Summarizing an entire frame.
- Ranking the most promising optimization targets.
- Correlating engine metadata with observed GPU execution.

---

# 18. Why this could be valuable without AI

A strong test for the project is:

> If LLMs disappeared tomorrow, would this still be useful?

In this case, yes.

A universal graphics IR could independently support:

- GPU debugging
- performance regression testing
- frame visualization
- CI validation
- custom queries
- synchronization analysis
- resource lifetime analysis
- memory/aliasing analysis
- capture comparison
- graphics engine diagnostics

The LLM is therefore an additional interface rather than the foundation.

That makes the project much less dependent on any particular model or AI vendor.

---

# 19. Similarity to LLVM

One useful mental model is LLVM.

LLVM's important contribution was not merely another compiler frontend. It provided an intermediate representation that many different tools could operate on.

The analogous idea here would be:

```text
Capture tools
      |
      v
Universal Graphics IR
      |
      +--> Debuggers
      +--> Profilers
      +--> Optimizers
      +--> Visualizers
      +--> CI tools
      +--> Query tools
      +--> LLMs
```

This could make the IR more valuable than any individual frontend.

The project is therefore conceptually closer to:

> **"An LLVM-like IR for captured graphics execution."**

than:

> "ChatGPT for RenderDoc."

---

# 20. Initial implementation strategy

Do not begin by supporting every capture format.

A sensible progression would be:

### Phase 1 — D3D12 + one capture source

Start with a system you understand deeply.

Build:

- resource representation
- command representation
- pass representation
- state transitions
- timing
- provenance

### Phase 2 — Query engine

Build deterministic queries before adding AI.

For example:

```text
find expensive draws
find resource users
find resource lifetime
find barriers
find dependencies
```

### Phase 3 — Graph reconstruction

Build:

- execution graph
- resource dependency graph
- synchronization graph
- lifetime graph

### Phase 4 — Useful analyses

Implement:

- barrier analysis
- aliasing opportunities
- dependency chains
- expensive-pass detection
- capture comparison

### Phase 5 — Visualization

Build a frontend capable of showing the inferred graph and navigating back to capture events.

### Phase 6 — LLM integration

Allow natural-language questions to become deterministic queries.

### Phase 7 — More capture sources

Add:

- PIX
- RenderDoc
- GFXReconstruct
- Nsight
- other sources

The adapter architecture should make these additions independent of the core IR.

---

# 21. A strong first demo

A compelling first version would be:

> Drop a D3D12 capture into the tool.

The tool:

1. Extracts the command/resource information.
2. Builds the semantic IR.
3. Reconstructs the observed dependency and lifetime graph.
4. Displays an inferred render graph.
5. Highlights suspicious synchronization.
6. Identifies possible aliasing opportunities.
7. Allows the user to navigate from an IR node back to the original capture event.

No AI is required for the first demo.

Then add:

> Why can't these two resources alias?

or:

> Why is this pass slow?

The LLM answers using evidence retrieved from the IR.

---

# 22. Open technical questions

Important research/engineering questions include:

- What is the minimum semantic model capable of representing both D3D12 and Vulkan?
- How generic can the IR be without becoming meaningless?
- Which facts can reliably be reconstructed from a command stream?
- Which facts require engine-side metadata?
- How should provenance be represented?
- How should inferred relationships and confidence be represented?
- How should asynchronous compute and multiple queues be modeled?
- How should queue ownership transfer be represented?
- How should hardware counters be normalized across vendors?
- How should shader source, DXIL, SPIR-V, and disassembly be represented?
- How should incomplete allocation information affect aliasing analysis?
- What query language makes the most sense?
- What can be extracted programmatically from RenderDoc?
- What can be extracted programmatically from PIX?
- How much can GFXReconstruct's replay representation inform the IR?

---

# 23. Working project statement

> **Build a universal intermediate representation for graphics captures that converts API- and tool-specific GPU execution traces into a semantic, queryable model of frames, passes, resources, dependencies, synchronization, lifetimes, pipelines, shaders, timing, and memory. Use that model to reconstruct observed render/dependency graphs, perform deterministic graphics analyses, and provide an evidence-grounded interface for LLMs to investigate GPU performance and debugging questions.**

---

# 24. Broader vision

The long-term vision is a common language for describing graphics execution across capture systems.

```text
Capture tools
      |
      v
Capture adapters
      |
      v
Universal Graphics IR
      |
      +--------------------+
      |                    |
      v                    v
Deterministic tools       AI tools
      |                    |
      +----------+---------+
                 |
                 v
          Graphics engineer
```

Capture tools become frontends.

The IR becomes the shared semantic layer.

Queries and analyses become reusable infrastructure.

LLMs become an intelligent interface for exploring and reasoning about the data.

The most valuable contribution may ultimately not be the AI integration at all. It may be defining the right representation of GPU execution so that many different tools can finally reason about graphics work at a common semantic level.

That is the part of the idea with the potential to become lasting infrastructure.
