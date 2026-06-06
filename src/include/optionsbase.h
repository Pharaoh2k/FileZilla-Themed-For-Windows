#ifndef FILEZILLA_ENGINE_OPTIONSBASE_HEADER
#define FILEZILLA_ENGINE_OPTIONSBASE_HEADER

#include "visibility.h"

#include <memory>
#include <string>
#include <vector>
#include <map>

#include <libfilezilla/event.hpp>
#include <libfilezilla/mutex.hpp>
#include <libfilezilla/rwmutex.hpp>
#include <libfilezilla/string.hpp>
#include <libfilezilla/util.hpp>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_LIBPUGIXML
#include <pugixml.hpp>
#else
#include "../pugixml/pugixml.hpp"
#endif

namespace pugi {
class xml_document;
class xml_node;
}

namespace fz {
	class event_handler;
}

enum class optionsIndex : int
{
	invalid = -1
};

enum class option_type
{
	string,
	number,
	boolean,
	xml
};

enum class option_flags
{
	normal = 0,
	internal = 1, // internal items won't get written to settings file nor loaded from there
	predefined_only = 2,
	predefined_priority = 4, // If that option is given in fzdefaults.xml, it overrides any user option
	platform = 8, // A non-portable platform specific option, nodes have platform attribute
	numeric_clamp = 16, // For numeric options, fixup input and clamp to allowed value range. If not set, setting invalid values discards
	sensitive_data = 32, // Flag to mark sensitive data. Can be used to clear private data even without domain knowledge.
	product = 64 // Product-specific options in common code
};
inline bool operator&(option_flags lhs, option_flags rhs) {
	return (static_cast<std::underlying_type_t<option_flags>>(lhs) & static_cast<std::underlying_type_t<option_flags>>(rhs)) != 0;
}
inline option_flags operator|(option_flags lhs, option_flags rhs)
{
	return static_cast<option_flags>(static_cast<std::underlying_type_t<option_flags>>(lhs) | static_cast<std::underlying_type_t<option_flags>>(rhs));
}
inline option_flags& operator|=(option_flags& lhs, option_flags rhs)
{
	lhs = lhs | rhs;
	return lhs;
}

struct FZC_PUBLIC_SYMBOL option_def final
{
	option_def(std::string_view name, std::wstring_view def, option_flags flags = option_flags::normal, size_t max_len = 10000000);
	option_def(std::string_view name, std::wstring_view def, option_flags flags, option_type t, size_t max_len = 10000000, bool (*validator)(std::wstring& v) = nullptr);
	option_def(std::string_view name, std::wstring_view def, option_flags flags, bool (*validator)(pugi::xml_node&));
	option_def(std::string_view name, int def, option_flags flags = option_flags::normal, int min = -2147483648, int max = 2147483647, bool (*validator)(int& v) = nullptr, std::vector<std::wstring_view> mnemonics = {});

	template<typename Bool, std::enable_if_t<std::is_same_v<Bool, bool>, int> = 0> // avoid implicit wchar_t*->bool conversion
	option_def(std::string_view name, Bool def, option_flags flags = option_flags::normal);

	inline std::string const& name() const { return name_; }
	inline std::wstring const& def() const { return default_; }
	inline option_type type() const { return type_; }
	inline option_flags flags() const { return flags_; }
	int min() const { return min_; }
	int max() const { return max_; }
	void* validator() const { return validator_; }
	std::vector<std::wstring_view> const& mnemonics() const { return mnemonics_; }

	int val_from_mnemonic(std::wstring_view const& val) const {
		return static_cast<int>(std::distance(mnemonics_.begin(), std::find(mnemonics_.begin(), mnemonics_.end(), val)));
	}

private:
	std::string name_;
	std::wstring default_; // Default values are stored as string even for numerical options
	option_type type_{};
	option_flags flags_{};
	int min_{};
	int max_{};
	void* validator_{};
	std::vector<std::wstring_view> mnemonics_;
};

struct FZC_PUBLIC_SYMBOL option_registrator
{
	option_registrator(unsigned int (*f)())
	{
		f();
	}
};
unsigned int FZC_PUBLIC_SYMBOL register_options(std::initializer_list<option_def> options);

struct FZC_PUBLIC_SYMBOL watched_options final
{
	explicit operator bool() const {
		return any();
	}

	template<typename Opt>
	bool test(Opt opt) const
	{
		return test(mapOption(opt));
	}

	bool any() const;
	void set(optionsIndex opt);
	void unset(optionsIndex opt);
	bool test(optionsIndex opt) const;

	void clear() {
		options_.clear();
	}

	watched_options& operator&=(watched_options const& op) {
		return *this &= op.options_;
	}
	watched_options& operator&=(std::vector<uint64_t> const& op);

	std::vector<uint64_t> options_;
};

struct options_changed_event_type{};
typedef fz::simple_event<options_changed_event_type, watched_options> options_changed_event;


