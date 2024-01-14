#include "package_manager.h"
#include "package_manifest.h"
#include "module.h"
#include "plugin.h"

#include <wizard/wizard.h>
#include <utils/file_system.h>
#include <utils/http_downloader.h>
#include <utils/json.h>
#include <mutex>
#include <thread>
#include <picosha2.h>
#include <miniz.h>

using namespace wizard;

static std::array<std::pair<std::string_view, std::string_view>, 2> packageTypes {
	std::pair{ "modules", Module::kFileExtension },
	std::pair{ "plugins", Plugin::kFileExtension },
	// Might add more package types in future
};

PackageManager::PackageManager(std::weak_ptr<IWizard> wizard) : IPackageManager(*this), WizardContext(std::move(wizard)) {
}

PackageManager::~PackageManager() {
	Terminate();
}

bool PackageManager::Initialize() {
	if (_httpDownloader)
		return false;
	auto debugStart = DateTime::Now();
	_httpDownloader = HTTPDownloader::Create();
	LoadLocalPackages();
	LoadRemotePackages();
	FindDependencies();
	WZ_LOG_DEBUG("PackageManager loaded in {}ms", (DateTime::Now() - debugStart).AsMilliseconds<float>());
	return true;
}

void PackageManager::Terminate() {
	_localPackages.clear();
	_remotePackages.clear();
	_missedPackages.clear();
	_conflictedPackages.clear();
	_httpDownloader.reset();
}

template<typename Cnt, typename Pr = std::equal_to<typename Cnt::value_type>>
bool RemoveDuplicates(Cnt& cnt, Pr cmp = Pr()) {
	auto size = std::size(cnt);
	Cnt result;
	result.reserve(size);

	std::copy_if(
		std::make_move_iterator(std::begin(cnt)),
		std::make_move_iterator(std::end(cnt)),
		std::back_inserter(result),
		[&](const typename Cnt::value_type& what) {
			return std::find_if(std::begin(result), std::end(result), [&](const typename Cnt::value_type& existing) {
				return cmp(what, existing);
			}) == std::end(result);
		}
	);

	cnt = std::move(result);
	return std::size(cnt) != size;
}

template<typename T>
std::optional<LocalPackage> GetPackageFromDescriptor(const fs::path& path, const std::string& name) {
	auto json = FileSystem::ReadText(path);
	auto descriptor = glz::read_json<T>(json);
	if (!descriptor.has_value()) {
		WZ_LOG_ERROR("Package: '{}' has JSON parsing error: {}", name, glz::format_error(descriptor.error(), json));
		return {};
	}
	if (!PackageManager::IsSupportsPlatform(descriptor->supportedPlatforms))
		return {};
	std::string type;
	if constexpr (std::is_same_v<T, LanguageModuleDescriptor>) {
		if (descriptor->language == "plugin") {
			WZ_LOG_ERROR("Module descriptor: '{}' has JSON parsing error: Forbidden language name", name);
			return {};
		}
		type = descriptor->language;
	} else {
		type = "plugin";

		if (RemoveDuplicates(descriptor->dependencies)) {
			WZ_LOG_WARNING("Plugin descriptor: '{}' has multiple dependencies with same name!", name);
		}

		if (RemoveDuplicates(descriptor->exportedMethods)) {
			WZ_LOG_WARNING("Plugin descriptor: '{}' has multiple method with same name!", name);
		}
	}
	auto version = descriptor->version;
	return { {Package{name, type}, path, version, std::make_unique<T>(std::move(*descriptor))} };
}

