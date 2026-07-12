// ExportWorker.cpp — InstaMAT2RemixExport.exe
//
// Out-of-process exporter for the plugin's Push flow. It hosts the InstaMAT
// SDK itself (GetInstaMAT from Studio's InstaMAT.dll), loads the saved
// layering .IMP, executes the layer graph, and writes canonical PBR channel
// images into an output directory.
//
// WHY a separate process: Studio 3.1 services IElementExecution::Execute
// through a host-registered GL-context callback; the QOpenGLContext it makes
// current is owned by Studio's render thread, so an Execute issued from a
// plugin (menu/GUI) thread hits Qt's "Cannot make QOpenGLContext current in
// a different thread" qFatal — a __fastfail that no SEH guard can catch
// (WinDbg-confirmed against the 2026-07-06 crash dumps). Hosted standalone,
// the SDK creates its own WGL context on THIS process's main thread, which
// is also the thread that initializes the SDK and runs Execute — exactly the
// contract InstaMATAPI.h documents. A worker crash can never take Studio
// down; the plugin just reports the failure.
//
// Protocol: machine-readable lines on stdout, prefixed "IM2RX " — everything
// else on stdout/stderr is engine noise and must be ignored by the parent.
//   IM2RX BUILDDATE=<sdk build date>   IM2RX SDKVERSION=<major.minor>
//   IM2RX AUTH=<ok|fail>               IM2RX INIT=<gpu|cpu|fail>
//   IM2RX LIBRARY=<ok|skip|fail>       IM2RX GRAPH=<name>
//   IM2RX RUNG rung=<name> bind=<...> execute=<...>
//   IM2RX OUTPUTCOUNT=<n>              IM2RX OUTPUTSKIP name='<n>' (<why>)
//   IM2RX OUTPUTFALLBACK name='<n>' canonical=albedo
//   IM2RX CHANNEL=<canonical>:<filename>
//   IM2RX FATAL=<usage|environment|project|outputs>   (deterministic failure —
//                the plugin must NOT retry; always followed by ERROR=)
//   IM2RX ERROR=<message>              IM2RX DONE=<ok|fail>
// Exit code 0 iff DONE=ok. Old plugins ignore unknown lines; a failure
// without FATAL= is treated as transient (retriable) — today's behavior.

#include "InstaMATAPI.h"
#include "PbrChannelMap.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX  // keep std::min/std::max usable (windows.h defines min/max macros)
#include <windows.h>

#include <cstdarg>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <chrono>

namespace fs = std::filesystem;

namespace {

std::string Narrow(const wchar_t* wide) {
    if (!wide || !*wide) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(len > 0 ? len - 1 : 0), '\0');
    if (len > 1) WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring Widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(len > 0 ? len - 1 : 0), L'\0');
    if (len > 1) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), len);
    return out;
}

void Say(const char* fmt, ...) {
    fputs("IM2RX ", stdout);
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
    fputc('\n', stdout);
    fflush(stdout);
}

int Fail(const std::string& message) {
    Say("ERROR=%s", message.c_str());
    Say("DONE=fail");
    return 1;
}

// Deterministic failure: a fresh worker process would fail identically, so
// the plugin must not burn its retry ladder on it. Reason tokens: usage,
// environment, project, outputs — the plugin appends a rename tip for
// "outputs".
int FailFatal(const char* reason, const std::string& message) {
    Say("FATAL=%s", reason);
    return Fail(message);
}

// The Studio-output-name -> canonical-PBR-channel mapping lives in the shared
// PbrChannelMap.h (used verbatim here and by the plugin's tests).
using InstaMAT2Remix::MapStudioOutputToCanonicalPbr;
using InstaMAT2Remix::ChannelMayLegitimatelyRenderFlat;

int BitsForCanonical(const std::string& canonical) {
    return canonical == "height" ? 16 : 8;
}

// Reads the resolution the user baked the project at. InstaMAT stores it in a
// "BakeSettings" JSON embedded in the .IMP:
//   "BakeSettings" : { … "Height" : 4096, … "Width" : 4096, … }
// This is the "baked size" Push must honor (the layer graph's own Resolution
// input reads 0x0 / auto, so it is NOT a usable source). We scan the .IMP
// bytes directly rather than round-tripping through an SDK resource API —
// simpler, and the container is a stable InstaMAT format. Returns false when
// the marker/keys are absent (older projects, format change) so the caller
// can fall back. outW/outH set only on success.
bool ReadBakeResolutionFromImp(const std::string& impPath, unsigned& outW, unsigned& outH) {
    std::ifstream f(Widen(impPath), std::ios::binary);
    if (!f) return false;
    const std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    // Anchor to the settings OBJECT ("BakeSettings" : {), not the later
    // ResourceMeta reference ("BakeSettings-<proj>.json" : {).
    const std::string marker = "\"BakeSettings\" : {";
    const size_t pos = data.find(marker);
    if (pos == std::string::npos) return false;
    const size_t windowEnd = std::min(data.size(), pos + 16384);

    auto readIntKey = [&](const char* key, unsigned& out) -> bool {
        const std::string k = std::string("\"") + key + "\" :";
        size_t kp = data.find(k, pos);
        if (kp == std::string::npos || kp >= windowEnd) return false;
        kp += k.size();
        while (kp < data.size() && (data[kp] == ' ' || data[kp] == '\t')) ++kp;
        unsigned val = 0;
        bool any = false;
        while (kp < data.size() && data[kp] >= '0' && data[kp] <= '9') {
            val = val * 10u + static_cast<unsigned>(data[kp] - '0');
            ++kp;
            any = true;
        }
        if (!any) return false;
        out = val;
        return true;
    };

    unsigned w = 0, h = 0;
    if (!readIntKey("Width", w) || !readIntKey("Height", h)) return false;
    if (w < 8 || h < 8 || w > 16384 || h > 16384) return false; // sanity
    outW = w;
    outH = h;
    return true;
}

