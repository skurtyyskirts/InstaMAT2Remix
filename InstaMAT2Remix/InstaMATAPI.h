/**
 * InstaMATAPI.h (InstaMAT)
 *
 * Copyright © 2025 InstaMaterial GmbH - All Rights Reserved.
 *
 * Unauthorized copying of this file, via any medium is strictly prohibited.
 * This file and all it's contents are proprietary and confidential.
 *
 * @file InstaMATAPI.h
 * @copyright 2025 InstaMaterial GmbH. All rights reserved.
 */

/*!@mainpage notitle
 * @section welcome_sec Welcome to InstaMAT SDK
 * We are thrilled that you want to integrate our SDK into your product. If you have any questions or need code-level support, 
 * If you have any questions, please don't hesitate to get in touch with us through the <a href="http://community.theabstract.co/c/InstaMAT">Abstract Community</a> 
 * or reach out to our technical support.
 * Integrating our SDK is a straightforward and fun task. There are no external dependencies on heavyweight runtimes like .NET
 * and great care has been taken to create a modern and clean API with little in your way to creating complex optimizations.
 * 
 * @section gettingstarted_sec Getting Started with InstaMAT SDK
 * The best way to get started with the InstaMAT SDK is by exploring the sample code that ships with the SDK
 * and to look at our game-engine integrations that ship with source code.
 * 
 * > For more information on the InstaMAT SDK and the included sample code, please refer to the <a href="https://docs.InstaMAT.io/Products/InstaMAT_C++_SDK">InstaMAT Online Documentation</a>.
 *
 * @section plugin_sec Developing InstaMAT Plugins
 * InstaMAT allows developers to extend the functionality through native C++ plugins.
 * Plugins developed with the InstaMAT Plugin SDK are compatible with all applications that leverage
 * the InstaMAT SDK. This applies to our in-house developed applications such as the standalone InstaMAT app
 * but it extends to our 3rd party integrations for DCC tools and realtime 3D engines.
 *
 * More information can be found on the \ref plugins page.
 */

#ifndef InstaMATAPI_h
#define InstaMATAPI_h

#include <stdlib.h>
#include <stdio.h>
#include <float.h>

/**
 * The project that builds the dylib/dll needs to define the following preprocessor defines
 *		INSTAMAT_LIB INSTAMAT_LIB_DYNAMIC INSTAMAT_LIB_VECTOR
 * The user of the dylib/dll needs to define the following preprocessor defines
 *		INSTAMAT_LIB_DYNAMIC
 */

#if defined(__cplusplus)
#	define INSTAMAT_EXTERNC extern "C"
#else
#	define INSTAMAT_EXTERNC
#endif

#if defined(_WIN32)
#	define INSTAMAT_DLL_EXPORT __declspec(dllexport)
#	define INSTAMAT_DLL_IMPORT __declspec(dllimport)
#elif defined(__APPLE__) || defined(__linux__)
#	define INSTAMAT_DLL_EXPORT __attribute__((visibility("default")))
#	define INSTAMAT_DLL_IMPORT
#endif

#if defined(INSTAMAT_LIB_DYNAMIC)
#	if defined(INSTAMAT_LIB)
#		define INSTAMAT_CAPI	INSTAMAT_EXTERNC INSTAMAT_DLL_EXPORT
#		define INSTAMAT_CPPAPI INSTAMAT_DLL_EXPORT
#	else
#		define INSTAMAT_CAPI	INSTAMAT_EXTERNC INSTAMAT_DLL_IMPORT
#		define INSTAMAT_CPPAPI INSTAMAT_DLL_IMPORT
#	endif
#elif defined(INSTAMAT_LIB_STATIC)
#	if defined(INSTAMAT_LIB)
#		define INSTAMAT_CAPI	INSTAMAT_EXTERNC INSTAMAT_DLL_EXPORT
#		define INSTAMAT_CPPAPI INSTAMAT_DLL_EXPORT
#	else
#		define INSTAMAT_CAPI INSTAMAT_EXTERNC
#		define INSTAMAT_CPPAPI
#	endif
#else
#	define INSTAMAT_CAPI INSTAMAT_EXTERNC
#	define INSTAMAT_CPPAPI
#endif
#define INSTAMAT_EXPORTEDCLASS	INSTAMAT_CPPAPI
#define INSTAMAT_EXPORTEDSTRUCT INSTAMAT_CPPAPI

namespace InstaLOD
{
	class IInstaLOD;
	class IInstaLODMesh;
	class IInstaLODRenderMeshBase;
} // namespace InstaLOD

namespace InstaMAT
{
#if !defined(INSTAMAT_LIB_VECTOR)
	typedef char int8;
	typedef unsigned char uint8;
	typedef short int16;
	typedef unsigned short uint16;
	typedef int int32;
	typedef unsigned int uint32;
	typedef long long int64;
	typedef unsigned long long uint64;

	struct GraphVec2F
	{
		inline GraphVec2F() {}
		inline GraphVec2F(float x, float y) :
		X(x), Y(y) {}
		float X, Y;
	};
	struct GraphVec3F
	{
		inline GraphVec3F() {}
		inline GraphVec3F(float x, float y, float z) :
		X(x), Y(y), Z(z) {}
		float X, Y, Z;
	};
	struct GraphVec3D
	{
		inline GraphVec3D() {}
		inline GraphVec3D(double x, double y, double z) :
		X(x), Y(y), Z(z) {}
		double X, Y, Z;
	};
	struct GraphQuaternionD
	{
		inline GraphQuaternionD() {}
		inline GraphQuaternionD(double x, double y, double z, double w) :
		X(x), Y(y), Z(z), W(w) {}
		double X, Y, Z, W;
	};
	struct GraphQuaternionF
	{
		inline GraphQuaternionF() {}
		inline GraphQuaternionF(float x, float y, float z, float w) :
		X(x), Y(y), Z(z), W(w) {}
		float X, Y, Z, W;
	};
	struct ColorRGBF32
	{
		inline ColorRGBF32() {}
		inline ColorRGBF32(float r, float g, float b) :
		R(r), G(g), B(b) {}
		float R, G, B;
	};
	struct ColorRGBAF32
	{
		inline ColorRGBAF32() {}
		inline ColorRGBAF32(float r, float g, float b, float a) :
		R(r), G(g), B(b), A(a) {}
		float R, G, B, A;
	};
	struct ColorRGBAUI8
	{
		inline ColorRGBAUI8() {}
		inline ColorRGBAUI8(uint8 r, uint8 g, uint8 b, uint8 a) :
		R(r), G(g), B(b), A(a) {}
		uint8 R, G, B, A;
	};
#endif

	/**
	 * The ArithmeticGraphValue union represents an arithmetic value.
	 */
	union ArithmeticGraphValue
	{
		bool BooleanValue;
		float Float32Value;
		int32 Int32Value;
		uint32 UInt32Value;
		float Vector2FValue[2];
		float Vector3FValue[3];
		float Vector4FValue[4];
		int32 Vector2I32Value[2];
		int32 Vector3I32Value[3];
		int32 Vector4I32Value[4];
		uint32 Vector2UI32Value[2];
		uint32 Vector3UI32Value[3];
		uint32 Vector4UI32Value[4];
		float Matrix2FValue[4];	 /**< 2x2 matrix values stored as column-major. */
		float Matrix3FValue[9];	 /**< 3x3 matrix values stored as column-major. */
		float Matrix4FValue[16]; /**< 4x4 matrix values stored as column-major. */
	};

	/**
	 * The GraphPosition structure represents a 2D position a GraphObject canvas.
	 */
	struct GraphPosition
	{
		inline GraphPosition() :
		X(0), Y(0) {}
		inline GraphPosition(const float x, const float y) :
		X(x), Y(y) {}
		inline GraphPosition(const double x, const double y) :
		X((float)x), Y((float)y) {}
		inline GraphPosition(const int x, const int y) :
		X((float)x), Y((float)y) {}
		inline bool operator==(const GraphPosition& rhs) const
		{
			return X == rhs.X && Y == rhs.Y;
		}
		float X, Y;
	};

	/**
	 * The MetaData namespace contains keys for publicly accessible meta-data fields
	 * that are common to all GraphObject classes.
	 */
	namespace MetaData
	{
		static const char* const KeyApplication = "Application";	 /**< Optional information about the application used to create the GraphObject.*/
		static const char* const KeyAuthor = "Author";				 /**< Optional information about the author of the GraphObject.*/
		static const char* const KeyCategory = "Category";			 /**< The category of the GraphObject.*/
		static const char* const KeyDocumentation = "Documentation"; /**< Additional information that describes the GraphObject.*/
		static const char* const KeyURL = "URL";					 /**< Optional information about the website of the GraphObject author.*/
		static const char* const KeyUIControlType = "UIControlType"; /**< The IGraphVariable::UIControlType enum value to represent the control in the UI.*/
		static const char* const KeyUISliderRange = "UISliderRange"; /**< The slider range if the control type is `UIControlTypeSlider` */
		static const char* const KeyUILocked = "UILocked";			 /**< Determines if a single change of a vector slider or vector spinbox changes all elements. */
		static const char* const KeyUIUnit = "UIUnit";				 /**< Specifies the unit in which a GraphVariable value is displayed in the UI. */
	};																 // namespace MetaData

	class IElementExecution;
	class IGraphObject;
	class IGraphVariable;
	class IGraph;
	class IImageSampler;
	class IInstaMAT;
	class IInstaMATElementEntityPlugin;

	/**
	 * The GraphPointCloudPoint struct represents an attributed point as used by the IGraphPointCloud interface.
	 */
	struct GraphPointCloudPoint
	{
		GraphVec3F Position; /**< Point position. */
		GraphVec3F Normal;	 /**< Point normal. */
		GraphVec3F Rotation; /**< Point orientation using Euler angles XYZ. */
		GraphVec3F Size;	 /**< Point size. */
		ColorRGBAUI8 Color;	 /**< Point color. */
		char Tag[4];		 /**< Custom 4-byte tag for the point. */
	};

	/**
	 * The IGraphPointCloud class represents an abstract interface for graph point clouds.
	 */
	class INSTAMAT_EXPORTEDCLASS IGraphPointCloud
	{
	protected:
		virtual ~IGraphPointCloud() {}

	public:
		/**
		 * Gets the points.
		 *
		 * @return The points.
		 */
		virtual GraphPointCloudPoint* GetPoints() = 0;

		/**
		 * Gets the points.
		 *
		 * @return The points.
		 */
		virtual const GraphPointCloudPoint* GetPoints() const = 0;

		/**
		 * Gets the number of point in the point cloud.
		 *
		 * @return The number of point.
		 */
		virtual uint32 GetPointCount() const = 0;
	};

	/**
	 * The GraphMaterialProperties struct controls
	 * material properties that can be used in conjunction with the `IGraphScene`.
	 */
	struct GraphMaterialProperties
	{
		GraphMaterialProperties() :
		BaseColor(0.5f, 0.5f, 0.5f),
		Roughness(0.5f),
		Metalness(0.0f),
		Opacity(1.0f),
		EmissiveColor(0.0f, 0.0f, 0.0f),
		EmissiveIntensity(0.0f),
		SubsurfaceWeight(0.0f),
		SubsurfaceScatteringRadius(1.0f, 1.0f, 1.0f),
		SubsurfaceColor(0.0f, 0.0f, 0.0f),
		DisplacementMinimum(0.0f),
		DisplacementMaximum(1.0f),
		ReflectionAnisotropy(0.0f),
		ReflectionAnisotropyRotation(0.0f),
		SheenWeight(0.0f),
		SheenColor(1.0f, 1.0f, 1.0f),
		CoatingWeight(0.0f),
		CoatingRoughness(0.01f),
		CoatingIOR(1.5f),
		CoatingThickness(0.0f),
		CoatingColor(1.0f, 1.0f, 1.0f),
		RefractionWeight(0.0f),
		RefractionIOR(1.5f),
		IsOpacityMasked(false)
		{
		}

		ColorRGBF32 BaseColor; /**< Material base color. */
		float Roughness;	   /**< Reflection roughness [0..1]. */
		float Metalness;	   /**< Reflection metalness [0..1]. */
		float Opacity;		   /**< Opacity [0..1]. */

		ColorRGBF32 EmissiveColor; /**< Emissive color. */
		float EmissiveIntensity;   /**< Emissive intensity [0..n]. Used as a factor to `EmissiveColor` */

		float SubsurfaceWeight;				   /**< Subsurface blending weight  [0..1].*/
		GraphVec3F SubsurfaceScatteringRadius; /**< Subsurface scatter radius in scene units. */
		ColorRGBF32 SubsurfaceColor;		   /**< Subsurface color. */

		float DisplacementMinimum; /**< Displacement minimum, requires a displacement map texture layer. */
		float DisplacementMaximum; /**< Displacement maximum, requires a displacement map texture layer. */

		float ReflectionAnisotropy;			/**< Reflection anisotropy [0..1] */
		float ReflectionAnisotropyRotation; /**< Reflection anisotropy rotation [0..1] */

		float SheenWeight;		/**< Sheen blending weight [0..1]. */
		ColorRGBF32 SheenColor; /**< Sheen color. */

		float CoatingWeight;	  /**< Coating blending weight [0..1]. */
		float CoatingRoughness;	  /**< Coating roughness [0..1].*/
		float CoatingIOR;		  /**< Coating IOR [1..3]. */
		float CoatingThickness;	  /**< Coating thickness. Set to value > 0 to enable transmission of CoatingColor. */
		ColorRGBF32 CoatingColor; /**< Coating color. */

		float RefractionWeight; /**< Refraction weight  [0..1]. */
		float RefractionIOR;	/**< Refraction IOR [1..3].*/

		bool IsOpacityMasked; /**< True if the opacity is rendered masked, or alpha blended. */
	};

	/**
	 * The GraphMaterialTextureWrapMode enumeration specifies different wrap modes when sampling from a texture.
	 */
	namespace GraphMaterialTextureWrapMode
	{
		enum Type
		{
			Repeat, /**< Repeated (tiling) texture sampling. */
			Clamp	/**< Fragments will be clamped to the texture border when sampling outside of [0...1]. */
		};
	}