void PackageManager::LoadLocalPackages()  {
	auto wizard = _wizard.lock();
	WZ_ASSERT(wizard);

	WZ_LOG_DEBUG("Loading local packages");

	_localPackages.clear();

	// TODO: add threads

	FileSystem::ReadDirectory(wizard->GetConfig().baseDir, [&](const fs::path& path, int depth) {
		if (depth != 1)
			return;

		auto extension = path.extension().string();
		bool isModule = extension == Module::kFileExtension;
		if (!isModule && extension != Plugin::kFileExtension)
			return;

		auto name = path.filename().replace_extension().string();
		if (name.empty())
			return;

		auto package = isModule ?
				GetPackageFromDescriptor<LanguageModuleDescriptor>(path, name) :
				GetPackageFromDescriptor<PluginDescriptor>(path, name);
		if (!package.has_value())
			return;

		auto it = std::find_if(_localPackages.begin(), _localPackages.end(), [&name](const auto& plugin) {
			return plugin.name == name;
		});

		if (it == _localPackages.end()) {
			_localPackages.emplace_back(std::move(*package));
		} else {
			auto& existingPackage = *it;

			auto& existingVersion = existingPackage.version;
			if (existingVersion != package->version) {
				WZ_LOG_WARNING("By default, prioritizing newer version (v{}) of '{}' package, over older version (v{}).", std::max(existingVersion, package->version), name, std::min(existingVersion, package->version));

				if (existingVersion < package->version) {
					existingPackage = std::move(*package);
				}
			} else {
				WZ_LOG_WARNING("The same version (v{}) of package '{}' exists at '{}' - second location will be ignored.", existingVersion, name, path.string());
			}
		}
	}, 3);
}

void PackageManager::LoadRemotePackages() {
	auto wizard = _wizard.lock();
	WZ_ASSERT(wizard);

	WZ_LOG_DEBUG("Loading remote packages");

	_remotePackages.clear();

	std::mutex mutex;

	auto fetchManifest = [&](const std::string& url) {
		_httpDownloader->CreateRequest(url, [&](int32_t statusCode, const std::string&/* contentType*/, HTTPDownloader::Request::Data data) {
			if (statusCode == HTTPDownloader::HTTP_STATUS_OK) {
				std::string buffer(data.begin(), data.end());
				auto manifest = glz::read_json<PackageManifest>(buffer);
				if (!manifest.has_value()) {
					WZ_LOG_ERROR("Packages manifest from '{}' has JSON parsing error: {}", url, glz::format_error(manifest.error(), buffer));
					return;
				}

				for (auto& [name, package] : manifest->content) {
					if (name.empty() || package.name != name) {
						WZ_LOG_ERROR("Package manifest: '{}' has different name in key and object: {} <-> {}", url, name, package.name);
						continue;
					}

					auto it = std::find_if(_remotePackages.begin(), _remotePackages.end(), [&name](const auto& plugin) {
						return plugin.name == name;
					});
					if (it == _remotePackages.end()) {
						std::unique_lock<std::mutex> lock(mutex);
						_remotePackages.emplace_back(std::move(package));
					} else {
						auto& existingPackage = *it;
						if (existingPackage == package) {
							std::unique_lock<std::mutex> lock(mutex);
							existingPackage.versions.merge(package.versions);
						} else {
							WZ_LOG_WARNING("The package '{}' exists at '{}' - second location will be ignored.", name, url);
						}
					}
				}
			}
		});
	};

	for (const auto& url : wizard->GetConfig().repositories) {
		if (!url.empty())
			fetchManifest(url);
	}

	for (const auto& package : _localPackages) {
		const auto& url = package.descriptor->updateURL;
		if (!url.empty())
			fetchManifest(url);
	}

	_httpDownloader->WaitForAllRequests();
}

template<typename T>
std::optional<std::reference_wrapper<const T>> FindLanguageModule(const std::vector<T>& container, const std::string& name)  {
	for (auto& package : container) {
		if (package.type == name) {
			return package;
		}
	}
	return {};
}