// --- SEH guards. Separate functions with no C++ objects in scope so
// __try/__except does not conflict with C++ unwinding (same pattern as
// ExecuteGuarded below). A worker crash is survivable, but a *caught*
// fault lets us report the rung and try the next one.
// Same signature as the (protected) IElementExecution::pfnProgressDelegate.
typedef bool (*ProgressFn)(const InstaMAT::IGraph& graph, const float progress);

bool ExecuteGuarded(InstaMAT::IElementExecution* exec,
                    ProgressFn progress,
                    unsigned long* outCode) {
    *outCode = 0;
    __try {
        return exec->Execute(progress);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *outCode = static_cast<unsigned long>(GetExceptionCode());
        return false;
    }
}

bool DeallocExecutionGuarded(InstaMAT::IInstaMAT* api, InstaMAT::IElementExecution* exec) {
    __try {
        api->DeallocElementExecution(exec);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int g_progressLastDecile = -1; // reset before each Execute (see TryRung)

bool ProgressDelegate(const InstaMAT::IGraph&, const float progress) {
    const int decile = static_cast<int>(progress * 10.0f);
    if (decile > g_progressLastDecile) {
        g_progressLastDecile = decile;
        Say("PROGRESS=%d", decile * 10);
    }
    return true; // never abort
}

struct Options {
    bool probe = false;
    bool skipLibrary = false;
    std::string impPath;
    std::string outDir;
    std::string meshPath;
    std::string imagePath; // Materialize source image (bytes-bound like the mesh)
    std::string studioDir = "C:\\Program Files\\InstaMAT Studio";
    std::string format = "png"; // png|tga|jpg
    std::string backend = "gpu"; // gpu|cpu
    // "InstaMAT" is the product entitlement id — it selects the shared
    // machine license Studio's activation wrote to
    // %PROGRAMDATA%\InstaMAT\InstaMAT\1.0\InstaMAT.license (probe-verified).
    std::string entitlements = "InstaMAT";
    std::string authPath = "null"; // "null" → nullptr (shared user storage)
    std::string forceRung;         // debug: "inherit"|"url"|"bytes" forces one rung
    bool noPlugins = false;        // debug: BackendFlagNoPlugins (suppresses importer plugins)
    bool diag = false;             // debug: dump instance inputs/outputs
    bool rawOut = false;           // debug: write raw outputs, skip composition
    unsigned sleepMs = 0;          // debug: delay between Execute and output walk
    bool noRetry = false;          // debug: single attempt, no step-down
    std::string only;              // debug: process only this canonical channel
    unsigned retries = 1;          // debug: in-process same-size execution attempts
    // 0x0 = project-native: skip SetFormat so the instance keeps the saved
    // project's own baked resolution (a 4K bake exports 4K).
    unsigned width = 0;
    unsigned height = 0;
};

// std::stoul throws on non-numeric input, which would abort the process
// without an IM2RX ERROR/DONE line — parse defensively instead.
bool ParseUnsigned(const std::string& v, unsigned& out) {
    if (v.empty()) return false;
    unsigned long acc = 0;
    for (char c : v) {
        if (c < '0' || c > '9') return false;
        acc = acc * 10ul + static_cast<unsigned long>(c - '0');
        if (acc > 0xFFFFFFFFul) return false;
    }
    out = static_cast<unsigned>(acc);
    return true;
}

bool ParseArgs(int argc, wchar_t** argv, Options& opt, std::string& err) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = Narrow(argv[i]);
        auto next = [&](std::string& into) -> bool {
            if (i + 1 >= argc) { err = "missing value for " + a; return false; }
            into = Narrow(argv[++i]);
            return true;
        };
        auto nextUnsigned = [&](unsigned& into) -> bool {
            std::string v;
            if (!next(v)) return false;
            if (!ParseUnsigned(v, into)) { err = "invalid number for " + a + ": " + v; return false; }
            return true;
        };
        if (a == "--probe") opt.probe = true;
        else if (a == "--no-library") opt.skipLibrary = true;
        else if (a == "--imp") { if (!next(opt.impPath)) return false; }
        else if (a == "--out") { if (!next(opt.outDir)) return false; }
        else if (a == "--mesh") { if (!next(opt.meshPath)) return false; }
        else if (a == "--image") { if (!next(opt.imagePath)) return false; }
        else if (a == "--studio") { if (!next(opt.studioDir)) return false; }
        else if (a == "--format") { if (!next(opt.format)) return false; }
        else if (a == "--backend") { if (!next(opt.backend)) return false; }
        else if (a == "--entitlements") { if (!next(opt.entitlements)) return false; }
        else if (a == "--authpath") { if (!next(opt.authPath)) return false; }
        else if (a == "--rung") { if (!next(opt.forceRung)) return false; }
        else if (a == "--no-plugins") opt.noPlugins = true;
        else if (a == "--diag") opt.diag = true;
        else if (a == "--rawout") opt.rawOut = true;
        else if (a == "--sleepms") { if (!nextUnsigned(opt.sleepMs)) return false; }
        else if (a == "--noretry") opt.noRetry = true;
        else if (a == "--only") { if (!next(opt.only)) return false; }
        else if (a == "--retries") { if (!nextUnsigned(opt.retries)) return false; }
        else if (a == "--width") { if (!nextUnsigned(opt.width)) return false; }
        else if (a == "--height") { if (!nextUnsigned(opt.height)) return false; }
        else { err = "unknown argument: " + a; return false; }
    }
    if (!opt.probe && (opt.impPath.empty() || opt.outDir.empty())) {
        err = "--imp and --out are required (or use --probe)";
        return false;
    }
    if (opt.format != "png" && opt.format != "tga" && opt.format != "jpg") opt.format = "png";
    return true;
}

