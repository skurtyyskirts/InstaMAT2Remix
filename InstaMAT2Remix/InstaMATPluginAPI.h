/**
 * InstaMATPluginAPI.h (InstaMAT)
 *
 * Copyright © 2024 InstaMaterial GmbH - All Rights Reserved.
 *
 * Unauthorized copying of this file, via any medium is strictly prohibited.
 * This file and all it's contents are proprietary and confidential.
 *
 * @file InstaMATPluginAPI.h
 * @copyright 2024 InstaMaterial GmbH. All rights reserved.
 * @section License
 */

/*!@page plugins InstaMAT SDK Plugin API
 * InstaMAT allows developers to extend the functionality through native C++ plugins.
 * Plugins developed with the InstaMAT Plugin SDK are compatible with all applications that leverage
 * the InstaMAT SDK. This applies to our in-house developed applications such as the standalone InstaMAT app
 * but it extends to our 3rd party integrations in to DCC tools or realtime 3D engines.
 *
 * With the plugin system, anything is possible. From the integration of AI networks into InstaMAT to processing or loading complex
 * data in an Element Graph.
 *
 * @section gettingstarted_with_plugin_sdk_sec Getting Started with InstaMAT Plugin SDK
 * The best way to get started with the InstaMAT Plugin SDK is by exploring the sample code that ships with the SDK.
 * The API is straightforward, easy to understand and fun to use!
 * More information on the InstaMAT Plugin SDK Sample project is available on the \ref plugins_sample page.
 *
 * One you have compiled your first plugin, simply create a new package in InstaMAT and drag and drop the plugin into your package.
 * InstaMAT will automatically register your plugin and all of the extensions that it registers during the call to `IInstaMATPlugin::Initialize()`.
 *
 * @section Fundamental Concepts
 * To register a plugin with the InstaMAT SDK, the library has to export a single C function called
 * `GetInstaMATPlugin`. This function needs to allocate a class that implements the `IInstaMATPlugin` and
 * assigns the pointer to the output parameter.
 * The `GetInstaMATPlugin` function is called when the InstaMAT SDK registers a package
 * that contains the library. Once the InstaMAT SDK has called the function, it will call `Initialize` and `Shutdown` at apropriate times
 * to perform plugin specific initialization.
 *
 * InstaMAT SDK plugins can register additional utilities and classes by calling functions such as
 * `IInstaMAT::RegisterElementEntityPlugin` with a custom class.
 */

#ifndef InstaMATPluginAPI_h
#define InstaMATPluginAPI_h

#include <stdlib.h>
#include <stdio.h>
#include <float.h>

/**
 * The project that builds the plugin library itself needs to define the following preprocessor defines
 *		INSTAMATPLUGIN_LIB
 */

#if defined(__cplusplus)
#	define INSTAMATPLUGIN_EXTERNC extern "C"
#else
#	define INSTAMATPLUGIN_EXTERNC
#endif

#if defined(_WIN32)
#	define INSTAMATPLUGIN_DLL_EXPORT __declspec(dllexport)
#	define INSTAMATPLUGIN_DLL_IMPORT __declspec(dllimport)
#elif defined(__APPLE__) || defined(__linux__)
#	define INSTAMATPLUGIN_DLL_EXPORT __attribute__((visibility("default")))
#	define INSTAMATPLUGIN_DLL_IMPORT
#endif

#if defined(INSTAMATPLUGIN_LIB) || defined(INSTAMAT_LIB)
#	define INSTAMATPLUGIN_API			 INSTAMATPLUGIN_EXTERNC INSTAMATPLUGIN_DLL_EXPORT
#	define INSTAMATPLUGIN_EXPORTEDCLASS INSTAMATPLUGIN_DLL_EXPORT
#else
#	define INSTAMATPLUGIN_API			 INSTAMATPLUGIN_EXTERNC INSTAMATPLUGIN_DLL_IMPORT
#	define INSTAMATPLUGIN_EXPORTEDCLASS INSTAMATPLUGIN_DLL_IMPORT
#endif

#define INSTAMATPLUGIN_API_VERSION_MAJOR INSTAMAT_API_VERSION_MAJOR
#define INSTAMATPLUGIN_API_VERSION_MINOR INSTAMAT_API_VERSION_MINOR
#define INSTAMATPLUGIN_API_VERSION		 (((INSTAMATPLUGIN_API_VERSION_MAJOR) << 16) | ((INSTAMATPLUGIN_API_VERSION_MINOR)&0xFFFF))

namespace InstaMAT
{
	class IInstaMATPlugin;
	class IInstaMATElementEntityPlugin;
	class IInstaMATGPUCPUBackend;
	class IInstaMATObjectManager;