void PackageManager::FindDependencies() {
	_missedPackages.clear();
	_conflictedPackages.clear();

	for (const auto& package : _localPackages) {
		if (package.type == "plugin") {
			auto pluginDescriptor = std::static_pointer_cast<PluginDescriptor>(package.descriptor);

			const auto& lang = pluginDescriptor->languageModule.name;
			if (!FindLanguageModule(_localPackages, lang)) {
				auto remotePackage = FindLanguageModule(_remotePackages, lang);
				if (remotePackage.has_value()) {
					auto it = _missedPackages.find(lang);
					if (it == _missedPackages.end()) {
						_missedPackages.emplace(lang, std::pair{*remotePackage, std::nullopt }); // by default prioritizing latest language modules
					}
				} else {
					WZ_LOG_ERROR("Package: '{}' has language module dependency: '{}', but it was not found.", package.name, lang);
					_conflictedPackages.emplace_back(package);
					continue;
				}
			}

			for (const auto& dependency : pluginDescriptor->dependencies) {
				if (dependency.optional || !IsSupportsPlatform(dependency.supportedPlatforms))
					continue;

				auto localPackage = FindLocalPackage(dependency.name);
				if (localPackage.has_value()) {
					if (dependency.requestedVersion.has_value() && *dependency.requestedVersion != localPackage->get().version)  {
						WZ_LOG_ERROR("Package: '{}' has dependency: '{}' which required (v{}), but (v{}) installed. Conflict cannot be resolved automatically.", package.name, dependency.name, *dependency.requestedVersion, localPackage->get().version);
					}
					continue;
				}

				auto remotePackage = FindRemotePackage(dependency.name);
				if (remotePackage.has_value()) {
					if (dependency.requestedVersion.has_value() && !remotePackage->get().Version(*dependency.requestedVersion).has_value()) {
						WZ_LOG_ERROR("Package: '{}' has dependency: '{}' which required (v{}), but version was not found. Problem cannot be resolved automatically.", package.name, dependency.name, *dependency.requestedVersion);
						_conflictedPackages.emplace_back(package);
						continue;
					}

					auto it = _missedPackages.find(dependency.name);
					if (it == _missedPackages.end()) {
						_missedPackages.emplace(dependency.name, std::pair{*remotePackage, dependency.requestedVersion });
					} else {
						auto& existingDependency = std::get<Dependency>(*it);

						auto& existingVersion = existingDependency.second;
						if (dependency.requestedVersion.has_value()) {
							if (existingVersion.has_value()) {
								if (*existingVersion != *dependency.requestedVersion) {
									WZ_LOG_WARNING("By default, prioritizing newer version (v{}) of '{}' dependency, over older version (v{}).", std::max(*existingVersion, *dependency.requestedVersion), dependency.name, std::min(*existingVersion, *dependency.requestedVersion));

									if (*existingVersion < *dependency.requestedVersion) {
										existingVersion = dependency.requestedVersion;
									}
								} else {
									WZ_LOG_WARNING("The same version (v{}) of dependency '{}' required by '{}' at '{}' - second one will be ignored.", *existingVersion, dependency.name, package.name, package.path.string());
								}
							} else {
								existingVersion = dependency.requestedVersion;
							}
						}
					}
				} else {
					WZ_LOG_ERROR("Package: '{}' has dependency: '{}' which could not be found.", package.name, dependency.name);
					_conflictedPackages.emplace_back(package);
				}
			}
		}
	}

	for (const auto& [_, dependency] : _missedPackages) {
		const auto& [package, version] = dependency;
		WZ_LOG_INFO("Required to install: '{}' [{}] (v{})", package.get().name, package.get().type, version.has_value() ? std::to_string(*version) : "[latest]");
	}

	for (const auto& packageRef : _conflictedPackages) {
		const auto& package = packageRef.get();
		WZ_LOG_WARNING("Unable to install: '{}' [{}] (v{}) due to unresolved conflicts", package.name, package.type, package.version);
	}
}

void PackageManager::InstallMissedPackages() {
	Request([&]{
		std::ostringstream missed;
		bool first = true;
		for (const auto& [name, dependency] : _missedPackages) {
			const auto& [package, version] = dependency;
			InstallPackage(package, version);
			if (first) {
				missed << "'" << name;
				first = false;
			} else {
				missed << "', '" << name;
			}
		}
		if (!first) {
			missed << "'";
			WZ_LOG_INFO("Trying install {} missing package(s) to solve dependency issues", missed.str());
		}
	}, __func__);
}

void PackageManager::UninstallConflictedPackages() {
	Request([&]{
		std::ostringstream conflicted;
		bool first = true;
		for (const auto& package : _conflictedPackages) {
			UninstallPackage(package);
			if (first) {
				conflicted << "'" << package.get().name;
				first = false;
			} else {
				conflicted << "', '" << package.get().name;
			}
		}
		if (!first) {
			conflicted << "'";
			WZ_LOG_INFO("Trying uninstall {} conflicted package(s) to solve dependency issues", conflicted.str());
		}
	}, __func__);
}