	/**
	 * The GraphMaterialTexture structure represents a transformation applied to UV coordinates
	 * when sampling from a texture.
	 * The order of transformations applied is: Rotate then scale and finally translate.
	 */
	struct GraphMaterialTextureTransform
	{
		GraphMaterialTextureTransform() :
		WrapModeU(GraphMaterialTextureWrapMode::Repeat),
		WrapModeV(GraphMaterialTextureWrapMode::Repeat),
		TransformPivot(0.0f, 0.0f),
		Translate(0.0f, 0.0f),
		RotationDegrees(0.0f),
		Scale(1.0f, 1.0f)
		{
		}

		GraphMaterialTextureWrapMode::Type WrapModeU; /**< Wrap mode along U axis. */
		GraphMaterialTextureWrapMode::Type WrapModeV; /**< Wrap mode along V axis. */
		GraphVec2F TransformPivot;					  /**< Pivot for transformation in UV space. */
		GraphVec2F Translate;						  /**< UV translation. */
		float RotationDegrees;						  /**< UV rotation in degrees around `TransformPivot`. */
		GraphVec2F Scale;							  /**< UV scale. */
	};

	/**
	 * The GraphMaterialTexture enum represents different material texture types.
	 */
	namespace GraphMaterialTexture
	{
		enum Type
		{
			TypeColor = 0,				   /**< Default color texture. */
			TypeNormalMapTangentSpace = 1, /**< Normal map texture with tangent space normals. */
			TypeNormalMapObjectSpace = 2,  /**< Normal map texture with object space normals.*/
			TypeOpacity = 3,			   /**< Greyscale opacity texture. */
			TypeAmbientOcclusion = 4,	   /**< Ambient occlusion texture. */
			TypeRoughness = 5,			   /**< Roughness texture. */
			TypeSpecular = 6,			   /**< Specular texture. */
			TypeMetalness = 7,			   /**< Metalness texture. */
			TypeEmissive = 8,			   /**< Emissive texture. */
			TypeSubsurfaceColor = 9,	   /**< Subsurface scattering color texture. */
			TypeSheenWeight = 10,		   /**< Sheen weight texture. */
			TypeHeight = 11,			   /**< Height/Displacement texture. */
			Count = 12
		};
	} // namespace GraphMaterialTexture

	/**
	 * The IGraphScene class represents an abstract interface for graph scenes.
	 */
	class INSTAMAT_EXPORTEDCLASS IGraphScene
	{
	protected:
		virtual ~IGraphScene() {}

	public:
		/**
		  * Gets the number of nodes in the scene.
		  *
		  * @return The number of nodes.
		  */
		virtual uint32 GetNodeCount() const = 0;

		/**
		  * Creates an empty node at the specified path.
		  *
		  * @param path The node path.
		  * @return True upon success.
		  */
		virtual bool CreateNode(const char* path) = 0;

		/**
		  * Destroys the node at the specified path.
		  *
		  * @param path The node path.
		  * @return True upon success.
		  */
		virtual bool DestroyNode(const char* path) = 0;

		/**
		  * Determines if the scene contains a node at the specified path.
		  *
		  * @param path The node path.
		  * @return True if the specified path is valid.
		  */
		virtual bool IsValidNode(const char* path) const = 0;

		/**
		  * Creates a unique path from the specified input path.
		  * This method can be used to ensure a unique path before inserting a node into the scene.
		  *
		  * @param path The preferred path.
		  * @return A unique path.
		  */
		virtual const char* GetUniquePath(const char* path) const = 0;

		/**
		  * Gets the number of children at the specified path.
		  *
		  * @param path The parent path.
		  * @return The number of child nodes.
		  */
		virtual uint32 GetChildNodeCount(const char* path) const = 0;

		/**
		  * Gets the name of a child node for the specified index.
		  *
		  * @param path The parent path.
		  * @param index The index of the child node.
		  * @return The name of the child node or nullptr if it does not exist.
		  */
		virtual const char* GetChildNodeName(const char* path, const uint32 index) const = 0;

		/**
		  * Determines if the node at the specified path contains a mesh.
		  *
		  * @param path The node path.
		  * @return True if there is a mesh at the specified path.
		  */
		virtual bool IsMeshAvailableForNode(const char* path) const = 0;

		/**
		  * Gets the mesh at the specified path.
		  *
		  * @param path The node path.
		  * @return A pointer to the mesh, or null if the node has no mesh.
		  */
		virtual const class IGraphMesh* GetMeshForNode(const char* path) const = 0;

		/**
		  * Sets a mesh for the specified path.
		  * @note The API will copy the data from the mesh and store it internally.
		  * The data can be released after invoking this method.
		  *
		  * @param path The node path.
		  * @param mesh The mesh.
		  * @param transformWorldToLocal Set to true if the vertex positions of the mesh are in world-space.
		  * @return True upon success.
		  */
		virtual bool SetMeshForNode(const char* path, const class IGraphMesh* mesh, bool transformWorldToLocal) = 0;

		/**
		  * Gets the transform of the node at the specified path.
		  *
		  * @param path The node path.
		  * @param [out] outTranslation The output translation.
		  * @param [out] outRotation The output rotation.
		  * @param [out] outScale The output scale.
		  * @return True upon success.
		  */
		virtual bool GetTransformForNode(const char* path, GraphVec3D& outTranslation,
										 GraphQuaternionD& outRotation,
										 GraphVec3D& outScale) const = 0;

		/**
		 * This method computes the world-transform matrix for the specified node.
		 *
		 * @param path The node path.
		 * @param [out] outMatrix A storage for a column-major 4x4 matrix
		 * @return True upon success.
		 */
		virtual bool GetTransformMatrixForNode(const char* path, double* outMatrix) const = 0;

		/**
		  * Sets the translation of the node at the specified path.
		  *
		  * @param path The node path.
		  * @param translation The translation.
		  * @param rotation The rotation.
		  * @param scale The scale.
		  * @return True upon success.
		  */
		virtual bool SetTransformForNode(const char* path, const GraphVec3D& translation,
										 const GraphQuaternionD& rotation,
										 const GraphVec3D& scale) = 0;

		/**
		  * This method computes the world-transform for the specified node.
		  *
		  * @param path The node path.
		  * @param [out] outTranslation The output translation.
		  * @param [out] outRotation The output rotation.
		  * @param [out] outScale The output scale.
		  * @return True upon success.
		  */
		virtual bool CalculateWorldTransformForNode(const char* path, GraphVec3D& outTranslation,
													GraphQuaternionD& outRotation,
													GraphVec3D& outScale) const = 0;

		/**
		  * This method computes the world-transform matrix for the specified node.
		  *
		  * @param path The node path.
		  * @param [out] outMatrix4 A storage for a column-major 4x4 matrix
		  * @return True upon success.
		  */
		virtual bool CalculateWorldTransformMatrixForNode(const char* path, double* outMatrix) const = 0;

		/**
		  * Gets the attribute for a node at the specified index.
		  * @note This method can be used to iterate over user attributes.
		  *
		  * @param path The node path.
		  * @param index The index of the attribute.
		  * @param [out] outName The output name of the attribute or nullptr if no attribute is found.
		  * @return True if a valid attribute is found for the node at the specified index.
		  */
		virtual bool GetNextUserAttribute(const char* path, uint32 index, const char** outName) const = 0;

		/**
		  * Sets a string attribute for the node at the specified path.
		  *
		  * @param path The node path.
		  * @param name The attribute name.
		  * @param value The attribute value.
		  * @return True upon success.
		  */
		virtual bool SetStringAttributeForNode(const char* path, const char* name, const char* value) = 0;

		/**
		  * Gets the value of a string attribute for the node at the specified path.
		  *
		  * @param path The node path.
		  * @param name The name of the attribute.
		  * @param [out] outValue The output string value or nullptr if no attribute is found.
		  * @return True upon success.
		  */
		virtual bool GetStringAttributeForNode(const char* path, const char* name, const char** outValue) const = 0;

		/**
		  * Deletes the attribute from the node at the specified source path.
		  *
		  * @param sourcePath The source node path.
		  * @param name the attribute name.
		  * @return true upon success.
		  */
		virtual bool DeleteAttributeForNode(const char* sourcePath, const char* name) = 0;

		/**
		  * Gets the number of scene materials.
		  *
		  * @return The number of scene materials.
		  */
		virtual uint32 GetMaterialCount() const = 0;

		/**
		  * Gets a unique material name for the specified name.
		  * @note This method can be used to ensure unique scene material names when creating materials.
		  *
		  * @param name The preferred name of the material.
		  * @return A unique material name.
		  */
		virtual const char* GetUniqueMaterialName(const char* name) const = 0;

		/**
		  * Gets the scene material name at the specified index.
		  *
		  * @param materialIndex The material index.
		  * @param [out] outMaterialName The output material name or nullptr if no material is found.
		  * @return True upon success.
		  */
		virtual bool GetMaterialNameForIndex(const uint32 materialIndex, const char** outMaterialName) const = 0;

		/**
		  * Finds the scene material with the specified name.
		  *
		  * @param name The name of the material.
		  * @param [out] outMaterialIndex The output material index.
		  * @return True if a material with the specified name is found.
		  */
		virtual bool GetMaterialIndex(const char* name, uint32& outMaterialIndex) const = 0;

		/**
		  * Creates a material with the specified name.
		  *
		  * @param name The material name.
		  * @return True upon success.
		  */
		virtual bool CreateMaterial(const char* name) = 0;

		/**
		  * Determines if a material with the specified name exists in the scene.
		  *
		  * @param name The name of the material.
		  * @return True if the material exists.
		  */
		virtual bool IsValidMaterial(const char* name) const = 0;

		/**
		  * Removes the material with the specified name from the scene.
		  *
		  * @param name The name of the material.
		  * @return True if the material is removed.
		  */
		virtual bool DestroyMaterial(const char* name) = 0;

		/**
		  * Sets the material properties for the material with the specified name.
		  *
		  * @param name The material name.
		  * @param materialParameters The material properties.
		  * @return True upon success.
		  */
		virtual bool SetMaterialParameters(const char* name, const GraphMaterialProperties& materialParameters) = 0;

		/**
		  * Gets the material properties of the material with the specified name.
		  *
		  * @param name The material name.
		  * @param [out] outMaterialParameters The output material properties.
		  *
		  * @return True upon success.
		  */
		virtual bool GetMaterialParameters(const char* name, GraphMaterialProperties& outMaterialParameters) const = 0;

		/**
		  * Assigns a texture with the specified `type` to the material with the specified `name`.
		  *
		  * @param name The name of the material.
		  * @param type The type of the material texture.
		  * @param filename The filepath of the texture. If null, the layer will be removed.
		  * @param transform The transform of the texture.
		  * @param sampler (optional) The image sampler @note The ownership of `sampler ` will be transferred to the scene.
		  * @return True upon success.
		  */
		virtual bool SetMaterialTexture(const char* name, const GraphMaterialTexture::Type type, const char* filename,
										const GraphMaterialTextureTransform& transform, IImageSampler* sampler) = 0;

		/**
		  * Gets the filename of the texture of the specified `type` material with the specified `name`.
		  *
		  * @param name The name of the material.
		  * @param type The texture type of the material texture.
		  * @param [out] outFilename The output filename of the texture.
		  * @param outTransform The transform of the texture.
		  * @return True if a material layer at the specified index exists for the specified type.
		  */
		virtual bool GetMaterialTexture(const char* name, const GraphMaterialTexture::Type type, const char** outFilename,
										GraphMaterialTextureTransform& outTransform) const = 0;

		/**
		 * Gets the sampler of a texture of the specified `type` assigned to the material with the specified `name`.
		 * @note If the sampler is not yet loaded into memory, the scene will
		 * attempt to load the sampler from its `filename` into memory.
		 *
		 * @param name The name of the material.
		 * @param type The texture type of the material texture.
		 * @return The image sampler if loaded into memory or null if not available.
		 */
		virtual const IImageSampler* GetMaterialTextureSampler(const char* name, const GraphMaterialTexture::Type type) const = 0;
	};

	/**
	 * The GraphMeshVertex struct represents an attributed vertex as used by the IGraphMesh interface.
	 */
	struct GraphMeshVertex
	{
		GraphVec3F Position; /**< Vertex postion. */
		GraphVec3F Normal;	 /**< Vertex normal. */
		GraphVec3F Binormal; /**< Vertex binormal. */
		GraphVec3F Tangent;	 /**< Vertex tangent. */
		GraphVec2F TexCoord; /**< Vertex texture coordinate. */
		ColorRGBAUI8 Color;	 /**< Vertex color. */
	};

	/**
	 * The IGraphMesh class represents an abstract interface for graph meshes.
	 * The polygon primitive of the mesh is a triangle.
	 */
	class INSTAMAT_EXPORTEDCLASS IGraphMesh
	{
	protected:
		virtual ~IGraphMesh() {}

	public:
		/**
		 * Gets the mesh vertices.
		 *
		 * @return The mesh vertices.
		 */
		virtual GraphMeshVertex* GetVertices() = 0;

		/**
		 * Gets the mesh vertices.
		 *
		 * @return The mesh vertices.
		 */
		virtual const GraphMeshVertex* GetVertices() const = 0;

		/**
		 * Gets the mesh indices.
		 *
		 * @return The mesh indices.
		 */
		virtual uint32* GetIndices() = 0;

		/**
		 * Gets the mesh indices.
		 *
		 * @return The mesh indices.
		 */
		virtual const uint32* GetIndices() const = 0;

		/**
		 * Gets the submesh mesh indices.
		 * @note Submesh must be in ascending order.
		 * If the criteria does not match, the mesh will need to be preprocessed.
		 * @note Submesh indices denote sub-mesh groups in a larger buffer.
		 * Submesh are used to distincly identify polygon groups on the mesh.
		 * @note The number of submesh indices must match
		 * the polygon count of the mesh, which is `IndexCount / 3`.
		 * @note If the mesh does not have submeshes, simply set each
		 * element of the array to 0.
		 *
		 * @return The submesh indices.
		 */
		virtual uint32* GetSubmeshIndices() = 0;