	/**
	 * The IInstaMATGPUCPUBackend class provides an interface to interact with
	 * an ElementGraph execution session.
	 */
	class INSTAMAT_EXPORTEDCLASS IInstaMATGPUCPUBackend
	{
	protected:
		virtual ~IInstaMATGPUCPUBackend() {}

	public:
		/**
		 * Gets the entity seed.
		 * @note This valud is different than ElementGraphEntity::Seed as the value
		 * is calculated based on the parent graph state, similar to the format.
		 *
		 * @return The Seed.
		 */
		virtual float GetSeed() const = 0;

		/**
		 * Reports the current progress to the backend.
		 *
		 * @param progress A floating-point progress in the range [0...1]
		 */
		virtual void SetProgress(float progress) = 0;

		/**
		 * Gets the value that is connect to the input parameter \p parameterName.
		 *
		 * @param parameterName The input parameter name.
		 * @param [out] outValue (optional) The value or null if not found.
		 * @param outValueSize (optional) The size of the \p outValue.
		 * @return The size of the string or null if not available.
		 */
		virtual uint64 GetInputParameterString(const char* parameterName, char* outValue, const uint64 outValueSize) const = 0;

		/**
		 * Gets the value that is connect to the input parameter \p parameterName.
		 *
		 * @param parameterName The input parameter name.
		 * @param [out] outValue (optional) The value or null if not found.
		 * @return true if a value is available
		 */
		virtual bool GetInputParameterConstantValue(const char* parameterName, ArithmeticGraphValue* outValue) const = 0;

		/**
		 * Gets the value that is connect to the input parameter \p parameterName.
		 *
		 * @param parameterName The input parameter name.
		 * @param [out] outValue (optional) The value or null if not found.
		 * @param outValueSize (optional) The size of the \p outValue.
		 * @return The size of the string or null if not available.
		 */
		virtual uint64 GetInputParameterResourceURLValue(const char* parameterName, char* outValue, const uint64 outValueSize) const = 0;

		/**
		 * Gets the value that is connect to the input parameter \p parameterName.
		 *
		 * @param parameterName The input parameter name.
		 * @param [out] outData (optional) The output pointer that will point to the read only resource data buffer.
		 * @return The size of data buffer.
		 */
		virtual uint64 GetInputParameterResourceData(const char* parameterName, const uint8** outData) const = 0;

		/**
		 * Gets the value that is connect to the input parameter \p parameterName.
		 *
		 * @param parameterName The input parameter name.
		 * @param [out] outValue (optional) The value or null if not found.
		 * @return true if a value is available
		 */
		virtual bool GetInputParameterEnumValue(const char* parameterName, uint32* outValue) const = 0;

		/**
		 * Sets the arithmetic value for the output parmeter with the name \p outputParameterName to the specified \p value.
		 *
		 * @param outputParameterName The output parameter name.
		 * @param value The value.
		 * @return true upon success.
		 */
		virtual bool SetArithmeticValueForOutputParameter(const char* outputParameterName, const ArithmeticGraphValue& value) = 0;

		/**
		 * Sets the \p color for the specified output parameter.
		 * @note This method is useful to set an unused image output to a valid image.
		 * This is important as all outputs parameters must always be set to valid data after the execution of the element.
		 * @remarks The \p color is expected to be in linear colorspace. sRGB color values should be converted to linear first.
		 *
		 * @param parameterName The parameter name.
		 * @param color The color.
		 * @return true upon success.
		 */
		virtual bool SetConstantColorForOutputImage(const char* outputParameterName, const ColorRGBAF32& color) = 0;

		/**
		 * Allocates an input image sampler for the specified \p parameterName
		 * The format of the image sampler will match the current execution format of the Graph.
		 * @note Allocating an IImageSampler for an input parameter will require the image data to be
		 * downloaded from the execution backend and copied into a CPU buffer.
		 * This is a slow operation and should only be done when the image data is actually needed.
		 *
		 * @param parameterName The input parameter name to allocate as image sampler.
		 * @return The IImageSampler or null if the parameter is invalid or cannot be allocated.
		 */
		virtual IImageSampler* AllocInputImageSampler(const char* parameterName) = 0;

		/**
		 * Allocates an output image sampler for the specified \p parameterName
		 * The format of the image sampler will match the current execution format of the Graph.
		 *
		 * @param parameterName The input parameter name to allocate as image sampler.
		 * @return The IImageSampler or null if the parameter is invalid or cannot be allocated.
		 */
		virtual IImageSampler* AllocOutputImageSampler(const char* parameterName) = 0;