void PackageManager::SnapshotPackages(const fs::path& manifestFilePath, bool prettify) {
	auto debugStart = DateTime::Now();

	std::unordered_map<std::string, RemotePackage> packages;
	packages.reserve(_localPackages.size());

	for (const auto& package : _localPackages) {
		packages.emplace(package.name, package);
	}

	if (packages.empty()) {
		WZ_LOG_WARNING("Packages was not found!");
		return;
	}

	PackageManifest manifest{ std::move(packages) };
	std::string buffer;
	glz::write_json(manifest, buffer);
	FileSystem::WriteText(manifestFilePath, prettify ? glz::prettify(buffer) : buffer);

	WZ_LOG_DEBUG("Snapshot '{}' created in {}ms", manifestFilePath.string(), (DateTime::Now() - debugStart).AsMilliseconds<float>());
}

void PackageManager::InstallPackage(const std::string& packageName, std::optional<int32_t> requiredVersion) {
	if (packageName.empty())
		return;

	Request([&] {
		auto package = FindRemotePackage(packageName);
		if (package.has_value()) {
			InstallPackage(*package, requiredVersion);
		} else {
			WZ_LOG_ERROR("Package: {} not found", packageName);
		}
	}, __func__);
}

void PackageManager::InstallPackages(std::span<const std::string> packageNames) {
	std::unordered_set<std::string> unique;
	unique.reserve(packageNames.size());
	Request([&] {
		std::ostringstream error;
		bool first = true;
		for (const auto& packageName: packageNames) {
			if (packageName.empty() || unique.contains(packageName))
				continue;
			auto package = FindRemotePackage(packageName);
			if (package.has_value()) {
				InstallPackage(*package);
			} else {
				if (first) {
					error << "'" << packageName;
					first = false;
				} else {
					error << "', '" << packageName;
				}
			}
			unique.insert(packageName);
		}
		if (!first) {
			error << "'";
			WZ_LOG_ERROR("Not found {} packages(s)", error.str());
		}
	}, __func__);
}

void PackageManager::InstallAllPackages(const fs::path& manifestFilePath, bool reinstall) {
	if (manifestFilePath.extension().string() != PackageManifest::kFileExtension) {
		WZ_LOG_ERROR("Package manifest: '{}' should be in *{} format", manifestFilePath.string(), PackageManifest::kFileExtension);
		return;
	}

	auto wizard = _wizard.lock();
	WZ_ASSERT(wizard);

	auto path = wizard->GetConfig().baseDir / manifestFilePath;

	WZ_LOG_INFO("Read package manifest from '{}'", path.string());

	auto json = FileSystem::ReadText(path);
	auto manifest = glz::read_json<PackageManifest>(json);
	if (!manifest.has_value()) {
		WZ_LOG_ERROR("Package manifest: '{}' has JSON parsing error: {}", path.string(), glz::format_error(manifest.error(), json));
		return;
	}

	if (!reinstall) {
		for (const auto& package : _localPackages) {
			manifest->content.erase(package.name);
		}
	}

	if (manifest->content.empty()) {
		WZ_LOG_WARNING("No packages to install was found! If you need to reinstall all installed packages, use the reinstall flag!");
		return;
	}

	Request([&] {
		for (const auto& [name, package]: manifest->content) {
			if (name.empty() || package.name != name) {
				WZ_LOG_ERROR("Package manifest: '{}' has different name in key and object: {} <-> {}", path.string(), name, package.name);
				continue;
			}
			InstallPackage(package);
		}
	}, __func__);
}

