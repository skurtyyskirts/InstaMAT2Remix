#pragma once
#include "InstaMATAPI.h"
#include <QString>
#include <QVector>
#include <cstdint>

namespace InstaMAT2Remix {

    // Triangle-mesh payload for IElementExecution::SetMeshForInputParameter.
    // Layout mirrors the official InstaMAT-for-Blender plugin: one attributed
    // vertex per face-corner (no sharing/dedup), loop-index triangles, and
    // per-polygon submesh/material arrays (all zero when unused).
    struct LoadedMesh {
        QVector<InstaMAT::GraphMeshVertex> vertices;
        QVector<InstaMAT::uint32> indices;          // 3 per triangle
        QVector<InstaMAT::uint32> submeshIndices;   // 1 per triangle
        QVector<InstaMAT::uint32> materialIndices;  // 1 per triangle
    };

    // Parses a Wavefront OBJ into a LoadedMesh. Supports v/vt/vn/f with all
    // four corner forms (v, v/vt, v//vn, v/vt/vn), negative (relative)
    // indices, and fan-triangulation of n-gons. Normals are computed per-face
    // when absent; tangents/binormals are derived per-face from UV deltas
    // (Lengyel) and Gram-Schmidt orthonormalized. No axis conversion is
    // applied: Blender's wm.obj_export already writes Y-up / -Z-forward,
    // which is what the InstaMAT engine expects.
    // Fails (false + outError) when the file is unreadable, contains no
    // triangles, or has no UVs — a texturing bake cannot run without them.
    bool LoadObjMesh(const QString& path, LoadedMesh& out, QString& outError);

    // Lightweight IGraphMesh over a LoadedMesh (contract: InstaMATAPI.h,
    // IGraphMesh). The adapter does not own the mesh; keep the LoadedMesh
    // alive for the adapter's whole lifetime (i.e. until Execute returns).
    class GraphMeshAdapter final : public InstaMAT::IGraphMesh {
    public:
        explicit GraphMeshAdapter(LoadedMesh& mesh) : m_mesh(mesh) {}

        InstaMAT::GraphMeshVertex* GetVertices() override { return m_mesh.vertices.data(); }
        const InstaMAT::GraphMeshVertex* GetVertices() const override { return m_mesh.vertices.constData(); }
        InstaMAT::uint32* GetIndices() override { return m_mesh.indices.data(); }
        const InstaMAT::uint32* GetIndices() const override { return m_mesh.indices.constData(); }
        InstaMAT::uint32* GetSubmeshIndices() override { return m_mesh.submeshIndices.data(); }
        const InstaMAT::uint32* GetSubmeshIndices() const override { return m_mesh.submeshIndices.constData(); }
        const char* GetSubmeshName(const InstaMAT::uint32 /*submeshIndex*/) const override { return "Default"; }
        InstaMAT::uint32* GetMaterialIndices() override { return m_mesh.materialIndices.data(); }
        const InstaMAT::uint32* GetMaterialIndices() const override { return m_mesh.materialIndices.constData(); }
        const char* GetMaterialName(const InstaMAT::uint32 /*materialIndex*/) const override { return "Default"; }
        InstaMAT::uint32 GetVertexCount() const override { return static_cast<InstaMAT::uint32>(m_mesh.vertices.size()); }
        InstaMAT::uint32 GetIndexCount() const override { return static_cast<InstaMAT::uint32>(m_mesh.indices.size()); }

    private:
        LoadedMesh& m_mesh;
    };

    // Reads the pixel dimensions out of a .dds header without decoding the
    // image. Returns false unless the file starts with the 'DDS ' magic and
    // a 124-byte DDS_HEADER (height at byte 12, width at byte 16, both LE).
    bool ReadDdsDimensions(const QString& path, uint32_t& outWidth, uint32_t& outHeight);

} // namespace InstaMAT2Remix
