#pragma once
// PbrChannelMap.h — Studio output-name -> canonical Remix PBR channel.
//
// Qt-FREE on purpose: this is the single shared copy used by
//   - ExportWorker.cpp (InstaMAT2RemixExport.exe, which must not link Qt), and
//   - the plugin / TestRemixConnector (Qt targets can include it freely).
//
// Canonical channel names double as the exported file stems (albedo.png, ...)
// and as the keys of kDefaultPbrSpecs / kPbrToIngestValidation in
// RemixConnector.cpp — keep all three in lock-step.
//
// Canonical set (mirrors what RTX Remix's AperturePBR materials consume):
//   albedo, normal, roughness, metallic, emissive, height,
//   transmittance   (opaque SSS transmittance OR translucent glass color —
//                    the push routes it to the right shader input),
//   sss_thickness   (subsurface_thickness_texture / MEASUREMENT_DISTANCE),
//   sss_scattering  (subsurface_single_scattering_texture / SINGLE_SCATTERING),
//   sss_radius      (subsurface_radius_texture, ingested as OTHER),
//   anisotropy      (anisotropy_texture — runtime currently reads the scalar),
//   opacity         (merged into albedo's alpha before push),
//   ao              (exported to disk only — Remix has no AO texture input).

#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

namespace InstaMAT2Remix {

// Lowercases and strips spaces/underscores/dashes so "Base Color",
// "base_color" and "BaseColor" all normalize to "basecolor".
inline std::string NormalizeStudioOutputName(const std::string& outputName) {
    std::string s;
    s.reserve(outputName.size());
    for (char c : outputName) {
        if (c == ' ' || c == '_' || c == '-') continue;
        s.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(c))));
    }
    return s;
}

// Returns the canonical Remix PBR channel for an InstaMAT graph output name,
// or an empty string when the output is not a recognized PBR channel.
inline std::string MapStudioOutputToCanonicalPbr(const std::string& outputName) {
    static const std::unordered_map<std::string, std::string> kStudioToCanonical = {
        {"basecolor", "albedo"},   {"basecolour", "albedo"},   {"albedo", "albedo"},
        {"diffuse", "albedo"},     {"color", "albedo"},        {"colour", "albedo"},
        // "Output"/"Default" -> Base Color mirrors InstaMAT's own Blender
        // add-on (kInstaMATBSDFShaderNodeOutputConnection) — the SDK sample
        // and fresh Element Graphs name their lone output "Output".
        {"output", "albedo"},      {"default", "albedo"},

        {"normal", "normal"},      {"normalmap", "normal"},

        {"roughness", "roughness"},

        {"metallic", "metallic"},  {"metalness", "metallic"},  {"metal", "metallic"},

        {"emissive", "emissive"},  {"emission", "emissive"},   {"emissivecolor", "emissive"},
        {"emissivecolour", "emissive"}, {"emissivemask", "emissive"},

        {"height", "height"},      {"displacement", "height"},

        {"opacity", "opacity"},    {"alpha", "opacity"},       {"transparency", "opacity"},

        {"ao", "ao"},              {"ambientocclusion", "ao"},

        // --- Remix SSS / translucency extension (beyond WBC's 7 channels) ---
        {"transmittance", "transmittance"}, {"transmission", "transmittance"},
        {"translucency", "transmittance"},  {"translucent", "transmittance"},
        {"transmissioncolor", "transmittance"}, {"transmissioncolour", "transmittance"},

        {"subsurface", "sss_scattering"},   {"sss", "sss_scattering"},
        {"subsurfacescattering", "sss_scattering"}, {"singlescattering", "sss_scattering"},
        {"scattering", "sss_scattering"},   {"subsurfacecolor", "sss_scattering"},
        {"subsurfacecolour", "sss_scattering"},

        {"thickness", "sss_thickness"},     {"subsurfacethickness", "sss_thickness"},
        {"measurementdistance", "sss_thickness"}, {"sssthickness", "sss_thickness"},

        {"subsurfaceradius", "sss_radius"}, {"scatteringradius", "sss_radius"},
        {"sssradius", "sss_radius"},

        {"anisotropy", "anisotropy"},       {"aniso", "anisotropy"},
    };
    const auto it = kStudioToCanonical.find(NormalizeStudioOutputName(outputName));
    return it == kStudioToCanonical.end() ? std::string() : it->second;
}

// Channels that legitimately render 1x1 when their content is uniform
// (InstaMAT collapses constant outputs to a single pixel — observed for
// height). These are exempt from the 1x1 render-race collapse checks in both
// the worker and the plugin's push guard.
inline bool ChannelMayLegitimatelyRenderFlat(const std::string& canonical) {
    return canonical == "height" || canonical == "sss_thickness" ||
           canonical == "sss_radius";
}

// The output names users should give their graph outputs. Shown in worker
// errors and the plugin's push-failure tip — keep the two in sync by always
// going through this helper.
inline const char* PbrOutputNamesHint() {
    return "Base Color, Roughness, Metallic, Normal, Height, Emissive, "
           "Opacity, Ambient Occlusion";
}

// Worker ERROR= payloads must stay single-line (the IM2RX stdout protocol is
// line-oriented) — collapse any stray newlines in user-authored output names.
inline std::string PbrSanitizeForProtocolLine(const std::string& text) {
    std::string s;
    s.reserve(text.size());
    for (char c : text)
        s.push_back((c == '\r' || c == '\n') ? ' ' : c);
    return s;
}

// ERROR text for a graph that exposes no output parameters at all.
inline std::string BuildNoOutputsError() {
    return std::string("The graph exposes no output parameters, so there is "
                       "nothing to export. Add Output nodes named after PBR "
                       "channels (") +
           PbrOutputNamesHint() +
           ") to the graph, save, and push again.";
}

// ERROR text for a graph whose outputs exist but none map to a PBR channel.
// Quotes at most 6 of the names it saw so the dialog stays readable.
inline std::string BuildUnmappedOutputsError(const std::vector<std::string>& seenNames) {
    const size_t kMaxListed = 6;
    std::string names;
    for (size_t i = 0; i < seenNames.size() && i < kMaxListed; ++i) {
        if (i) names += ", ";
        names += "'" + PbrSanitizeForProtocolLine(seenNames[i]) + "'";
    }
    if (seenNames.size() > kMaxListed)
        names += " (+" + std::to_string(seenNames.size() - kMaxListed) + " more)";
    return "None of the graph's " + std::to_string(seenNames.size()) +
           " output(s) is named after a PBR channel — found: " + names +
           ". Rename each output to the PBR channel it holds (" +
           PbrOutputNamesHint() + "), save, and push again.";
}

// Lone-output fallback: a fresh Element Graph's single generically-named
// image output is exported as albedo so "generate a texture, push it" works
// (InstaMAT's Blender add-on treats such outputs as Base Color too). Never
// fires when any output mapped — partial naming means the user is naming
// deliberately — and respects the worker's --only debug filter.
inline bool ShouldFallbackLoneOutputToAlbedo(int mappedMatchedCount,
                                             int unmappedImageOutputCount,
                                             const std::string& onlyFilter) {
    if (mappedMatchedCount != 0) return false;
    if (unmappedImageOutputCount != 1) return false;
    return onlyFilter.empty() || onlyFilter == "albedo";
}

} // namespace InstaMAT2Remix