void PackageManager::InstallAllPackages(const std::string& manifestUrl, bool reinstall) {
	if (manifestUrl.empty())
		return;

	WZ_LOG_INFO("Read package manifest from '{}'", manifestUrl);

	auto func = __func__;

	_httpDownloader->CreateRequest(manifestUrl, [&](int32_t statusCode, const std::string& /*contentType*/, HTTPDownloader::Request::Data data) {
		if (statusCode == HTTPDownloader::HTTP_STATUS_OK) {
			std::string buffer(data.begin(), data.end());
			auto manifest = glz::read_json<PackageManifest>(buffer);
			if (!manifest.has_value()) {
				WZ_LOG_ERROR("Packages manifest from '{}' has JSON parsing error: {}", manifestUrl, glz::format_error(manifest.error(), buffer));
				return;
			}

			if (!reinstall) {
				for (const auto& package : _localPackages) {
					manifest->content.erase(package.name);
				};
			}

			if (manifest->content.empty()) {
				WZ_LOG_WARNING("No packages to install was found! If you need to reinstall all installed packages, use the reinstall flag!");
				return;
			}

			Request([&] {
				for (const auto& [name, package] : manifest->content) {
					if (name.empty() || package.name != name) {
						WZ_LOG_ERROR("Package manifest: '{}' has different name in key and object: {} <-> {}", manifestUrl, name, package.name);
						continue;
					}
					InstallPackage(package);
				}
			}, func);
		}
	});

	_httpDownloader->WaitForAllRequests();
}

bool PackageManager::InstallPackage(const RemotePackage& package, std::optional<int32_t> requiredVersion) {
	auto localPackage = FindLocalPackage(package.name);
	if (localPackage.has_value()) {
		WZ_LOG_WARNING("Package: '{}' (v{}) already installed", package.name, localPackage->get().version);
		return false;
	}

	PackageOpt newVersion;
	if (requiredVersion.has_value()) {
		newVersion = package.Version(*requiredVersion);
		if (newVersion.has_value()) {
			const auto& version = newVersion->get();
			if (!IsSupportsPlatform(version.platforms))
				return false;
		} else {
			WZ_LOG_WARNING("Package: '{}' (v{}) has not been found", package.name, *requiredVersion);
			return false;
		}
	} else {
		newVersion = package.LatestVersion();
		if (newVersion.has_value()) {
			const auto& version = newVersion->get();
			if (!IsSupportsPlatform(version.platforms))
				return false;
		} else {
			WZ_LOG_WARNING("Package: '{}' (v[latest]]) has not been found", package.name);
			return false;
		}
	}

	return DownloadPackage(package, newVersion->get());
}

void PackageManager::UpdatePackage(const std::string& packageName, std::optional<int32_t> requiredVersion) {
	if (packageName.empty())
		return;

	Request([&] {
		auto package = FindLocalPackage(packageName);
		if (package.has_value()) {
			UpdatePackage(*package, requiredVersion);
		} else {
			WZ_LOG_ERROR("Package: {} not found", packageName);
		}
	}, __func__);
}

void PackageManager::UpdatePackages(std::span<const std::string> packageNames) {
	std::unordered_set<std::string> unique;
	unique.reserve(packageNames.size());
	Request([&] {
		std::ostringstream error;
		bool first = true;
		for (const auto& packageName: packageNames) {
			if (packageName.empty() || unique.contains(packageName))
				continue;
			auto package = FindLocalPackage(packageName);
			if (package.has_value()) {
				UpdatePackage(*package);
			} else {
				if (first) {
					error << "'" << packageName;
					first = false;
				} else {
					error << "', '" << packageName;
				}
			}
			unique.insert(packageName);
		}
		if (!first) {
			error << "'";
			WZ_LOG_ERROR("Not found {} packages(s)", error.str());
		}
	}, __func__);
}

void PackageManager::UpdateAllPackages() {
	Request([&] {
		for (const auto& package : _localPackages) {
			UpdatePackage(package);
		}
	}, __func__);
}