enum class RungMode { Inherit, Url, Bytes };
const char* RungName(RungMode m) {
    switch (m) {
    case RungMode::Inherit: return "inherit";
    case RungMode::Url:     return "url";
    default:                return "bytes";
    }
}

// One fresh Alloc → CreateInstance → SetFormat → bind → Execute attempt.
// Mirrors the plugin's TryExecuteRung (RemixConnector.cpp) minus the
// custom-geometry rung, which is not needed when the saved package binding
// resolves in a clean process.
InstaMAT::IElementExecution* TryRung(InstaMAT::IInstaMAT& api,
                                     InstaMAT::IGraph& graph,
                                     RungMode mode,
                                     const Options& opt,
                                     unsigned width,
                                     unsigned height,
                                     bool* executeAttempted) {
    InstaMAT::IElementExecution* exec = api.AllocElementExecution();
    if (!exec) {
        Say("RUNG rung=%s bind=n/a execute=skipped (AllocElementExecution failed)", RungName(mode));
        return nullptr;
    }
    if (!exec->CreateInstance(graph, InstaMAT::ElementExecutionFlags::None)) {
        DeallocExecutionGuarded(&api, exec);
        Say("RUNG rung=%s bind=n/a execute=skipped (CreateInstance failed)", RungName(mode));
        return nullptr;
    }
    if (opt.diag) {
        if (InstaMAT::IGraph* instance = exec->GetInstance()) {
            const InstaMAT::uint32 inCount = instance->GetParameterCount(InstaMAT::IGraph::ParameterTypeInput);
            for (InstaMAT::uint32 i = 0; i < inCount; ++i) {
                InstaMAT::IGraphVariable* var = instance->GetParameterAtIndex(i, InstaMAT::IGraph::ParameterTypeInput);
                if (!var) continue;
                const InstaMAT::IGraphObject* varObj = var->AsObject();
                const char* nm = varObj ? varObj->GetName(true) : "?";
                const InstaMAT::uint32 vt = var->GetVariableTypeValue();
                const InstaMAT::ArithmeticGraphValue v = var->GetArithmeticValue();
                Say("DIAGIN[%u] name='%s' type=%u vec2ui=(%u,%u) f2=(%.3f,%.3f)",
                    i, nm ? nm : "?", vt,
                    v.Vector2UI32Value[0], v.Vector2UI32Value[1],
                    v.Vector2FValue[0], v.Vector2FValue[1]);
            }
        }
    }

    // A fresh instance's execution format defaults to 1x1 (measured), so
    // SetFormat is mandatory even for "native" export.
    if (!exec->SetFormat(width, height, InstaMAT::ElementExecutionFormat::Normalized16)) {
        DeallocExecutionGuarded(&api, exec);
        Say("RUNG rung=%s bind=n/a execute=skipped (SetFormat failed)", RungName(mode));
        return nullptr;
    }

    InstaMAT::IGraphVariable* meshVar = nullptr;
    if (InstaMAT::IGraph* instance = exec->GetInstance()) {
        const InstaMAT::uint32 inCount = instance->GetParameterCount(InstaMAT::IGraph::ParameterTypeInput);
        for (InstaMAT::uint32 i = 0; i < inCount; ++i) {
            InstaMAT::IGraphVariable* var = instance->GetParameterAtIndex(i, InstaMAT::IGraph::ParameterTypeInput);
            if (var && var->GetVariableTypeValue() == InstaMAT::IGraphVariable::TypeElementMesh) {
                meshVar = var;
                break;
            }
        }
    }

    std::string bindNote = "ok";
    bool bindOk = true;
    switch (mode) {
    case RungMode::Inherit: {
        if (!meshVar) { bindNote = "ok (graph has no ElementMesh input)"; break; }
        // A real mesh input with only its saved pkg:// binding renders BLANK
        // outputs in a standalone process (the URL doesn't resolve here —
        // headless-verified 2026-07-06). Refuse UNCONDITIONALLY (not only when
        // a mesh file was supplied — a stale layer project reached without
        // --mesh must fail, not blank-push) unless the rung was forced for
        // debugging. Graphs without a mesh input pass through above.
        if (opt.forceRung.empty()) {
            bindOk = false;
            bindNote = "skip: inherited pkg:// mesh bindings render blank outputs "
                       "standalone; a mesh file bytes bind is required";
            break;
        }
        const char* url = meshVar->GetResourceURLValue();
        if (!url || !*url) { bindOk = false; bindNote = "skip: instance has no inherited mesh binding"; }
        else bindNote = std::string("ok (inherited ") + url + ")";
        break;
    }
    case RungMode::Url: {
        if (!meshVar) { bindOk = false; bindNote = "skip: no ElementMesh input"; break; }
        if (opt.meshPath.empty() || !fs::exists(Widen(opt.meshPath))) {
            bindOk = false; bindNote = "skip: mesh path missing (" + opt.meshPath + ")";
            break;
        }
        std::string slash = opt.meshPath;
        for (char& c : slash) if (c == '\\') c = '/';
        const std::string meshUrl = "file:///" + slash;
        if (!exec->SetResourceURLForGraphVariable(*meshVar, meshUrl.c_str())) {
            bindOk = false; bindNote = "skip: SetResourceURLForGraphVariable returned false";
        }
        break;
    }
    case RungMode::Bytes: {
        if (!meshVar) { bindOk = false; bindNote = "skip: no ElementMesh input"; break; }
        std::ifstream f(Widen(opt.meshPath), std::ios::binary);
        if (opt.meshPath.empty() || !f) { bindOk = false; bindNote = "skip: cannot read mesh file"; break; }
        std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const std::string name = fs::path(Widen(opt.meshPath)).filename().u8string();
        if (!exec->SetResourceForGraphVariable(*meshVar, name.c_str(), bytes.data(),
                                               static_cast<InstaMAT::uint64>(bytes.size()))) {
            bindOk = false; bindNote = "skip: SetResourceForGraphVariable returned false";
        }
        break;
    }
    }

    // Materialize source image: bytes-bind the first ElementImage input when
    // the caller supplied one (a package's own image binding may not resolve
    // standalone — the same failure mode as meshes).
    if (bindOk && !opt.imagePath.empty()) {
        InstaMAT::IGraphVariable* imageVar = nullptr;
        if (InstaMAT::IGraph* instance = exec->GetInstance()) {
            const InstaMAT::uint32 inCount = instance->GetParameterCount(InstaMAT::IGraph::ParameterTypeInput);
            for (InstaMAT::uint32 i = 0; i < inCount; ++i) {
                InstaMAT::IGraphVariable* var = instance->GetParameterAtIndex(i, InstaMAT::IGraph::ParameterTypeInput);
                if (!var) continue;
                const InstaMAT::uint32 vt = var->GetVariableTypeValue();
                if (vt == InstaMAT::IGraphVariable::TypeElementImage ||
                    vt == InstaMAT::IGraphVariable::TypeElementImageGray) {
                    imageVar = var;
                    break;
                }
            }
        }
        if (!imageVar) {
            bindNote += " (no ElementImage input for --image; package binding kept)";
        } else {
            std::ifstream f(Widen(opt.imagePath), std::ios::binary);
            if (!f) {
                bindOk = false;
                bindNote = "skip: cannot read image file (" + opt.imagePath + ")";
            } else {
                std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                const std::string name = fs::path(Widen(opt.imagePath)).filename().u8string();
                if (exec->SetResourceForGraphVariable(*imageVar, name.c_str(), bytes.data(),
                                                      static_cast<InstaMAT::uint64>(bytes.size()))) {
                    bindNote += " (+image bytes)";
                } else {
                    bindOk = false;
                    bindNote = "skip: SetResourceForGraphVariable(image) returned false";
                }
            }
        }
    }

    if (!bindOk) {
        DeallocExecutionGuarded(&api, exec);
        Say("RUNG rung=%s bind=%s execute=skipped", RungName(mode), bindNote.c_str());
        return nullptr;
    }

    g_progressLastDecile = -1;
    unsigned long sehCode = 0;
    // Everything above is a deterministic bind refusal; from here on a
    // failure may be the driver-transient render race — the caller uses this
    // to decide whether an all-rungs failure is worth a fresh-process retry.
    if (executeAttempted) *executeAttempted = true;
    const bool ok = ExecuteGuarded(exec, &ProgressDelegate, &sehCode);
    if (!ok) {
        if (sehCode != 0) Say("RUNG rung=%s bind=%s execute=SEH 0x%08lX", RungName(mode), bindNote.c_str(), sehCode);
        else Say("RUNG rung=%s bind=%s execute=returned false", RungName(mode), bindNote.c_str());
        DeallocExecutionGuarded(&api, exec);
        return nullptr;
    }
    Say("RUNG rung=%s bind=%s execute=ok", RungName(mode), bindNote.c_str());
    return exec;
}

