#include "wizard_provider.h"
#include "plugin_manager.h"
#include <wizard/wizard.h>

namespace wizard {
	class Wizard final : public IWizard, public std::enable_shared_from_this<Wizard> {
	public:
		Wizard() = default;
		~Wizard() override {
			if (_inited) {
				Terminate();
			}
		};

		bool Initialize() override {
			if (_inited) {
				return false;
			}

			_inited = true;

			WIZARD_LOG("Wizard Init!", ErrorLevel::INFO);
			WIZARD_LOG("Git: [" WIZARD_GIT_COMMIT_HASH "]:(" WIZARD_GIT_TAG ") - " WIZARD_GIT_COMMIT_SUBJECT " on " WIZARD_GIT_BRANCH " at '" WIZARD_GIT_COMMIT_DATE "'", ErrorLevel::INFO);
			WIZARD_LOG("Compiled on: " WIZARD_COMPILED_SYSTEM " from: " WIZARD_COMPILED_GENERATOR" with: '" WIZARD_COMPILED_COMPILER "'", ErrorLevel::INFO);

			_provider = std::make_shared<WizardProvider>(weak_from_this());
			_pluginManager = std::make_shared<PluginManager>(weak_from_this());

			return true;
		}

		void Terminate() override {
			if (!_inited) {
				return;
			}

			if (_pluginManager.use_count() != 1) {
				WIZARD_LOG("Lack of owning for plugin manager! Will not released on wizard terminate", ErrorLevel::ERROR);
			}
			_pluginManager.reset();

			_inited = false;

			WIZARD_LOG("Wizard Terminated!", ErrorLevel::INFO);
		}

		void Log(const std::string& msg, ErrorLevel level) override {
			WIZARD_LOG(msg, level);
		}

		void SetLogger(std::shared_ptr<ILogger> logger) override {
			LogSystem::SetLogger(std::move(logger));
		}

		std::weak_ptr<IPluginManager> GetPluginManager() override {
			return _pluginManager;
		}

		std::weak_ptr<IWizardProvider> GetProvider() override {
			return _provider;
		}

	private:
		bool _inited{ false };
		std::shared_ptr<PluginManager> _pluginManager;
		std::shared_ptr<WizardProvider> _provider;
	};

	std::shared_ptr<IWizard> MakeWizard() {
		return std::make_shared<Wizard>();
	}
}