bool PackageManager::UpdatePackage(const LocalPackage& package, std::optional<int32_t> requiredVersion) {
	auto remotePackage = FindRemotePackage(package.name);
	if (!remotePackage.has_value()) {
		WZ_LOG_WARNING("Package: '{}' has not been found", package.name);
		return false;
	}

	const auto& newPackage =  remotePackage->get();
	PackageOpt newVersion;
	if (requiredVersion.has_value()) {
		newVersion = newPackage.Version(*requiredVersion);
		if (newVersion.has_value()) {
			const auto& version = newVersion->get();
			if (!IsSupportsPlatform(version.platforms))
				return false;

			WZ_LOG_INFO("Package '{}' (v{}) will be {}, to different version (v{})", package.name, package.version, version.version > package.version ? "upgraded" : version.version == package.version ? "reinstalled" : "downgraded", version.version);
		} else {
			WZ_LOG_WARNING("Package: '{}' (v{}) has not been found", package.name, *requiredVersion);
			return false;
		}
	} else {
		newVersion = newPackage.LatestVersion();
		if (newVersion.has_value()) {
			const auto& version = newVersion->get();
			if (!IsSupportsPlatform(version.platforms))
				return false;

			if (version.version > package.version) {
				WZ_LOG_INFO("Update available, prioritizing newer version (v{}) of '{}' package, over older version (v{}).", std::max(package.version, version.version), newPackage.name, std::min(package.version, version.version));
			} else {
				WZ_LOG_WARNING("Package: '{}' has no update available", package.name);
				return false;
			}
		} else {
			WZ_LOG_WARNING("Package: '{}' (v[latest]) has not been found", package.name);
			return false;
		}
	}

	return DownloadPackage(package, newVersion->get());
}

void PackageManager::UninstallPackage(const std::string& packageName) {
	if (packageName.empty())
		return;

	Request([&] {
		auto package = FindLocalPackage(packageName);
		if (package.has_value()) {
			UninstallPackage(*package);
		} else {
			WZ_LOG_ERROR("Package: {} not found", packageName);
		}
	}, __func__);
}

void PackageManager::UninstallPackages(std::span<const std::string> packageNames) {
	std::unordered_set<std::string> unique;
	unique.reserve(packageNames.size());
	Request([&] {
		std::ostringstream error;
		bool first = true;
		for (const auto& packageName: packageNames) {
			if (packageName.empty() || unique.contains(packageName))
				continue;
			auto package = FindLocalPackage(packageName);
			if (package.has_value()) {
				UninstallPackage(*package);
			} else {
				if (first) {
					error << "'" << packageName;
					first = false;
				} else {
					error << "', '" << packageName;
				}
			}
			unique.insert(packageName);
		}
		if (!first) {
			error << "'";
			WZ_LOG_ERROR("Not found {} packages(s)", error.str());
		}
	}, __func__);
}

void PackageManager::UninstallAllPackages() {
	Request([&] {
		for (const auto& package : _localPackages) {
			UninstallPackage(package, false);
		}
		_localPackages.clear();
	}, __func__);
}

bool PackageManager::UninstallPackage(const LocalPackage& package, bool remove) {
	WZ_ASSERT(package.path.has_parent_path(), "Package path doesn't contain parent path");
	auto packagePath = package.path.parent_path();
	std::error_code ec = FileSystem::RemoveFolder(packagePath);
	if (!ec) {
		if (remove)
			_localPackages.erase(std::find(_localPackages.begin(), _localPackages.end(), package));
		WZ_LOG_ERROR("Package: '{}' (v{}) was removed from: '{}'", package.name, package.version, packagePath.string());
		return true;
	}
	return false;
}

LocalPackageOpt PackageManager::FindLocalPackage(const std::string& packageName) {
	auto it = std::find_if(_localPackages.begin(), _localPackages.end(), [&packageName](const auto& plugin) {
		return plugin.name == packageName;
	});
	if (it != _localPackages.end())
		return *it;
	return {};
}

RemotePackageOpt PackageManager::FindRemotePackage(const std::string& packageName) {
	auto it = std::find_if(_remotePackages.begin(), _remotePackages.end(), [&packageName](const auto& plugin) {
		return plugin.name == packageName;
	});
	if (it != _remotePackages.end())
		return *it;
	return {};
}

std::vector<LocalPackageRef> PackageManager::GetLocalPackages() {
	std::vector<LocalPackageRef> localPackages;
	localPackages.reserve(_localPackages.size());
	for (const auto& package : _localPackages)  {
		localPackages.emplace_back(package);
	}
	return localPackages;
}