		/**
		 * Gets the submesh mesh indices.
		 * @note Submesh must be in ascending order.
		 * If the criteria does not match, the mesh will need to be preprocessed.
		 * @note Submesh indices denote sub-mesh groups in a larger buffer.
		 * Submesh are used to distincly identify polygon groups on the mesh.
		 * @note The number of submesh indices must match
		 * the polygon count of the mesh, which is `IndexCount / 3`.
		 * @note If the mesh does not have submeshes, simply set each
		 * element of the array to 0.
		 *
		 * @return The submesh indices.
		 */
		virtual const uint32* GetSubmeshIndices() const = 0;

		/**
		 * Gets the submesh name for \p submeshIndex
		 * @note Each submesh index in the SubmeshIndices array
		 * must return a valid name.
		 *
		 * @param submeshIndex The submesh index.
		 * @return The submesh name.
		 */
		virtual const char* GetSubmeshName(const uint32 submeshIndex) const = 0;

		/**
		 * Gets the material indices.
		 * @note Material indices denote the material assigned to a face .
		 * @note The number of material indices must match
		 * the polygon count of the mesh, which is `IndexCount / 3`.
		 *
		 * @return The material indices.
		 */
		virtual uint32* GetMaterialIndices() = 0;

		/**
		 * Gets the material indices.
		 * @note Material indices denote the material assigned to a face .
		 * @note The number of material indices must match
		 * the polygon count of the mesh, which is `IndexCount / 3`.
		 *
		 * @return The material indices.
		 */
		virtual const uint32* GetMaterialIndices() const = 0;

		/**
		 * Gets the material name for \p materialIndex
		 * @note All values up to max(materialIndex) in the MaterialIndices array
		 * must return a valid name.
		 * Example: If the maximum material index is 12, then material
		 * indices 0..12 must return valid names, even if only 12 is used.
		 *
		 * @param materialIndex The material index.
		 * @return The material name.
		 */
		virtual const char* GetMaterialName(const uint32 materialIndex) const = 0;

		/**
		 * Gets the vertex count.
		 *
		 * @return Vertex count.
		 */
		virtual uint32 GetVertexCount() const = 0;

		/**
		 * Gets the index count.
		 *
		 * @return Index count.
		 */
		virtual uint32 GetIndexCount() const = 0;

		/**
		 * Gets the polygon count.
		 *
		 * @return The polygon count.
		 */
		uint32 GetPolygonCount() const
		{
			return GetIndexCount() / 3u;
		}
	};

	typedef bool (*pfnGraphMeshIntersectorFilterCallback)(const IGraphMesh* mesh, const uint32 faceIndex, const double s, const double t, const GraphVec3F& barycentric, void* userData);
	typedef bool (*pfnGraphMeshSphereIntersectorFilterCallback)(const uint32 faceIndex, double distance, const GraphVec3F& barycentric, void* userData);

	/**
	 * The IGraphMeshIntersector class can be used to perform accelerated
	 * intersection tests against an IGraphMesh.
	 */
	class INSTAMAT_EXPORTEDCLASS IGraphMeshIntersector
	{
	protected:
		virtual ~IGraphMeshIntersector() {}

	public:
		/**
		 * Performs segment-triangle intersection test and returns the closest point to \p p.
		 *
		 * @param p segment start
		 * @param q segment end
		 * @param outFaceIndex pointer to a variable to store the hit face index
		 * @param outS pointer to a variable to store the intersection fraction
		 * @param outT pointer to a variable to store the intersection fraction
		 * @param outBarycentric pointer to a variable to store the barycentric coordinate for the hit face.
		 * @return true if a face was intersected by the specified segment.
		 */
		virtual bool IsIntersected(const GraphVec3F& p, const GraphVec3F& q, uint32* outFaceIndex, double* outS, double* outT, GraphVec3F* outBarycentric) const = 0;

		/**
		 * Performs a filtered segment-triangle intersection test.
		 *
		 * @param p segment start
		 * @param q segment end
		 * @param outFaceIndex pointer to a variable to store the hit face index
		 * @param outS pointer to a variable to store the intersection fraction
		 * @param outT pointer to a variable to store the intersection fraction
		 * @param outBarycentric pointer to a variable to store the barycentric coordinate for the hit face.
		 * @param filterCallback user specified function to return true if the hit is valid
		 * @param userData (optional) User data to be passed into the callback.
		 * @return true if a face was intersected by the specified segment.
		 */
		virtual bool IsIntersectedFiltered(const GraphVec3F& p, const GraphVec3F& q, uint32* outFaceIndex, double* outS, double* outT, GraphVec3F* outBarycentric, pfnGraphMeshIntersectorFilterCallback filterCallback, void* userData) const = 0;

		/**
		 * Performs a filtered triangle-sphere intersection test.
		 *
		 * @param origin sphere origin
		 * @param sphereRadius sphere radius
		 * @param filterCallback user specified function to return true if the hit is valid, if the callback returns false, the search is aborted.
		 * @param userData (optional) User data to be passed into the callback.
		 * @return true if a face was intersected by the specified sphere.
		 */
		virtual bool IsSphereIntersectedFiltered(const GraphVec3F& origin, const float sphereRadius, pfnGraphMeshSphereIntersectorFilterCallback filterCallback, void* userData) const = 0;
	};

	/**
	 * The IGraphVariable class represents a variable in the InstaMAT graph.
	 * Variables can be used for input and output parameters as well as local variables in a graph.
	 */
	class INSTAMAT_EXPORTEDCLASS IGraphVariable
	{
	protected:
		virtual ~IGraphVariable() {}

	public:
		/**
		 * The UIControlType enum represents different types of UI representations for a variable.
		 */
		enum UIControlType
		{
			UIControlTypeSlider,
			UIControlTypeSpinBox,
			UIControlTypeColorPicker,
			UIControlTypeVectorAngular,
			UIControlTypeVectorPoint,
			UIControlTypeSliderRange,
			UIControlTypeImagePicker,
			UIControlTypeSpawnAttachmentPicker,
			UIControlTypePercentageSlider
		};

		/**
		 * The Type enum represents fundamental types for a variable.
		 */
		enum Type
		{
			TypeFloat32,
			TypeInt32,
			TypeUInt32,
			TypeVector2F,
			TypeVector3F,
			TypeVector4F,
			TypeAtomInputImage,
			TypeAtomOutputImage,
			TypeElementImage,
			TypeElementMesh,
			TypeBoolean,
			TypeAtomInputImageGray,
			TypeAtomOutputImageGray,
			TypeElementImageGray,
			TypeElementResource,
			TypeVector2I32,
			TypeVector3I32,
			TypeVector4I32,
			TypeVector2UI32,
			TypeVector3UI32,
			TypeVector4UI32,
			TypeMatrix2F,
			TypeMatrix3F,
			TypeMatrix4F,
			TypeElementString,
			TypeEnumValue,
			TypeElementPointCloud,
			TypeElementScene,
			TypeInvalid,
			TypeOverload
		};

		/**
		 * The ColorSpaceType enum represents different  color space types.
		 */
		enum ColorSpaceType
		{
			ColorSpaceTypeSRGB,	  /**< sRGB color space, default for all color outputs.*/
			ColorSpaceTypeLinear, /**< Linear color space, default for all grayscale images. */
			ColorSpaceTypeAuto	  /**< Automatic determination.*/
		};

		/**
		 * The UIAngularOrientation enum represents different UI orientation types when using UI controls for a variable that can be rotated.
		 */
		enum UIAngularOrientation
		{
			Top,
			Right,
			Bottom,
			Left
		};

		/**
		 * The UIAngularRotation enum represents different rotation types when using UI controls for a variable that can be rotated.
		 */
		enum UIAngularRotation
		{
			Clockwise,
			CounterClockwise
		};

		/**
		 * Gets the type of this instance.
		 *
		 * @return Type.
		 */
		virtual Type GetVariableTypeValue() const = 0;

		/**
		 * Gets the type of this instance as string.
		 *
		 * @return Type as String.
		 */
		virtual const char* GetVariableTypeString() const = 0;

		/**
		 * Gets the arithmetic value.
		 *
		 * @return Arithmetic value.
		 */
		virtual ArithmeticGraphValue GetArithmeticValue() const = 0;

		/**
		 * Gets the enum value as 32-bit unsigned integer.
		 *
		 * @return Enum value or ~0u if not an enum type.
		 */
		virtual uint32 GetEnumValueUI32() const = 0;

		/**
		 * Acquires the enum string value for the specified \p enumValue from the referenced type provider.
		 * @note This method performs a search for the referenced type provider this the return values
		 * should be cached in the user interface.
		 *
		 * @return String value or null if the type is not EnumValue, or if \p enumValue is out of range, or if the type provider could not be found
		 */
		virtual const char* GetEnumValueString(uint32 enumValue) const = 0;

		/**
		 * Acquires the enum case count from the referenced type provider.
		 * @note This method performs a search for the referenced type provider this the return values
		 * should be cached in the user interface.
		 *
		 * @return Enum case count or ~0u if not an enum type, or if the type provider could not be found.
		 */
		virtual uint32 GetEnumCaseCount() const = 0;

		/**
		 * Gets the string value.
		 *
		 * @return String value or null if the type is not ElementString
		 */
		virtual const char* GetElementStringValue() const = 0;

		/**
		 * Gets the GraphResourceURL that is currently set.
		 * Format: pkg://{Package-ID}/{Name}
		 * @note Only image types, mesh types and generic resources
		 * may call this method.
		 *
		 * @return GraphResourceURL or null if the type does not support GraphResourceURLs.
		 */
		virtual const char* GetResourceURLValue() const = 0;

		/**
		 * Gets the colorspace of the variable
		 * @note Only image types may call this method.
		 *
		 * @return The ColorSpaceType
		 */
		virtual ColorSpaceType GetColorSpaceTypeValue() const = 0;

		/**
		 * Determines if this instance is an auxiliary variable.
		 * @note Auxiliary variables values cannot be set.
		 * They can only be connected in the parent graph.
		 *
		 * @return True if auxiliary.
		 */
		virtual bool IsAuxiliary() const = 0;

		/**
		 * Converts this instance to its IGraphObject representation.
		 *
		 * @return IGraphObject.
		 */
		virtual IGraphObject* AsObject() = 0;

		/**
		 * Converts this instance to its IGraphObject representation.
		 *
		 * @return IGraphObject.
		 */
		virtual const IGraphObject* AsObject() const = 0;
	};

	/**
	 * The IGraph class represents a graph in the InstaMAT SDK.
	 * @note The IGraph class be implemented by different types of graphs including ElementGraphs or AtomGraphs.
	 */
	class INSTAMAT_EXPORTEDCLASS IGraph
	{
	protected:
		virtual ~IGraph() {}

	public:
		/**
		 * The ParameterType enum represents different semantics for graph parameters.
		 */
		enum ParameterType
		{
			ParameterTypeInput,
			ParameterTypeOutput,
		};

		/**
		 * The GraphType enum represents different graph types accesible via the public API.
		 */
		enum GraphTypePublic
		{
			GraphTypeFunction = 1u,
			GraphTypeAtom = 10u,
			GraphTypeElement = 11u,
			GraphTypeElementEntity = 12u,
			GraphTypeNPassElement = 15u,
			GraphTypeElementLayer = 20u,
			GraphTypeElementMaterialLayer = 23u,
			GraphTypeMaterialize = 22u,
			GraphTypeLazy = 24u
		};

		/**
		 * Gets the type of this instance.
		 *
		 * @return Type.
		 */
		virtual uint32 GetGraphTypeUI32() const = 0;

		/**
		 * Gets the type of this instance as string.
		 *
		 * @return Type as String.
		 */
		virtual const char* GetGraphTypeString() const = 0;

		/**
		 * Gets the lazy graph type of this instance.
		 * @note If this instance is not a lazy graph ~0u will be returned.
		 *
		 * @return The lazy graph type or ~0u if the graph is not lazy.
		 */
		virtual uint32 GetLazyGraphTypeUI32() const = 0;

		/**
		 * Gets the lazy graph type of this instance as string.
		 * @note If this instance is not a lazy graph null will be returned.
		 *
		 * @return The lazy graph type or null if the graph is not lazy.
		 */
		virtual const char* GetLazyGraphTypeString() const = 0;

		/**
		 * Resolves the lazy graph.
		 *
		 * @note The resolve causes the lazy graph and potentially other IGraph*
		 * pointers that are currently registered as a lazy graph to become invalid.
		 * This happens if the instance that is being resolved requires other lazy
		 * graphs to be resolved that are dependencies of the resolving lazy graph.
		 * It is recommended to rebuild any lists of pointers after a resolve.
		 * @note A resolve can happen indirectly when loading or requests a IGraphObject
		 * or IGraph via the SDK API.
		 * To avoid dangling pointers, it's best to copy the results from library queries
		 * to your own struct and reference objects by their GraphID.
		 * This avoids the need to maintain proper lists.
		 *
		 * @return The resolved graph or null if the resolve failed.
		 */
		virtual IGraph* ResolveLazyGraph() = 0;

		/**
		 * Gets the parameter with the specified \p name.
		 *
		 * @param name The parameter name.
		 * @param parameterType The parameter type.
		 * @return The parameter or null if not found.
		 */
		virtual IGraphVariable* GetParameterWithName(const char* name, const ParameterType parameterType) = 0;

		/**
		 * Gets the parameter with the specified \p name.
		 *
		 * @param name The parameter name.
		 * @param index The index of the parameter to return, this is required if multiple parameters exist with the same name. Setting \p index to 1 will return the second parameter with specified \p name.
		 * @param parameterType The parameter type.
		 * @return The parameter or null if not found.
		 */
		virtual const IGraphVariable* GetParameterWithName(const char* name, const ParameterType parameterType) const = 0;

		/**
		 * Gets the parameter at the specified \p index.
		 *
		 * @param index The index of the parameter to return, this is required if multiple parameters exist with the same name. Setting \p index to 1 will return the second parameter with specified \p name.
		 * @param parameterType The parameter type.
		 * @return The parameter or null if not found.
		 */
		virtual IGraphVariable* GetParameterAtIndex(const uint32 index, const ParameterType parameterType) = 0;

		/**
		 * Gets the parameter at the specified \p index.
		 *
		 * @param index The index of the parameter to return, this is required if multiple parameters exist with the same name. Setting \p index to 1 will return the second parameter with specified \p name.
		 * @param parameterType The parameter type.
		 * @return The parameter or null if not found.
		 */
		virtual const IGraphVariable* GetParameterAtIndex(const uint32 index, const ParameterType parameterType) const = 0;

