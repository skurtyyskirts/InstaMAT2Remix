#pragma once
// ProjectTemplates.h — the InstaMAT project templates Pull From Remix can
// auto-create, plus the pure tile-matching logic the New Project recipe uses
// to pick the right template tile (unit-tested; no SDK dependency).

#include <QList>
#include <QString>
#include <QStringList>

class QWidget;

namespace InstaMAT2Remix {

enum class ProjectTemplate {
    AssetTexturing,   // paint on the pulled mesh (default; original behavior)
    ElementGraph,     // empty node graph for automated pipelines around the pull
    MaterializeImage, // photo->PBR from the pulled Remix texture
};

// Per-template configuration for the New Project recipe.
struct TemplateRecipeConfig {
    ProjectTemplate id;
    const char* settingsValue;       // QSettings value ("asset_texturing", ...)
    const char* displayName;         // "Asset Texturing", ...
    const char* chooserDescription;  // one-liner in the Pull chooser dialog
    QStringList tileExactMatches;    // pass 1: normalized EXACT equality
    QStringList tileContainsMatches; // pass 2: contains fallback
    QStringList tileExcludes;        // pass 2: reject buttons containing these
    bool hasPickerStep;              // false -> tile click then straight to Create
    const char* pickerPreferredPrefix; // "WIDGET_Mesh" / "WIDGET_Image"
    bool setUpAxis;                  // run the Up-Axis=Z step (mesh imports only)
    bool allowFirstTileFallback;     // fall back to typeButtons.first() on no match
};

// The templates offered on Pull, in chooser display order.
const QList<TemplateRecipeConfig>& AllTemplateConfigs();

const TemplateRecipeConfig& GetTemplateConfig(ProjectTemplate t);

// Settings-string round trip. Unknown/empty settings values yield false and
// leave outTemplate untouched (callers treat that as "ask").
bool TemplateFromSettingsValue(const QString& value, ProjectTemplate& outTemplate);

// Picks the tile index for cfg from each button's scraped strings
// (labels/accessible names/properties, one QStringList per tile button).
// Pass 1 requires an exact normalized match — immune to the "nPass Element
// Graph" title and to Atom Graph's description mentioning "Element Graph".
// Pass 2 falls back to contains-matching with per-button excludes.
// Returns -1 when nothing matches or the match is ambiguous (2+ hits).
int MatchProjectTypeTile(const QList<QStringList>& perButtonStrings,
                         const TemplateRecipeConfig& cfg);

// Modal chooser shown by Pull From Remix when PullProjectTemplate == "ask".
// Returns false when the user cancels. outRemember reflects the
// "remember my choice" checkbox.
bool AskPullTemplate(QWidget* parent, ProjectTemplate& outTemplate, bool& outRemember);

} // namespace InstaMAT2Remix