std::vector<RemotePackageRef> PackageManager::GetRemotePackages() {
	std::vector<RemotePackageRef> remotePackages;
	remotePackages.reserve(remotePackages.size());
	for (const auto& package : _remotePackages)  {
		remotePackages.emplace_back(package);
	}
	return remotePackages;
}

void PackageManager::Request(const std::function<void()>& action, std::string_view function) {
	auto debugStart = DateTime::Now();

	action();

	_httpDownloader->WaitForAllRequests();

	LoadLocalPackages();
	LoadRemotePackages();
	FindDependencies();

	WZ_LOG_DEBUG("{} processed in {}ms", function, (DateTime::Now() - debugStart).AsMilliseconds<float>());
}

bool PackageManager::DownloadPackage(const Package& package, const PackageVersion& version) const {
	/*if (!IsPackageAuthorized(package.name, version.version)) {
		WZ_LOG_WARNING("Tried to download a package that is not verified, aborting");
		return false;
	}*/

	WZ_LOG_VERBOSE("Start downloading: '{}'", package.name);

	auto wizard = _wizard.lock();
	WZ_ASSERT(wizard);

	_httpDownloader->CreateRequest(version.mirrors[0], [&name = package.name, plugin = (package.type == "plugin"), &baseDir = wizard->GetConfig().baseDir] // should be safe to pass ref
		(int32_t statusCode, const std::string& contentType, HTTPDownloader::Request::Data data) {
		if (statusCode == HTTPDownloader::HTTP_STATUS_OK) {
			WZ_LOG_VERBOSE("Done downloading: '{}'", name);

			if (contentType != "application/zip") {
				WZ_LOG_ERROR("Package: '{}' should be in *.zip format to be extracted correctly", name);
				return;
			}

			/*if (!IsPackageLegit(name, version, data)) {
				WZ_LOG_WARNING("Archive hash does not match expected checksum, aborting");
				return;
			}*/

			const auto& [folder, extension] = packageTypes[plugin];

			fs::path finalPath = baseDir / folder;
			fs::path finalLocation = finalPath / std::format("{}-{}", name, wizard::DateTime::Get("%Y_%m_%d_%H_%M_%S"));

			std::error_code ec;
			if (!fs::exists(finalLocation, ec) || !fs::is_directory(finalLocation, ec)) {
				if (!fs::create_directories(finalLocation, ec)) {
					WZ_LOG_ERROR("Error creating output directory '{}'", finalLocation.string());
				}
			}

			auto error = ExtractPackage(data, finalLocation, extension);
			if (error.empty()) {
				WZ_LOG_VERBOSE("Done extracting: '{}'", name);
				auto destinationPath = finalPath / name;
				ec = FileSystem::MoveFolder(finalLocation, destinationPath);
				if (ec) {
					WZ_LOG_ERROR("Package: '{}' could be renamed from '{}' to '{}' - {}", name, finalLocation.string(), destinationPath.string(), ec.message());
				}
			} else {
				WZ_LOG_ERROR("Failed extracting: '{}' - {}", name, error);
			}
		} else {
			WZ_LOG_ERROR("Failed downloading: '{}' - Code: {}", name, statusCode);
		}
	});

	return true;
}

