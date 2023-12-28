#pragma once

#include <string>
#include <string_view>
#include <filesystem>
#include <wizard_export.h>

namespace wizard {
    class IPlugin;
    class PluginManager;
    struct PluginReferenceDescriptor;

    // Plugin manager provided to user, which implemented in core
    class WIZARD_API IPluginManager {
    protected:
        explicit IPluginManager(PluginManager& impl);
        ~IPluginManager() = default;

    public:
        std::shared_ptr<IPlugin> FindPlugin(const std::string& pluginName);
        std::shared_ptr<IPlugin> FindPlugin(std::string_view pluginName);

        std::shared_ptr<IPlugin> FindPluginFromId(uint64_t pluginId);
        std::shared_ptr<IPlugin> FindPluginFromPath(const std::filesystem::path& pluginFilePath);
        std::shared_ptr<IPlugin> FindPluginFromDescriptor(const PluginReferenceDescriptor& pluginDescriptor);

        std::vector<std::shared_ptr<IPlugin>> GetPlugins();

        bool GetPluginDependencies(const std::string& pluginName, std::vector<PluginReferenceDescriptor>& pluginDependencies);
        bool GetPluginDependencies_FromFilePath(const std::filesystem::path& pluginFilePath, std::vector<PluginReferenceDescriptor>& pluginDependencies);
        bool GetPluginDependencies_FromDescriptor(const PluginReferenceDescriptor& pluginDescriptor, std::vector<PluginReferenceDescriptor>& pluginDependencies);

    protected:
        PluginManager& impl;
    };

}
