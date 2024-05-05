#pragma once

#include <plugify/method.h>
#include <asmjit/asmjit.h>
#include <memory>
#include <functional>

namespace plugify {
	/**
	 * @struct Parameters
	 * @brief Structure to represent function parameters.
	 */
	struct Parameters {
		/**
		 * @brief Set the value of the argument at the specified index.
		 * @tparam T Type of the argument.
		 * @param idx Index of the argument.
		 * @param val Value to set.
		 * @noreturn
		 */
		template<typename T>
		void SetArgument(uint8_t idx, T val) const {
			*(T*) GetArgumentPtr(idx) = val;
		}

		/**
		 * @brief Get the value of the argument at the specified index.
		 * @tparam T Type of the argument.
		 * @param idx Index of the argument.
		 * @return Value of the argument.
		 */
		template<typename T>
		[[nodiscard]] T GetArgument(uint8_t idx) const {
			return *(T*) GetArgumentPtr(idx);
		}

		volatile uintptr_t arguments; ///< Raw storage for function arguments.

		/**
		 * @brief Get a pointer to the argument at the specified index.
		 * @param idx Index of the argument.
		 * @return Pointer to the argument.
		 */
		[[nodiscard]] int8_t* GetArgumentPtr(uint8_t idx) const {
			return ((int8_t*)&arguments) + sizeof(uintptr_t) * idx;
		}
	};

	/**
	 * @struct ReturnValue
	 * @brief Structure to represent the return value of a function.
	 */
	struct ReturnValue {
		/**
		 * @brief Set the return value.
		 * @tparam T Type of the return value.
		 * @param val Value to set as the return value.
		 * @noreturn
		 */
		template<typename T>
		void SetReturnPtr(T val) const {
			*(T*) GetReturnPtr() = val;
		}

		/**
		 * @brief Get a pointer to the return value.
		 * @return Pointer to the return value.
		 */
		[[nodiscard]] uint8_t* GetReturnPtr() const {
			return (uint8_t*)&ret;
		}

		uintptr_t ret; ///< Raw storage for the return value.
	};

	/**
	 * @class Function
	 * @brief Class for dynamic function generation.
	 */
	class Function {
	public:
		/**
		 * @brief Constructor.
		 * @param rt Weak pointer to the asmjit::JitRuntime.
		 */
		explicit Function(std::weak_ptr<asmjit::JitRuntime> rt);

		/**
		 * @brief Move constructor.
		 * @param other Another instance of Function.
		 */
		Function(Function&& other) noexcept;

		/**
		 * @brief Destructor.
		 */
		~Function();

		using FuncCallback = void(*)(const Method* method, void* data, const Parameters* params, uint8_t count, const ReturnValue* ret);

		/**
		 * @brief Get a dynamically created callback function based on the raw signature.
		 * @param sig Function signature.
		 * @param method Reference to the method.
		 * @param callback Callback function.
		 * @param data User data.
		 * @return Pointer to the generated function.
		 */
		void* GetJitFunc(const asmjit::FuncSignature& sig, const Method& method, FuncCallback callback, void* data = nullptr);

		/**
		 * @brief Get a dynamically created callback function using a typedef represented as a string.
		 * @param method Reference to the method.
		 * @param callback Callback function.
		 * @param data User data.
		 * @param obj If true, return will be pass as first argument.
		 * @return Pointer to the generated function.
		 */
		void* GetJitFunc(const Method& method, FuncCallback callback, void* data = nullptr, bool obj = true);

		/**
		 * @brief Get a dynamically created function.
		 * @return Pointer to the already generated function.
		 * @note The returned pointer can be nullptr if function is not generate.
		 */
		[[nodiscard]] void* GetFunction() const { return _function; }

		/**
		 * @brief Get the user data associated with the object.
		 * @details This function returns a pointer to the user data associated with the object.
		 * @return A void pointer to the user data.
		 * @note The returned pointer can be nullptr if no user data is set.
		 */
		[[nodiscard]] void* GetUserData() const { return _userData; }

		/**
		 * @brief Get the error message, if any.
		 * @return Error message.
		 */
		[[nodiscard]] const std::string& GetError() { return _error; }

	private:
		static asmjit::CallConvId GetCallConv(const std::string& conv);
		static asmjit::TypeId GetTypeId(ValueType type);

		static bool IsGeneralReg(asmjit::TypeId typeId) ;
		static bool IsXmmReg(asmjit::TypeId typeId) ;

	private:
		std::weak_ptr<asmjit::JitRuntime> _rt;
		void* _function{ nullptr };
		void* _userData{ nullptr };
		std::string _error;
	};
} // namespace plugify