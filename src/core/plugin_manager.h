#pragma once

#include "wizard_context.h"
#include <wizard/plugin_manager.h>
#include <wizard/plugin.h>
#include <wizard/language_module.h>

namespace wizard {
	class Plugin;
	class Module;
	class IWizard;

	class PluginManager final : public IPluginManager, public WizardContext {
	public:
		explicit PluginManager(std::weak_ptr<IWizard> wizard);
		~PluginManager();

	public:
		/** IPluginManager interface */
		bool Initialize();
		void Terminate();
		bool IsInitialized();

		ModuleOpt FindModule(const std::string& moduleName);
		ModuleOpt FindModule(std::string_view moduleName);
		ModuleOpt FindModuleFromId(UniqueId moduleId);
		ModuleOpt FindModuleFromLang(const std::string& moduleLang);
		ModuleOpt FindModuleFromPath(const std::filesystem::path& moduleFilePath);
		ModuleOpt FindModuleFromDescriptor(const PluginReferenceDescriptor& moduleDescriptor);
		std::vector<ModuleRef> GetModules();

		PluginOpt FindPlugin(const std::string& pluginName);
		PluginOpt FindPlugin(std::string_view pluginName);
		PluginOpt FindPluginFromId(UniqueId pluginId);
		PluginOpt FindPluginFromPath(const fs::path& pluginFilePath);
		PluginOpt FindPluginFromDescriptor(const PluginReferenceDescriptor& pluginDescriptor);
		std::vector<PluginRef> GetPlugins();

		bool GetPluginDependencies(const std::string& pluginName, std::vector<PluginReferenceDescriptor>& pluginDependencies);
		bool GetPluginDependencies_FromFilePath(const fs::path& pluginFilePath, std::vector<PluginReferenceDescriptor>& pluginDependencies);
		bool GetPluginDependencies_FromDescriptor(const PluginReferenceDescriptor& pluginDescriptor, std::vector<PluginReferenceDescriptor>& pluginDependencies);

	private:
		using PluginList = std::vector<std::unique_ptr<Plugin>>;
		using ModuleList = std::vector<std::unique_ptr<Module>>;
		using VisitedPluginMap = std::unordered_map<std::string, std::pair<bool, bool>>;

		void DiscoverAllModulesAndPlugins();
		void LoadRequiredLanguageModules();
		void LoadAndStartAvailablePlugins();
		void TerminateAllPlugins();

		static void SortPluginsByDependencies(const std::string& pluginName, PluginList& sourceList, PluginList& targetList);
		static bool HasCyclicDependencies(PluginList& plugins);
		static bool IsCyclic(const std::unique_ptr<Plugin>& plugin, PluginList& plugins, VisitedPluginMap& visitedPlugins);

	private:
		ModuleList _allModules;
		PluginList _allPlugins;
	};
}