		/**
		 * Determines if the \p parameter should be visible in the UI to configure the graph.
		 * @note This method will only respond properly to instances.
		 * Invoking this method on Graph class objects results in undefined behavior.
		 *
		 * @param parameter The parameter variable.
		 * @param hideAdvanced True to hide all parameters marked as advanced.
		 * @return True if the parameter should be displayed in the UI.
		 */
		virtual bool IsParameterUIRelevant(const IGraphVariable& parameter, const bool hideAdvanced) const = 0;

		/**
		 * Gets the number of parameters for the specified \p parameterType.
		 *
		 * @param parameterType The parameter type.
		 * @return The number of parameters.
		 */
		virtual uint32 GetParameterCount(const ParameterType parameterType) const = 0;

		/**
		 * Gets the physical size in centimeters of this instance.
		 *
		 * @param [out] outWidthInCentimeters The width in centimeters.
		 * @param [out] outHeightInCentimeters The height in centimeters.
		 * @param [out] outDepthInCentimeters The depth in centimeters (useful for displacement mapping).
		 * @return True if the size is valid.
		 */
		virtual bool GetPhysicalSize(float& outWidthInCentimeters, float& outHeightInCentimeters, float& outDepthInCentimeters) const = 0;

		/**
		 * Determines if the graph is grayscale permutable.
		 *
		 * @return True if permutable
		 */
		virtual bool IsGrayscalePermutableValue() const = 0;

		/**
		 * Converts this instance to its IGraphObject representation.
		 *
		 * @return IGraphObject.
		 */
		virtual IGraphObject* AsObject() = 0;

		/**
		 * Converts this instance to its IGraphObject representation.
		 *
		 * @return IGraphObject.
		 */
		virtual const IGraphObject* AsObject() const = 0;
	};

	/**
	 * The IGraphTemplate represents a template.
	 * Templates can be instanced and used in IElementExecution instances
	 * in the same way that IGraph types can be used.
	 */
	class INSTAMAT_EXPORTEDCLASS IGraphTemplate
	{
	protected:
		virtual ~IGraphTemplate() {}

	public:
		/**
		 * Gets the class ID as string.
		 * @note The class ID may point to another IGraphTemplate.
		 * @note To determine the size of the ID string this function can be call
		 * with a null value for \p buffer.
		 *
		 * @param [out] buffer (optional) The output buffer
		 * @param bufferSize The size of the output buffer
		 * @return The size of the ID string.
		 */
		virtual uint64 GetClassID(char* buffer, const uint64 bufferSize) const = 0;

		/**
		 * Gets the type of this instance's class.
		 *
		 * @return Type or 0 if the class is invalid.
		 */
		virtual uint32 GetClassGraphTypeUI32() const = 0;

		/**
		 * Gets the type of this instance's class as string.
		 *
		 * @return Type as String or null if the class is invalid.
		 */
		virtual const char* GetClassGraphTypeString() const = 0;

		/**
		 * Converts this instance to its IGraphObject representation.
		 *
		 * @return IGraphObject.
		 */
		virtual IGraphObject* AsObject() = 0;

		/**
		 * Converts this instance to its IGraphObject representation.
		 *
		 * @return IGraphObject.
		 */
		virtual const IGraphObject* AsObject() const = 0;
	};

	/**
	 * The IGraphObject represents the fundamental GraphObject.
	 */
	class INSTAMAT_EXPORTEDCLASS IGraphObject
	{
	protected:
		virtual ~IGraphObject() {}

	public:
		/**
		 * Gets the name of this instance.
		 *
		 * @param friendlyName True to return the friendly name used for UIs.
		 * @return The name.
		 */
		virtual const char* GetName(bool friendlyName) const = 0;

		/**
		 * Gets the type of this instance.
		 *
		 * @return The type of this instance.
		 */
		virtual uint32 GetTypeUI32() const = 0;

		/**
		 * Gets the ID as string.
		 * @note To determine the size of the ID string this function can be call
		 * with a null value for \p buffer.
		 *
		 * @param [out] buffer (optional) The output buffer
		 * @param bufferSize The size of the output buffer
		 * @return The size of the ID string.
		 */
		virtual uint64 GetID(char* buffer, const uint64 bufferSize) const = 0;

		/**
		 * Gets the version of this instance.
		 *
		 * @param [out] outMajorVersion The output buffer for the major version.
		 * @param [out] outMinorVersion The output buffer for the minor version.
		 */
		virtual void GetVersion(uint32& outMajorVersion, uint32& outMinorVersion) const = 0;

		/**
		 * Gets the canvas position of this instance.
		 *
		 * @return The canvas position.
		 */
		virtual const GraphPosition& GetGraphPosition() const = 0;

		/**
		 * Gets the parent of this instance.
		 *
		 * @return The parent or null if not available.
		 */
		virtual IGraph* GetParent() = 0;

		/**
		 * Gets the parent of this instance.
		 *
		 * @return The parent or null if not available.
		 */
		virtual const IGraph* GetParent() const = 0;

		/**
		 * Gets the package of this instance.
		 *
		 * @note
		 * Not all GraphObject must reside in a package.
		 * If the GraphObject is not stored inside a package, this API returns null.
		 *
		 * @return The package or null.
		 */
		virtual const class IGraphPackage* GetParentPackage() const = 0;

		/**
		 * Gets the user pointer.
		 *
		 * @return Userpointer.
		 */
		virtual void* GetUserPointer() const = 0;

		/**
		 * Sets the user pointer to \p userPointer
		 *
		 * @param userPointer The user pointer to set.
		 */
		virtual void SetUserPointer(void* userPointer) = 0;

		/**
		 * Determines if the meta data value for the specified \p key exists.
		 *
		 * @param key The meta data key.
		 * @return True if meta data exists.
		 */
		virtual bool ContainsMetaDataKeyChar(const char* key) const = 0;

		/**
		 * Gets the meta data value for the specified \p key.
		 *
		 * @param key The meta data key.
		 * @return The meta data value.
		 */
		virtual const char* GetMetaDataAsChar(const char* key) const = 0;

		/**
		 * Sets the meta data of this instance.
		 * @note If \p value is an empty string the meta data entry for \p key
		 * will be removed.
		 *
		 * @param key The meta data key.
		 * @param value The meta data value.
		 */
		virtual void SetMetaDataAsChar(const char* key, const char* value) = 0;

		/**
		 * Gets the meta data value for the specified \p key.
		 * @note Calling this method for a key that does not exists results in undefined behavior.
		 * Make sure the \p key exists by calling \# ContainsMetaDataKeyChar with the same \p key.
		 *
		 * @param key The meta data key.
		 * @return The meta data value.
		 */
		virtual uint32 GetMetaDataAsCharUInt32(const char* key) const = 0;

		/**
		 * Sets the meta data of this instance.
		 * @note If \p value is an empty string the meta data entry for \p key
		 * will be removed.
		 *
		 * @param key The meta data key.
		 * @param value The meta data value.
		 */
		virtual void SetMetaDataAsCharUInt32(const char* key, const uint32 value) = 0;

		/**
		 * Gets the meta data value for the specified \p key.
		 * @note Calling this method for a key that does not exists results in undefined behavior.
		 * Make sure the \p key exists by calling \# ContainsMetaDataKeyChar with the same \p key.
		 *
		 * @param key The meta data key.
		 * @return The meta data value.
		 */
		virtual int32 GetMetaDataAsCharInt32(const char* key) const = 0;

		/**
		 * Sets the meta data of this instance.
		 * @note If \p value is an empty string the meta data entry for \p key
		 * will be removed.
		 *
		 * @param key The meta data key.
		 * @param value The meta data value.
		 */
		virtual void SetMetaDataAsCharInt32(const char* key, const int32 value) = 0;

		/**
		 * Gets the meta data value for the specified \p key.
		 * @note Calling this method for a key that does not exists results in undefined behavior.
		 * Make sure the \p key exists by calling \# ContainsMetaDataKeyChar with the same \p key.
		 *
		 * @param key The meta data key.
		 * @return The meta data value.
		 */
		virtual bool GetMetaDataAsCharBoolean(const char* key) const = 0;

		/**
		 * Sets the meta data of this instance.
		 * @note If \p value is an empty string the meta data entry for \p key
		 * will be removed.
		 *
		 * @param key The meta data key.
		 * @param value The meta data value.
		 */
		virtual void SetMetaDataAsCharBoolean(const char* key, const bool value) = 0;

		/**
		 * Removes the specified meta data entry.
		 *
		 * @param key The meta data key to remove.
		 */
		virtual void RemoveMetaDataAsChar(const char* key) = 0;

		/**
		 * Converts this instance to its IGraph representation.
		 *
		 * @return IGraph or null if type mismatch.
		 */
		virtual IGraph* AsGraph() = 0;

		/**
		 * Converts this instance to its IGraph representation.
		 *
		 * @return IGraph or null if type mismatch.
		 */
		virtual const IGraph* AsGraph() const = 0;

		/**
		 * Converts this instance to its IGraphVariable representation.
		 *
		 * @return IGraphVariable or null if type mismatch.
		 */
		virtual IGraphVariable* AsVariable() = 0;

		/**
		 * Converts this instance to its IGraphVariable representation.
		 *
		 * @return IGraphVariable or null if type mismatch.
		 */
		virtual const IGraphVariable* AsVariable() const = 0;

		/**
		 * Converts this instance to its IGraphTemplate representation.
		 *
		 * @return IGraphTemplate or null if type mismatch.
		 */
		virtual IGraphTemplate* AsTemplate() = 0;

		/**
		 * Converts this instance to its IGraphTemplate representation.
		 *
		 * @return IGraphTemplate or null if type mismatch.
		 */
		virtual const IGraphTemplate* AsTemplate() const = 0;
	};

	/**
	 * The IGraphPackage class represents a package in the InstaMAT SDK.
	 */
	class INSTAMAT_EXPORTEDCLASS IGraphPackage
	{
	protected:
		virtual ~IGraphPackage() {}

	public:
		/**
		 * Gets the name of this instance.
		 *
		 * @return The name.
		 */
		virtual const char* GetName() const = 0;

		/**
		 * Gets the origin path of this instance.
		 *
		 * @return The origin path.
		 */
		virtual const char* GetOriginPath() const = 0;

		/**
		 * Gets the origin type of this instance.
		 * One of `SystemLibrary`, `UserLibrary`, `External`.
		 *
		 * @return The origin type.
		 */
		virtual const char* GetOriginType() const = 0;

		/**
		 * Gets the ID as string.
		 * @note To determine the size of the ID string this function can be call
		 * with a null value for \p buffer.
		 *
		 * @param [out] buffer (optional) The output buffer
		 * @param bufferSize The size of the output buffer
		 * @return The size of the ID string.
		 */
		virtual uint64 GetID(char* buffer, const uint64 bufferSize) const = 0;

		/**
		 * Gets the meta data value for the specified \p key.
		 *
		 * @param key The meta data key.
		 * @return The meta data value.
		 */
		virtual const char* GetMetaDataAsChar(const char* key) const = 0;

		/**
		 * Determines if the meta data value for the specified \p key exists.
		 *
		 * @param key The meta data key.
		 * @return True if meta data exists.
		 */
		virtual bool ContainsMetaDataKeyChar(const char* key) const = 0;
	};

	/**
	 * The IImageSampler represents a texture page as referenced by a material.
	 * A material can reference multiple texture pages of different types and pixel specifications.
	 */
	class INSTAMAT_EXPORTEDCLASS IImageSampler
	{
	protected:
		virtual ~IImageSampler() {}

	public:
		/**
		 * The ComponentType specifies the scalar type of a pixel component.
		 */
		enum ComponentType
		{
			ComponentTypeUInt8 = 0,	 /**< A pixel component is an unsigned 8-bit integer. */
			ComponentTypeUInt16 = 1, /**< A pixel component is an unsigned 16-bit integer. */
			ComponentTypeFloat32 = 2 /**< A pixel component is a 32-bit float. */
		};

		/**
		 * The PixelType enumeration specifies the component semantic and amount of components.
		 */
		enum PixelType
		{
			PixelTypeLuminance = 0,		 /**< Single channel. */
			PixelTypeLuminanceAlpha = 1, /**< Two channels. */
			PixelTypeRGB = 2,			 /**< Three channels. */
			PixelTypeRGBA = 3			 /**< Four channels. */
		};

		/**
		 * The CompressionType enumeration specifies different types of image compression.
		 */
		enum CompressionType
		{
			CompressionTypeNone = 0,	/**< No compression. */
			CompressionTypeDeflate = 1, /**< Deflate compression. */
			CompressionTypeLZW = 2,		/**< LZW compression. */
			CompressionTypeJPEG = 3		/**< JPEG compression. */
		};

		/**
		 * The FilterType enumeration specifies various types of texture filtering.
		 */
		enum FilterType
		{
			FilterTypeNearest = 0,	/**< Nearest neighbor filtering. */
			FilterTypeBilinear = 1, /**< Bilinear filtering. */
			FilterTypeBicubic = 2	/**< Bicubic filtering. */
		};

		/**
		 * Gets the bits per component.
		 *
		 * @return bits per component.
		 */
		virtual uint32 GetBitsPerComponent() const = 0;

		/**
		 * Gets the bytes per component.
		 *
		 * @return bytes per component.
		 */
		virtual uint32 GetBytesPerComponent() const = 0;

		/**
		 * Gets the bytes per pixel.
		 *
		 * @return bytes per pixel.
		 */
		virtual uint32 GetBytesPerPixel() const = 0;

		/**
		 * Gets the component count per pixel.
		 *
		 * @return component count per pixel.
		 */
		virtual uint32 GetComponentCount() const = 0;

		/**
		 * Gets the component type.
		 *
		 * @return component type.
		 */
		virtual ComponentType GetComponentType() const = 0;

		/**
		 * Gets the pixel type.
		 *
		 * @return pixel type
		 */
		virtual PixelType GetPixelType() const = 0;

		/**
		 * Determines if this instance has an alpha channel.
		 *
		 * @return true if this instance has an alpha channel.
		 */
		virtual bool HasAlphaChannel() const = 0;

		/**
		 * Determines if this instance is valid.
		 *
		 * @return true if this instance is valid.
		 */
		virtual bool IsValid() const = 0;

