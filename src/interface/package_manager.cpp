#include <wizard/package_manager.h>
#include <core/package_manager.h>

using namespace wizard;

IPackageManager::IPackageManager(PackageManager& impl) : _impl{impl} {
}

void IPackageManager::UpdatePackages() {
	return _impl.UpdateAllPackages();
}

void IPackageManager::InstallPackages(const fs::path& manifestFilePath, bool reinstall) {
	return _impl.InstallAllPackages(manifestFilePath, reinstall);
}

void IPackageManager::SnapshotPackages(const fs::path& manifestFilePath, bool prettify) {
	return _impl.SnapshotPackages(manifestFilePath, prettify);
}