std::string PackageManager::ExtractPackage(std::span<const uint8_t> packageData, const fs::path& extractPath, std::string_view descriptorExt) {
	WZ_LOG_VERBOSE("Start extracting....");

	auto zip_close = [](mz_zip_archive* zipArchive){ mz_zip_reader_end(zipArchive); delete zipArchive; };
	std::unique_ptr<mz_zip_archive, decltype(zip_close)> zipArchive(new mz_zip_archive, zip_close);
	std::memset(zipArchive.get(), 0, sizeof(mz_zip_archive));

	mz_zip_reader_init_mem(zipArchive.get(), packageData.data(), packageData.size(), 0);

	//state.total = zipArchive->m_archive_size;
	//state.progress = 0;

	size_t numFiles = mz_zip_reader_get_num_files(zipArchive.get());
	std::vector<mz_zip_archive_file_stat> fileStats(numFiles);

	bool foundDescriptor = false;

	for (uint32_t i = 0; i < numFiles; ++i) {
		mz_zip_archive_file_stat& fileStat = fileStats[i];

		if (!mz_zip_reader_file_stat(zipArchive.get(), i, &fileStat)) {
			return std::format("Error getting file stat: {}", i);
		}

		fs::path filename = fileStat.m_filename;
		if (filename.extension().string() == descriptorExt) {
			foundDescriptor = true;
		}
	}

	if (!foundDescriptor) {
		return std::format("Package descriptor *{} missing", descriptorExt);
	}

	for (uint32_t i = 0; i < numFiles; ++i) {
		mz_zip_archive_file_stat& fileStat = fileStats[i];

		std::vector<char> fileData(fileStat.m_uncomp_size);

		if (!mz_zip_reader_extract_to_mem(zipArchive.get(), i, fileData.data(), fileData.size(), 0)) {
			return std::format("Failed extracting file: '{}'", fileStat.m_filename);
		}

		fs::path finalPath = extractPath / fileStat.m_filename;
		fs::path finalDir = finalPath.parent_path();

		std::error_code ec;
		if (!fs::exists(finalDir, ec) || !fs::is_directory(finalDir, ec)) {
			if (!fs::create_directories(finalDir, ec)) {
				return std::format("Error creating output directory '{}'", finalDir.string());
			}
		}

		std::ofstream outputFile(finalPath, std::ios::binary);
		if (outputFile.is_open()) {
			outputFile.write(fileData.data(), static_cast<std::streamsize>(fileData.size()));
		} else {
			return std::format("Failed creating destination file: '{}'", fileStat.m_filename);
		}

		//state.progress += fileStat.m_comp_size;
		//state.ratio = std::roundf(static_cast<float>(_packageState.progress) / static_cast<float>(_packageState.total) * 100.0f);
	}

	return {};
}

/*bool PackageManager::IsPackageAuthorized(const std::string& packageName, int32_t packageVersion) const {
	if (!_config.packageVerification)
		return true;

	auto it = _packages.verified.find(packageName);
	if (it == _packages.verified.end())
		return false;

	return std::get<VerifiedPackageDetails>(*it).versions.contains(VerifiedPackageVersion{packageVersion, ""});
}

bool PackageManager::IsPackageLegit(const std::string& packageName, int32_t packageVersion, std::span<const uint8_t> packageData) const {
	if (!_config.packageVerification)
		return true;

	auto it = _packages.verified[packageName].versions.find(VerifiedPackageVersion{packageVersion, ""});

	std::vector<uint8_t> bytes(picosha2::k_digest_size);
	picosha2::hash256(packageData, bytes.begin(), bytes.end());
	std::string hash = picosha2::bytes_to_hex_string(bytes.begin(), bytes.end());

	WZ_LOG_VERBOSE("Expected checksum: {}", it->checksum);
	WZ_LOG_VERBOSE("Computed checksum: {}", hash);

	return it->checksum == hash;
}

void PackageManager::FetchPackagesListFromAPI() {
	if (!_config.packageVerification)
		return;

	if (!_config.packageVerifyUrl.empty()) {
		WZ_LOG_INFO("Found custom verified packages URL in config: '{}'", _config.packageVerifyUrl);
	} else {
		WZ_LOG_INFO("Custom verified packages URL not found in config, using default URL");
		_config.packageVerifyUrl = kDefaultPackageList;
	}

	WZ_LOG_INFO("Loading verification packages...");

	auto json = FetchJsonFromURL(_config.packageVerifyUrl);
	if (!json.has_value()) {
		WZ_LOG_ERROR("Verification packages from '{}' not found", _config.packageVerifyUrl);
		return;
	}

	auto packages = glz::read_json<VerifiedPackageMap>(*json);
	if (!packages.has_value()) {
		WZ_LOG_ERROR("Verification packages from '{}' has JSON parsing error: {}", _config.packageVerifyUrl, glz::format_error(packages.error(), *json));
		return;
	}

	_packages = std::move(*packages);

	if (_packages.verified.empty()) {
		WZ_LOG_WARNING("Empty verification packages list");
	} else {
		WZ_LOG_INFO("Done loading verified packages list.");
	}
}
 */