struct ExportOutcome {
    bool executed = false;   // a rung ran to completion
    int written = 0;         // channels written to disk
    bool collapsed = false;  // a color/normal channel came out 1x1 while others were full
    unsigned maxDim = 0;     // largest channel width produced
    // Diagnostics for the written==0 case: distinguish "graph exposes no
    // outputs" from "outputs exist but none is named after a PBR channel"
    // from "--only filtered everything" from "sampler/write failures".
    unsigned outputTotal = 0;               // GetParameterCount(ParameterTypeOutput)
    int mappedMatched = 0;                  // mapped by name AND passed --only
    std::vector<std::string> unmappedNames; // outputs MapStudioOutputToCanonicalPbr rejected
    bool albedoFallback = false;            // lone unmapped image output exported as albedo
    std::string fallbackOutputName;
    bool executeAttempted = false;          // some rung got past bind to Execute
};

// Runs the rung ladder at (width,height), then the two-pass output walk into
// opt.outDir. Deallocs the execution it creates; leaves the package/backend to
// the caller (so it can retry at a different size). See wmain for the retry.
ExportOutcome RunExportAtSize(InstaMAT::IInstaMAT& api, InstaMAT::IGraph& layerGraph,
                              const Options& opt, unsigned width, unsigned height) {
    ExportOutcome out;

    // Rung ladder. When the caller supplies the real mesh file, feed its RAW
    // BYTES first — headless-verified on a real project (2026-07-06):
    //  - inherit: saved projects bind their mesh via a pkg:// URL of the
    //    parent project package, unresolvable in a fresh process → Execute
    //    "succeeds" but every output is a blank, uncomposited default.
    //  - url: the standalone engine cannot read file:/// resources → same.
    //  - bytes: SetResourceForGraphVariable(raw file bytes) → the layer stack
    //    composites correctly.
    // No Url rung in the default ladders: file:/// resources are documented to
    // never resolve standalone (Execute "succeeds" with blank outputs), so it
    // exists only behind the --rung debug flag. With a mesh file supplied,
    // Bytes is the one correct bind; Inherit stays as the fallback for graphs
    // that genuinely have no ElementMesh input (it refuses otherwise).
    std::vector<RungMode> ladder;
    if (opt.forceRung == "inherit")    ladder = {RungMode::Inherit};
    else if (opt.forceRung == "url")   ladder = {RungMode::Url};
    else if (opt.forceRung == "bytes") ladder = {RungMode::Bytes};
    else if (!opt.meshPath.empty())    ladder = {RungMode::Bytes, RungMode::Inherit};
    else                               ladder = {RungMode::Inherit};

    InstaMAT::IElementExecution* exec = nullptr;
    for (const RungMode mode : ladder) {
        bool attempted = false;
        exec = TryRung(api, layerGraph, mode, opt, width, height, &attempted);
        out.executeAttempted = out.executeAttempted || attempted;
        if (exec) break;
    }
    if (!exec) return out; // executed=false

    InstaMAT::IGraph* instance = exec->GetInstance();
    if (!instance) {
        DeallocExecutionGuarded(&api, exec);
        Say("ERROR=GetInstance returned null after Execute.");
        return out;
    }
    out.executed = true;

    std::error_code ec;
    fs::create_directories(Widen(opt.outDir), ec);
    if (opt.sleepMs > 0) { Say("SLEEP=%u", opt.sleepMs); Sleep(opt.sleepMs); }

    // TWO-PASS output walk. Sampling one composition output and releasing its
    // sampler before sampling the next collapses the earlier channels to 1x1 —
    // only the last output ('Normal') survived a one-pass loop (headless
    // forensics 2026-07-06). Allocating ALL output samplers first and keeping
    // every one alive until all are written keeps each channel's GPU resource
    // realized. Pass 1: alloc. Pass 2: write. Pass 3: dealloc.
    struct Pending {
        std::string canonical;
        std::string outputName;
        InstaMAT::IImageSampler* sampler;
    };
    std::vector<Pending> pending;

    // Pre-scan: resolve every output's name/type/canonical channel BEFORE any
    // sampler is allocated, so unmapped outputs show up in the log
    // (OUTPUTSKIP) and the lone-output albedo fallback can decide with the
    // full picture. Fresh Element Graphs expose generically-named outputs —
    // silently skipping them here was the "no recognized PBR channel outputs"
    // bug (2026-07-12).
    struct Scanned {
        InstaMAT::IGraphVariable* var;
        std::string outputName;
        std::string canonical;  // empty = unmapped
        bool isImage;
    };
    std::vector<Scanned> scanned;

    const InstaMAT::uint32 outputCount = instance->GetParameterCount(InstaMAT::IGraph::ParameterTypeOutput);
    out.outputTotal = outputCount;
    Say("OUTPUTCOUNT=%u", outputCount);
    for (InstaMAT::uint32 i = 0; i < outputCount; ++i) {
        InstaMAT::IGraphVariable* var = instance->GetParameterAtIndex(i, InstaMAT::IGraph::ParameterTypeOutput);
        if (!var) continue;
        const InstaMAT::IGraphObject* varObj = var->AsObject();
        if (!varObj) continue;
        const char* rawName = varObj->GetName(true);
        const std::string outputName = rawName ? rawName : "";
        const InstaMAT::uint32 vt = var->GetVariableTypeValue();
        const bool isImage = (vt == InstaMAT::IGraphVariable::TypeElementImage ||
                              vt == InstaMAT::IGraphVariable::TypeElementImageGray);
        scanned.push_back({var, outputName, MapStudioOutputToCanonicalPbr(outputName), isImage});
    }

    int unmappedImageOutputs = 0;
    std::vector<char> exportable(scanned.size(), 0);
    for (size_t i = 0; i < scanned.size(); ++i) {
        Scanned& s = scanned[i];
        if (s.canonical.empty()) {
            Say("OUTPUTSKIP name='%s' (unmapped)",
                InstaMAT2Remix::PbrSanitizeForProtocolLine(s.outputName).c_str());
            out.unmappedNames.push_back(s.outputName);
            if (s.isImage) ++unmappedImageOutputs;
            continue;
        }
        if (!opt.only.empty() && s.canonical != opt.only) {
            Say("OUTPUTSKIP name='%s' (filtered by --only=%s)",
                InstaMAT2Remix::PbrSanitizeForProtocolLine(s.outputName).c_str(),
                opt.only.c_str());
            continue;
        }
        exportable[i] = 1;
        ++out.mappedMatched;
    }

    // Lone-output fallback: nothing mapped but exactly one unmapped image
    // output → export it as albedo ("generate a texture, push it" — mirrors
    // InstaMAT's own Blender add-on treating generic outputs as Base Color).
    if (InstaMAT2Remix::ShouldFallbackLoneOutputToAlbedo(out.mappedMatched,
                                                         unmappedImageOutputs, opt.only)) {
        for (size_t i = 0; i < scanned.size(); ++i) {
            Scanned& s = scanned[i];
            if (!s.canonical.empty() || !s.isImage) continue;
            s.canonical = "albedo";
            exportable[i] = 1;
            ++out.mappedMatched;
            out.albedoFallback = true;
            out.fallbackOutputName = s.outputName;
            Say("OUTPUTFALLBACK name='%s' canonical=albedo",
                InstaMAT2Remix::PbrSanitizeForProtocolLine(s.outputName).c_str());
            break;
        }
    }

    for (size_t i = 0; i < scanned.size(); ++i) {
        if (!exportable[i]) continue;
        InstaMAT::IGraphVariable* var = scanned[i].var;
        const std::string& outputName = scanned[i].outputName;
        const std::string& canonical = scanned[i].canonical;

        InstaMAT::IGraphVariable* finalVar = opt.rawOut
            ? nullptr : exec->GetCompositionGraphOutputForOutputParameter(*var);
        const bool isCompositionOutput = (finalVar != nullptr);
        if (!finalVar) finalVar = var;

        const bool sRGB = isCompositionOutput
            ? false
            : (finalVar->GetVariableTypeValue() != InstaMAT::IGraphVariable::TypeElementImageGray &&
               finalVar->GetColorSpaceTypeValue() == InstaMAT::IGraphVariable::ColorSpaceTypeSRGB);

        InstaMAT::IImageSampler* sampler = exec->AllocImageSamplerForOutputParameter(*finalVar, sRGB);
        if (!sampler) {
            Say("CHANNELFAIL=%s (AllocImageSamplerForOutputParameter failed)", outputName.c_str());
            continue;
        }
        pending.push_back({canonical, outputName, sampler});
    }

    // Determine the largest dimension produced (proves what the engine CAN
    // render this pass) to judge whether smaller channels are collapsed.
    for (const Pending& p : pending) {
        out.maxDim = (std::max)(out.maxDim, static_cast<unsigned>(p.sampler->GetWidth()));
    }
    // Total collapse: EVERY channel came out 1x1 while a real size was
    // requested. The per-channel check below compares against maxDim and
    // cannot see this case, which used to report COLLAPSED=0 and defeat the
    // plugin's fresh-process retry. (Unless every output is a legitimately
    // flat channel — then 1x1 across the board is valid.)
    if (!pending.empty() && width > 1 && out.maxDim <= 1) {
        for (const Pending& p : pending) {
            if (!ChannelMayLegitimatelyRenderFlat(p.canonical)) {
                out.collapsed = true;
                break;
            }
        }
    }

    bool sizeReported = false;
    for (const Pending& p : pending) {
        const unsigned w = p.sampler->GetWidth();
        const unsigned h = p.sampler->GetHeight();
        Say("CHANNELSIZE name='%s' canonical=%s comp=%ux%u", p.outputName.c_str(),
            p.canonical.c_str(), w, h);
        if (!sizeReported) { Say("SIZE=%ux%u", w, h); sizeReported = true; }

        // A color/normal/roughness/metalness channel that came out 1x1 while
        // another channel rendered full is the standalone SDK's high-res
        // collapse (uniform grayscale channels like height/thickness/radius
        // are excluded — they legitimately render flat/1x1).
        if (out.maxDim > 1 && w <= 1 && !ChannelMayLegitimatelyRenderFlat(p.canonical)) {
            out.collapsed = true;
        }

        const std::string fileName = p.canonical + "." + opt.format;
        // u8string(): the SDK's Write* APIs take UTF-8.
        const std::string destPath = (fs::path(Widen(opt.outDir)) / Widen(fileName)).u8string();
        bool ok = false;
        if (opt.format == "tga")      ok = p.sampler->WriteTGA(destPath.c_str(), false);
        else if (opt.format == "jpg") ok = p.sampler->WriteJPEG(destPath.c_str(), 95, false);
        else                          ok = p.sampler->WritePNG(destPath.c_str(),
                                                              BitsForCanonical(p.canonical), false);
        if (ok) {
            Say("CHANNEL=%s:%s", p.canonical.c_str(), fileName.c_str());
            ++out.written;
        } else {
            Say("CHANNELFAIL=%s (Write%s failed)", p.outputName.c_str(), opt.format.c_str());
        }
    }
    for (const Pending& p : pending) api.DeallocImageSampler(p.sampler);

    DeallocExecutionGuarded(&api, exec);
    return out;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);

    Options opt;
    std::string argErr;
    if (!ParseArgs(argc, argv, opt, argErr)) {
        return FailFatal("usage",
                    argErr + " — usage: InstaMAT2RemixExport.exe [--probe] --imp <file> --out <dir> "
                             "[--width N] [--height N] [--format png|tga|jpg] [--mesh <file>] "
                             "[--image <file>] [--studio <dir>] [--backend gpu|cpu] [--no-library]");
    }

    // Verify the output directory is writable BEFORE the expensive SDK init;
    // a failure here used to surface only after a full render as a misleading
    // "no recognized PBR channel outputs" (times the plugin's retries).
    if (!opt.probe) {
        std::error_code outDirEc;
        fs::create_directories(Widen(opt.outDir), outDirEc);
        if (outDirEc) {
            return FailFatal("environment",
                        "Cannot create the output directory: " + opt.outDir +
                        " (" + outDirEc.message() + ")");
        }
    }

    // 1. Host the SDK from the Studio installation.
    const std::wstring studioW = Widen(opt.studioDir);
    SetDllDirectoryW(studioW.c_str());
    const std::wstring dllPath = studioW + L"\\InstaMAT.dll";
    HMODULE mod = LoadLibraryW(dllPath.c_str());
    if (!mod) {
        return FailFatal("environment",
                    "LoadLibrary failed for " + opt.studioDir + "\\InstaMAT.dll (error " +
                    std::to_string(GetLastError()) + ")");
    }

    const auto getBuildDate = reinterpret_cast<pfnGetInstaMATBuildDate>(
        GetProcAddress(mod, "GetInstaMATBuildDate"));
    const auto getInstaMAT = reinterpret_cast<pfnGetInstaMAT>(
        GetProcAddress(mod, "GetInstaMAT"));
    if (!getBuildDate || !getInstaMAT) {
        return FailFatal("environment",
                    "GetInstaMAT/GetInstaMATBuildDate exports not found in InstaMAT.dll");
    }

    InstaMAT::int32 dllVersion = 0;
    const char* buildDate = getBuildDate(&dllVersion);
    Say("BUILDDATE=%s", buildDate ? buildDate : "?");
    Say("SDKVERSION=%d.%d", dllVersion >> 16, dllVersion & 0xFFFF);
    // Same major-match rule as PluginMain.cpp: minor bumps are additive, so
    // request the DLL's own version to pass its equality gate.
    if ((dllVersion >> 16) != (INSTAMAT_API_VERSION >> 16)) {
        return FailFatal("environment",
                    "SDK major version mismatch (headers " +
                    std::to_string(INSTAMAT_API_VERSION >> 16) + ", dll " +
                    std::to_string(dllVersion >> 16) + ")");
    }

    InstaMAT::IInstaMAT* api = nullptr;
    if (getInstaMAT(dllVersion, &api) != 1u || !api) {
        return FailFatal("environment", "GetInstaMAT failed");
    }

    // 2. Authorization: NULL storage path = the shared machine license
    //    Studio's own activation already wrote.
    const char* entitlements = (opt.entitlements == "null") ? nullptr : opt.entitlements.c_str();
    const char* authPath = (opt.authPath == "null") ? nullptr : opt.authPath.c_str();
    const bool authInit = api->InitializeAuthorization(entitlements, authPath);
    Say("AUTHINITRC=%d entitlements=%s authpath=%s", authInit ? 1 : 0,
        entitlements ? entitlements : "(null)", authPath ? authPath : "(null)");
    const bool authorized = api->IsHostAuthorized();
    Say("AUTH=%s", authorized ? "ok" : "fail");
    if (!authorized) {
        const char* info = api->GetAuthorizationInformation();
        Say("AUTHINFO=%s", info ? info : "?");
        // keep going — Initialize() reports the authoritative failure
    }

    // 3. Initialize the execution backend on THIS thread (the thread that
    //    will Execute — the InstaMATAPI.h contract).
    const auto t0 = std::chrono::steady_clock::now();
    std::string backendUsed = opt.backend;
    // BackendFlagNone: the engine loads its bundled importer plugins (USD/
    // OBJ/FBX mesh import lives in separate InstaLOD*Plugin.dll files).
    const InstaMAT::IInstaMAT::BackendFlags initFlags = opt.noPlugins
        ? InstaMAT::IInstaMAT::BackendFlagNoPlugins
        : InstaMAT::IInstaMAT::BackendFlagNone;
    bool initOk = api->Initialize(opt.backend == "cpu" ? InstaMAT::IInstaMAT::BackendTypeCPU
                                                       : InstaMAT::IInstaMAT::BackendTypeGPU,
                                  initFlags);
    if (!initOk && opt.backend != "cpu") {
        Say("INITNOTE=GPU backend init failed, retrying CPU");
        initOk = api->Initialize(InstaMAT::IInstaMAT::BackendTypeCPU, initFlags);
        backendUsed = "cpu";
    }
    const auto initMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count();
    Say("INIT=%s (%lld ms)", initOk ? backendUsed.c_str() : "fail", static_cast<long long>(initMs));
    if (!initOk) {
        // The GPU→CPU in-process retry above already absorbed the transient
        // case; a double init failure is licensing/installation.
        const char* info = api->GetAuthorizationInformation();
        return FailFatal("environment",
                    std::string("SDK Initialize failed. Authorization info: ") + (info ? info : "?"));
    }

    if (opt.probe) {
        api->ShutdownBackend();
        Say("DONE=ok");
        return 0;
    }

    // 4. System library first (layer graphs instance Library nodes), then the
    //    saved project package.
    if (!opt.skipLibrary) {
        const std::string libPath = opt.studioDir + "\\Environment\\Library.IMP";
        const auto tLib = std::chrono::steady_clock::now();
        const bool libOk = api->LoadPackage(libPath.c_str(), true, true);
        const auto libMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - tLib).count();
        Say("LIBRARY=%s (%lld ms)", libOk ? "ok" : "fail", static_cast<long long>(libMs));
    } else {
        Say("LIBRARY=skip");
    }

    InstaMAT::IGraphPackage* pkg = api->AllocPackageFromFile(opt.impPath.c_str(), false);
    if (!pkg) return Fail("AllocPackageFromFile failed for: " + opt.impPath);

    InstaMAT::IGraphObject** objs = nullptr;
    if (!api->GetGraphObjectsInPackage(*pkg, &objs) || !objs) {
        api->DeallocPackage(pkg);
        return FailFatal("project",
                    "GetGraphObjectsInPackage returned nothing for: " + opt.impPath);
    }

    // Tiered graph selection: layering graphs first (Asset Texturing /
    // Material Layering projects), then plain Element graphs, then Materialize
    // graphs — so all Pull templates can be pushed. NPass/Atom/Function graphs
    // stay rejected (no PBR output contract).
    struct GraphTier {
        const char* label;
        InstaMAT::uint32 typeA;
        InstaMAT::uint32 typeB;
    };
    static const GraphTier kGraphTiers[] = {
        {"layer",       InstaMAT::IGraph::GraphTypeElementLayer,
                        InstaMAT::IGraph::GraphTypeElementMaterialLayer},
        {"element",     InstaMAT::IGraph::GraphTypeElement,
                        InstaMAT::IGraph::GraphTypeElement},
        {"materialize", InstaMAT::IGraph::GraphTypeMaterialize,
                        InstaMAT::IGraph::GraphTypeMaterialize},
    };

    InstaMAT::IGraph* layerGraph = nullptr;
    std::string layerGraphName;
    std::string graphTypeLabel;
    std::string typesSeen;
    for (const GraphTier& tier : kGraphTiers) {
        int tierCount = 0;
        for (InstaMAT::uint32 i = 0; objs[i] != nullptr; ++i) {
            InstaMAT::IGraph* g = objs[i]->AsGraph();
            if (!g) continue;
            const InstaMAT::uint32 gtype = g->GetGraphTypeUI32();
            if (gtype != tier.typeA && gtype != tier.typeB) continue;
            ++tierCount;
            if (!layerGraph) {
                layerGraph = g;
                graphTypeLabel = tier.label;
                const char* name = objs[i]->GetName(true);
                layerGraphName = name ? name : "";
            }
        }
        if (tierCount > 0) Say("GRAPHTIER tier=%s count=%d", tier.label, tierCount);
        if (layerGraph) break;
    }
    if (!layerGraph) {
        for (InstaMAT::uint32 i = 0; objs[i] != nullptr; ++i) {
            InstaMAT::IGraph* g = objs[i]->AsGraph();
            if (!g) continue;
            const char* ts = g->GetGraphTypeString();
            if (!typesSeen.empty()) typesSeen += ", ";
            typesSeen += (ts && *ts) ? ts : std::to_string(g->GetGraphTypeUI32());
        }
        api->DeallocPackage(pkg);
        return FailFatal("project",
                    "No layer/element/materialize graph in: " + opt.impPath +
                    (typesSeen.empty() ? std::string(" (no graphs at all)")
                                       : (" (graph types found: " + typesSeen + ")")));
    }
    Say("GRAPH=%s", layerGraphName.c_str());
    Say("GRAPHTYPE=%s", graphTypeLabel.c_str());

    // 5. Resolve the export size. Explicit --width/--height wins (the plugin
    // passes it for a fixed ExportResolution setting). Otherwise Auto → the
    // resolution the user BAKED the project at (BakeSettings JSON), which is
    // exactly what "push the baked size" means; 2048 only if unreadable.
    unsigned width = opt.width;
    unsigned height = opt.height;
    if (width == 0 || height == 0) {
        unsigned bakeW = 0, bakeH = 0;
        if (ReadBakeResolutionFromImp(opt.impPath, bakeW, bakeH)) {
            width = bakeW; height = bakeH;
            Say("NATIVE=%ux%u (BakeSettings)", width, height);
        } else {
            width = 2048; height = 2048;
            Say("NATIVE=unavailable (no BakeSettings resolution) — defaulting %ux%u", width, height);
        }
    }

    // 6. Export ONCE at the resolved size. The standalone SDK renderer has a
    // nondeterministic race where non-'Normal' channels intermittently come
    // out 1x1 instead of full resolution (independent of size/mesh/timing —
    // exhaustive 2026-07-06 forensics; 'Normal', processed last, always wins;
    // GPU contention from other apps makes it worse). Retrying at a DIFFERENT
    // size in-process corrupts the backend, so the worker is single-shot and
    // reports COLLAPSED=1 when it happens — the PLUGIN re-spawns a fresh worker
    // process (an independent race draw) to get a clean render.
    // Single-shot: the collapse race is per-PROCESS (a process whose first
    // execution collapses keeps collapsing — in-process re-execution does NOT
    // recover, verified 2026-07-06). Retrying is therefore the plugin's job:
    // it re-spawns a fresh worker (an independent race draw) on COLLAPSED=1.
    const ExportOutcome outcome = RunExportAtSize(*api, *layerGraph, opt, width, height);

    api->DeallocPackage(pkg);
    api->ShutdownBackend();

    if (!outcome.executed) {
        // Bind refusals (pkg:// mesh, unreadable files) are deterministic;
        // only a failure that reached Execute may be the driver-transient
        // render race and is worth the plugin's fresh-process retry.
        if (!outcome.executeAttempted)
            return FailFatal("project",
                        "Could not execute the layer project (every bind attempt "
                        "was refused — see RUNG lines).");
        return Fail("Could not execute the layer project (all rungs failed — see RUNG lines).");
    }
    if (outcome.written == 0) {
        if (outcome.outputTotal == 0)
            return FailFatal("outputs", InstaMAT2Remix::BuildNoOutputsError());
        if (outcome.mappedMatched == 0 && !outcome.unmappedNames.empty())
            return FailFatal("outputs",
                        InstaMAT2Remix::BuildUnmappedOutputsError(outcome.unmappedNames));
        if (outcome.mappedMatched == 0)
            return FailFatal("usage",
                        "--only=" + opt.only + " matched none of the graph's mapped outputs.");
        return Fail("Recognized PBR outputs were found but none could be exported "
                    "(sampler or file-write failures — see CHANNELFAIL lines).");
    }

    Say("FINALSIZE=%ux%u", width, height);
    if (outcome.collapsed) {
        Say("COLLAPSED=1");
        Say("WARNNOTE=some channels rendered at 1x1 (standalone renderer race — retry recommended)");
    } else {
        Say("COLLAPSED=0");
    }
    Say("DONE=ok");
    return 0;
}
