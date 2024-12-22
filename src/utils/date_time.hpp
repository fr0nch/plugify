#pragma once

#include <chrono>
#include <sstream>

namespace plugify {
	using namespace std::chrono_literals;

	class DateTime {
	public:
		DateTime() = default;
		~DateTime() = default;

		template<typename Rep, typename Period>
		constexpr DateTime(const std::chrono::duration<Rep, Period>& duration) noexcept : _value{std::chrono::duration_cast<std::chrono::microseconds>(duration).count()} {}

		template<typename T = float>
		constexpr static DateTime Seconds(const T& seconds) noexcept { return {std::chrono::duration<T>(seconds)}; }

		template<typename T = double>
		constexpr static DateTime Milliseconds(const T& milliseconds) noexcept { return {std::chrono::duration<T, std::micro>(milliseconds)}; }

		template<typename T = uint64_t>
		constexpr static DateTime Microseconds(const T& microseconds) noexcept { return {std::chrono::duration<T, std::micro>(microseconds)}; }

		template<typename T = float>
		constexpr auto AsSeconds() const noexcept { return static_cast<T>(_value.count()) / static_cast<T>(1000000); }

		template<typename T = double>
		constexpr auto AsMilliseconds() const noexcept { return static_cast<T>(_value.count()) / static_cast<T>(1000); }

		template<typename T = uint64_t>
		constexpr auto AsMicroseconds() const noexcept { return static_cast<T>(_value.count()); }

		static DateTime Now() noexcept {
			static const auto localEpoch = std::chrono::high_resolution_clock::now();
			return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - localEpoch);
		}

		static std::string Get(std::string_view format = "%Y-%m-%d %H:%M:%S") {
			auto now = std::chrono::system_clock::now();
			auto timeT = std::chrono::system_clock::to_time_t(now);
			std::tm localTime;
#ifdef _WIN32
			localtime_s(&localTime, &timeT); // Windows-specific
#else
			localtime_r(&timeT, &localTime); // POSIX-compliant
#endif
			std::stringstream ss;
			ss << std::put_time(&localTime, format.data());
			return ss.str();
		}

		template<typename Rep, typename Period>
		constexpr explicit operator std::chrono::duration<Rep, Period>() const noexcept {
			return std::chrono::duration_cast<std::chrono::duration<Rep, Period>>(_value);
		}

		//constexpr auto operator<=>(const DateTime& rhs) const { return _value <=> rhs._value; }
		constexpr bool operator==(const DateTime& rhs) const noexcept { return _value == rhs._value; }
		constexpr bool operator!=(const DateTime& rhs) const noexcept { return _value != rhs._value; }
		constexpr bool operator<(const DateTime& rhs) const noexcept { return _value < rhs._value; }
		constexpr bool operator<=(const DateTime& rhs) const noexcept { return _value <= rhs._value; }
		constexpr bool operator>(const DateTime& rhs) const noexcept { return _value > rhs._value; }
		constexpr bool operator>=(const DateTime& rhs) const noexcept { return _value >= rhs._value; }
		constexpr DateTime operator-() const noexcept { return { -_value}; }

		constexpr friend DateTime operator+(const DateTime& lhs, const DateTime& rhs) noexcept { return lhs._value + rhs._value; }
		constexpr friend DateTime operator-(const DateTime& lhs, const DateTime& rhs) noexcept { return lhs._value - rhs._value; }
		constexpr friend DateTime operator*(const DateTime& lhs, float rhs) noexcept { return lhs._value * rhs; }
		constexpr friend DateTime operator*(const DateTime& lhs, int64_t rhs) noexcept { return lhs._value * rhs; }
		constexpr friend DateTime operator*(float lhs, const DateTime& rhs) noexcept { return rhs * lhs; }
		constexpr friend DateTime operator*(int64_t lhs, const DateTime& rhs) noexcept { return rhs * lhs; }
		constexpr friend DateTime operator/(const DateTime& lhs, float rhs) noexcept { return lhs._value / rhs; }
		constexpr friend DateTime operator/(const DateTime& lhs, int64_t rhs) noexcept { return lhs._value / rhs; }
		constexpr friend double operator/(const DateTime& lhs, const DateTime& rhs) noexcept { return static_cast<double>(lhs._value.count()) / static_cast<double>(rhs._value.count()); }
		
		template<typename Period = std::ratio<1, 1>>
		constexpr friend double operator%(const DateTime& lhs, const DateTime& rhs) {
			return std::modf(std::chrono::duration_cast<std::chrono::duration<double, Period>>(lhs._value), std::chrono::duration_cast<std::chrono::duration<double, Period>>(rhs._value));
		}

		constexpr DateTime& operator+=(const DateTime& rhs) noexcept { return *this = *this + rhs; }
		constexpr DateTime& operator-=(const DateTime& rhs) noexcept { return *this = *this - rhs; }
		constexpr DateTime& operator*=(float rhs) noexcept { return *this = *this * rhs; }
		constexpr DateTime& operator*=(int64_t rhs) noexcept{ return *this = *this * rhs; }
		constexpr DateTime& operator/=(float rhs) noexcept { return *this = *this / rhs; }
		constexpr DateTime& operator/=(int64_t rhs) noexcept { return *this = *this / rhs; }

	private:
		std::chrono::microseconds _value{};
	};
}
