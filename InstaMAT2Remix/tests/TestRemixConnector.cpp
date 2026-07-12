#include <QtTest>
#include <QCoreApplication>
#include <QSettings>
#include <QDir>
#include <QStandardPaths>
#include <cmath>
#include <string>
#include <QTemporaryDir>
#include <QFile>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

#define private public
#include "RemixConnector.h"
#undef private

#include "MeshData.h"
#include "PbrChannelMap.h"
#include "ProjectTemplates.h"

#include <QColor>
#include <QImage>
#include <QSet>

class TestRemixConnector : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("InstaMAT2Remix_Test");
        QCoreApplication::setApplicationName("Config_Test");
    }

    void testSetRemixApiBaseUrl() {
        QSettings settings("InstaMAT2Remix", "Config");
        settings.clear();

        // Create a fake IInstaMAT pointer and cast to reference to satisfy RemixConnector constructor.
        // It's safe here because SetRemixApiBaseUrl and RemixConnector's constructor do not
        // execute virtual methods on m_instaMAT.
        InstaMAT::IInstaMAT* dummy = reinterpret_cast<InstaMAT::IInstaMAT*>(0x1234);
        InstaMAT2Remix::RemixConnector connector(*dummy, nullptr);

        std::string testUrl = "http://test-remix-url:1234";
        connector.SetRemixApiBaseUrl(testUrl);

        QCOMPARE(settings.value("RemixApiBaseUrl").toString(), QString::fromStdString(testUrl));
        QCOMPARE(QString::fromStdString(connector.m_remixApiBaseUrl), QString::fromStdString(testUrl));
    }

    void testResolveCanonicalChannel_wbcTable() {
        using IM = InstaMAT2Remix::RemixConnector;

        // Every alias in the WBC table maps to its canonical channel.
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:diffuse_texture"),         QString("albedo"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:albedo_texture"),          QString("albedo"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:basecolor_texture"),       QString("albedo"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:base_color_texture"),      QString("albedo"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:normalmap_texture"),       QString("normal"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:normal_texture"),          QString("normal"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:worldspacenormal_texture"),QString("normal"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:heightmap_texture"),       QString("height"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:height_texture"),          QString("height"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:displacement_texture"),    QString("height"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:roughness_texture"),       QString("roughness"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:reflectionroughness_texture"), QString("roughness"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:specularroughness_texture"),   QString("roughness"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:metallic_texture"),        QString("metallic"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:metalness_texture"),       QString("metallic"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:emissive_mask_texture"),   QString("emissive"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:emissive_texture"),        QString("emissive"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:emissive_color_texture"),  QString("emissive"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:opacity_texture"),         QString("opacity"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:opacitymask_texture"),     QString("opacity"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:opacity"),                 QString("opacity"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:transparency_texture"),    QString("opacity"));

        // No namespace prefix — the last-colon rule degenerates to the whole string.
        QCOMPARE(IM::ResolveCanonicalChannel("diffuse_texture"), QString("albedo"));

        // Remix SSS / translucency extension (deliberate deviation from WBC).
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:transmittance_texture"),
                 QString("transmittance"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:subsurface_transmittance_texture"),
                 QString("transmittance"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:subsurface_thickness_texture"),
                 QString("sss_thickness"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:subsurface_single_scattering_texture"),
                 QString("sss_scattering"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:subsurface_radius_texture"),
                 QString("sss_radius"));
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:anisotropy_texture"),
                 QString("anisotropy"));

        // Unknown suffix returns empty string.
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:custom_thing"), QString());

        // Case sensitivity: WBC's Python table is case-sensitive. Pin the behavior
        // so future changes are deliberate.
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:OPACITY_TEXTURE"), QString());
    }

    void testIngestValidationTypes_onlyValidRemixEnumNames() {
        using IM = InstaMAT2Remix::RemixConnector;
        // The complete set of TextureTypes enum member names the RTX Remix
        // mass-validator accepts (toolkit-remix data_models/enums.py).
        const QSet<QString> kValid = {
            "DIFFUSE", "ROUGHNESS", "ANISOTROPY", "METALLIC", "EMISSIVE",
            "NORMAL_OGL", "NORMAL_DX", "NORMAL_OTH", "HEIGHT",
            "TRANSMITTANCE", "MEASUREMENT_DISTANCE", "SINGLE_SCATTERING", "OTHER",
        };

        const QStringList channels = {
            "albedo", "roughness", "metallic", "emissive", "height",
            "transmittance", "sss_thickness", "sss_scattering", "sss_radius",
            "anisotropy", "normal",
        };
        for (const QString& c : channels) {
            const QString t = IM::ResolveIngestValidationType(c, "dx");
            QVERIFY2(kValid.contains(t),
                     QString("%1 -> %2 is not a valid Remix ingest type").arg(c, t).toUtf8());
        }

        // The exact routings that matter.
        QCOMPARE(IM::ResolveIngestValidationType("albedo", "dx"), QString("DIFFUSE"));
        QCOMPARE(IM::ResolveIngestValidationType("transmittance", "dx"), QString("TRANSMITTANCE"));
        QCOMPARE(IM::ResolveIngestValidationType("sss_thickness", "dx"), QString("MEASUREMENT_DISTANCE"));
        QCOMPARE(IM::ResolveIngestValidationType("sss_scattering", "dx"), QString("SINGLE_SCATTERING"));
        QCOMPARE(IM::ResolveIngestValidationType("sss_radius", "dx"), QString("OTHER"));
        QCOMPARE(IM::ResolveIngestValidationType("anisotropy", "dx"), QString("ANISOTROPY"));

        // Normal encoding resolution.
        QCOMPARE(IM::ResolveIngestValidationType("normal", "dx"), QString("NORMAL_DX"));
        QCOMPARE(IM::ResolveIngestValidationType("normal", "ogl"), QString("NORMAL_OGL"));
        QCOMPARE(IM::ResolveIngestValidationType("normal", "oth"), QString("NORMAL_OTH"));
        QCOMPARE(IM::ResolveIngestValidationType("normal", "garbage"), QString("NORMAL_DX"));
        QCOMPARE(IM::ResolveIngestValidationType("Normal", ""), QString("NORMAL_DX"));

        // The old invalid WBC-inherited names must be gone: unknown channels
        // resolve to OTHER (raw), never AO/OPACITY/IOR/SUBSURFACE.
        QCOMPARE(IM::ResolveIngestValidationType("ao", "dx"), QString("OTHER"));
        QCOMPARE(IM::ResolveIngestValidationType("opacity", "dx"), QString("OTHER"));
        QCOMPARE(IM::ResolveIngestValidationType("ior", "dx"), QString("OTHER"));
        QCOMPARE(IM::ResolveIngestValidationType("subsurface", "dx"), QString("OTHER"));
    }

    void testClassifyMaterialFromTextureAttrs() {
        using IM = InstaMAT2Remix::RemixConnector;
        using Kind = IM::RemixMaterialKind;

        // Opaque material.
        QVERIFY(IM::ClassifyMaterialFromTextureAttrs(
                    {"/RootNode/Looks/mat_A/Shader.inputs:diffuse_texture",
                     "/RootNode/Looks/mat_A/Shader.inputs:normalmap_texture"})
                == Kind::Opaque);

        // Translucent (glass): exact transmittance_texture suffix.
        QVERIFY(IM::ClassifyMaterialFromTextureAttrs(
                    {"/RootNode/Looks/mat_B/Shader.inputs:transmittance_texture",
                     "/RootNode/Looks/mat_B/Shader.inputs:normalmap_texture"})
                == Kind::Translucent);

        // The opaque SSS input must NOT be mistaken for glass.
        QVERIFY(IM::ClassifyMaterialFromTextureAttrs(
                    {"/RootNode/Looks/mat_C/Shader.inputs:subsurface_transmittance_texture",
                     "/RootNode/Looks/mat_C/Shader.inputs:diffuse_texture"})
                == Kind::Opaque);

        // Nothing recognizable.
        QVERIFY(IM::ClassifyMaterialFromTextureAttrs({"/x/Shader.inputs:custom"})
                == Kind::Unknown);
        QVERIFY(IM::ClassifyMaterialFromTextureAttrs({}) == Kind::Unknown);
    }

    void testBuildTexturePutPairs_routesByMaterialKind() {
        using IM = InstaMAT2Remix::RemixConnector;
        const QString prim = "/RootNode/Looks/mat_A3F09C4D8E12BB77";

        QHash<QString, QString> ingested;
        ingested.insert("albedo",         "C:/ing/a.rtex.dds");
        ingested.insert("normal",         "C:/ing/n.rtex.dds");
        ingested.insert("transmittance",  "C:/ing/t.rtex.dds");
        ingested.insert("sss_scattering", "C:/ing/s.rtex.dds");

        auto attrsOf = [](const QJsonArray& pairs) {
            QStringList out;
            for (const QJsonValue& v : pairs) out << v.toArray().at(0).toString();
            return out;
        };

        // Opaque: everything routes; transmittance goes to the SSS input.
        QStringList skipped;
        const QJsonArray opaque = IM::BuildTexturePutPairs(prim, ingested, false, &skipped);
        QCOMPARE(opaque.size(), 4);
        QVERIFY(skipped.isEmpty());
        const QStringList oa = attrsOf(opaque);
        QVERIFY(oa.contains(prim + "/Shader.inputs:diffuse_texture"));
        QVERIFY(oa.contains(prim + "/Shader.inputs:subsurface_transmittance_texture"));
        QVERIFY(oa.contains(prim + "/Shader.inputs:subsurface_single_scattering_texture"));

        // Translucent: only normal + transmittance survive; transmittance goes
        // to the glass input; albedo/sss_scattering are skipped.
        const QJsonArray glass = IM::BuildTexturePutPairs(prim, ingested, true, &skipped);
        QCOMPARE(glass.size(), 2);
        const QStringList ga = attrsOf(glass);
        QVERIFY(ga.contains(prim + "/Shader.inputs:transmittance_texture"));
        QVERIFY(ga.contains(prim + "/Shader.inputs:normalmap_texture"));
        QVERIFY(skipped.contains("albedo"));
        QVERIFY(skipped.contains("sss_scattering"));
    }

    void testPbrChannelMap_studioOutputAliases() {
        using InstaMAT2Remix::MapStudioOutputToCanonicalPbr;
        QCOMPARE(QString::fromStdString(MapStudioOutputToCanonicalPbr("Base Color")), QString("albedo"));
        QCOMPARE(QString::fromStdString(MapStudioOutputToCanonicalPbr("Metalness")), QString("metallic"));
        QCOMPARE(QString::fromStdString(MapStudioOutputToCanonicalPbr("Transmission")), QString("transmittance"));
        QCOMPARE(QString::fromStdString(MapStudioOutputToCanonicalPbr("Translucency")), QString("transmittance"));
        QCOMPARE(QString::fromStdString(MapStudioOutputToCanonicalPbr("Subsurface Scattering")), QString("sss_scattering"));
        QCOMPARE(QString::fromStdString(MapStudioOutputToCanonicalPbr("SSS")), QString("sss_scattering"));
        QCOMPARE(QString::fromStdString(MapStudioOutputToCanonicalPbr("Single Scattering")), QString("sss_scattering"));
        QCOMPARE(QString::fromStdString(MapStudioOutputToCanonicalPbr("Thickness")), QString("sss_thickness"));
        QCOMPARE(QString::fromStdString(MapStudioOutputToCanonicalPbr("Measurement Distance")), QString("sss_thickness"));
        QCOMPARE(QString::fromStdString(MapStudioOutputToCanonicalPbr("Subsurface Radius")), QString("sss_radius"));
        QCOMPARE(QString::fromStdString(MapStudioOutputToCanonicalPbr("Anisotropy")), QString("anisotropy"));
        QCOMPARE(QString::fromStdString(MapStudioOutputToCanonicalPbr("Ambient Occlusion")), QString("ao"));
        QCOMPARE(QString::fromStdString(MapStudioOutputToCanonicalPbr("Opacity")), QString("opacity"));
        QCOMPARE(QString::fromStdString(MapStudioOutputToCanonicalPbr("Something Else")), QString());

        // Generic-name aliases adopted from InstaMAT's own Blender add-on
        // ("Output"/"Default" -> Base Color) — fresh Element Graphs and the
        // SDK sample name their lone output "Output".
        QCOMPARE(QString::fromStdString(MapStudioOutputToCanonicalPbr("Output")), QString("albedo"));
        QCOMPARE(QString::fromStdString(MapStudioOutputToCanonicalPbr("Default")), QString("albedo"));
        QCOMPARE(QString::fromStdString(MapStudioOutputToCanonicalPbr("Metal")), QString("metallic"));
        QCOMPARE(QString::fromStdString(MapStudioOutputToCanonicalPbr("Transparency")), QString("opacity"));
        // "Output 2" must NOT alias (normalization keeps the digit).
        QCOMPARE(QString::fromStdString(MapStudioOutputToCanonicalPbr("Output 2")), QString());

        using InstaMAT2Remix::ChannelMayLegitimatelyRenderFlat;
        QVERIFY(ChannelMayLegitimatelyRenderFlat("height"));
        QVERIFY(ChannelMayLegitimatelyRenderFlat("sss_thickness"));
        QVERIFY(ChannelMayLegitimatelyRenderFlat("sss_radius"));
        QVERIFY(!ChannelMayLegitimatelyRenderFlat("albedo"));
        QVERIFY(!ChannelMayLegitimatelyRenderFlat("normal"));
    }

    void testWorkerErrorMessageBuilders() {
        using namespace InstaMAT2Remix;

        const QString noOutputs = QString::fromStdString(BuildNoOutputsError());
        QVERIFY(noOutputs.contains("exposes no output parameters"));
        QVERIFY(noOutputs.contains(QString::fromUtf8(PbrOutputNamesHint())));
        QVERIFY(!noOutputs.contains('\n'));

        const QString one = QString::fromStdString(BuildUnmappedOutputsError({"Output 3"}));
        QVERIFY(one.contains("graph's 1 output(s)"));
        QVERIFY(one.contains("'Output 3'"));
        QVERIFY(one.contains(QString::fromUtf8(PbrOutputNamesHint())));

        // More than 6 names: list caps at 6 and reports the overflow count.
        std::vector<std::string> eight;
        for (int i = 0; i < 8; ++i) eight.push_back("Out" + std::to_string(i));
        const QString many = QString::fromStdString(BuildUnmappedOutputsError(eight));
        QVERIFY(many.contains("graph's 8 output(s)"));
        QVERIFY(many.contains("'Out5'"));
        QVERIFY(!many.contains("'Out6'"));
        QVERIFY(many.contains("(+2 more)"));

        // Names with embedded newlines must not break the single-line protocol.
        const QString hostile = QString::fromStdString(
            BuildUnmappedOutputsError({"Bad\nName\r"}));
        QVERIFY(!hostile.contains('\n'));
        QVERIFY(!hostile.contains('\r'));
    }

    void testSingleOutputAlbedoFallbackGuard() {
        using InstaMAT2Remix::ShouldFallbackLoneOutputToAlbedo;
        // Fires: nothing mapped, exactly one unmapped image output.
        QVERIFY(ShouldFallbackLoneOutputToAlbedo(0, 1, ""));
        QVERIFY(ShouldFallbackLoneOutputToAlbedo(0, 1, "albedo"));
        // Never fires when something already mapped (deliberate naming),
        // when the count is wrong, or when --only asks for another channel.
        QVERIFY(!ShouldFallbackLoneOutputToAlbedo(1, 1, ""));
        QVERIFY(!ShouldFallbackLoneOutputToAlbedo(0, 2, ""));
        QVERIFY(!ShouldFallbackLoneOutputToAlbedo(0, 0, ""));
        QVERIFY(!ShouldFallbackLoneOutputToAlbedo(0, 1, "normal"));
    }

    void testParseWorkerStdout_fatalAndFallback() {
        using IM = InstaMAT2Remix::RemixConnector;

        // Success transcript, interleaved with engine noise the parser must
        // route into engineTail rather than the protocol fields.
        const QString success =
            "...using GPU='NVIDIA GeForce RTX 5090'\n"
            "IM2RX GRAPHTYPE=layer\n"
            "IM2RX OUTPUTCOUNT=2\n"
            "IM2RX CHANNEL=albedo:albedo.png\n"
            "IM2RX CHANNEL=normal:normal.png\n"
            "IM2RX FINALSIZE=4096x4096\n"
            "IM2RX COLLAPSED=0\n"
            "IM2RX DONE=ok\n";
        IM::WorkerRunParse p = IM::ParseWorkerStdout(success);
        QCOMPARE(p.channelFiles, QStringList({"albedo.png", "normal.png"}));
        QVERIFY(!p.collapsed);
        QCOMPARE(p.finalSize, QString("4096x4096"));
        QCOMPARE(p.graphType, QString("layer"));
        QVERIFY(p.error.isEmpty());
        QVERIFY(p.fatalReason.isEmpty());
        QCOMPARE(p.engineTail, QStringList({"...using GPU='NVIDIA GeForce RTX 5090'"}));
        QVERIFY(IM::ShouldRetryWorkerFailure(p.fatalReason));

        // Deterministic failure: FATAL= present -> no retry + dialog tip.
        const QString fatal =
            "IM2RX GRAPHTYPE=element\n"
            "IM2RX OUTPUTCOUNT=1\n"
            "IM2RX OUTPUTSKIP name='Custom Thing' (unmapped)\n"
            "IM2RX FATAL=outputs\n"
            "IM2RX ERROR=None of the graph's 1 output(s) is named after a PBR channel.\n"
            "IM2RX DONE=fail\n";
        p = IM::ParseWorkerStdout(fatal);
        QVERIFY(p.channelFiles.isEmpty());
        QCOMPARE(p.fatalReason, QString("outputs"));
        QVERIFY(p.error.startsWith("None of the graph's 1 output(s)"));
        QVERIFY(!IM::ShouldRetryWorkerFailure(p.fatalReason));
        QVERIFY(IM::WorkerErrorTip(p.fatalReason).contains("Base Color"));
        QVERIFY(IM::WorkerErrorTip("environment").isEmpty());
        QVERIFY(IM::WorkerErrorTip(QString()).isEmpty());

        // Lone-output albedo fallback: name extracted for the summary note.
        const QString fallback =
            "IM2RX OUTPUTCOUNT=1\n"
            "IM2RX OUTPUTFALLBACK name='My Texture' canonical=albedo\n"
            "IM2RX CHANNEL=albedo:albedo.png\n"
            "IM2RX COLLAPSED=0\n"
            "IM2RX DONE=ok\n";
        p = IM::ParseWorkerStdout(fallback);
        QCOMPARE(p.fallbackOutputName, QString("My Texture"));
        QCOMPARE(p.channelFiles, QStringList({"albedo.png"}));

        // Legacy worker (pre-FATAL protocol): ERROR without FATAL stays
        // retriable — the 0.0.1 behavior.
        const QString legacy =
            "IM2RX ERROR=Execution succeeded but no recognized PBR channel outputs were written.\n"
            "IM2RX DONE=fail\n";
        p = IM::ParseWorkerStdout(legacy);
        QVERIFY(p.fatalReason.isEmpty());
        QVERIFY(!p.error.isEmpty());
        QVERIFY(IM::ShouldRetryWorkerFailure(p.fatalReason));
    }

    void testMatchProjectTypeTile() {
        using namespace InstaMAT2Remix;
        // Simulated New Project dialog tiles (per-button scraped strings),
        // mirroring the real Studio layout incl. the ambiguity traps.
        const QList<QStringList> tiles = {
            /*0*/ {"Asset Texturing", "New workflows for scalable texturing and painting."},
            /*1*/ {"Material Layering", "Artist centric procedural material creation."},
            /*2*/ {"Materialize Image", "Turn a regular photograph into a PBR material."},
            /*3*/ {"Element Graph", "From beautiful materials to procedural geometry..."},
            /*4*/ {"nPass Element Graph", "Leverage the self-repeating nPass Element Graph..."},
            /*5*/ {"Atom Graph", "Create custom image processing nodes that can be "
                   "instanced in any Element Graph."},
            /*6*/ {"Function Graph", "Develop small functions..."},
        };

        QCOMPARE(MatchProjectTypeTile(tiles, GetTemplateConfig(ProjectTemplate::AssetTexturing)), 0);
        QCOMPARE(MatchProjectTypeTile(tiles, GetTemplateConfig(ProjectTemplate::ElementGraph)), 3);
        QCOMPARE(MatchProjectTypeTile(tiles, GetTemplateConfig(ProjectTemplate::MaterializeImage)), 2);

        // No exact title present: contains-pass must still avoid nPass/Atom.
        const QList<QStringList> noExact = {
            {"nPass Element Graph", "self-repeating"},
            {"Atom Graph", "instanced in any Element Graph"},
            {"Some Element Graph Tool", "unrelated"},
        };
        QCOMPARE(MatchProjectTypeTile(noExact, GetTemplateConfig(ProjectTemplate::ElementGraph)), 2);

        // Ambiguous exact matches -> -1.
        const QList<QStringList> dup = {{"Element Graph"}, {"Element Graph"}};
        QCOMPARE(MatchProjectTypeTile(dup, GetTemplateConfig(ProjectTemplate::ElementGraph)), -1);

        // Nothing matches -> -1.
        const QList<QStringList> none = {{"Material Layering"}, {"Function Graph"}};
        QCOMPARE(MatchProjectTypeTile(none, GetTemplateConfig(ProjectTemplate::ElementGraph)), -1);
    }

    void testProjectTemplateSettingsRoundTrip() {
        using namespace InstaMAT2Remix;
        for (const TemplateRecipeConfig& cfg : AllTemplateConfigs()) {
            ProjectTemplate t = ProjectTemplate::AssetTexturing;
            QVERIFY(TemplateFromSettingsValue(QLatin1String(cfg.settingsValue), t));
            QVERIFY(t == cfg.id);
        }
        ProjectTemplate t = ProjectTemplate::AssetTexturing;
        QVERIFY(!TemplateFromSettingsValue("", t));
        QVERIFY(!TemplateFromSettingsValue("ask", t));
        QVERIFY(!TemplateFromSettingsValue("bogus_template", t));
        // Case/whitespace tolerant.
        QVERIFY(TemplateFromSettingsValue("  Element_Graph ", t));
        QVERIFY(t == ProjectTemplate::ElementGraph);
    }

    void testMergeOpacityIntoAlbedoAlpha() {
        using IM = InstaMAT2Remix::RemixConnector;
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // 4x4 red albedo, 4x4 opacity gradient (per-pixel gray = x*60).
        QImage albedo(4, 4, QImage::Format_RGB32);
        albedo.fill(QColor(200, 30, 30));
        const QString albedoPath = tmp.path() + "/albedo.png";
        QVERIFY(albedo.save(albedoPath));

        QImage opacity(4, 4, QImage::Format_RGB32);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                opacity.setPixel(x, y, qRgb(x * 60, x * 60, x * 60));
        const QString opacityPath = tmp.path() + "/opacity.png";
        QVERIFY(opacity.save(opacityPath));

        QString merged, err;
        QVERIFY2(IM::MergeOpacityIntoAlbedoAlpha(albedoPath, opacityPath, merged, err),
                 err.toUtf8().constData());
        QVERIFY(merged.endsWith(".png"));
        QImage out(merged);
        QVERIFY(!out.isNull());
        QCOMPARE(out.size(), albedo.size());
        const QImage outArgb = out.convertToFormat(QImage::Format_ARGB32);
        for (int x = 0; x < 4; ++x) {
            QCOMPARE(qAlpha(outArgb.pixel(x, 0)), x * 60); // alpha = opacity gray
            QCOMPARE(qRed(outArgb.pixel(x, 0)), 200);      // RGB untouched
        }

        // Size mismatch: opacity is scaled to the albedo size.
        QImage bigOpacity(8, 8, QImage::Format_RGB32);
        bigOpacity.fill(QColor(128, 128, 128));
        const QString bigPath = tmp.path() + "/opacity_big.png";
        QVERIFY(bigOpacity.save(bigPath));
        QVERIFY(IM::MergeOpacityIntoAlbedoAlpha(albedoPath, bigPath, merged, err));
        const QImage out2 = QImage(merged).convertToFormat(QImage::Format_ARGB32);
        QCOMPARE(out2.size(), albedo.size());
        QCOMPARE(qAlpha(out2.pixel(1, 1)), 128);

        // Missing inputs fail with a message.
        QVERIFY(!IM::MergeOpacityIntoAlbedoAlpha(tmp.path() + "/nope.png", opacityPath, merged, err));
        QVERIFY(!err.isEmpty());
    }

    void testMeshCacheDirFor_documentsRooted() {
        using IM = InstaMAT2Remix::RemixConnector;
        const QString d = IM::MeshCacheDirFor("C:/tmp/mesh_641213F243CC7715_unwrapped.fbx");
        QVERIFY(d.contains("InstaMAT2Remix/MeshCache/"));
        QVERIFY(d.endsWith("mesh_641213F243CC7715_unwrapped"));
        // Deterministic.
        QCOMPARE(IM::MeshCacheDirFor("C:/tmp/mesh_641213F243CC7715_unwrapped.fbx"), d);
        // Extension-less and empty stems still yield a usable dir.
        QVERIFY(IM::MeshCacheDirFor("C:/tmp/rawmesh").endsWith("rawmesh"));
        QVERIFY(IM::MeshCacheDirFor("").endsWith("mesh"));
    }

    void testSanitizeFilenameStem() {
        using IM = InstaMAT2Remix::RemixConnector;
        QCOMPARE(IM::SanitizeFilenameStem("plain_root-1"), QString("plain_root-1"));
        QCOMPARE(IM::SanitizeFilenameStem("a\\b/c:d*e?f\"g<h>i|j"), QString("abcdefghij"));
        QCOMPARE(IM::SanitizeFilenameStem("  padded  "), QString("padded"));
        QCOMPARE(IM::SanitizeFilenameStem(""), QString(""));
    }

    void testDeriveDesiredRootFromPrim() {
        using IM = InstaMAT2Remix::RemixConnector;
        QCOMPARE(IM::DeriveDesiredRootFromPrim("/RootNode/Looks/mat_A3F09C4D8E12BB77"),
                 QString("A3F09C4D8E12BB77"));
        // Backslashed input is normalized first.
        QCOMPARE(IM::DeriveDesiredRootFromPrim("\\RootNode\\Looks\\mat_A3F09C4D8E12BB77"),
                 QString("A3F09C4D8E12BB77"));
        // Non-hash tails yield empty.
        QCOMPARE(IM::DeriveDesiredRootFromPrim("/RootNode/Looks/my_material"), QString());
        QCOMPARE(IM::DeriveDesiredRootFromPrim(""), QString());
    }

    void testForcePushRootConflicts_boundaryAware() {
        using IM = InstaMAT2Remix::RemixConnector;
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        auto touch = [&](const QString& name) {
            QFile f(tmp.path() + "/" + name);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("X");
            f.close();
        };
        touch("abc_albedo.rtex.dds");
        touch("abcd.dds");
        touch("abc-x.dds");
        touch("notdds_abc.txt");

        // Boundary-aware: "abc" conflicts via '_' and '-', not via "abcd".
        QVERIFY(IM::ForcePushRootConflicts("abc", tmp.path()));
        QVERIFY(!IM::ForcePushRootConflicts("ab", tmp.path()));   // "abc…" continues with 'c'
        QVERIFY(IM::ForcePushRootConflicts("abcd", tmp.path()));  // exact-stem "abcd.dds"
        QVERIFY(!IM::ForcePushRootConflicts("zzz", tmp.path()));
        // Case-insensitive.
        QVERIFY(IM::ForcePushRootConflicts("ABC", tmp.path()));
        // Non-.dds files never conflict.
        QVERIFY(!IM::ForcePushRootConflicts("notdds", tmp.path()));
        // Missing dir / empty root never conflict.
        QVERIFY(!IM::ForcePushRootConflicts("abc", tmp.path() + "/missing"));
        QVERIFY(!IM::ForcePushRootConflicts("", tmp.path()));
    }

    void testChooseNonOverwritingRoot_chain() {
        using IM = InstaMAT2Remix::RemixConnector;
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        auto touch = [&](const QString& name) {
            QFile f(tmp.path() + "/" + name);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("X");
            f.close();
        };

        // Empty dir: desired root comes back unchanged (sanitized).
        QCOMPARE(IM::ChooseNonOverwritingRoot("h", tmp.path()), QString("h"));
        QCOMPARE(IM::ChooseNonOverwritingRoot("we?ird", tmp.path()), QString("weird"));
        // Fully illegal root falls back to "ForcePush".
        QCOMPARE(IM::ChooseNonOverwritingRoot("???", tmp.path()), QString("ForcePush"));

        // Chain: h, h_1 taken -> h_2.
        touch("h_albedo.rtex.dds");
        touch("h_1_normal.dds");
        QCOMPARE(IM::ChooseNonOverwritingRoot("h", tmp.path()), QString("h_2"));
    }

    void testStageSourceForIngest_forcedDestName() {
        InstaMAT::IInstaMAT* dummy = reinterpret_cast<InstaMAT::IInstaMAT*>(0x1234);
        InstaMAT2Remix::RemixConnector connector(*dummy, nullptr);

        const QString stageDir = InstaMAT2Remix::RemixConnector::PreIngestStageDir();
        if (QDir(stageDir).exists()) QDir(stageDir).removeRecursively();

        QTemporaryDir src;
        QVERIFY(src.isValid());
        const QString srcPath = src.path() + "/albedo.png";
        {
            QFile f(srcPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray("PNG-PAYLOAD"));
            f.close();
        }

        QString err;
        const QString staged = connector.StageSourceForIngest(
            srcPath, "A3F09C4D8E12BB77_albedo.png", err);
        QVERIFY2(!staged.isEmpty(), ("stage failed: " + err).toUtf8().constData());
        QCOMPARE(QDir::cleanPath(staged),
                 QDir::cleanPath(stageDir + "/A3F09C4D8E12BB77_albedo.png"));
        QVERIFY(QFile::exists(staged));
        {
            QFile f(staged);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll(), QByteArray("PNG-PAYLOAD"));
        }

        // Empty dest name falls back to the source basename.
        QString err2;
        const QString staged2 = connector.StageSourceForIngest(srcPath, QString(), err2);
        QCOMPARE(QDir::cleanPath(staged2), QDir::cleanPath(stageDir + "/albedo.png"));
    }

    void testFindMostRecentLayerPackageIn_picksNewestImpExceptPluginImp() {
        InstaMAT::IInstaMAT* dummy = reinterpret_cast<InstaMAT::IInstaMAT*>(0x1234);
        InstaMAT2Remix::RemixConnector connector(*dummy, nullptr);

        QTemporaryDir lib_dir;
        QVERIFY(lib_dir.isValid());

        // --- Empty dir: returns false with a populated error. ---
        {
            QString path, err;
            QDateTime mtime;
            QVERIFY(!connector.FindMostRecentLayerPackageIn(lib_dir.path(), path, mtime, err));
            QVERIFY(path.isEmpty());
            QVERIFY(!mtime.isValid());
            QVERIFY(!err.isEmpty());
        }

        // --- Missing dir: returns false. ---
        {
            QString path, err;
            QDateTime mtime;
            QVERIFY(!connector.FindMostRecentLayerPackageIn(
                lib_dir.path() + "/does_not_exist", path, mtime, err));
            QVERIFY(!err.isEmpty());
        }

        // --- Three .IMP files with distinct mtimes; picker returns the newest.
        //     plus a non-IMP file with a newer mtime (must be ignored) and the
        //     plugin's own InstaMAT2Remix.IMP with the very newest mtime
        //     (must be skipped because it's not a layer-stack project). ---
        const QDateTime now = QDateTime::currentDateTime();
        const QString aaa = lib_dir.path() + "/aaa.IMP";
        const QString bbb = lib_dir.path() + "/bbb.IMP";
        const QString ccc = lib_dir.path() + "/ccc.IMP";
        const QString txt = lib_dir.path() + "/ignore_me.txt";
        const QString plug = lib_dir.path() + "/InstaMAT2Remix.IMP";
        for (const auto& p : {aaa, bbb, ccc, txt, plug}) {
            QFile f(p);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write("FAKE");
            f.close();
        }
        // QFile::setFileTime sets mtime; needs the file to be opened.
        auto setMtime = [](const QString& p, const QDateTime& when) -> bool {
            QFile f(p);
            if (!f.open(QIODevice::ReadWrite)) return false;
            const bool ok = f.setFileTime(when, QFileDevice::FileModificationTime);
            f.close();
            return ok;
        };
        QVERIFY(setMtime(aaa, now.addSecs(-300)));
        QVERIFY(setMtime(bbb, now.addSecs(-30)));   // newest non-plugin .IMP
        QVERIFY(setMtime(ccc, now.addSecs(-150)));
        QVERIFY(setMtime(txt, now.addSecs(-5)));    // newer overall but wrong ext
        QVERIFY(setMtime(plug, now.addSecs(-1)));   // newest .IMP overall but is the plugin

        QString path, err;
        QDateTime mtime;
        QVERIFY2(connector.FindMostRecentLayerPackageIn(lib_dir.path(), path, mtime, err),
                 ("find failed: " + err).toUtf8().constData());
        QCOMPARE(QDir::cleanPath(path), QDir::cleanPath(bbb));
        QVERIFY(mtime.isValid());
        const qint64 ageSec = mtime.secsTo(now);
        QVERIFY2(ageSec >= 25 && ageSec <= 60,
                 QString("mtime age out of band: %1s").arg(ageSec).toUtf8().constData());

        // --- Case-insensitive skip: a lowercase instamat2remix.imp must also be
        //     skipped even on case-sensitive filesystems. We can't reliably create
        //     a lowercase variant on NTFS (case-insensitive lookup would clobber
        //     the existing InstaMAT2Remix.IMP), so verify the only candidate left
        //     when bbb is removed is ccc (next-newest non-plugin), not plug. ---
        QVERIFY(QFile::remove(bbb));
        path.clear(); err.clear(); mtime = QDateTime();
        QVERIFY(connector.FindMostRecentLayerPackageIn(lib_dir.path(), path, mtime, err));
        QCOMPARE(QDir::cleanPath(path), QDir::cleanPath(ccc));
    }

    void testDefaultLayerProjectDir_isDocumentsInstamatLibrary() {
        const QString d = InstaMAT2Remix::RemixConnector::DefaultLayerProjectDir();
        // Documents resolves via the shell known folder (OneDrive-redirected
        // setups included), falling back to <home>/Documents.
        QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (docs.isEmpty()) docs = QDir::homePath() + "/Documents";
        const QString expected = QDir::cleanPath(docs + "/InstaMAT/Library");
        QCOMPARE(QDir::cleanPath(d), expected);
    }

    void testIsSafeToWipe() {
        using IM = InstaMAT2Remix::RemixConnector;

        // Empty/whitespace/relative paths are never safe: QDir("") is the CWD.
        QVERIFY(!IM::IsSafeToWipe(""));
        QVERIFY(!IM::IsSafeToWipe("   "));
        QVERIFY(!IM::IsSafeToWipe("relative/dir"));

        // Drive roots and well-known user folders are protected.
        QVERIFY(!IM::IsSafeToWipe("C:/"));
        QVERIFY(!IM::IsSafeToWipe("C:\\"));
        QVERIFY(!IM::IsSafeToWipe(QDir::homePath()));
        const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (!docs.isEmpty()) {
            QVERIFY(!IM::IsSafeToWipe(docs));
            QVERIFY(!IM::IsSafeToWipe(docs.toUpper())); // case-insensitive
        }

        // Dedicated folders are fine (existence is not required — pure path logic).
        QVERIFY(IM::IsSafeToWipe(QDir::tempPath() + "/InstaMAT2Remix_Export"));
        QVERIFY(IM::IsSafeToWipe("C:/Some/Dedicated/ExportFolder"));
        if (!docs.isEmpty()) {
            QVERIFY(IM::IsSafeToWipe(docs + "/InstaMAT2Remix/Exports"));
        }
    }

    void testStageSourceForIngest_copiesIntoTempStageDir() {
        InstaMAT::IInstaMAT* dummy = reinterpret_cast<InstaMAT::IInstaMAT*>(0x1234);
        InstaMAT2Remix::RemixConnector connector(*dummy, nullptr);

        // Stage dir is %TEMP%/InstaMAT2Remix_PreIngest. Wipe leftovers from a
        // previous test run so file-existence checks below are unambiguous.
        const QString stageDir = InstaMAT2Remix::RemixConnector::PreIngestStageDir();
        if (QDir(stageDir).exists()) QDir(stageDir).removeRecursively();

        QTemporaryDir src;
        QVERIFY(src.isValid());

        // --- Happy path: source exists, gets copied, returned path is in stageDir
        //     and contents match the source byte-for-byte. ---
        const QString srcPath = src.path() + "/albedo.png";
        {
            QFile f(srcPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray("PNG-PAYLOAD-A"));
            f.close();
        }

        QString err;
        const QString staged = connector.StageSourceForIngest(srcPath, err);
        QVERIFY2(!staged.isEmpty(), ("stage failed: " + err).toUtf8().constData());
        QVERIFY(err.isEmpty());
        QCOMPARE(QDir::cleanPath(staged),
                 QDir::cleanPath(stageDir + "/albedo.png"));
        QVERIFY(QFile::exists(staged));
        {
            QFile f(staged);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll(), QByteArray("PNG-PAYLOAD-A"));
        }

        // --- Overwrite: a second call with different source contents replaces
        //     the staged file (QFile::copy normally refuses to overwrite). ---
        {
            QFile f(srcPath);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(QByteArray("PNG-PAYLOAD-B"));
            f.close();
        }
        err.clear();
        const QString staged2 = connector.StageSourceForIngest(srcPath, err);
        QCOMPARE(staged2, staged);
        QVERIFY(err.isEmpty());
        {
            QFile f(staged2);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll(), QByteArray("PNG-PAYLOAD-B"));
        }

        // --- Missing source: returns empty + populates outErr. ---
        {
            QString err2;
            const QString out = connector.StageSourceForIngest(src.path() + "/does_not_exist.png", err2);
            QVERIFY(out.isEmpty());
            QVERIFY(!err2.isEmpty());
        }
    }

    // Writes `content` to <tmpDir>/<name> and returns the absolute path.
    static QString writeTempFile(const QTemporaryDir& tmp, const QString& name, const QByteArray& content) {
        const QString p = tmp.path() + "/" + name;
        QFile f(p);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return QString();
        f.write(content);
        f.close();
        return p;
    }

    void testLoadObjMesh_quadCube() {
        // Blender-style quad cube: 6 quads -> fan-triangulated to 12 triangles,
        // 36 corners = 36 per-corner vertices = 36 indices.
        static const char* kCubeObj =
            "# quad cube\n"
            "v -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\n"
            "v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\n"
            "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
            "vn 0 0 1\nvn 0 0 -1\nvn 0 1 0\nvn 0 -1 0\nvn 1 0 0\nvn -1 0 0\n"
            "f 1/1/1 2/2/1 3/3/1 4/4/1\n"
            "f 6/1/2 5/2/2 8/3/2 7/4/2\n"
            "f 4/1/3 3/2/3 7/3/3 8/4/3\n"
            "f 5/1/4 6/2/4 2/3/4 1/4/4\n"
            "f 2/1/5 6/2/5 7/3/5 3/4/5\n"
            "f 5/1/6 1/2/6 4/3/6 8/4/6\n";

        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString objPath = writeTempFile(tmp, "cube.obj", kCubeObj);
        QVERIFY(!objPath.isEmpty());

        InstaMAT2Remix::LoadedMesh mesh;
        QString err;
        QVERIFY2(InstaMAT2Remix::LoadObjMesh(objPath, mesh, err), err.toUtf8().constData());

        QCOMPARE(mesh.indices.size(), 36);
        QCOMPARE(mesh.vertices.size(), 36);
        QCOMPARE(mesh.submeshIndices.size(), 12);
        QCOMPARE(mesh.materialIndices.size(), 12);
        for (const auto s : mesh.submeshIndices) QCOMPARE(s, InstaMAT::uint32(0));
        for (const auto m : mesh.materialIndices) QCOMPARE(m, InstaMAT::uint32(0));

        // Indices are the identity sequence (one vertex per corner).
        for (int i = 0; i < mesh.indices.size(); ++i)
            QCOMPARE(mesh.indices[i], InstaMAT::uint32(i));

        // First triangle is corners 1/2/3 of the +Z quad: positions and UVs
        // must match the pools exactly, normal is the authored +Z.
        QCOMPARE(mesh.vertices[0].Position.X, -1.0f);
        QCOMPARE(mesh.vertices[0].Position.Z, 1.0f);
        QCOMPARE(mesh.vertices[0].TexCoord.X, 0.0f);
        QCOMPARE(mesh.vertices[0].TexCoord.Y, 0.0f);
        QCOMPARE(mesh.vertices[1].TexCoord.X, 1.0f);
        QCOMPARE(mesh.vertices[2].TexCoord.X, 1.0f);
        QCOMPARE(mesh.vertices[2].TexCoord.Y, 1.0f);
        QCOMPARE(mesh.vertices[0].Normal.Z, 1.0f);

        // GraphMeshAdapter exposes the same buffers.
        InstaMAT2Remix::GraphMeshAdapter adapter(mesh);
        QCOMPARE(adapter.GetVertexCount(), InstaMAT::uint32(36));
        QCOMPARE(adapter.GetIndexCount(), InstaMAT::uint32(36));
        QCOMPARE(adapter.GetPolygonCount(), InstaMAT::uint32(12));
        QVERIFY(adapter.GetVertices() == mesh.vertices.data());
        QCOMPARE(QString::fromUtf8(adapter.GetSubmeshName(0)), QString("Default"));
        QCOMPARE(QString::fromUtf8(adapter.GetMaterialName(0)), QString("Default"));
    }

    void testLoadObjMesh_tangentBasisOrthonormal() {
        static const char* kCubeObj =
            "v -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\n"
            "v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\n"
            "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
            "vn 0 0 1\nvn 0 0 -1\nvn 0 1 0\nvn 0 -1 0\nvn 1 0 0\nvn -1 0 0\n"
            "f 1/1/1 2/2/1 3/3/1 4/4/1\n"
            "f 6/1/2 5/2/2 8/3/2 7/4/2\n"
            "f 4/1/3 3/2/3 7/3/3 8/4/3\n"
            "f 5/1/4 6/2/4 2/3/4 1/4/4\n"
            "f 2/1/5 6/2/5 7/3/5 3/4/5\n"
            "f 5/1/6 1/2/6 4/3/6 8/4/6\n";

        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString objPath = writeTempFile(tmp, "cube.obj", kCubeObj);

        InstaMAT2Remix::LoadedMesh mesh;
        QString err;
        QVERIFY2(InstaMAT2Remix::LoadObjMesh(objPath, mesh, err), err.toUtf8().constData());

        auto dot = [](const InstaMAT::GraphVec3F& a, const InstaMAT::GraphVec3F& b) {
            return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
        };
        for (const auto& v : mesh.vertices) {
            // Unit-length basis vectors.
            QVERIFY(std::fabs(std::sqrt(dot(v.Normal, v.Normal)) - 1.0f) < 1e-3f);
            QVERIFY(std::fabs(std::sqrt(dot(v.Tangent, v.Tangent)) - 1.0f) < 1e-3f);
            QVERIFY(std::fabs(std::sqrt(dot(v.Binormal, v.Binormal)) - 1.0f) < 1e-3f);
            // Tangent orthogonal to normal (Gram-Schmidt guarantee).
            QVERIFY(std::fabs(dot(v.Normal, v.Tangent)) < 1e-3f);
            // Binormal orthogonal to both.
            QVERIFY(std::fabs(dot(v.Normal, v.Binormal)) < 1e-3f);
            QVERIFY(std::fabs(dot(v.Tangent, v.Binormal)) < 1e-3f);
        }
    }

    void testLoadObjMesh_relativeIndices() {
        // Negative OBJ indices are relative to the pool size at face time.
        static const char* kTriObj =
            "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
            "vt 0 0\nvt 1 0\nvt 0 1\n"
            "f -3/-3 -2/-2 -1/-1\n";

        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString objPath = writeTempFile(tmp, "tri.obj", kTriObj);

        InstaMAT2Remix::LoadedMesh mesh;
        QString err;
        QVERIFY2(InstaMAT2Remix::LoadObjMesh(objPath, mesh, err), err.toUtf8().constData());
        QCOMPARE(mesh.vertices.size(), 3);
        QCOMPARE(mesh.indices.size(), 3);
        QCOMPARE(mesh.vertices[1].Position.X, 1.0f);
        QCOMPARE(mesh.vertices[2].Position.Y, 1.0f);
        QCOMPARE(mesh.vertices[1].TexCoord.X, 1.0f);
        QCOMPARE(mesh.vertices[2].TexCoord.Y, 1.0f);
        // No vn in file: per-face normal is +Z for this CCW triangle.
        QVERIFY(mesh.vertices[0].Normal.Z > 0.99f);
    }

    void testLoadObjMesh_missingUVsFails() {
        static const char* kNoUvObj =
            "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
            "f 1 2 3\n";

        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString objPath = writeTempFile(tmp, "nouv.obj", kNoUvObj);

        InstaMAT2Remix::LoadedMesh mesh;
        QString err;
        QVERIFY(!InstaMAT2Remix::LoadObjMesh(objPath, mesh, err));
        QVERIFY2(err.contains("UV", Qt::CaseInsensitive) || err.contains("vt"),
                 ("unexpected error text: " + err).toUtf8().constData());

        // Unreadable path also fails with a message.
        QString err2;
        QVERIFY(!InstaMAT2Remix::LoadObjMesh(tmp.path() + "/does_not_exist.obj", mesh, err2));
        QVERIFY(!err2.isEmpty());
    }

    void testReadDdsDimensions() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // Hand-built minimal DDS: 'DDS ' magic, dwSize=124, height=512 @12,
        // width=1024 @16, zero-padded to 128 bytes.
        QByteArray dds(128, '\0');
        dds[0] = 'D'; dds[1] = 'D'; dds[2] = 'S'; dds[3] = ' ';
        auto writeLE32 = [&dds](int offset, quint32 value) {
            dds[offset]     = char(value & 0xFF);
            dds[offset + 1] = char((value >> 8) & 0xFF);
            dds[offset + 2] = char((value >> 16) & 0xFF);
            dds[offset + 3] = char((value >> 24) & 0xFF);
        };
        writeLE32(4, 124);
        writeLE32(12, 512);   // height
        writeLE32(16, 1024);  // width

        const QString ddsPath = writeTempFile(tmp, "test.dds", dds);
        uint32_t w = 0, h = 0;
        QVERIFY(InstaMAT2Remix::ReadDdsDimensions(ddsPath, w, h));
        QCOMPARE(w, uint32_t(1024));
        QCOMPARE(h, uint32_t(512));

        // PNG magic is rejected.
        QByteArray png = QByteArray::fromHex("89504e470d0a1a0a") + QByteArray(120, '\0');
        const QString pngPath = writeTempFile(tmp, "test.png", png);
        QVERIFY(!InstaMAT2Remix::ReadDdsDimensions(pngPath, w, h));

        // Right magic, wrong header size is rejected.
        QByteArray bad = dds;
        bad[4] = 7;
        const QString badPath = writeTempFile(tmp, "bad.dds", bad);
        QVERIFY(!InstaMAT2Remix::ReadDdsDimensions(badPath, w, h));

        // Truncated file is rejected.
        const QString shortPath = writeTempFile(tmp, "short.dds", QByteArray("DDS "));
        QVERIFY(!InstaMAT2Remix::ReadDdsDimensions(shortPath, w, h));

        // Missing file is rejected.
        QVERIFY(!InstaMAT2Remix::ReadDdsDimensions(tmp.path() + "/missing.dds", w, h));
    }
};

QTEST_MAIN(TestRemixConnector)
#include "TestRemixConnector.moc"