		/**
		 * Gets the name of this instance.
		 *
		 * @return the name of this instance.
		 */
		virtual const char* GetName() const = 0;

		/**
		 * Sets the name of this instance.
		 *
		 * @param name The new name
		 */
		virtual void SetName(const char* name) = 0;

		/**
		 * Gets the data array of this instance.
		 *
		 * @param [out] count (optional) the size of the data array will be written to this pointer
		 * @return the data array of this instance
		 */
		virtual uint8* GetData(uint64* count) = 0;

		/**
		 * Gets the data array of this instance.
		 *
		 * @param [out] count (optional) the size of the data array will be written to this pointer
		 * @return the data array of this instance
		 */
		virtual const uint8* GetData(uint64* count) const = 0;

		/**
		 * Reallocates the data array of this instance to the specified dimensions.
		 *
		 * @param width the width
		 * @param height the height
		 */
		virtual void Reallocate(const uint32 width, const uint32 height) = 0;

		/**
		 * Writes the dimensions of this instance to the specified pointers
		 *
		 * @param outWidth (optional) the width will be written to this pointer
		 * @param outHeight (optional) the height will be written to this pointer
		 */
		virtual void GetSize(uint32* outWidth, uint32* outHeight) const = 0;

		/**
		 * Gets the width of this instance.
		 *
		 * @return the width of this instance.
		 */
		virtual uint32 GetWidth() const = 0;

		/**
		 * Gets the height of this instance.
		 *
		 * @return the height of this instance.
		 */
		virtual uint32 GetHeight() const = 0;

		/**
		 * Sets the specified pixel value at the specified position.
		 * @note If the component type is not float it will be converted and unused channels will be ignored.
		 *  - RGBA = all channels used
		 *  - RGB = alpha channel undefined
		 *  - LA = green and blue undefined, red = luminance
		 *  - L = green, blue and alpha undefined, red = luminance
		 * @param x the x coordinate
		 * @param y the y coordinate
		 * @param value the color value
		 */
		virtual void SetPixelFloat(const uint32 x, const uint32 y, const ColorRGBAF32& value) = 0;

		/**
		 * Sets the specified pixel value at the specified position via alpha blending.
		 * @param x the x coordinate
		 * @param y the y coordinate
		 * @param value the color value to blend
		 */
		virtual void SetPixelFloatBlend(const uint32 x, const uint32 y, const ColorRGBAF32& value) = 0;

		/**
		 * Samples the pixel value and returns the result as float color.
		 * @note If the component type is not float it will be converted and unused channels will be ignored.
		 *  - RGBA = all channels used
		 *  - RGB = alpha channel undefined
		 *  - LA = green and blue undefined, red = luminance
		 *  - L = green, blue and alpha undefined, red = luminance
		 * @param x the x coordinate
		 * @param y the y coordinate
		 * @return the color value as float
		 */
		virtual ColorRGBAF32 SampleFloat(const uint32 x, const uint32 y) const = 0;

		/**
		 * Samples the pixel value and returns the result as float color.
		 * @note If the component type is not float it will be converted and unused channels will be ignored.
		 *  - RGBA = all channels used
		 *  - RGB = alpha channel undefined
		 *  - LA = green and blue undefined, red = luminance
		 *  - L = green, blue and alpha undefined, red = luminance
		 * @param x the x coordinate
		 * @param y the y coordinate
		 * @return the color value as float
		 */
		virtual ColorRGBAF32 SampleAsRepeatedFloat(const int32 x, const int32 y) const = 0;

		/**
		 * Samples the pixel value and returns the result as float color.
		 * @note If the component type is not float it will be converted and unused channels will be ignored.
		 *  - RGBA = all channels used
		 *  - RGB = alpha channel undefined
		 *  - LA = green and blue undefined, red = luminance
		 *  - L = green, blue and alpha undefined, red = luminance
		 * @param x the x coordinate
		 * @param y the y coordinate
		 * @return the color value as float
		 */
		virtual ColorRGBAF32 SampleAsClampedFloat(const int32 x, const int32 y) const = 0;

		/**
		 * Samples the pixel value with nearest filtering and returns the result as float color.
		 * @note If the component type is not float it will be converted and unused channels will be ignored.
		 *  - RGBA = all channels used
		 *  - RGB = alpha channel undefined
		 *  - LA = green and blue undefined, red = luminance
		 *  - L = green, blue and alpha undefined, red = luminance
		 * @param uv the uv coordinate
		 * @return the color value as float
		 */
		virtual ColorRGBAF32 SampleNearestInterpolatedFloat(const GraphVec2F& uv) const = 0;

		/**
		 * Samples the pixel value with bilinear(2x2) filtering and returns the result as float color.
		 * @note If the component type is not float it will be converted and unused channels will be ignored.
		 *  - RGBA = all channels used
		 *  - RGB = alpha channel undefined
		 *  - LA = green and blue undefined, red = luminance
		 *  - L = green, blue and alpha undefined, red = luminance
		 * @param uv the uv coordinate
		 * @return the color value as float
		 */
		virtual ColorRGBAF32 SampleBilinearInterpolatedFloat(const GraphVec2F& uv) const = 0;

		/**
		 * Samples the pixel value with bicubic(4x4) filtering and returns the result as float color.
		 * @note This method has a potentially high processing cost due to 16 texture reads per invocation.
		 * @note If the component type is not float it will be converted and unused channels will be ignored.
		 *  - RGBA = all channels used
		 *  - RGB = alpha channel undefined
		 *  - LA = green and blue undefined, red = luminance
		 *  - L = green, blue and alpha undefined, red = luminance
		 * @param uv the uv coordinate
		 * @return the color value as float
		 */
		virtual ColorRGBAF32 SampleBicubicInterpolatedFloat(const GraphVec2F& uv) const = 0;

		/**
		 * Samples the pixel value with specified filtering and returns the result as float color.
		 * @note If the component type is not float it will be converted and unused channels will be ignored.
		 *  - RGBA = all channels used
		 *  - RGB = alpha channel undefined
		 *  - LA = green and blue undefined, red = luminance
		 *  - L = green, blue and alpha undefined, red = luminance
		 * @param uv the uv coordinate
		 * @return the color value as float
		 */
		virtual ColorRGBAF32 SampleInterpolatedFloat(const FilterType filter, const GraphVec2F& uv) const = 0;

		/**
		 * Fills the specified channel of this texture page with the
		 * channel of the specified value.
		 * @note the color will be converted to match the pixel specification.
		 *
		 * @param index the channel index
		 * @param value the value
		 */
		virtual void FillChannel(const uint32 index, const ColorRGBAF32& value) = 0;

		/**
		 * Fills all channels of this texture page with the specified color.
		 * @note the color will be converted to match the pixel specification.
		 *
		 * @param value fill value
		 */
		virtual void Fill(const ColorRGBAF32& value) = 0;

		/**
		 * Sets all bytes of the data storage to 0.
		 */
		virtual void Clear() = 0;

		/**
		 * Writes the contents of this image to disk as PNG.
		 * @note If the output bits per pixel component value is lower than the bits per pixel component of
		 * the texture page. The resulting PNG will can be dithered during downsampling to
		 * improve the image quality by avoiding banding by enabling allowDithering.
		 *
		 * @param path the output path for the image file
		 * @param bitsPerComponent bits per pixel component of the output image, must be either 8 or 16.
		 * @param allowDithering enables dithering when downsampling on export
		 * @return true if successful
		 */
		virtual bool WritePNG(const char* path, const uint32 bitsPerComponent, const bool allowDithering) const = 0;

		/**
		 * Writes the contents of this image to disk as BMP.
		 * @note If the output bits (8) per pixel component value is lower than the bits per pixel component of
		 * the texture page. The resulting PNG will can be dithered during downsampling to
		 * improve the image quality by avoiding banding by enabling allowDithering.
		 *
		 * @param path the output path for the image file
		 * @param allowDithering enables dithering when downsampling on export
		 * @return true if successful
		 */
		virtual bool WriteBMP(const char* path, const bool allowDithering) const = 0;

		/**
		 * Writes the contents of this image to disk as TGA.
		 * @note If the output bits (8) per pixel component value is lower than the bits per pixel component of
		 * the texture page. The resulting PNG will can be dithered during downsampling to
		 * improve the image quality by avoiding banding by enabling allowDithering.
		 *
		 * @param path the output path for the image file
		 * @param allowDithering enables dithering when downsampling on export
		 * @return true if successful
		 */
		virtual bool WriteTGA(const char* path, const bool allowDithering) const = 0;

		/**
		 * Writes the contents of this image to disk as 32-bit linear HDR.
		 *
		 * @param path the output path for the image file
		 * @return true if successful
		 */
		virtual bool WriteHDR(const char* path) const = 0;

		/**
		 * Writes the contents of this image to disk as 8-bit JPEG.
		 *
		 * @param path the output path for the image file
		 * @param quality the JPG quality [0...100]
		 * @param allowDithering enables dithering when downsampling on export
		 * @return true if successful
		 */
		virtual bool WriteJPEG(const char* path, int quality, bool allowDithering) const = 0;

		/**
		 * Writes the contents of this image to disk as 16 or 32-bit linear EXR.
		 *
		 * @param path the output path for the image file
		 * @param bitsPerComponent bits per pixel component of the output image, must be either 16 (half-float) or 32 (float)
		 * @return true if successful
		 */
		virtual bool WriteEXR(const char* path, const uint32 bitsPerComponent) const = 0;

		/**
		 * Writes the contents of a texture page to disk as 8bit, 16bit or 32-bit TIFF.
		 * Additionally the TIFF can be compressed.
		 *
		 * @note With texture pages with pixel types IInstaLODTexturePage::PixelTypeRGBA and IInstaLODTexturePage::PixelTypeLuminanceAlpha,
		 * setting isAlphaPremultiplied to true will result in image data written and subsequently read as-is, setting isAlphaPremultiplied
		 * to false may lead to libTIFF later transforming the image data read from the file and filling the texture page with the modified data.
		 *
		 * @note When compression type is CompressionTypeJPEG, \p outputComponentType must be set to IInstaLODTexturePage::ComponentTypeUInt8,
		 * any other component type will result in false returned and no file created on disk.
		 *
		 * @param path the output path for the image file
		 * @param outputComponentType the output component type
		 * @param compressionType the compression type to use
		 * @param isAlphaPremultiplied sets aplha as premultipled in the resulting file
		 * @param allowDithering enables dithering when downsampling on export
		 * @return true if successful
		 */
		virtual bool WriteTIFF(const char* const path, const ComponentType outputComponentType, const CompressionType compressionType, const bool isAlphaPremultiplied, const bool allowDithering) const = 0;

		/**
		 * Resizes this instance to the specified size using bilinear filtering.
		 *
		 * @param width the width
		 * @param height the height
		 */
		virtual void ResizeBicubic(const uint32 width, const uint32 height) = 0;

		/**
		 * Resizes this instance to the specified size using parallel bilinear filtering.
		 *
		 * @param width the width
		 * @param height the height
		 */
		virtual void ResizeBilinearParallel(const uint32 width, const uint32 height) = 0;

		typedef void (*pfnEvaluateFloatPixelParallel)(const uint32 x, const uint32 y, const ColorRGBAF32& color, uint32 taskID);
		/**
		 * Evaluates and samples all pixels of the image in a parallel manner.
		 *
		 * @param delegate A delegate that evaluates the current sampled pixel, it also return the ID of the task that executes it.
		 */
		virtual void EvaluateFloatParallel(const pfnEvaluateFloatPixelParallel delegate) const = 0;

		typedef ColorRGBAF32 (*pfnProcessPixelParallel)(const uint32 x, const uint32 y, void* userPointer);

		/**
		 * Processes all pixels of the image in a parallel manner.
		 *
		 * @param taskCount Number of parallel tasks to execute.
		 * @param delegate A delegate that returns a 32-bit float color for the current pixel.
		 * @param userPointer A data pointer that is called with the delegate.
		 */
		virtual void ProcessFloatParallel(const pfnProcessPixelParallel delegate, void* userPointer) = 0;

		/**
		 * Copies the contents from the specified into this instance.
		 * @note If the image dimensions or format does not match
		 * the image will be resampled to fit into this instance.
		 *
		 * @param page The source page.
		 * @return true upon success.
		 */
		virtual bool CopyContentsFrom(const IImageSampler* page) = 0;

		/**
		 * Swaps the underyling data buffers between both images.
		 * @note This method only works if the dimensions and pixel types are identical matches.
		 * This method is useful if a front/backbuffer iteration is used.
		 *
		 * @param rhs The other image sampler.
		 * @return true upon success.
		 */
		virtual bool Swap(IImageSampler* rhs) = 0;
	};

	/**
	 * The ElementExecutionFormat enum represents different format types.
	 */
	namespace ElementExecutionFormat
	{
		enum Type
		{
			Normalized8,										/**< 8 bits normalized per component.*/
			Normalized16,										/**< 16 bits normalized per component.*/
			FullRange16,										/**< 16 bits full range per component.*/
			FullRange32,										/**< 32 bits full range per component.*/
			Inherit,											/**< Inherit the format from the parent. */
			DeriveFromFirstAvailableImageInput,					/**< Inherit both size and execution format from the first available image input */
			InheritFormatDeriveSizeFromFirstAvailableImageInput /**< Inherit format from parent, size from first available image input. */
		};
	} // namespace ElementExecutionFormat

	/**
	 * The ElementExecutionFlags enum represents different IElementExecution flags.
	 */
	namespace ElementExecutionFlags
	{
		enum Type
		{
			None = 0u,					   /**< No flags.*/
			GrayscalePermutation = 1u << 0 /**< Execute .*/
		};
	} // namespace ElementExecutionFlags

	/**
	 * The IElementExecution provides a convenient interface for the execution of Atom, ElementGraphs and Layering projects.
	 * To execute an element and generate its outputs:
	 *
	 *   1. Allocate an IElementExecution instance using the IInstaMAT class.
	 *   2. Create an instance of a class object using CreateInstance.
	 *   3. Set the execution format using SetFormat
	 *   4. Setup the parameter input values using the corresponding setters.
	 *   5. Execute the graph
	 *   6. Use AllocImageSamplerForOutputParameter to generate an IImageSampler for the composition graph image outputs of the executed instance.
	 *   7. Write the images to disk or access the raw bytes using the IImageSampler.
	 *
	 * @note 
	 * Every `IElementExecution` instance must be deallocated on the thread where the SDK backend has been initalized.
	 */
	class INSTAMAT_EXPORTEDCLASS IElementExecution
	{
	protected:
		virtual ~IElementExecution() {}

