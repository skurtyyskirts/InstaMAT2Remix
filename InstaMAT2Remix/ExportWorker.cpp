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
//   IM2RX CHANNEL=<canonical>:<filename>
//   IM2RX ERROR=<message>              IM2RX DONE=<ok|fail>
// Exit code 0 iff DONE=ok.

#include "InstaMATAPI.h"

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

// Maps an InstaMAT Layering / Asset Texturing project output name (e.g.
// "Base Color", "Metalness") to the canonical Remix PBR channel name.
// Case-insensitive, ignores spaces/underscores/dashes. This is the only
// copy — the plugin no longer walks layer-graph outputs in-process.
std::string MapStudioOutputToCanonicalPbr(const std::string& outputName) {
    std::string s;
    s.reserve(outputName.size());
    for (char c : outputName) {
        if (c == ' ' || c == '_' || c == '-') continue;
        s.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(c))));
    }
    static const std::unordered_map<std::string, std::string> kStudioToCanonical = {
        {"basecolor", "albedo"},   {"basecolour", "albedo"},   {"albedo", "albedo"},
        {"diffuse", "albedo"},     {"color", "albedo"},        {"colour", "albedo"},
        {"normal", "normal"},      {"normalmap", "normal"},
        {"roughness", "roughness"},
        {"metallic", "metallic"},  {"metalness", "metallic"},
        {"emissive", "emissive"},  {"emission", "emissive"},   {"emissivecolor", "emissive"},
        {"emissivecolour", "emissive"}, {"emissivemask", "emissive"},
        {"height", "height"},      {"displacement", "height"},
        {"opacity", "opacity"},    {"alpha", "opacity"},
        {"ao", "ao"},              {"ambientocclusion", "ao"},
    };
    const auto it = kStudioToCanonical.find(s);
    return it == kStudioToCanonical.end() ? std::string() : it->second;
}

