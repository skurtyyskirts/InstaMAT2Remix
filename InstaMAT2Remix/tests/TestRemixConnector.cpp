#include <QtTest>
#include <QCoreApplication>
#include <QSettings>
#include <QDir>
#include <cmath>
#include <string>
#include <QTemporaryDir>
#include <QFile>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QPair>
#include <QRegularExpression>

#define private public
#include "RemixConnector.h"
#undef private

#include "MeshData.h"

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

        // Unknown suffix returns empty string.
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:custom_thing"), QString());

        // Case sensitivity: WBC's Python table is case-sensitive. Pin the behavior
        // so future changes are deliberate.
        QCOMPARE(IM::ResolveCanonicalChannel("inputs:OPACITY_TEXTURE"), QString());
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
        const QString expected = QDir::cleanPath(
            QDir::homePath() + "/Documents/InstaMAT/Library");
        QCOMPARE(QDir::cleanPath(d), expected);
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

    // ---- InstaMAT2Duplicate ------------------------------------------------

    void testGenerateMaterialHash_format() {
        using IM = InstaMAT2Remix::RemixConnector;
        const QString h = IM::GenerateMaterialHash();
        QCOMPARE(h.length(), 16);
        const QRegularExpression hexRe("^[0-9A-F]{16}$");
        QVERIFY2(hexRe.match(h).hasMatch(),
                 qPrintable("Hash not 16 uppercase hex chars: " + h));
    }

    void testGenerateMaterialHash_unique() {
        using IM = InstaMAT2Remix::RemixConnector;
        const QString h1 = IM::GenerateMaterialHash();
        const QString h2 = IM::GenerateMaterialHash();
        QVERIFY2(h1 != h2, "Two consecutive hashes were identical");
    }

    void testBuildDuplicateUsdaSidecar_containsPrimPathAndMdlInputs() {
        using IM = InstaMAT2Remix::RemixConnector;
        const QString primPath = "/RootNode/Looks/mat_AABBCCDD11223344";
        QList<QPair<QString, QString>> channels = {
            {"diffuse_texture",   "mat_AABBCCDD11223344_albedo.dds"},
            {"normalmap_texture", "mat_AABBCCDD11223344_normal.dds"},
        };
        const QString usda = IM::BuildDuplicateUsdaSidecar(primPath, channels);
        QVERIFY2(usda.contains("mat_AABBCCDD11223344"), "USDA missing material hash");
        QVERIFY2(usda.contains("AperturePBR_Translucency"), "USDA missing MDL source identifier");
        QVERIFY2(usda.contains("diffuse_texture"), "USDA missing diffuse_texture MDL input");
        QVERIFY2(usda.contains("normalmap_texture"), "USDA missing normalmap_texture MDL input");
    }

    void testBuildDuplicateUsdaSidecar_sRGBAnnotation() {
        using IM = InstaMAT2Remix::RemixConnector;
        const QString primPath = "/RootNode/Looks/mat_TEST0000TEST0000";
        QList<QPair<QString, QString>> channels = {
            {"diffuse_texture",             "albedo.dds"},
            {"normalmap_texture",           "normal.dds"},
            {"reflectionroughness_texture", "roughness.dds"},
        };
        const QString usda = IM::BuildDuplicateUsdaSidecar(primPath, channels);
        QVERIFY2(usda.indexOf("diffuse_texture") < usda.indexOf("colorSpace"),
                 "sRGB annotation not placed after diffuse_texture");
        QCOMPARE(usda.count("colorSpace = \"sRGB\""), 1);
    }

    void testBuildDuplicateUsdaSidecar_startsWithMagicAndMdlTokens() {
        using IM = InstaMAT2Remix::RemixConnector;
        const QString primPath = "/RootNode/Looks/mat_00000000FFFFFFFF";
        const QString usda = IM::BuildDuplicateUsdaSidecar(primPath, {});
        QVERIFY2(usda.startsWith("#usda 1.0"), "USDA must start with '#usda 1.0'");
        QVERIFY(usda.contains("outputs:mdl:surface.connect"));
        QVERIFY(usda.contains("outputs:mdl:displacement.connect"));
        QVERIFY(usda.contains("outputs:mdl:volume.connect"));
    }

    void testBuildDuplicateValidatePayload_shape() {
        using IM = InstaMAT2Remix::RemixConnector;
        const QJsonObject payload = IM::BuildDuplicateValidatePayload(
            "Dup_albedo_mat_ABC", "/tmp/InstaMAT2Remix_PreIngest/mat_ABC_albedo.png",
            "/remix/project/Textures/InstaMAT2Remix_Ingested", "DIFFUSE");

        QCOMPARE(payload.value("name").toString(), QString("Dup_albedo_mat_ABC"));
        QCOMPARE(payload.value("executor").toInt(), 1);

        const QJsonObject ctx = payload.value("context_plugin").toObject();
        QCOMPARE(ctx.value("name").toString(), QString("TextureImporter"));
        const QJsonObject ctxData = ctx.value("data").toObject();
        const QJsonArray inputFiles = ctxData.value("input_files").toArray();
        QCOMPARE(inputFiles.size(), 1);
        const QJsonArray firstInput = inputFiles.at(0).toArray();
        QCOMPARE(firstInput.at(0).toString(), QString("/tmp/InstaMAT2Remix_PreIngest/mat_ABC_albedo.png"));
        QCOMPARE(firstInput.at(1).toString(), QString("DIFFUSE"));

        const QJsonArray checks = payload.value("check_plugins").toArray();
        QCOMPARE(checks.size(), 1);
        QCOMPARE(checks.at(0).toObject().value("name").toString(), QString("ConvertToDDS"));
    }

    void testExtractDuplicateIngestedPath_prefersRtexDds() {
        using IM = InstaMAT2Remix::RemixConnector;

        auto makeSchema = [](const QStringList& outputs) {
            QJsonArray outs;
            for (const QString& o : outputs) outs.append(o);
            QJsonObject flow{{"channel", "ingestion_output"}, {"output_data", outs}};
            QJsonObject data{{"data_flows", QJsonArray{flow}}};
            QJsonObject ctx{{"name", "TextureImporter"}, {"data", data}};
            return QJsonObject{{"context_plugin", ctx}};
        };

        const QJsonObject schema = makeSchema({
            "/out/mat_ABC_albedo.dds",
            "/out/mat_ABC_albedo.rtex.dds",
        });
        QCOMPARE(IM::ExtractDuplicateIngestedPath(schema, "mat_ABC_albedo"),
                 QString("/out/mat_ABC_albedo.rtex.dds"));
    }

    void testExtractDuplicateIngestedPath_noMatchFallsBackToFirst() {
        using IM = InstaMAT2Remix::RemixConnector;

        QJsonObject flow{{"channel", "ingestion_output"},
                         {"output_data", QJsonArray{"/out/unrelated.dds"}}};
        QJsonObject data{{"data_flows", QJsonArray{flow}}};
        QJsonObject ctx{{"name", "TextureImporter"}, {"data", data}};
        QJsonObject schema{{"context_plugin", ctx}};

        QCOMPARE(IM::ExtractDuplicateIngestedPath(schema, "no_such_base"),
                 QString("/out/unrelated.dds"));
    }
};

QTEST_MAIN(TestRemixConnector)
#include "TestRemixConnector.moc"