		typedef bool (*pfnProgressDelegate)(const IGraph& graph, const float progress);

	public:
		/**
		 * Sets the execution format of this instance.
		 * @note It is possible to change the format after execution and call Execute again.
		 * @note This method may only be invoked on the thread that initalized the SDK.
		 * @note All previously generated data will be invalidated upon calling SetFormat.
		 * @note Calling SetFormat may invalidate previously set resources that are persisted in the execution state.
		 * Such parameters may include custom image inputs or mesh inputs but may extend to other resources.
		 * It's best to first set the format and the pass custom data to the graph input parameters.
		 * @note The `format` should be kept at `Normalized16` for most purposes. Changing may cause side-effects and degradation.
		 *
		 * @param width The width.
		 * @param height The height.
		 * @param format The ElementExecutionFormat (Default: Normalized16)
		 * @return true upon success.
		 */
		virtual bool SetFormat(const uint32 width, const uint32 height, ElementExecutionFormat::Type format = ElementExecutionFormat::Normalized16) = 0;

		/**
		 * Creates an instance of the specified \p classObject for execution.
		 *
		 * @note The API can cause a lazy graph to be resolved @see IGraph::Resolve for more information.
		 *
		 * @param classObject The graph.
		 * @param flags The execution flags.
		 * @return true upon success.
		 */
		virtual bool CreateInstance(const IGraph& classObject, const ElementExecutionFlags::Type flags) = 0;

		/**
		 * Creates an instance of the specified \p templateObject for execution.
		 *
		 * @note The API can cause a lazy graph to be resolved @see IGraph::Resolve for more information.
		 *
		 * @param templateObject The template.
		 * @param flags The execution flags.
		 * @return true upon success.
		 */
		virtual bool CreateInstance(const IGraphTemplate& templateObject, const ElementExecutionFlags::Type flags) = 0;

		/**
		 * Gets the output composition graph node for the specified \p variable.
		 * @note The output composition node can be used to apply a transform or to remap output values
		 * such as from roughness to smoothness. Or to convert between OpenGL and DirectX normal maps.
		 * The level adjustment node is also used to perform downsampling of the format such as converting 16-bit to 8-bit outputs
		 * or to downsample the output resolution.
		 * The input parameters can be tweaked in identical fashion to the ElementExecution's instance to fit to your needs.
		 * @note Query the composition graph input parameters to see the available features.
		 * To do this, it's best to inspect the variable name and category as features might be added in newer SDK versions.
		 *
		 * @param variable The variable.
		 * @return The output composition graph node or nullptr if not available.
		 */
		virtual IGraph* GetCompositionGraphForOutputParameter(const IGraphVariable& variable) = 0;

		/**
		 * Changes the execution format for the output composition of the specified \p variable.
		 * @note The output composition node can be used to remap output values
		 * such as from roughness to smoothness. Or to convert between OpenGL and DirectX normal maps.
		 * The composition graph node is also used to perform downsampling of the format such as converting 16-bit to 8-bit outputs
		 * or to downsample the output resolution.
		 * The input parameters can be tweaked in identical fashion to the ElementExecution's instance to fit to your needs.
		 * @note The \p width and \p height apply a shift of the input image resolution. If the value is positive
		 * the shift is to the left (increases resolution) if the value is negative the shift is to the right (decreases resolution).
		 * @note To keep using the same execution format as the ElementExecution but change the resolution set the ElementExecutionFormat to Inherit.
		 *
		 * @param variable The variable.
		 * @param width The width shift
		 * @param height The height shift
		 * @param format The execution format.
		 * @return true upon success.
		 */
		virtual bool SetCompositionGraphFormatForOutputParameter(const IGraphVariable& variable, const int32 width, const int32 height, ElementExecutionFormat::Type format) = 0;

		/**
		 * Gets the final composition output for the specified \p variable.
		 * @note This is the output that is used when writing or accessing the result of an execution
		 * as the actual instance outputs is potentially further transformed by its output composition node.
		 *
		 * @param variable
		 * @return The composition output or nullptr if variable is not an instance output.
		 */
		virtual IGraphVariable* GetCompositionGraphOutputForOutputParameter(const IGraphVariable& variable) = 0;

		/**
		 * Gets the current instance.
		 * @note The instance must be used to access input parameters that should be set.
		 * Or when accessing the generated output image.
		 *
		 * @return The instance or null if not available.
		 */
		virtual IGraph* GetInstance() = 0;

		/**
		 * Gets the instance seed.
		 *
		 * @return The instance seed or ~0u if the Instance is not available.
		 */
		virtual uint32 GetInstanceSeed() const = 0;

		/**
		 * Sets the instance seed to \p seed.
		 *
		 * @return True upon success.
		 */
		virtual bool SetInstanceSeed(const uint32 seed) = 0;

		/**
		 * Resets the input parameter \p variable to its default value.
		 *
		 * @return True upon success.
		 */
		virtual bool ResetInputParameter(IGraphVariable& variable) = 0;

		/**
		 * Sets the \p value for the specified \p variable.
		 * @note Variables should be input parameters of the instance created with a call to CreateInstance.
		 * While other variable values can be set, it has no effect and will consume memory.
		 * @note This function will return false if the \p variable type does not support the value.
		 * @note Arithmetic values are also used for ElementImage inputs when no image resource has been set.
		 * In this case, the Vector4FValue field should be used to set a default RGBA color.
		 * The color values needs to be specified in the colorspace of the parameter.
		 * If the colors are specified in sRGB colorspace, but a linear colorspace is set on the parameter, set the \p sRGB to true to convert to linear.
		 * For the type ElementImageGray it is enough to set the Float32Value field and assume a linear colorspace.
		 *
		 * @param variable The input parameter variable.
		 * @param value The value.
		 * @param sRGB True if the color values should be converted from sRGB to linear colorspace.
		 * @return true upon success.
		 */
		virtual bool SetArithmeticValueForInputParameter(IGraphVariable& variable, const ArithmeticGraphValue& value, const bool sRGB) = 0;

		/**
		 * Sets the \p value for the specified \p variable.
		 * @note Variables should be input parameters of the instance created with a call to CreateInstance.
		 * While other variable values can be set, it has no effect and will consume memory.
		 * @note This function will return false if the \p variable type does not support the value.
		 *
		 * @param variable The input parameter variable.
		 * @param value The value.
		 * @return true upon success.
		 */
		virtual bool SetEnumValueForInputParameter(IGraphVariable& variable, const uint32 value) = 0;

		/**
		 * Sets the \p value for the specified \p variable.
		 * @note Variables should be input parameters of the instance created with a call to CreateInstance.
		 * While other variable values can be set, it has no effect and will consume memory.
		 * @note This function will return false if the \p variable type does not support the value.
		 *
		 * @param variable The input parameter variable.
		 * @param value The value.
		 * @return true upon success.
		 */
		virtual bool SetStringValueForInputParameter(IGraphVariable& variable, const char* value) = 0;

		/**
		 * Sets the specified generic resource for the specified \p variable.
		 * @note Variables should be input parameters of the instance created with a call to CreateInstance.
		 * While other variable values can be set, it has no effect and will consume memory.
		 * @note The resource will be stored in a temporary package.
		 * @note This function will return false if the \p variable type does not support the value.
		 *
		 * @param variable The input parameter variable.
		 * @param filename The filename of the resource that will be used for the GraphPackageResource in the temporary package.
		 * @param data The data of the resource.
		 * @param dataSize The size in bytes of the \p data.
		 * @return true upon success.
		 */
		virtual bool SetResourceForGraphVariable(IGraphVariable& variable, const char* filename, const void* data, const uint64 dataSize) = 0;

		/**
		 * Sets the specified \p resourceURL for the specified \p variable.
		 * @note Variables should be input parameters of the instance created with a call to CreateInstance.
		 * While other variable values can be set, it has no effect and will consume memory.
		 * @note This function will return false if the \p variable type does not support the value.
		 *
		 * @param variable The input parameter variable.
		 * @param resourceURL The graph resource URL - either package local (pkg:) or file system absolute (file:).
		 * @return true upon success.
		 */
		virtual bool SetResourceURLForGraphVariable(IGraphVariable& variable, const char* resourceURL) = 0;

		/**
		 * Sets the \p sampler for the specified \p variable.
		 * @note Variables should be input parameters of the instance created with a call to CreateInstance.
		 * While other variable values can be set, it has no effect and will consume memory.
		 * @note The image sampler will be uploaded to the GPU with the exact format and size of the \p sampler.
		 * @note This function will return false if the \p variable type does not support the value.
		 * @note This method may only be invoked on the thread that initalized the SDK.
		 *
		 * @param variable The input parameter variable.
		 * @param sampler The Image Sampler.
		 * @param sRGB True if the contents of the image sampler are in the sRGB color space.
		 * @return true upon success.
		 */
		virtual bool SetImageSamplerForInputParameter(IGraphVariable& variable, IImageSampler& sampler, const bool sRGB) = 0;

		/**
		 * Sets the \p mesh for the specified \p variable.
		 * @note Variables should be input parameters of the instance created with a call to CreateInstance.
		 * While other variable values can be set, it has no effect and will consume memory.
		 * @note The mesh does not need to be constructed through the InstaMAT API.
		 * It is acceptable to implement the light-weight IGraphMesh interface and pass
		 * the custom class to this function.
		 * @note This function will return false if the \p variable type does not support the value.
		 * @note This method may only be invoked on the thread that initalized the SDK.
		 *
		 * @param variable The input parameter variable.
		 * @param mesh The mesh.
		 * @return true upon success.
		 */
		virtual bool SetMeshForInputParameter(IGraphVariable& variable, IGraphMesh& mesh) = 0;

		/**
		 * Allocates an IImageSampler for the specified \p variable.
		 * @note The Image Sampler must be deallocated manually through the IInstaMAT class.
		 * @note This method may only be invoked on the thread that initalized the SDK.
		 *
		 * @param variable The input parameter variable.
		 * @param sRGB True if the image sampler contents should be in sRGB color space.
		 * 				Use the IGraphVariable::GetColorSpaceTypeValue API to determine if the output assumes sRGB color space.
		 * 				If the output colorspace is sRGB, this value should be set to true in most cases.
		 * 				@note If the output parametern \p variable is owned by a CompositionGraph,
		 * 				then the output is already colorspaced and the parameter \p sRGB should be set to false.
		 * @return IImageSampler upon success or nullptr if failed.
		 */
		virtual IImageSampler* AllocImageSamplerForOutputParameter(const IGraphVariable& variable, const bool sRGB) = 0;

		/**
		 * Retrieves the graph mesh for the specified \p variable.
		 * @note This method may only be invoked on the thread that initalized the SDK.
		 *
		 * @param variable The input parameter variable.
		 * @return IGraphMesh upon success or nullptr if failed.
		 */
		virtual const IGraphMesh* GetMeshForOutputParameter(const IGraphVariable& variable) = 0;

		/**
		 * Writes the mesh for the specified graph \p variable to the disk.
		 * @note This function may only be invoked if the backend context is current on the current thread.
		 * @note Currently supported fileformats include: OBJ, FBX, ILME
		 *
		 * @param variable The variable.
		 * @param path The output path.
		 * @return true upon success
		 */
		virtual bool WriteMeshForGraphVariableToDisk(const IGraphVariable& variable, const char* path) = 0;

		/**
		 * Retrieves the point cloud for the specified \p variable.
		 * @note This method may only be invoked on the thread that initalized the SDK.
		 *
		 * @param variable The input parameter variable.
		 * @return IGraphPointCloud upon success or nullptr if failed.
		 */
		virtual const IGraphPointCloud* GetPointCloudForOutputParameter(const IGraphVariable& variable) = 0;

		/**
		 * Writes the point cloud for the specified graph \p variable to the disk.
		 * @note This function may only be invoked if the backend context is current on the current thread.
		 * @note Currently supported fileformats include: PLY
		 *
		 * @param variable The variable.
		 * @param path The output path.
		 * @return true upon success
		 */
		virtual bool WritePointCloudForGraphVariableToDisk(const IGraphVariable& variable, const char* path) = 0;

		/**
		 * Retrieves the scene for the specified \p variable.
		 * @note This method may only be invoked on the thread that initalized the SDK.
		 *
		 * @param variable The input parameter variable.
		 * @return IGraphScene upon success or nullptr if failed.
		 */
		virtual const IGraphScene* GetSceneForOutputParameter(const IGraphVariable& variable) = 0;

		/**
		 * Writes the scene for the specified graph \p variable to the disk.
		 * @note This function may only be invoked if the backend context is current on the current thread.
		 * @note For currently supported fileformats @see WriteMeshForGraphVariableToDisk
		 *
		 * @param variable The variable.
		 * @param path The output path including the file format extension, e.g. `export.usdz`.
		 * @param imageExtension The image format used when writing scene textures, e.g. `.png`
		 * @param bitsPerComponent The maximum bits per component when exporting images. Set `0u` to use the bits of the image in the scene.
		 * @param allowDithering True to dither the image when downsampling image BPP.
		 * @return true upon success
		 */
		virtual bool WriteSceneForGraphVariableToDisk(const IGraphVariable& variable, const char* path, const char* imageExtension,
													  const uint32 bitsPerComponent, const bool allowDithering) = 0;

		/**
		 * Retrieves the ArithmeticGraphValue for the specified \p variable.
		 * @note This method may only be invoked on the thread that initalized the SDK.
		 *
		 * @param variable The input parameter variable.
		 * @param outValue The output storage for the ArithmeticGraphValue.
		 * @return true upon success or nullptr if failed.
		 */
		virtual bool GetArithmeticGraphValueOutputParameter(const IGraphVariable& variable, ArithmeticGraphValue& outValue) = 0;

		/**
		 * Executes this instance.
		 * @note Progress is reported to the \p progressDelegate.
		 * If the \p progressDelegate returns false, the execution is aborted.
		 * @note This method may only be invoked on the thread that initalized the SDK.
		 *
		 * @param progressDelegate (optional) The progress delegate.
		 * @return true upon success.
		 */
		virtual bool Execute(pfnProgressDelegate progressDelegate) = 0;