		/**
		 * Sets the \p sampler for the specified output parameter.
		 * @note Setting an output image buffer or sampler will require the image data to be
		 * uploaded to the execution backend from a CPU buffer.
		 * This is a slow operation and should only be done when the image data is actually needed.
		 *
		 * @param parameterName The parameter name.
		 * @param sampler The sampler.
		 * @return true upon success.
		 */
		virtual bool SetOutputImageSampler(const char* parameterName, IImageSampler* sampler) = 0;

		/**
		 * Aliases the input image attached to the input parameter with name \p inputParameterName to the image output parameter
		 * with name \p outputParameterName.
		 * @note This is a zero-cost operation as the texture data is not copied, or rendered but aliased.
		 *
		 * @param inputParameterName The input parameter name.
		 * @param outputParameterName The output parameter name.
		 * @return true upon success.
		 */
		virtual bool AliasInputImageToOutputImage(const char* inputParameterName, const char* outputParameterName) = 0;
	};

	/**
	 * The IInstaMATElementEntityPlugin implements an Element that is executed on the CPU.
	 */
	class INSTAMATPLUGIN_EXPORTEDCLASS IInstaMATElementEntityPlugin
	{
	protected:
		IInstaMATElementEntityPlugin(IInstaMATPlugin& instaMATPlugin) :
		InstaMATPlugin(instaMATPlugin) {}
		virtual ~IInstaMATElementEntityPlugin() {}

	public:
		/**
		 * The Definition structure is used to retrieve fundamental parameter information.
		 * @note Pointers for string values such as \# Name must stay valid until the plugin is deallocated.
		 * It is recommended to use string literals for these fields.
		 */
		struct Definition
		{
			const char* Name;						   /**< The name of the parameter. */
			const char* Documentation;				   /**< If the type supports a String, set this value to the default value. */
			IGraphVariable::Type Type;				   /**< The type of the parameter. */
			IGraphVariable::UIControlType ControlType; /**< For arithmetic types: The UI control type of the parameter. */
			GraphVec2F ControlRange[4];				   /**< For arithmetic types: The UI control ranges. */
			bool ControlLocked;						   /**< For arithmetic types: True if the UI control is element-wise locked. */
			ArithmeticGraphValue ArithmeticValue;	   /**< If the type supports a ArithmeticGraphValue, set this value to the default value. */
			union
			{
				const char* StringValue;		  /**< If the type supports a String, set this value to the default value. */
				const char* ResourceURLValue;	  /**< If the type supports an ElementResource, set this value to the default value.*/
				const char* EnumProviderURLValue; /**< If the type supports an Enum, set this value to the graph resource URL.*/
			};
		};

		/**
		 * Gets the name of the entity.
		 *
		 * @return Name.
		 */
		virtual const char* GetName() const = 0;

		/**
		 * Gets the unique GraphID of the plugin.
		 * @note If another graph is using the same ID unexpected results will occur.
		 * Make sure to choose a random ID instead of using a formatted, human-readable ID
		 * to lower the chances of collision.
		 *
		 * @return GraphID.
		 */
		virtual const char* GetID() const = 0;

		/**
		 * Gets the category of this instance.
		 *
		 * @return Category.
		 */
		virtual const char* GetCategory() const = 0;

		/**
		 * Gets the documentation of this instance.
		 * @note The documentation should provide helpful information to the user
		 * as what the input represent and what the outputs are generated.
		 *
		 * @return Documentation.
		 */
		virtual const char* GetDocumentation() const = 0;

		/**
		 * Setup the MetaData for the \p graph object.
		 * @note This function should be used to setup documentation for the graph and
		 * its parameters.
		 */
		virtual void SetupMetaDataForGraphObject(IGraphObject& graph) = 0;

		/**
		 * Gets the parameter definition for the parameter at the specified \p index and \p type.
		 * @note The API will call this method with an incrementing \p index until it returns false.
		 *
		 * @param index The parameter index.
		 * @param type The type.
		 * @param [out] outParameterDefinition The output storage for the parameter definition.
		 * @return true upon success
		 */
		virtual bool GetParameterDefinition(const uint32 index, const IGraph::ParameterType type, Definition& outParameterDefinition) = 0;

		/**
		 * Determines if the execution format of this instance is relevant to its execution.
		 * @note For user created element this should always return true.
		 * However, some non-image processing elements that execute on the CPU
		 * may choose not to expose the Execution Format configuration in the UI
		 * as it does not influence the exectuion.
		 *
		 * @return true if the execution format of this instance is configurable
		 */
		virtual bool IsExecutionFormatRelevant() const = 0;

