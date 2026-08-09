# thirdparty

Vendored dependencies for framedb.

---

## RenderDoc — v1.45

Provides the **replay API**: opening a `.rdc`, walking the action tree, and querying per-event
pipeline state. Note this is *not* `renderdoc_app.h`, which is the in-application capture API and
is unrelated to what framedb does.

### Producing the binaries

The RenderDoc installer ships neither an import library nor the replay headers, so the backend is
built from source:

1. Download the source for tag `v1.45` from the RenderDoc repo.
2. Open the solution in the source tree and build the **core `renderdoc` module** (the backend —
   the UI, Python bindings, and other projects are not needed) as **x64 Release**.
3. Copy `renderdoc.dll`, `renderdoc.lib`, `renderdoc.exp` and `renderdoc.pdb` into
   `thirdparty/renderdoc/`.
4. Copy the public headers from `renderdoc/api/replay/*.h` in the source tree into the same
   folder, plus `version.h`.

Building the backend yourself is what makes this work at all: it produces the `renderdoc.lib`
import library that the installer does not ship, and it guarantees the headers and the DLL come
from the same build, which matters because these are internal headers with no ABI guarantee
across releases.

### Consuming it

- **`-DRENDERDOC_PLATFORM_WIN32` is required.** Without it, `apidefs.h` fails with
  `#error "Unknown platform"` — an unhelpful first error if you don't know to expect it.
- Include path is this directory; `renderdoc_replay.h` pulls in everything else transitively.
- Link `renderdoc.lib`. Nothing needs `LoadLibrary`/`GetProcAddress`.
- `rdcarray<T>` and `rdcstr` allocate through the DLL's exported `RENDERDOC_AllocArrayMem` /
  `RENDERDOC_FreeArrayMem`, so memory is allocated and freed on the same side of the module
  boundary. This is why linking against the import lib matters and why `-MD` is safe.

### Notes

- This is a local build, so `RENDERDOC_GetCommitHash()` returns
  `NO_GIT_COMMIT_HASH_DEFINED_AT_BUILD_TIME` rather than a hash. Version is confirmed via
  `RENDERDOC_GetVersionString()` → `1.45`.
- A separate official RenderDoc install may also exist on the machine (typically `C:\RenderDoc`),
  and its `renderdoc.dll` is a *different binary* from this one despite both reporting 1.45. Keep
  the copy from this folder next to `framedb.exe` so the executable-directory search order
  guarantees the DLL matching this `.lib` is the one loaded.

---

## SQLite — 3.53.4

Precompiled DLL plus the matching amalgamation header, from sqlite.org.

If a single self-contained `framedb.exe` becomes preferable to shipping a DLL alongside it,
switch to the amalgamation `sqlite3.c` and compile it as C (`-TC`, since the project defaults to
`-TP`). That drops the runtime dependency entirely.

---

## Verifying a fresh setup

From a Developer PowerShell, compile and link a translation unit that includes both libraries:

```
cl -nologo -c -std:c++17 -EHsc -Zi -MD -TP -DRENDERDOC_PLATFORM_WIN32 ^
   -Ithirdparty\renderdoc -Ithirdparty\sqlite check.cpp
link -NOLOGO -DEBUG check.obj thirdparty\renderdoc\renderdoc.lib thirdparty\sqlite\sqlite3.lib
```

With the DLLs beside the resulting executable, a good setup reports matching versions on both
sides:

```
sqlite header  : 3.53.4
sqlite runtime : 3.53.4
renderdoc dll  : 1.45
```

Returning an `rdcstr` from the DLL and pushing to an `rdcarray` are the useful things to exercise
— they cross the allocator boundary, which is where a mismatched header/binary pair shows up.
