#include "ProjectTemplates.h"
#include "PluginInfo.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>

namespace InstaMAT2Remix {

namespace {

// "&Asset Texturing " -> "asset texturing"; also used space-stripped for the
// exact pass so "AssetTexturing" (a property value) still matches.
QString NormalizeTileString(const QString& s) {
    QString t = s;
    t.remove('&');
    return t.trimmed().toLower();
}

QString StripSeparators(const QString& s) {
    QString t = s;
    t.remove(' ');
    t.remove('_');
    t.remove('-');
    return t;
}

} // namespace

const QList<TemplateRecipeConfig>& AllTemplateConfigs() {
    static const QList<TemplateRecipeConfig> kConfigs = {
        {
            ProjectTemplate::AssetTexturing,
            "asset_texturing",
            "Asset Texturing",
            "Paint PBR textures directly on the pulled mesh (default).",
            /*exact*/    {QStringLiteral("asset texturing")},
            /*contains*/ {QStringLiteral("asset texturing"), QStringLiteral("assettexturing")},
            /*excludes*/ {},
            /*hasPickerStep*/ true,
            /*pickerPrefix*/ "WIDGET_Mesh",
            /*setUpAxis*/ true,
            /*firstTileFallback*/ true,
        },
        {
            ProjectTemplate::ElementGraph,
            "element_graph",
            "Element Graph",
            "Node-based graph linked to the pulled material — build automated "
            "texturing pipelines. Name outputs Base Color/Roughness/... so "
            "Push can find them.",
            /*exact*/    {QStringLiteral("element graph")},
            // "nPass Element Graph" and Atom Graph's description ("...instanced
            // in any Element Graph") also contain the phrase — the excludes
            // keep the contains pass from ever clicking those tiles.
            /*contains*/ {QStringLiteral("element graph")},
            /*excludes*/ {QStringLiteral("npass"), QStringLiteral("n-pass"), QStringLiteral("atom")},
            /*hasPickerStep*/ false,
            /*pickerPrefix*/ "",
            /*setUpAxis*/ false,
            /*firstTileFallback*/ false,
        },
        {
            ProjectTemplate::MaterializeImage,
            "materialize_image",
            "Materialize Image",
            "Turn the material's current Remix texture into a full PBR "
            "material (the pulled texture is selected automatically).",
            /*exact*/    {QStringLiteral("materialize image")},
            /*contains*/ {QStringLiteral("materialize")},
            /*excludes*/ {},
            /*hasPickerStep*/ true,
            /*pickerPrefix*/ "WIDGET_Image",
            /*setUpAxis*/ false,
            /*firstTileFallback*/ false,
        },
    };
    return kConfigs;
}

const TemplateRecipeConfig& GetTemplateConfig(ProjectTemplate t) {
    for (const TemplateRecipeConfig& cfg : AllTemplateConfigs()) {
        if (cfg.id == t) return cfg;
    }
    return AllTemplateConfigs().first(); // unreachable; AssetTexturing default
}

bool TemplateFromSettingsValue(const QString& value, ProjectTemplate& outTemplate) {
    const QString v = value.trimmed().toLower();
    for (const TemplateRecipeConfig& cfg : AllTemplateConfigs()) {
        if (v == QLatin1String(cfg.settingsValue)) {
            outTemplate = cfg.id;
            return true;
        }
    }
    return false;
}

int MatchProjectTypeTile(const QList<QStringList>& perButtonStrings,
                         const TemplateRecipeConfig& cfg) {
    // Pass 1: exact normalized equality against any of the button's strings.
    // A description sentence can never *equal* the template title, so this is
    // immune to cross-tile phrase collisions.
    {
        int hit = -1;
        for (int i = 0; i < perButtonStrings.size(); ++i) {
            bool matches = false;
            for (const QString& raw : perButtonStrings.at(i)) {
                const QString norm = NormalizeTileString(raw);
                for (const QString& want : cfg.tileExactMatches) {
                    if (norm == want || StripSeparators(norm) == StripSeparators(want)) {
                        matches = true;
                        break;
                    }
                }
                if (matches) break;
            }
            if (!matches) continue;
            if (hit >= 0) return -1; // ambiguous
            hit = i;
        }
        if (hit >= 0) return hit;
    }

    // Pass 2: contains-matching, skipping any button that mentions an
    // excluded token anywhere in its strings.
    {
        int hit = -1;
        for (int i = 0; i < perButtonStrings.size(); ++i) {
            bool excluded = false;
            bool matches = false;
            for (const QString& raw : perButtonStrings.at(i)) {
                const QString norm = NormalizeTileString(raw);
                for (const QString& bad : cfg.tileExcludes) {
                    if (norm.contains(bad)) { excluded = true; break; }
                }
                if (excluded) break;
                for (const QString& want : cfg.tileContainsMatches) {
                    if (norm.contains(want) || StripSeparators(norm).contains(StripSeparators(want))) {
                        matches = true;
                        break;
                    }
                }
            }
            if (excluded || !matches) continue;
            if (hit >= 0) return -1; // ambiguous
            hit = i;
        }
        return hit;
    }
}

bool AskPullTemplate(QWidget* parent, ProjectTemplate& outTemplate, bool& outRemember) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QString("%1 - Pull From Remix").arg(kPluginName));
    dlg.setModal(true);

    auto* layout = new QVBoxLayout(&dlg);
    auto* header = new QLabel("Which kind of InstaMAT project should be created "
                              "from the Remix selection?", &dlg);
    header->setWordWrap(true);
    layout->addWidget(header);

    QList<QRadioButton*> radios;
    for (const TemplateRecipeConfig& cfg : AllTemplateConfigs()) {
        // fromUtf8, NOT fromLatin1: chooserDescription carries a UTF-8 em-dash
        // that Latin-1 decoding rendered as "â " mojibake (user-reported).
        auto* radio = new QRadioButton(QString::fromUtf8(cfg.displayName), &dlg);
        auto* desc = new QLabel(QString::fromUtf8(cfg.chooserDescription), &dlg);
        desc->setWordWrap(true);
        desc->setIndent(24);
        desc->setStyleSheet("color: #888;");
        layout->addWidget(radio);
        layout->addWidget(desc);
        radios.append(radio);
    }
    radios.first()->setChecked(true); // Asset Texturing default

    auto* remember = new QCheckBox("Remember my choice and don't ask again "
                                   "(change later in Settings > Pull)", &dlg);
    layout->addSpacing(8);
    layout->addWidget(remember);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) return false;

    for (int i = 0; i < radios.size(); ++i) {
        if (radios.at(i)->isChecked()) {
            outTemplate = AllTemplateConfigs().at(i).id;
            break;
        }
    }
    outRemember = remember->isChecked();
    return true;
}

} // namespace InstaMAT2Remix
