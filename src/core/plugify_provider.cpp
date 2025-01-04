#include "plugify_provider.hpp"
#include "plugin_descriptor.hpp"
#include <plugify/language_module_descriptor.hpp>
#include <plugify/module.hpp>
#include <plugify/plugin.hpp>
#include <plugify/plugin_descriptor.hpp>
#include <plugify/plugin_manager.hpp>

using namespace plugify;

PlugifyProvider::PlugifyProvider(std::weak_ptr<IPlugify> plugify) : IPlugifyProvider(*this), PlugifyContext(std::move(plugify)) {
}

PlugifyProvider::~PlugifyProvider() = default;

void PlugifyProvider::Log(std::string_view msg, Severity severity) {
	if (auto plugify = _plugify.lock()) {
		plugify->Log(msg, severity);
	}
}

const fs::path& PlugifyProvider::GetBaseDir() noexcept {
	if (auto plugify = _plugify.lock()) {
		return plugify->GetConfig().baseDir;
	}
	static fs::path _;
	return _;
}

bool PlugifyProvider::IsPreferOwnSymbols() noexcept {
	if (auto plugify = _plugify.lock()) {
		return plugify->GetConfig().preferOwnSymbols;
	}
	return false;
}

bool PlugifyProvider::IsPluginLoaded(std::string_view name, std::optional<int32_t> requiredVersion, bool minimum) noexcept {
	if (auto plugify = _plugify.lock()) {
		if (auto pluginManager = plugify->GetPluginManager().lock()) {
			auto plugin = pluginManager->FindPlugin(name);
			if (!plugin.has_value())
				return false;
			if (plugin->GetState() != PluginState::Loaded && plugin->GetState() != PluginState::Running)
				return false;
			if (requiredVersion.has_value()) {
				if (minimum) {
					return plugin->GetDescriptor().GetVersion() >= *requiredVersion;
				} else {
					return plugin->GetDescriptor().GetVersion() == *requiredVersion;
				}
			} else {
				return true;
			}
		}
	}
	return false;
}

bool PlugifyProvider::IsModuleLoaded(std::string_view name, std::optional<int32_t> requiredVersion, bool minimum) noexcept {
	if (auto plugify = _plugify.lock()) {
		if (auto pluginManager = plugify->GetPluginManager().lock()) {
			auto module = pluginManager->FindModule(name);
			if (!module.has_value())
				return false;
			if (module->GetState() != ModuleState::Loaded)
				return false;
			if (requiredVersion.has_value()) {
				if (minimum) {
					return module->GetDescriptor().GetVersion() >= *requiredVersion;
				} else {
					return module->GetDescriptor().GetVersion() == *requiredVersion;
				}
			} else {
				return true;
			}
		}
	}
	return false;
}

PluginOpt PlugifyProvider::FindPlugin(std::string_view name) noexcept {
	if (auto plugify = _plugify.lock()) {
		if (auto pluginManager = plugify->GetPluginManager().lock()) {
			return pluginManager->FindPlugin(name);
		}
	}
	return {};
}

ModuleOpt PlugifyProvider::FindModule(std::string_view name) noexcept {
	if (auto plugify = _plugify.lock()) {
		if (auto pluginManager = plugify->GetPluginManager().lock()) {
			return pluginManager->FindModule(name);
		}
	}
	return {};
}