		/**
		 * Exports the previoulsy executed instance using the specified \p templateJson to \p outputPathFormat.
		 * @note Templates can be created using the InstaMAT standalone app.
		 *
		 * @param templateJson A string containing the JSON of the template
		 * @param outputPathFormat The output path format string. The format string supports the same tokens as MAT and Pipeline.
		 * @param imageFileExtension The image file extension e.g. "PNG" or "TIFF"
		 * @param meshFileExtension The mesh file extension e.g. "FBX
		 * @param dilationInPercent The dilation in percent [0...1].
		 * @param diffusionIntensity The diffusion in percent [0...1].
		 * @return true upon success
		 */
		virtual bool ExportWithTemplate(const char* templateJson, const char* outputPathFormat,
										const char* imageFileExtension, const char* meshFileExtension, float dilationInPercent, float diffusionIntensity) = 0;

		/**
		 * Exports the previoulsy executed instance using the template JSON located at \p path to \p outputPathFormat.
		 * @note Templates can be created using the InstaMAT standalone app.
		 *
		 * @param templateJson A string containing the JSON of the template
		 * @param outputPathFormat The output path format string. The format string supports the same tokens as MAT and Pipeline.
		 * @param imageFileExtension The image file extension e.g. "PNG" or "TIFF"
		 * @param meshFileExtension The mesh file extension e.g. "FBX
		 * @param dilationInPercent The dilation in percent [0...1].
		 * @param diffusionIntensity The diffusion in percent [0...1].
		 * @return true upon success
		 */
		virtual bool ExportWithTemplateAtPath(const char* path, const char* outputPathFormat,
											  const char* imageFileExtension, const char* meshFileExtension, float dilationInPercent, float diffusionIntensity) = 0;
	};

	/** The InstaMATThread task callback with optional \p userData. */
	typedef bool (*pfnThreadCallback)(void* userData);

	/**
	 * The IInstaMATThread class implements a task processing that is run on a dedicated thread.
	 * @note This is a helper class to aid with the implementation of asynchronous graph executions.
	 * The backend thread needs to be initialized on the thread, that is used for graph execution.
	 */
	class IInstaMATThread
	{
	protected:
		virtual ~IInstaMATThread() {}

	public:
		/**
		 * Starts the thread.
		 *
		 * @return true upon success
		 */
		virtual bool Start() = 0;

		/**
		 * Stops the thread.
		 * @note The thread will not stop immediately. Instead an abort signal is posted
		 * and the calling thread waits until the task thread exited.
		 * If you just want to post the abort signal use \# RequestShutdown instead.
		 *
		 * @return true upon success.
		 */
		virtual bool Stop() = 0;

		/**
		 * Requests the task thread to shutdown.
		 *
		 * @return true upon success.
		 */
		virtual bool RequestShutdown() = 0;

		/**
		 * Determines if a shutdown is requested.
		 *
		 * @return true if a shutdown is requested.
		 */
		virtual bool IsShutdownRequested() const = 0;

		/**
		 * Determines if the task thread is currently processing a task.
		 *
		 * @return true if processing.
		 */
		virtual bool IsBusy() const = 0;

		/**
		 * Determines the number of tasks in the queue.
		 *
		 * @return The number of tasks in the queue.
		 */
		virtual uint32 GetTaskCount() const = 0;

		/**
		 * Controls logging behavior.
		 * @note Logging to stdout/err is enabled by default.
		 *
		 * @param isEnabled True to enable logging.
		 */
		virtual void SetLoggingEnabled(bool isEnabled) = 0;

		/**
		 * Creates a task with \p callback.
		 * @note The task will be added to the queue and the task thread will be
		 * notified to begin processing.
		 *
		 * @param callback The task callback.
		 * @param userData The task user data.
		 * @param tag The task tag.
		 */
		virtual void AddTask(const pfnThreadCallback callback, void* userData, const char* tag) = 0;

		/**
		 * Creates a task with \p callback and waits for its completion.
		 * @note The task will be added to the queue and the task thread will be
		 * notified to begin processing.
		 *
		 * @param callback The task callback.
		 * @param userData The task user data.
		 * @param tag The task tag.
		 */
		virtual bool AddTaskAndWait(const pfnThreadCallback callback, void* userData, const char* tag) = 0;
	};

	/**
	 * The IInstaMAT interface is the main API for the InstaMAT SDK.
	 * @note Invoke GetInstaMAT to allocate an instance that implements this interface.
	 * @note Use the authorization methods to make sure the SDK is properly configured prior to using it.
	 */
	class INSTAMAT_EXPORTEDCLASS IInstaMAT
	{
	protected:
		virtual ~IInstaMAT() {}

	public:
		/**
		 * The BackendType enum represents different types of backends to be used for graph/project execution.
		 */
		enum BackendType
		{
			BackendTypeNone, /**< No backend is available. Only the GSL will be available. */
			BackendTypeGPU,	 /**< The GPU accelerate backend. */
			BackendTypeCPU	 /**< The CPU backend. @note Currently only available on Windows. */
		};

		/**
		 * The BackendFlags bit enum represents different types of initialization modes for the backend execution.
		 */
		enum BackendFlags
		{
			BackendFlagNone = 0u,
			BackendFlagNoPlugins = 1u << 0
		};

		/**
		 * Returns the SDK version.
		 *
		 * @return the SDK version.
		 */
		virtual int32 GetVersion() const = 0;

		/**
		 * Returns the SDK build date as string.
		 *
		 * @return the SDK build date.
		 */
		virtual const char* GetBuildDate() const = 0;

		/**
		 * Initializes the SDK.
		 * @note The call to Initialize must be executed on the host-application's thread that
		 * will be used to perform graph executions, s the GPU execution engines will be initialized.
		 * The execution of this call can take several seconds as the GSL will be initialized.
		 * Once the SDK has been initialized, custom paths can be registered.
		 * @note The SDK must be fully authorized before the initilization of the SDK can be done.
		 *
		 * @param initializeBackend True to initialize the execution backend. If false, only the GSL will be available.
		 * @param flags The BackendFlags used to initialize the backend.
		 * @return true upon success.
		 */
		virtual bool Initialize(const BackendType backendType, const BackendFlags flags) = 0;

		/**
		 * Performs a shutdown of the execution engine.
		 * @note This API is intended to allow a graceful shutdown of the GPU context on the
		 * thread that was used to create it.
		 *
		 * @return true upon success.
		 */
		virtual bool ShutdownBackend() = 0;

		/**
		 * Shuts down the SDK.
		 */
		virtual void Dealloc() = 0;

		/**
		 * Loads the package from the specified \p filePath into the library.
		 * @note Before loading external packages, the library should be fully loaded into memory with calls to LoadPackagesAtPath
		 * and LoadPackage.
		 * @note Once a package has been allocate, the objects can be iterated with a call to GetGraphObjectsInPackage.
		 *
		 * @param filePath				The file path to load the package from.
		 * @param persistentResources	True to keep resources in memory after loading packag information. This should be active for all library packages.
		 * 								Otherwise, browsing the library might cause hits to the disk.
		 * @return The loaded graph package or nullptr upon failutre.
		 */
		virtual IGraphPackage* AllocPackageFromFile(const char* filePath, bool persistentResources) = 0;

		/**
		 * Deallocates the \p package.
		 *
		 * @param package The package.
		 * @return true upon success
		 */
		virtual bool DeallocPackage(IGraphPackage* package) = 0;

		/**
		 * Loads the package from the specified \p filePath into the library.
		 *
		 * @note The API can cause a lazy graph to be resolved @see IGraph::Resolve for more information.
		 *
		 * @param filePath				The file path to load the package from.
		 * @param persistentResources	True to keep resources in memory after loading package information. This should be active for all library packages.
		 * 								Otherwise, browsing the library might cause hits to the disk.
		 * @param systemLibrary			True when loading the system library, e.g. from the InstaMAT Studio folder.
		 * @return True upon success.
		 */
		virtual bool LoadPackage(const char* filePath, bool persistentResources, bool systemLibrary) = 0;

		/**
		 * Loads the packages from the specified path into the library.
		 * @note This functions also loads all packages from sub-directories.
		 *
		 * @param path					The path to load packages from.
		 * @param persistentResources	True to keep resources in memory after loading package information. This should be active for all library packages.
		 *								Otherwise, browsing the library might cause hits to the disk.
		 * @param systemLibrary			True when loading the system library, e.g. from the InstaMAT Studio folder.
		 * @return The number of packages loaded.
		 */
		virtual uint32 LoadPackagesAtPath(const char* path, bool persistentResources, bool systemLibrary) = 0;

		/**
         * Loads the external assets from the specified path into the library.
         * @note This functions also loads all assets from sub-directories.
         *
         * @param path The path to load external assets from.
         * @return The number of packages loaded.
         */
		virtual uint32 RegisterExternalAssetFolder(const char* path) = 0;

		/**
         * Unregisters all external asset folders and unloads the assets.
         * @note To refresh a folder, call this API and then re-register the folder.
         */
		virtual void UnregisterExternalAssetFolders() = 0;

		/**
		 * Gets the Graph class-object with the specified \p GraphID string.
		 * @note If the graph object is a LazyGraph, the pointer will become invalid
		 * once the graph is resolved.
		 *
		 * @param graphID The GraphID string.
		 * @return The GraphObject or null if not found.
		 */
		virtual const IGraphObject* GetGraphObjectByID(const char* graphID) = 0;

		/**
		 * Searches for the Graph with the specified \p name.
		 * @note This function can be used to iterate on all objects with the same \p name
		 * simply increment the \p index until the method returns null.
		 * @note If the graph object is a LazyGraph, the pointer will become invalid
		 * once the graph is resolved.
		 *
		 * @param name	The object name.
		 * @param index The index, if multiple objects with the same name exist.
		 * @return The GraphObject or null if not found.
		 */
		virtual IGraph* GetGraphByName(const char* name, const uint32 index) const = 0;

		/**
		 * Gets an array of all categories in the library.
		 * @note The array can be iterated until an nullptr item is iterated.
		 * @note The array will remain valid for the calling thread until the method
		 * is called again by the same thread.
		 *
		 * @param [out] outCategories An array of strings.
		 * @return true upon success.
		 */
		virtual bool GetCategories(const char*** outCategories) const = 0;

		/**
		 * Gets an array of all GraphObjects in the \p category.
		 * @note The array can be iterated until a nullptr item is iterated.
		 * @note The array will remain valid for the calling thread until the method
		 * is called again by the same thread.
		 * @note If the \p outGraphObjects contains a LazyGraph, the pointer will become invalid
		 * once the graph is resolved. It's important to note, that resolving a LazyGraph might
		 * result in resolving hundreds of other LazyGraphs that need to be resolved indirectly
		 * due to being a dependency. If you want to hold on to the list for longer,
		 * make sure to copy the data that is important to your application to a different
		 * struct.  @see IGraph::Resolve for more information.
		 *
		 * @param category					The category.
		 * @param [out] outGraphObjects		An array of IGraphObjects.
		 * @return true upon success
		 */
		virtual bool GetGraphObjectsInCategory(const char* category, IGraphObject*** outGraphObjects) = 0;

		/**
		 * Gets an array of all GraphObjects in the \p package.
		 * @note The array can be iterated until a nullptr item is iterated.
		 * @note The array will remain valid for the calling thread until the method
		 * is called again by the same thread.
		 * @note If the \p outGraphObjects contains a LazyGraph, the pointer will become invalid
		 * once the graph is resolved. It's important to note, that resolving a LazyGraph might
		 * result in resolving hundreds of other LazyGraphs that need to be resolved indirectly
		 * due to being a dependency. If you want to hold on to the list for longer,
		 * make sure to copy the data that is important to your application to a different
		 * struct. @see IGraph::Resolve for more information.
		 *
		 * @param package					The package.
		 * @param [out] outGraphObjects		An array of IGraphObjects.
		 * @return true upon success
		 */
		virtual bool GetGraphObjectsInPackage(const IGraphPackage& package, IGraphObject*** outGraphObjects) = 0;

		/**
		 * Initializes the preview generator.
		 * @note This method must be invoked from the thread where the SDK backend has been initalized.
		 *
		 * @return True upon success or false upon failure or if already initialized.
		 */
		virtual bool InitializePreviewGenerator() = 0;

		/**
		 * Generates the preview for the specified \p graphObject.
		 * @note This method must be invoked from the thread where the SDK backend has been initalized.
		 * @note The first call to this function will initialized the preview generator and may take an
		 * extended time until the control is returned back to the caller.
		 * @note To initialize the PreviewGenerator call InitializePreviewGenerator.
		 * @note The preview generation will commence immediately.
		 * @note This APIs can cause a lazy graph to be resolved @see IGraph::Resolve for more information.
		 *
		 * @param graphObject The graph object.
		 * @return The IImageSampler object for the preview or nullptr upon failure.
		 */
		virtual IImageSampler* GeneratePreviewForGraphObject(const IGraphObject& graphObject) = 0;

		/**
		 * Allocates an element execution instance.
		 * @note The allocated instance must be freed on the thread where the SDK backend has been initalized
		 * by a call to `DeallocElementExecution`.
		 *
		 * @return The element execution or nullptr if allocation failed.
		 */
		virtual IElementExecution* AllocElementExecution() = 0;

		/**
		 * Deallocates the \p elementExecution.
		 * @note This method must be invoked from the thread where the SDK backend has been initalized.
		 *
		 * @param elementExecution
		 * @return true upon success
		 */
		virtual bool DeallocElementExecution(IElementExecution* elementExecution) = 0;

		/**
		 * Allocates a task thread.
		 *
		 * @return The task thread or nullptr if allocation failed.
		 */
		virtual IInstaMATThread* AllocTaskThread() = 0;

		/**
		 * Deallocates the task \p thread.
		 *
		 * @param thread The thread.
		 * @return true upon success
		 */
		virtual bool DeallocTaskThread(IInstaMATThread* thread) = 0;

