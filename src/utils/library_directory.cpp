#include "library_directory.h"
#include <utils/os.h>

using namespace wizard;

struct wizard::LibraryDirectoryHandle {
#if WIZARD_PLATFORM_WINDOWS
	DLL_DIRECTORY_COOKIE cookie;
#endif
};

static LibraryDirectoryHandle AddLibraryDirectory([[maybe_unused]] const std::filesystem::path& directoryPath) {
#if WIZARD_PLATFORM_WINDOWS
	return { AddDllDirectory(directoryPath.c_str()) };
#else
	return {};
#endif
}

static void RemoveLibraryDirectory([[maybe_unused]] LibraryDirectoryHandle handle) {
#if WIZARD_PLATFORM_WINDOWS
	RemoveDllDirectory(handle.cookie);
#endif
}

LibraryDirectory::LibraryDirectory(const std::filesystem::path& directoryPath) : _handle{std::make_unique<LibraryDirectoryHandle>(AddLibraryDirectory(directoryPath))} {
}

LibraryDirectory::LibraryDirectory(LibraryDirectory&& other) = default;

LibraryDirectory::~LibraryDirectory() {
	RemoveLibraryDirectory(*_handle);
}