typedef void (*watcher_notifier)(void* handler, watched_options&& options);
std::tuple<void*, watcher_notifier> get_option_watcher_notifier(fz::event_handler* handler);

class FZC_PUBLIC_SYMBOL COptionsBase
{
public:
	virtual ~COptionsBase() noexcept = default;

	template<typename T>
	std::wstring get_string(T opt)
	{
		return get_string(mapOption(opt));
	}

	template<typename T>
	int get_int(T opt)
	{
		return get_int(mapOption(opt));
	}

	template<typename R, typename T>
	R get(T opt)
	{
		return fz::clamped_cast<R>(get_int(mapOption(opt)));
	}

	template<typename T>
	bool get_bool(T opt)
	{
		return get_int(opt) != 0;
	}

	template<typename T>
	pugi::xml_document get_xml(T opt)
	{
		return get_xml(mapOption(opt));
	}

	template <typename T>
	std::wstring_view get_mnemonic(T opt)
	{
		return get_mnemonic(mapOption(opt));
	}

	template<typename T>
	bool predefined(T opt)
	{
		return predefined(mapOption(opt));
	}
	bool predefined(optionsIndex opt);

	template<typename T>
	void set(T opt, std::wstring_view const& value)
	{
		set(mapOption(opt), value);
	}

	template<typename T>
	void set(T opt, int value)
	{
		set(mapOption(opt), value);
	}

	template<typename T>
	void set(T opt, pugi::xml_node const& value)
	{
		set(mapOption(opt), value);
	}

	template<typename Opt, typename Handler>
	void watch(Opt opt, Handler* handler)
	{
		watch(mapOption(opt), handler);
	}

	template<typename Opt, typename Handler>
	void unwatch(Opt opt, Handler* handler)
	{
		unwatch(mapOption(opt), handler);
	}

	template<typename Handler>
	void watch(optionsIndex opt, Handler* handler)
	{
		watch(opt, get_option_watcher_notifier(handler));
	}

	template<typename Handler>
	void watch_all(Handler* handler)
	{
		watch_all(get_option_watcher_notifier(handler));
	}

	template<typename Handler>
	void unwatch(optionsIndex opt, Handler* handler)
	{
		unwatch(opt, get_option_watcher_notifier(handler));
	}

	template<typename Handler>
	void unwatch_all(Handler* handler)
	{
		unwatch_all(get_option_watcher_notifier(handler));
	}

	template<typename T>
	uint64_t change_count(T opt)
	{
		return change_count(mapOption(opt));
	}

	struct option_value final
	{
		std::wstring str_;
		std::unique_ptr<pugi::xml_document> xml_{};
		uint64_t change_counter_{};
		int v_{};
		bool predefined_{};
	};


	std::wstring get_string(optionsIndex opt);
	void set(optionsIndex opt, std::wstring_view const& value, bool predefined = false);
	bool validate(optionsIndex opt, std::wstring_view const& value);

protected:
	template<typename T>
	void set(T opt, std::wstring_view const& value, bool predefined)
	{
		set(mapOption(opt), value, predefined);
	}

	void add_missing(fz::scoped_write_lock & l);

	int get_int(optionsIndex opt);
	pugi::xml_document get_xml(optionsIndex opt);
	std::wstring_view get_mnemonic(optionsIndex opt);

	bool validate(option_def const& def, int value);
	bool validate(option_def const& def, std::wstring_view const& value);
	bool validate(option_def const& def, pugi::xml_document const& value);

	void set(optionsIndex opt, int value);
	void set(optionsIndex opt, pugi::xml_node const& value);

	void set(optionsIndex opt, option_def const& def, option_value& val, int value, bool predefined = false);
	void set(optionsIndex opt, option_def const& def, option_value& val, std::wstring_view const& value, bool predefined = false);
	void set(optionsIndex opt, option_def const& def, option_value& val, pugi::xml_document&& value, bool predefined = false);

	void set_changed(optionsIndex opt);

	void watch(optionsIndex opt, std::tuple<void*, watcher_notifier> handler);
	void watch_all(std::tuple<void*, watcher_notifier> handler);
	void unwatch(optionsIndex opt, std::tuple<void*, watcher_notifier> handler);
	void unwatch_all(std::tuple<void*, watcher_notifier> handler);

	uint64_t change_count(optionsIndex opt);

	void set_default_value(optionsIndex opt);

	fz::rwmutex mtx_;

	std::vector<option_def> options_;
	std::map<std::string, size_t, std::less<>> name_to_option_;
	std::vector<option_value> values_;

	bool can_notify_{};
	watched_options changed_;

	virtual void notify_changed() = 0;
	void continue_notify_changed();

	// Gets called from continue_notify_changed with mtx_ held with write_lock.
	virtual void process_changed(watched_options const&) {}


	fz::mutex notification_mtx_;


	struct watcher final
	{
		void* handler_{};
		watcher_notifier notifier_{};
		watched_options options_;
		bool all_{};
	};
	std::vector<watcher> watchers_;
};

#endif
