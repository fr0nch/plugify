#pragma once

#include <plugify/vector.h>
#include <plugify/compat_format.h>

#include <cstddef>
#include <stdexcept>
#include <vector>

template<typename T>
std::string join(const T& vec) {
	std::string result;
	if (!vec.empty()) {
		result = std::format("{}", vec[0]);
		for (auto it = std::next(vec.begin()); it != vec.end(); ++it) {
			std::format_to(std::back_inserter(result), ", {}", *it);
		}
	}
	return std::format("{{{}}}", result);
}

template <typename VecA, typename VecB>
void assert_eq(VecA const& a, VecB const& b) {
    if (a.size() != b.size()) {
        throw std::runtime_error(std::format("vec size != svec size: {} != {}", a.size(), b.size()));
    }
    if (!std::equal(a.begin(), a.end(), b.begin(), b.end())) {
        throw std::runtime_error(
				std::format("vec content != svec content:\n[{}]\n[{}]", join(a), join(b)));
    }
}

template <class T>
class VecTester {
    std::vector<T> _v{};
    plg::vector<T> _s{};

public:
    template <class... Args>
    void emplace_back(Args&&... args) {
        _v.emplace_back(std::forward<Args>(args)...);
        _s.emplace_back(std::forward<Args>(args)...);
        assert_eq(_v, _s);
    }

    template <class... Args>
    void emplace_at(size_t idx, Args&&... args) {
        auto it_v = _v.emplace(_v.begin() + idx, std::forward<Args>(args)...);
        auto it_s = _s.emplace(_s.cbegin() + idx, std::forward<Args>(args)...);
        REQUIRE(*it_v == *it_s);
        assert_eq(_v, _s);
    }

    [[nodiscard]] auto size() const -> size_t {
        return _v.size();
    }
};
