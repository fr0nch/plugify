#include <wizard/plugin.h>
#include <core/plugin.h>

using namespace wizard;

IPlugin::IPlugin(Plugin& impl) : _impl{impl} {
}

IPlugin::~IPlugin() = default;

uint64_t IPlugin::GetId() const {
    return _impl.GetId();
}

const std::string& IPlugin::GetName() const {
    return _impl.GetName();
}

const std::string& IPlugin::GetFriendlyName() const {
    return _impl.GetFriendlyName();
}

const fs::path& IPlugin::GetFilePath() const {
    return _impl.GetFilePath();
}

fs::path IPlugin::GetBaseDir() const {
    return _impl.GetBaseDir();
}

fs::path IPlugin::GetContentDir() const {
    return _impl.GetContentDir();
}

fs::path IPlugin::GetMountedAssetPath() const {
    return _impl.GetMountedAssetPath();
}

const PluginDescriptor& IPlugin::GetDescriptor() const {
    return _impl.GetDescriptor();
}

PluginState IPlugin::GetState() const {
    return _impl.GetState();
}

const std::string& IPlugin::GetError() const {
    return _impl.GetError();
}

const std::vector<MethodData>& IPlugin::GetMethods() const {
    return _impl.GetMethods();
}