		/**
		 * Executes the element for the specified \p graph.
		 * @note Output values should be assigned through the \p backend.
		 *
		 * @param elementGraph The executing graph.
		 * @param entityGraph The executing instance. This is the entity that is instancing this plugin.
		 * @param backend The CPU/GPU backend.
		 */
		virtual void Execute(const IGraph& elementGraph, const IGraph& entityGraph, IInstaMATGPUCPUBackend& backend) = 0;

		IInstaMATPlugin& InstaMATPlugin; /**< The InstaMAT SDK handle. */
	};

	/**
	 * The IInstaMATObjectManager implements an object manager.
	 */
	class INSTAMAT_EXPORTEDCLASS IInstaMATObjectManager
	{
	protected:
		virtual ~IInstaMATObjectManager() {}

	public:
		/**
		 * Copies the resource data with \p resourceName to \p outData.
		 * @note To get the resource size call this method with a null pointer for \p outData.
		 *
		 * @param resourceName The resource name in the package.
		 * @param plugin The plugin.
		 * @param [out] outData (optional) The output buffer for the data.
		 * @param dataSize The size of the data
		 * @return The number of bytes of the resource.
		 */
		virtual uint64 CopyResourceFromPluginPackage(const char* resourceName, const IInstaMATPlugin& plugin, void* outData, const uint64 dataSize) const = 0;

	public:
		virtual bool IsLibrary() const = 0;
	};

	/**
	 * The IInstaMATPlugin implements a basic plugin.
	 */
	class INSTAMATPLUGIN_EXPORTEDCLASS IInstaMATPlugin
	{
	protected:
		IInstaMATPlugin(IInstaMAT& instaMAT, IInstaMATObjectManager& objectManager) :
		InstaMAT(instaMAT), ObjectManager(objectManager) {}
		virtual ~IInstaMATPlugin() {}

	public:
		/**
		 * Gets the version of the plugin.
		 * @note The plugin should return the INSTAMATPLUGIN_API_VERSION constant here.
		 *
		 * @return INSTAMATPLUGIN_API_VERSION used to build this plugin.
		 */
		virtual int32 GetVersion() const = 0;

		/**
		 * Initializes this instance.
		 * @note This is the right place to perform more complex initializations.
		 */
		virtual void Initialize() = 0;

		/**
		 * Shutdowns this instance.
		 * @note All open file handles should be cleaned up.
		 */
		virtual void Shutdown() = 0;

		/**
		 * Deletes this instance.
		 */
		virtual void Dealloc() = 0;

		IInstaMAT& InstaMAT;				   /**< The InstaMAT SDK handle. */
		IInstaMATObjectManager& ObjectManager; /**< The object manager. */
	};
} // namespace InstaMAT

#define INSTAMATPLUGIN_RETURNVALUE_FAILED		  0u /**< The plugin initialization failed. */
#define INSTAMATPLUGIN_RETURNVALUE_SUCCESS		  1u /**< The plugin was initialized successfully. */
#define INSTAMATPLUGIN_RETURNVALUE_INVALIDVERSION 2u /**< The plugin is incompatible with the requested SDK version. */

/**
 * Gets the InstaMAT plugin API.
 * @note This is the only library exported method that must be implemented by your plugin.
 *
 * @param version the requested API version number. The SDK will pass the define INSTAMATPLUGIN_API_VERSION.
 * Your plugin must test the \p version against the defined INSTAMATPLUGIN_API_VERSION.
 * If the values do not match, the function should immediately return INSTAMATPLUGIN_RETURNVALUE_INVALIDVERSION.
 * @param instaMAT The IInstaMAT handle must be passed to the constructor of your IInstaMATPlugin implementation to access the SDK.
 * @param objectManager The IInstaMATObjectManager handle must be passed to the constructor of your IInstaMATPlugin implementation to access the objectManager that owns the plugin.
 * @param [out] outInstaMAT output pointer for the InstaMAT API.
 * @return INSTAMATPLUGIN_RETURNVALUE_SUCCESS upon sucess or another error value upon failure.
 */
INSTAMATPLUGIN_API InstaMAT::uint32 GetInstaMATPlugin(const InstaMAT::int32 version,
													  InstaMAT::IInstaMAT& instaMAT,
													  InstaMAT::IInstaMATObjectManager& objectManager,
													  InstaMAT::IInstaMATPlugin** outInstaMATPlugin);

typedef InstaMAT::uint32 (*pfnGetInstaMATPlugin)(const InstaMAT::int32 version,
												 InstaMAT::IInstaMAT& instaMAT,
												 InstaMAT::IInstaMATObjectManager& objectManager,
												 InstaMAT::IInstaMATPlugin** outInstaMATPlugin);

#endif /* InstaMATPluginAPI_h */
