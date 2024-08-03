#pragma once

#include <cstdint>
#include <optional>
#include <filesystem>
#include <plugify/reference_wrapper.h>
#include <plugify_export.h>

namespace plugify {
	class Module;
	class LanguageModuleDescriptorRef;

	/**
	 * @enum ModuleState
	 * @brief Represents the possible states of a module.
	 *
	 * The ModuleState enum defines the various states that a module can be in,
	 * such as NotLoaded, Error, Loaded, and Unknown.
	 */
	enum class ModuleState : uint8_t {
		NotLoaded,
		Error,
		Loaded,
		Unknown,
	};

	/**
	 * @typedef UniqueId
	 * @brief Represents a unique identifier for modules.
	 */
	using UniqueId = std::ptrdiff_t;

	/**
	 * @class ModuleRef
	 * @brief Reference wrapper to access language module's information.
	 */
	class PLUGIFY_API ModuleRef : public Ref<const Module> {
		using Ref::Ref;
	public:
		/**
		 * @brief Get the unique identifier of the language module.
		 * @return The unique identifier.
		 */
		[[nodiscard]] UniqueId GetId() const noexcept;

		/**
		 * @brief Get the name of the language module.
		 * @return The name of the language module.
		 */
		[[nodiscard]] const std::string& GetName() const noexcept;

		/**
		 * @brief Get the language of the language module.
		 * @return The language of the language module.
		 */
		[[nodiscard]] const std::string& GetLanguage() const noexcept;

		/**
		 * @brief Get the friendly name of the language module.
		 * @return The friendly name of the language module.
		 */
		[[nodiscard]] const std::string& GetFriendlyName() const noexcept;

		/**
		 * @brief Get the file path of the language module.
		 * @return The file path as a filesystem path.
		 */
		[[nodiscard]] const std::filesystem::path& GetFilePath() const noexcept;

		/**
		 * @brief Get the base directory of the language module.
		 * @return The base directory as a filesystem path.
		 */
		[[nodiscard]] const std::filesystem::path& GetBaseDir() const noexcept;

		/**
		 * @brief Get the descriptor of the language module.
		 * @return The descriptor of the language module.
		 */
		[[nodiscard]] LanguageModuleDescriptorRef GetDescriptor() const noexcept;

		/**
		 * @brief Get the state of the language module.
		 * @return The state of the language module.
		 */
		[[nodiscard]] ModuleState GetState() const noexcept;

		/**
		 * @brief Get the error message associated with the language module.
		 * @return The error message.
		 */
		[[nodiscard]] const std::string& GetError() const noexcept;

		/**
		 * @brief Find a resource file associated with the module.
		 *
		 * This method attempts to find a resource file located within the module's directory structure.
		 * If the resource file is found, its path is returned. If the resource file does not exist
		 * within the module's directory, std::nullopt is returned.
		 *
		 * If a user-overridden file exists in the base directory of Plugify with the same name and path,
		 * the path returned by this function will direct to that overridden file.
		 *
		 * @param path The relative path to the resource file.
		 * @return An optional containing the absolute path to the resource file if found, or std::nullopt otherwise.
		 *
		 * @code
		 * Example:
		 * // Assuming the module name is "sample_module"
		 * // File located at: plugify/modules/sample_module/configs/core.cfg
		 * // User-overridden file could be located at: plugify/configs/core.cfg
		 * auto resourcePath = module.FindResource("configs/core.cfg");
		 * @endcode
		 */
		[[nodiscard]] std::optional<std::filesystem::path> FindResource(const std::filesystem::path& path) const;
	};
	static_assert(is_ref_v<ModuleRef>);

	/**
	 * @brief Namespace containing utility functions of ModuleState enum.
	 */
	namespace ModuleUtils {
		/**
		 * @brief Convert a ModuleState enum value to its string representation.
		 * @param state The ModuleState value to convert.
		 * @return The string representation of the ModuleState.
		 */
		[[maybe_unused]] constexpr std::string_view ToString(ModuleState state) noexcept {
			switch (state) {
				case ModuleState::NotLoaded: return "NotLoaded";
				case ModuleState::Error:     return "Error";
				case ModuleState::Loaded:    return "Loaded";
				default:                     return "Unknown";
			}
		}

		/**
		 * @brief Convert a string representation to a ModuleState enum value.
		 * @param state The string representation of ModuleState.
		 * @return The corresponding ModuleState enum value.
		 */
		[[maybe_unused]] constexpr ModuleState FromString(std::string_view state) noexcept {
			if (state == "NotLoaded") {
				return ModuleState::NotLoaded;
			} else if (state == "Error") {
				return ModuleState::Error;
			} else if (state == "Loaded") {
				return ModuleState::Loaded;
			}
			return ModuleState::Unknown;
		}
	} // namespace ModuleUtils

} // namespace plugify
