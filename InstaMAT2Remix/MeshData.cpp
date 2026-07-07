#include "MeshData.h"

#include <QFile>
#include <QFileInfo>
#include <cmath>

namespace InstaMAT2Remix {

namespace {

    using InstaMAT::GraphVec2F;
    using InstaMAT::GraphVec3F;

    inline GraphVec3F Sub(const GraphVec3F& a, const GraphVec3F& b) {
        return GraphVec3F(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
    }
    inline GraphVec3F Scale(const GraphVec3F& a, float s) {
        return GraphVec3F(a.X * s, a.Y * s, a.Z * s);
    }
    inline float Dot(const GraphVec3F& a, const GraphVec3F& b) {
        return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
    }
    inline GraphVec3F Cross(const GraphVec3F& a, const GraphVec3F& b) {
        return GraphVec3F(a.Y * b.Z - a.Z * b.Y,
                          a.Z * b.X - a.X * b.Z,
                          a.X * b.Y - a.Y * b.X);
    }
    inline float Length(const GraphVec3F& a) {
        return std::sqrt(Dot(a, a));
    }
    inline GraphVec3F Normalized(const GraphVec3F& a, const GraphVec3F& fallback) {
        const float len = Length(a);
        if (len < 1e-12f) return fallback;
        return Scale(a, 1.0f / len);
    }

    // Any unit vector perpendicular to n — used when UVs are degenerate for
    // a face and no tangent can be derived from them.
    inline GraphVec3F ArbitraryPerpendicular(const GraphVec3F& n) {
        const GraphVec3F axis = (std::fabs(n.X) < 0.9f) ? GraphVec3F(1.0f, 0.0f, 0.0f)
                                                        : GraphVec3F(0.0f, 1.0f, 0.0f);
        return Normalized(Cross(n, axis), GraphVec3F(1.0f, 0.0f, 0.0f));
    }

    // One corner of an OBJ face: indices into the position/uv/normal pools,
    // already resolved to 0-based (or -1 when absent).
    struct FaceCorner {
        int v = -1;
        int vt = -1;
        int vn = -1;
    };

    // Resolves an OBJ 1-based (or negative relative) index against a pool
    // size. Returns -1 when out of range or zero.
    inline int ResolveObjIndex(long long idx, int poolSize) {
        if (idx > 0 && idx <= poolSize) return static_cast<int>(idx - 1);
        if (idx < 0 && -idx <= poolSize) return static_cast<int>(poolSize + idx);
        return -1;
    }

    bool ParseCornerToken(const QByteArray& token, int vCount, int vtCount, int vnCount, FaceCorner& out) {
        const QList<QByteArray> parts = token.split('/');
        if (parts.isEmpty() || parts[0].isEmpty()) return false;

        bool ok = false;
        const long long vIdx = parts[0].toLongLong(&ok);
        if (!ok) return false;
        out.v = ResolveObjIndex(vIdx, vCount);
        if (out.v < 0) return false;

        if (parts.size() >= 2 && !parts[1].isEmpty()) {
            const long long vtIdx = parts[1].toLongLong(&ok);
            if (ok) out.vt = ResolveObjIndex(vtIdx, vtCount);
        }
        if (parts.size() >= 3 && !parts[2].isEmpty()) {
            const long long vnIdx = parts[2].toLongLong(&ok);
            if (ok) out.vn = ResolveObjIndex(vnIdx, vnCount);
        }
        return true;
    }

} // namespace

bool LoadObjMesh(const QString& path, LoadedMesh& out, QString& outError) {
    out = LoadedMesh();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        outError = QString("Cannot open OBJ file: %1").arg(path);
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();

    QVector<GraphVec3F> positions;
    QVector<GraphVec2F> uvs;
    QVector<GraphVec3F> normals;

    // First pass: collect pools and triangles (as corner triples). Corner
    // indices are resolved on the fly because negative OBJ indices are
    // relative to the pool size at the time the face statement appears.
    QVector<FaceCorner> triangleCorners; // 3 entries per triangle

    int lineStart = 0;
    const int dataSize = data.size();
    while (lineStart < dataSize) {
        int lineEnd = data.indexOf('\n', lineStart);
        if (lineEnd < 0) lineEnd = dataSize;
        QByteArray line = data.mid(lineStart, lineEnd - lineStart).trimmed();
        lineStart = lineEnd + 1;

        if (line.isEmpty() || line.startsWith('#')) continue;

        const QList<QByteArray> tokens = line.simplified().split(' ');
        if (tokens.isEmpty()) continue;
        const QByteArray& tag = tokens[0];

        if (tag == "v" && tokens.size() >= 4) {
            positions.append(GraphVec3F(tokens[1].toFloat(), tokens[2].toFloat(), tokens[3].toFloat()));
        } else if (tag == "vt" && tokens.size() >= 3) {
            uvs.append(GraphVec2F(tokens[1].toFloat(), tokens[2].toFloat()));
        } else if (tag == "vn" && tokens.size() >= 4) {
            normals.append(GraphVec3F(tokens[1].toFloat(), tokens[2].toFloat(), tokens[3].toFloat()));
        } else if (tag == "f" && tokens.size() >= 4) {
            QVector<FaceCorner> corners;
            corners.reserve(tokens.size() - 1);
            bool cornerFailed = false;
            for (int i = 1; i < tokens.size(); ++i) {
                FaceCorner corner;
                if (!ParseCornerToken(tokens[i], positions.size(), uvs.size(), normals.size(), corner)) {
                    cornerFailed = true;
                    break;
                }
                corners.append(corner);
            }
            if (cornerFailed || corners.size() < 3) continue; // skip malformed faces

            // Fan triangulation.
            for (int k = 1; k + 1 < corners.size(); ++k) {
                triangleCorners.append(corners[0]);
                triangleCorners.append(corners[k]);
                triangleCorners.append(corners[k + 1]);
            }
        }
    }

    if (triangleCorners.isEmpty()) {
        outError = QString("OBJ contains no triangle faces: %1").arg(path);
        return false;
    }
    if (uvs.isEmpty()) {
        outError = QString("OBJ has no texture coordinates (vt) — a texturing bake "
                           "needs UVs. Re-export the mesh with UVs: %1").arg(path);
        return false;
    }

    // Second pass: emit one attributed vertex per face-corner.
    const int triangleCount = triangleCorners.size() / 3;
    out.vertices.reserve(triangleCorners.size());
    out.indices.reserve(triangleCorners.size());
    out.submeshIndices.fill(0u, triangleCount);
    out.materialIndices.fill(0u, triangleCount);

    for (int tri = 0; tri < triangleCount; ++tri) {
        const FaceCorner* c = &triangleCorners[tri * 3];
        const GraphVec3F p0 = positions[c[0].v];
        const GraphVec3F p1 = positions[c[1].v];
        const GraphVec3F p2 = positions[c[2].v];

        const GraphVec2F uv0 = (c[0].vt >= 0) ? uvs[c[0].vt] : GraphVec2F(0.0f, 0.0f);
        const GraphVec2F uv1 = (c[1].vt >= 0) ? uvs[c[1].vt] : GraphVec2F(0.0f, 0.0f);
        const GraphVec2F uv2 = (c[2].vt >= 0) ? uvs[c[2].vt] : GraphVec2F(0.0f, 0.0f);

        const GraphVec3F e1 = Sub(p1, p0);
        const GraphVec3F e2 = Sub(p2, p0);
        const GraphVec3F faceNormal = Normalized(Cross(e1, e2), GraphVec3F(0.0f, 1.0f, 0.0f));

        // Per-face tangent basis from UV deltas (Lengyel). With per-corner
        // vertices there is no sharing, so the face basis is the corner basis.
        const float du1 = uv1.X - uv0.X, dv1 = uv1.Y - uv0.Y;
        const float du2 = uv2.X - uv0.X, dv2 = uv2.Y - uv0.Y;
        const float det = du1 * dv2 - du2 * dv1;

        GraphVec3F faceTangent(0.0f, 0.0f, 0.0f);
        GraphVec3F faceBitangent(0.0f, 0.0f, 0.0f);
        bool uvDegenerate = std::fabs(det) < 1e-12f;
        if (!uvDegenerate) {
            const float r = 1.0f / det;
            faceTangent = GraphVec3F((e1.X * dv2 - e2.X * dv1) * r,
                                     (e1.Y * dv2 - e2.Y * dv1) * r,
                                     (e1.Z * dv2 - e2.Z * dv1) * r);
            faceBitangent = GraphVec3F((e2.X * du1 - e1.X * du2) * r,
                                       (e2.Y * du1 - e1.Y * du2) * r,
                                       (e2.Z * du1 - e1.Z * du2) * r);
            if (Length(faceTangent) < 1e-12f) uvDegenerate = true;
        }

        for (int k = 0; k < 3; ++k) {
            InstaMAT::GraphMeshVertex vertex;
            vertex.Position = positions[c[k].v];
            vertex.Normal = (c[k].vn >= 0) ? Normalized(normals[c[k].vn], faceNormal) : faceNormal;
            vertex.TexCoord = (c[k].vt >= 0) ? uvs[c[k].vt] : GraphVec2F(0.0f, 0.0f);
            vertex.Color = InstaMAT::ColorRGBAUI8(255, 255, 255, 255);

            const GraphVec3F n = vertex.Normal;
            if (uvDegenerate) {
                vertex.Tangent = ArbitraryPerpendicular(n);
                vertex.Binormal = Normalized(Cross(n, vertex.Tangent), GraphVec3F(0.0f, 0.0f, 1.0f));
            } else {
                // Gram-Schmidt against this corner's normal, then rebuild the
                // binormal preserving the UV-derived handedness.
                const GraphVec3F t = Normalized(Sub(faceTangent, Scale(n, Dot(n, faceTangent))),
                                                ArbitraryPerpendicular(n));
                const float handedness = (Dot(Cross(n, t), faceBitangent) < 0.0f) ? -1.0f : 1.0f;
                vertex.Tangent = t;
                vertex.Binormal = Scale(Normalized(Cross(n, t), GraphVec3F(0.0f, 0.0f, 1.0f)), handedness);
            }

            out.indices.append(static_cast<InstaMAT::uint32>(out.vertices.size()));
            out.vertices.append(vertex);
        }
    }

    return true;
}

bool ReadDdsDimensions(const QString& path, uint32_t& outWidth, uint32_t& outHeight) {
    outWidth = 0;
    outHeight = 0;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray header = file.read(128);
    file.close();

    // 4-byte magic 'DDS ' + DDS_HEADER (dwSize==124, dwHeight @12, dwWidth @16).
    if (header.size() < 24) return false;
    if (header[0] != 'D' || header[1] != 'D' || header[2] != 'S' || header[3] != ' ') return false;

    const auto readLE32 = [&header](int offset) -> uint32_t {
        return static_cast<uint32_t>(static_cast<unsigned char>(header[offset]))
             | (static_cast<uint32_t>(static_cast<unsigned char>(header[offset + 1])) << 8)
             | (static_cast<uint32_t>(static_cast<unsigned char>(header[offset + 2])) << 16)
             | (static_cast<uint32_t>(static_cast<unsigned char>(header[offset + 3])) << 24);
    };

    if (readLE32(4) != 124u) return false;
    const uint32_t height = readLE32(12);
    const uint32_t width = readLE32(16);
    if (width == 0 || height == 0 || width > 65536u || height > 65536u) return false;

    outWidth = width;
    outHeight = height;
    return true;
}

} // namespace InstaMAT2Remix