int BitsForCanonical(const std::string& canonical) {
    return canonical == "height" ? 16 : 8; // mirrors kDefaultPbrSpecs
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

bool ProgressDelegate(const InstaMAT::IGraph&, const float progress) {
    static int lastDecile = -1;
    const int decile = static_cast<int>(progress * 10.0f);
    if (decile > lastDecile) {
        lastDecile = decile;
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

bool ParseArgs(int argc, wchar_t** argv, Options& opt, std::string& err) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = Narrow(argv[i]);
        auto next = [&](std::string& into) -> bool {
            if (i + 1 >= argc) { err = "missing value for " + a; return false; }
            into = Narrow(argv[++i]);
            return true;
        };
        std::string v;
        if (a == "--probe") opt.probe = true;
        else if (a == "--no-library") opt.skipLibrary = true;
        else if (a == "--imp") { if (!next(opt.impPath)) return false; }
        else if (a == "--out") { if (!next(opt.outDir)) return false; }
        else if (a == "--mesh") { if (!next(opt.meshPath)) return false; }
        else if (a == "--studio") { if (!next(opt.studioDir)) return false; }
        else if (a == "--format") { if (!next(opt.format)) return false; }
        else if (a == "--backend") { if (!next(opt.backend)) return false; }
        else if (a == "--entitlements") { if (!next(opt.entitlements)) return false; }
        else if (a == "--authpath") { if (!next(opt.authPath)) return false; }
        else if (a == "--rung") { if (!next(opt.forceRung)) return false; }
        else if (a == "--no-plugins") opt.noPlugins = true;
        else if (a == "--diag") opt.diag = true;
        else if (a == "--rawout") opt.rawOut = true;
        else if (a == "--sleepms") { if (!next(v)) return false; opt.sleepMs = static_cast<unsigned>(std::stoul(v)); }
        else if (a == "--noretry") opt.noRetry = true;
        else if (a == "--only") { if (!next(opt.only)) return false; }
        else if (a == "--retries") { if (!next(v)) return false; opt.retries = static_cast<unsigned>(std::stoul(v)); }
        else if (a == "--width") { if (!next(v)) return false; opt.width = static_cast<unsigned>(std::stoul(v)); }
        else if (a == "--height") { if (!next(v)) return false; opt.height = static_cast<unsigned>(std::stoul(v)); }
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
                                     unsigned height) {
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

    if (!bindOk) {
        DeallocExecutionGuarded(&api, exec);
        Say("RUNG rung=%s bind=%s execute=skipped", RungName(mode), bindNote.c_str());
        return nullptr;
    }

    unsigned long sehCode = 0;
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
    std::vector<RungMode> ladder;
    if (opt.forceRung == "inherit")    ladder = {RungMode::Inherit};
    else if (opt.forceRung == "url")   ladder = {RungMode::Url};
    else if (opt.forceRung == "bytes") ladder = {RungMode::Bytes};
    else if (!opt.meshPath.empty())    ladder = {RungMode::Bytes, RungMode::Url, RungMode::Inherit};
    else                               ladder = {RungMode::Inherit, RungMode::Url, RungMode::Bytes};

    InstaMAT::IElementExecution* exec = nullptr;
    for (const RungMode mode : ladder) {
        exec = TryRung(api, layerGraph, mode, opt, width, height);
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

    const InstaMAT::uint32 outputCount = instance->GetParameterCount(InstaMAT::IGraph::ParameterTypeOutput);
    for (InstaMAT::uint32 i = 0; i < outputCount; ++i) {
        InstaMAT::IGraphVariable* var = instance->GetParameterAtIndex(i, InstaMAT::IGraph::ParameterTypeOutput);
        if (!var) continue;
        const InstaMAT::IGraphObject* varObj = var->AsObject();
        if (!varObj) continue;
        const char* rawName = varObj->GetName(true);
        const std::string outputName = rawName ? rawName : "";
        const std::string canonical = MapStudioOutputToCanonicalPbr(outputName);
        if (canonical.empty()) continue;
        if (!opt.only.empty() && canonical != opt.only) continue;

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

    bool sizeReported = false;
    for (const Pending& p : pending) {
        const unsigned w = p.sampler->GetWidth();
        const unsigned h = p.sampler->GetHeight();
        Say("CHANNELSIZE name='%s' canonical=%s comp=%ux%u", p.outputName.c_str(),
            p.canonical.c_str(), w, h);
        if (!sizeReported) { Say("SIZE=%ux%u", w, h); sizeReported = true; }

        // A color/normal/roughness/metalness channel that came out 1x1 while
        // another channel rendered full is the standalone SDK's high-res
        // collapse (height is excluded — it is legitimately flat/1x1 when the
        // project has no height data).
        if (out.maxDim > 1 && w <= 1 && p.canonical != "height") {
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
        return Fail(argErr + " — usage: InstaMAT2RemixExport.exe [--probe] --imp <file> --out <dir> "
                             "[--width N] [--height N] [--format png|tga|jpg] [--mesh <file>] "
                             "[--studio <dir>] [--backend gpu|cpu] [--no-library]");
    }

    // 1. Host the SDK from the Studio installation.
    const std::wstring studioW = Widen(opt.studioDir);
    SetDllDirectoryW(studioW.c_str());
    const std::wstring dllPath = studioW + L"\\InstaMAT.dll";
    HMODULE mod = LoadLibraryW(dllPath.c_str());
    if (!mod) {
        return Fail("LoadLibrary failed for " + opt.studioDir + "\\InstaMAT.dll (error " +
                    std::to_string(GetLastError()) + ")");
    }

    const auto getBuildDate = reinterpret_cast<pfnGetInstaMATBuildDate>(
        GetProcAddress(mod, "GetInstaMATBuildDate"));
    const auto getInstaMAT = reinterpret_cast<pfnGetInstaMAT>(
        GetProcAddress(mod, "GetInstaMAT"));
    if (!getBuildDate || !getInstaMAT) {
        return Fail("GetInstaMAT/GetInstaMATBuildDate exports not found in InstaMAT.dll");
    }

    InstaMAT::int32 dllVersion = 0;
    const char* buildDate = getBuildDate(&dllVersion);
    Say("BUILDDATE=%s", buildDate ? buildDate : "?");
    Say("SDKVERSION=%d.%d", dllVersion >> 16, dllVersion & 0xFFFF);
    // Same major-match rule as PluginMain.cpp: minor bumps are additive, so
    // request the DLL's own version to pass its equality gate.
    if ((dllVersion >> 16) != (INSTAMAT_API_VERSION >> 16)) {
        return Fail("SDK major version mismatch (headers " +
                    std::to_string(INSTAMAT_API_VERSION >> 16) + ", dll " +
                    std::to_string(dllVersion >> 16) + ")");
    }

    InstaMAT::IInstaMAT* api = nullptr;
    if (getInstaMAT(dllVersion, &api) != 1u || !api) {
        return Fail("GetInstaMAT failed");
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
        const char* info = api->GetAuthorizationInformation();
        return Fail(std::string("SDK Initialize failed. Authorization info: ") + (info ? info : "?"));
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
        return Fail("GetGraphObjectsInPackage returned nothing for: " + opt.impPath);
    }

    InstaMAT::IGraph* layerGraph = nullptr;
    std::string layerGraphName;
    for (InstaMAT::uint32 i = 0; objs[i] != nullptr; ++i) {
        InstaMAT::IGraph* g = objs[i]->AsGraph();
        if (!g) continue;
        const InstaMAT::uint32 gtype = g->GetGraphTypeUI32();
        if (gtype != InstaMAT::IGraph::GraphTypeElementLayer &&
            gtype != InstaMAT::IGraph::GraphTypeElementMaterialLayer) continue;
        layerGraph = g;
        const char* name = objs[i]->GetName(true);
        layerGraphName = name ? name : "";
        break;
    }
    if (!layerGraph) {
        api->DeallocPackage(pkg);
        return Fail("No ElementLayer / ElementMaterialLayer graph in: " + opt.impPath);
    }
    Say("GRAPH=%s", layerGraphName.c_str());

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

    if (!outcome.executed)
        return Fail("Could not execute the layer project (all rungs failed — see RUNG lines).");
    if (outcome.written == 0)
        return Fail("Execution succeeded but no recognized PBR channel outputs were written.");

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