		/**
		 * Registers the specified \p plugin.
		 * @note The plugin must stay valid until it is unregistered with a call to UnregisterElementEntityPlugin
		 * @note This APIs can cause a lazy graph to be resolved @see IGraph::Resolve for more information.
		 *
		 * @param plugin The plugin.
		 * @return true upon success.
		 */
		virtual bool RegisterElementEntityPlugin(IInstaMATElementEntityPlugin& plugin) = 0;

		/**
		 * Unregisters the specified \p plugin.
		 * @note This method should be called in the Shutdown method of your plugin.
		 *
		 * @param plugin The plugin.
		 * @return true upon success.
		 */
		virtual bool UnregisterElementEntityPlugin(IInstaMATElementEntityPlugin& plugin) = 0;

		/**
		 * Allocates the image sampler for the specified \p componentType and \p pixelType.
		 *
		 * @param componentType
		 * @param pixelType
		 * @return The ImageSampler or nullptr if allocation failed.
		 */
		virtual IImageSampler* AllocImageSamplerForType(IImageSampler::ComponentType componentType, IImageSampler::PixelType pixelType) = 0;

		/**
		 * Allocates the image sampler for the specified \p path.
		 * @note Compressed image types such as PNG or TIFF are supported.
		 * @note The bit depth and storage will be properly loaded into memory without a conversion.
		 *
		 * @return The ImageSampler or nullptr if allocation failed.
		 */
		virtual IImageSampler* AllocImageSamplerFromDisk(const char* path) = 0;

		/**
		 * Deallocates the \p imageSampler.
		 *
		 * @param imageSampler
		 * @return true upon success
		 */
		virtual bool DeallocImageSampler(IImageSampler* imageSampler) = 0;

		/**
		 * Initializes machine authorization module.
		 * The authorization storage path must be writable by the current process.
		 *
		 * @param entitlements the entitlements
		 * @param authorizationStoragePath The authorization storage path. Set to NULL to use platform specific shared user storage path.
		 * @return true if licensing was initialized
		 */
		virtual bool InitializeAuthorization(const char* entitlements, const char* authorizationStoragePath) = 0;

		/**
		 * Authorizes this machine to use InstaMAT using the specified license file.
		 * @note Before the InstaMAT API can be used the activation needs to be performed.
		 * @note The method requires an active internet connection in order to perform
		 * machine authorization.
		 * @note InstaMAT will try to communicate with servers at "api.InstaMAT.io" via HTTPS.
		 *
		 * @param username the username/email
		 * @param password the password/license key
		 * @return true if the machine has been activated
		 */
		virtual bool AuthorizeMachine(const char* username, const char* password) = 0;

		/**
		 * Deauthorizes this machine and removes the license file.
		 * @note Before uninstalling InstaMAT it is required to deactivate this machine
		 * in order to avoid hitting the active machine limit of the license.
		 * @note Indie licenses are limited to two active machines.
		 * @note AAA and regular license machine limits depend on the license term.
		 * @note InstaMAT will try to communicate with servers at "api.InstaMAT.io" via HTTPS.
		 *
		 * @param username the username/email
		 * @param password the password/license key
		 * @return true if the machine has been deactivated
		 */
		virtual bool DeauthorizeMachine(const char* username, const char* password) = 0;

		/**
		 * Determines whether the host is authorized to use InstaMAT.
		 * @note This method may contact InstaMAT servers to verify or update the current license.
		 * @note An active internet connection is required in order to perform license refresh.
		 * @note InstaMAT will try to communicate with servers at "api.InstaMAT.io" via HTTPS.
		 * @remarks Due to thread synchronization mechanisms this method is not const.
		 *
		 * @return true if the host is authorized
		 */
		virtual bool IsHostAuthorized() = 0;

		/**
		 * Determines whether the host is authorized for the specified entitlements.
		 * @note This method may contact InstaMAT servers to verify or update the current license.
		 * @note An active internet connection is required in order to perform license refresh.
		 * @note InstaMAT will try to communicate with servers at "api.InstaMAT.io" via HTTPS.
		 * @remarks Due to thread synchronization mechanisms this method is not const.
		 *
		 * @param productEntitlement The product's entitlement identifier.
		 * @return true if the host is authorized
		 */
		virtual bool IsHostEntitledForProduct(const char* productEntitlement) = 0;

		/**
		 * Gets the machine authorization key that is necessary to start a license request with the InstaMAT licensing API.
		 * @note This call does not require an active connection to the internet. This method is intended to be used
		 * to obtain a machine specific key that can be used to generate a license file from a different computer using
		 * the AuthorizeMachineWithKey() method.
		 * Regular SDK authorization should use the AuthorizeMachine() method to authorize this machine.
		 *
		 *
		 * @param buffer (optional) the output buffer. Pass a null pointer to query the size of the key string.
		 * @param bufferSize the output buffer size
		 * @param keySize (optional) the size of the key string
		 * @return the amount of bytes written to buffer.
		 */
		virtual uint64 GetMachineAuthorizationKey(char* buffer, const uint64 bufferSize, uint64* keySize) = 0;

		/**
		 * Authorizes a machine with the specified machine authorization key and writes the license file to the specified file path.
		 * The license can be copied to the machine where the key originated from and ingested using IngestMachineAuthorizationLicense().
		 *
		 * @note Before the InstaMAT API can be used the activation needs to be performed.
		 * @note The method requires an active internet connection in order to perform
		 * machine authorization.
		 * @note InstaMAT will try to communicate with servers at "api.InstaMAT.io" via HTTPS.
		 *
		 * @param key the machine key
		 * @param username the username/email
		 * @param password the password/license key
		 * @param licenseFilePath the path the license file will be written to
		 * @return true if the machine has been activated and the license was written to disk
		 */
		virtual bool AuthorizeMachineWithKey(const char* key, const char* username, const char* password, const char* licenseFilePath) = 0;

		/**
		 * Loads the specified license file from disk and authorizes this machine if the license file
		 * was generated using the machine authorization key that originated from this host.
		 *
		 * @param licenseFilePath the license file path
		 * @return true if the machine has been activated
		 */
		virtual bool IngestMachineAuthorizationLicense(const char* licenseFilePath) = 0;

		/**
		 * Returns authorization information.
		 * @note Whenever a call to a authorization method fails. The authorization information
		 * will return error information.
		 * @note Errors will be written to the specified current error output stream.
		 * @remarks Due to thread synchronization mechanisms this method is not const.
		 *
		 * @return the authorization information.
		 */
		virtual const char* GetAuthorizationInformation() = 0;

		typedef bool (*pfnForceLicenseRefreshCallback)(const char** outError);
		typedef void (*pfnLicenseUnavailableCallback)(pfnForceLicenseRefreshCallback licenseRefreshCallback);

		/**
		 * Sets a callback that is called when a floating license becomes unauthorized.
		 * @note The callback can be used by UI applications to lock the user interface until the
		 * license becomes unlocked again.
		 * pfnForceLicenseRefreshCallback can then be called to verify the license once again.
		 * The latter either returns true upon success or logs an error what the license is still unavailable.
		 * @remarks The SDK may invoke the callback at any point from a side-thread and UI applications may
		 * need to defer the execution of UI-related code to the main thread.
		 * If triggering \p licenseUnavailableCallback is desired during initial authorization,
		 * it must be set prior to invoking InitializeAuthorization.
		 *
		 * @param licenseUnavailableCallback The floating license callback.
		 */
		virtual void SetLicenseUnavailableCallback(pfnLicenseUnavailableCallback licenseUnavailableCallback) = 0;

		/**
		 * Gets the output streams of the specified type.
		 * @note If your C runtime is using a different pointer to stdout or stderr
		 * This method will return null. To circumvent runtime difference
		 * it is possible to pass (FILE*)1 for stdout or (FILE*)2 for stderr.
		 *
		 * @param type Pass stdout or stderr.
		 * @return The output stream used by InstaMAT for the specified type.
		 */
		virtual FILE* GetOutputStream(FILE* type) = 0;

		/**
		 * Sets the output streams for the specified type.
		 * @note If your C runtime is using a different pointer to stdout or stderr
		 * This method will return false. To circumvent runtime difference
		 * it is possible to pass (FILE*)1 for stdout or (FILE*)2 for stderr.
		 *
		 * @param stream The output stream..
		 * @param type Pass stdout or stderr.
		 * @return true upon success.
		 */
		virtual bool SetOutputStream(FILE* stream, const FILE* type) = 0;

		/**
		 * Gets the task scheduler if available.
		 *
		 * @return The task scheduler or null if no task scheduler is available.
		 */
		virtual class ITaskScheduler* GetTaskScheduler() = 0;

		/**
		 * Gets the math kernel if available.
		 *
		 * @return The math kernel or null if no math kernel is available.
		 */
		virtual class IMathKernel* GetMathKernel() = 0;

		/**
		 * Sets the shared InstaLOD SDK API handle.
		 * @note The version of the InstaLOD SDK API must match the version
		 * used to compile the InstaMAT SDK.
		 *
		 * @param instaLOD A pointer to an initialized InstaLOD API handle.
		 * @param apiVersion The INSTALOD_API_VERSION of the \p instaLOD.
		 * @return true upon success.
		 */
		virtual bool SetInstaLOD(InstaLOD::IInstaLOD* instaLOD, const int apiVersion) = 0;

		/**
		 * Gets the InstaMAT handle.
		 *
		 * @return The InstaMAT handle if it is available or nullptr if it isn't.
		 */
		virtual InstaLOD::IInstaLOD* GetInstaLOD() const = 0;

		/**
		 * Gets the global value for \p key.
		 * @note This API is used to allow communication between submodules
		 * and third-party plugins. The values are not persisted between application launches.
		 * @note This API is thread-safe.
		 * @note The return value remain valid until the next call to GetGlobalValueForKey
		 * by the same thread.
		 *
		 * @param Key The global value key.
		 * @return The value for \p key or null if the global value does not exist
		 */
		virtual const char* GetGlobalValueForKey(const char* key) = 0;

		/**
		 * Sets the global value for \p key to \p value.
		 * @note This API is used to allow communication between submodules
		 * and third-party plugins. The values are not persisted between application launches.
		 * @note This API is thread-safe.
		 *
		 * @param Key The global value key.
		 * @param Value The value for \p key.
		 * @return True if set or false if the global value does not exist or could not be changed.
		 */
		virtual bool SetGlobalValueForKey(const char* key, const char* value) = 0;

		/**
		 * Releases any memory reserved for resources such as Textures and Meshes.
		 * @note This method must be called on the backend thread.
		 * @note This method should only be called if the memory should be used
		 * for other purposes. InstaMAT attempts to retain the amount of memory
		 * that is specified by the Video Memory budgets to allow for faster executions.
		 */
		virtual void ReleaseVideoMemory() = 0;

		/**
		 * Gets the total available video memory in bytes.
		 *
		 * @note This API might not be available on all platforms.
		 * @return The available video memory or -1 on failure.
		 */
		virtual int64 GetTotalAvailableVideoMemory() = 0;

		/**
		 * Sets the video memory budget in bytes.
		 *
		 * @note It is possible that the execution exceeds the budget.
		 * This can happen if a graph is built in a way that a graph instance
		 * requires a large number of input textures, or textures are retained
		 * for a long period of the graph in a way that the execution can
		 * not offload the memory.
		 *
		 * @param budgetInBytes The video memory budget in bytes, set -1 to disable the budget.
		 */
		virtual void SetVideoMemoryBudget(const int64& budgetInBytes) = 0;

		/**
		 * Gets the video memory budget in bytes.
		 *
		 * @return The video memory budget in bytes, or -1 when no budget is set.
		 */
		virtual int64 GetVideoMemoryBudget() const = 0;

		/**
		 * Gets the texture memory currently in use.
		 *
		 * @®eturn The used texture memory in bytes.
		 */
		virtual uint64 GetUsedTextureMemoryInBytes() const = 0;
	};
} // namespace InstaMAT

#define INSTAMAT_API_VERSION_MAJOR 2u
#define INSTAMAT_API_VERSION_MINOR 0u
#define INSTAMAT_API_VERSION	   (((INSTAMAT_API_VERSION_MAJOR) << 16) | ((INSTAMAT_API_VERSION_MINOR)&0xFFFF))
#define INSTAMAT_API_RELEASEDATE   "2025-09-01"

#if defined(_MSC_VER)
#	define INSTAMAT_LIBRARY_NAME "InstaMAT.dll"
#	define INSTAMAT_DLOPEN(x)	  LoadLibrary(x)
#	define INSTAMAT_DLSYM(x, y)  GetProcAddress((HMODULE)(x), (y))
#	define INSTAMAT_DLCLOSE(x)	  FreeLibrary((HMODULE)(x))
#	define INSTAMAT_DLHANDLE	  HMODULE
#else
#	if defined(__linux__)
#		define INSTAMAT_LIBRARY_NAME "libInstaMAT.so"
#	else
#		define INSTAMAT_LIBRARY_NAME "libInstaMAT.dylib"
#	endif
#	define INSTAMAT_DLOPEN(x)	 dlopen(x, RTLD_NOW)
#	define INSTAMAT_DLSYM(x, y) dlsym(x, y)
#	define INSTAMAT_DLCLOSE(x)	 dlclose(x)
#	define INSTAMAT_DLHANDLE	 void*
#endif

/**
 * Gets the InstaMAT SDK API.
 *
 * @param version the requested API version number. Pass the define INSTAMAT_API_VERSION.
 * This is used to protect against mixing incompatible header versions.
 * requesting a wrong version number will result in an undefined API pointer.
 * @param [out] outInstaMAT output pointer for the InstaMAT API.
 * @return 1u upon sucess.
 */
INSTAMAT_CAPI InstaMAT::uint32 GetInstaMAT(const InstaMAT::int32 version, InstaMAT::IInstaMAT** outInstaMAT);
typedef InstaMAT::uint32 (*pfnGetInstaMAT)(const InstaMAT::int32 version, InstaMAT::IInstaMAT** outInstaMAT);

/**
 * Gets the InstaMAT SDK build date.
 * @note This method is helpful to debug SDK incompatibility issues.
 *
 * @param [out] outVersion (optional) The output storage for the version.
 * @return The InstaMAT build date.
 */
INSTAMAT_CAPI const char* GetInstaMATBuildDate(InstaMAT::int32* outVersion);
typedef const char* (*pfnGetInstaMATBuildDate)(InstaMAT::int32* outVersion);

#endif /* InstaMATAPI_h */
