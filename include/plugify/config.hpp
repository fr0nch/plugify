#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <plugify/log.hpp>

namespace plugify {
	/**
	 * @struct Config
	 * @brief Represents configuration settings for a program.
	 *
	 * This struct encapsulates various configuration parameters.
	 */
	struct Config final {
		std::filesystem::path baseDir; ///< The base directory for the program.
		Severity logSeverity{ Severity::Verbose }; ///< The severity level for logging.
		std::set<std::string> repositories; ///< A collection of repository paths.
		bool preferOwnSymbols{ false }; ///< Flag indicating if the modules should prefer its own symbols over shared symbols.
	};
} // namespace plugify
