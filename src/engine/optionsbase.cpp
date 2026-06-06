#include "../include/optionsbase.h"

#include <libfilezilla/event_handler.hpp>

option_def::option_def(std::string_view name, std::wstring_view def, option_flags flags, size_t max_len)
	: name_(name)
	, default_(def)
	, type_(option_type::string)
	, flags_(flags)
	, max_(static_cast<int>(max_len))
{}

option_def::option_def(std::string_view name, std::wstring_view def, option_flags flags, option_type t, size_t max_len, bool (*validator)(std::wstring& v))
	: name_(name)
	, default_(def)
	, type_(t)
	, flags_(flags)
	, max_(static_cast<int>(max_len))
	, validator_((t == option_type::string) ? reinterpret_cast<void*>(validator) : nullptr)
{
}

option_def::option_def(std::string_view name, std::wstring_view def, option_flags flags, bool (*validator)(pugi::xml_node&))
	: name_(name)
	, default_(def)
	, type_(option_type::xml)
	, flags_(flags)
	, max_(10000000)
	, validator_(reinterpret_cast<void*>(validator))
{}

option_def::option_def(std::string_view name, int def, option_flags flags, int min, int max, bool (*validator)(int& v), std::vector<std::wstring_view> mnemonics)
	: name_(name)
	, default_(fz::to_wstring(def))
	, type_(option_type::number)
	, flags_(flags)
	, min_(min)
	, max_(max)
	, validator_(reinterpret_cast<void*>(validator))
	, mnemonics_(std::move(mnemonics))
{}

template<>
FZC_PUBLIC_SYMBOL option_def::option_def(std::string_view name, bool def, option_flags flags)
	: name_(name)
	, default_(fz::to_wstring(def))
	, type_(option_type::boolean)
	, flags_(flags)
	, max_(1)
{}



namespace {
void event_handler_option_watcher_notifier(void* handler, watched_options&& options)
{
	static_cast<fz::event_handler*>(handler)->send_event<options_changed_event>(std::move(options));
}
}

std::tuple<void*, watcher_notifier> get_option_watcher_notifier(fz::event_handler * handler)
{
	return std::make_tuple(handler, &event_handler_option_watcher_notifier);
}

namespace {
class option_registry final {
public:
	fz::mutex mtx_;
	std::vector<option_def> options_;
	std::map<std::string, size_t, std::less<>> name_to_option_;
};

std::pair<option_registry&, fz::scoped_lock> get_option_registry()
{
	static option_registry reg;
	return std::make_pair(std::ref(reg), fz::scoped_lock(reg.mtx_));
}
}

unsigned int register_options(std::initializer_list<option_def> options)
{
	auto registry = get_option_registry();
	size_t const prev = registry.first.options_.size();
	registry.first.options_.insert(registry.first.options_.end(), options);
	for (size_t i = prev; i < registry.first.options_.size(); ++i) {
		auto const& n = registry.first.options_[i].name();
		if (!n.empty()) {
			registry.first.name_to_option_[n] = i;
		}
	}

	return static_cast<unsigned int>(prev);
}

namespace {
void set_default_value(size_t i, std::vector<option_def>& options, std::vector<COptionsBase::option_value>& values)
{
	auto& val = values[i];
	auto const& def = options[i];

	if (def.type() == option_type::xml) {
		val.xml_ = std::make_unique<pugi::xml_document>();
		val.xml_->load_string(fz::to_utf8(def.def()).c_str());
	}
	else {
		val.str_ = def.def();
		val.v_ = fz::to_integral<int>(def.def());
	}
}

template<typename Lock>
bool do_add_missing(optionsIndex opt, Lock & l, fz::rwmutex & mtx, std::vector<option_def> & options, std::map<std::string, size_t, std::less<>> & name_to_option, std::vector<COptionsBase::option_value> & values)
{
	l.unlock();

	{
		auto registry = get_option_registry();

		if (static_cast<size_t>(opt) >= registry.first.options_.size()) {
			return false;
		}

		mtx.lock_write();

		options = registry.first.options_;
		name_to_option = registry.first.name_to_option_;
	}

	size_t i = values.size();
	values.resize(options.size());

	for (; i < options.size(); ++i) {
		set_default_value(i, options, values);
	}
	mtx.unlock_write();
	l.lock();
	return true;
}
}

void COptionsBase::add_missing(fz::scoped_write_lock & l)
{
	do_add_missing(static_cast<optionsIndex>(0), l, mtx_, options_, name_to_option_, values_);
}

int COptionsBase::get_int(optionsIndex opt)
{
	if (opt == optionsIndex::invalid) {
		return 0;
	}

	fz::scoped_read_lock l(mtx_);
	if (static_cast<size_t>(opt) >= values_.size() && !do_add_missing(opt, l, mtx_, options_, name_to_option_, values_)) {
		return 0;
	}

	auto& val = values_[static_cast<size_t>(opt)];
	return val.v_;
}

std::wstring COptionsBase::get_string(optionsIndex opt)
{
	if (opt == optionsIndex::invalid) {
		return std::wstring();
	}

	fz::scoped_read_lock l(mtx_);
	if (static_cast<size_t>(opt) >= values_.size() && !do_add_missing(opt, l, mtx_, options_, name_to_option_, values_)) {
		return std::wstring();
	}

	auto& val = values_[static_cast<size_t>(opt)];
	return val.str_;
}

pugi::xml_document COptionsBase::get_xml(optionsIndex opt)
{
	pugi::xml_document ret;
	if (opt == optionsIndex::invalid) {
		return ret;
	}

	fz::scoped_write_lock l(mtx_); // Aquire write lock as we don't know what pugixml does internally
	if (static_cast<size_t>(opt) >= values_.size() && !do_add_missing(opt, l, mtx_, options_, name_to_option_, values_)) {
		return ret;
	}

	auto& val = values_[static_cast<size_t>(opt)];
	if (val.xml_) {
		for (auto c = val.xml_->first_child(); c; c = c.next_sibling()) {
			ret.append_copy(c);
		}
	}
	return ret;
}

bool COptionsBase::predefined(optionsIndex opt)
{
	fz::scoped_read_lock l(mtx_);
	if (opt == optionsIndex::invalid || static_cast<size_t>(opt) >= values_.size()) {
		// No need for add_missing, predefined_ can only be set from set()
		return false;
	}

	auto& val = values_[static_cast<size_t>(opt)];
	return val.predefined_;
}

bool COptionsBase::validate(option_def const& def, int value)
{
	if (def.type() == option_type::number) {
		if (value < def.min() || value > def.max()) {
			if (!(def.flags() & option_flags::numeric_clamp)) {
				return false;
			}
		}
		if (def.validator()) {
			return reinterpret_cast<bool (*)(int&)>(def.validator())(value);
		}
	}

	return true;
}

bool COptionsBase::validate(option_def const& def, std::wstring_view const& value)
{
	if (def.type() == option_type::number) {
		auto v = fz::to_integral<int>(value, std::numeric_limits<int>::min());
		if (v == std::numeric_limits<int>::min()) {
			if (def.mnemonics().empty()) {
				return false;
			}
			v = def.val_from_mnemonic(value);
		}
		return validate(def, v);
	}
	else if (def.type() == option_type::string) {
		if (def.validator()) {
			std::wstring v(value);
			return reinterpret_cast<bool (*)(std::wstring&)>(def.validator())(v);
		}
	}

	return true;
}

bool COptionsBase::validate(optionsIndex opt, std::wstring_view const& value)
{
	fz::scoped_read_lock l(mtx_);
	auto const& def = options_[static_cast<size_t>(opt)];
	return validate(def, value);
}

bool COptionsBase::validate(option_def const& def, pugi::xml_document const& value)
{
	if (def.validator()) {
		return reinterpret_cast<bool (*)(pugi::xml_node const&)>(def.validator())(value);
	}

	return true;
}

void COptionsBase::set(optionsIndex opt, int value)
{
	if (opt == optionsIndex::invalid) {
		return;
	}

	fz::scoped_write_lock l(mtx_);
	if (static_cast<size_t>(opt) >= values_.size() && !do_add_missing(opt, l, mtx_, options_, name_to_option_, values_)) {
		return;
	}

	auto const& def = options_[static_cast<size_t>(opt)];
	auto& val = values_[static_cast<size_t>(opt)];

	// Type conversion
	if (def.type() == option_type::number) {
		set(opt, def, val, value);
	}
	else if (def.type() == option_type::boolean) {
		set(opt, def, val, value ? 1 : 0);
	}
	else if (def.type() == option_type::string) {
		set(opt, def, val, fz::to_wstring(value));
	}
}

void COptionsBase::set(optionsIndex opt, std::wstring_view const& value, bool predefined)
{
	if (opt == optionsIndex::invalid) {
		return;
	}

	fz::scoped_write_lock l(mtx_);
	if (static_cast<size_t>(opt) >= values_.size() && !do_add_missing(opt, l, mtx_, options_, name_to_option_, values_)) {
		return;
	}

	auto const& def = options_[static_cast<size_t>(opt)];
	auto& val = values_[static_cast<size_t>(opt)];

	// Type conversion
	if (def.type() == option_type::number) {
		auto v = fz::to_integral<int>(value, std::numeric_limits<int>::min());
		if (v == std::numeric_limits<int>::min()) {
			if (!def.mnemonics().empty()) {
				v = def.val_from_mnemonic(value);
			}
		}
		set(opt, def, val, v, predefined);
	}
	else if (def.type() == option_type::boolean) {
		set(opt, def, val, fz::to_integral<int>(value), predefined);
	}
	else if (def.type() == option_type::string) {
		set(opt, def, val, value, predefined);
	}
}

void COptionsBase::set(optionsIndex opt, pugi::xml_node const& value)
{
	if (opt == optionsIndex::invalid) {
		return;
	}

	pugi::xml_document doc;
	if (value) {
		if (value.type() == pugi::node_document) {
			for (auto c = value.first_child(); c; c = c.next_sibling()) {
				if (c.type() == pugi::node_element) {
					doc.append_copy(c);
				}
			}
		}
		else {
			doc.append_copy(value);
		}
	}

	fz::scoped_write_lock l(mtx_);
	if (static_cast<size_t>(opt) >= values_.size() && !do_add_missing(opt, l, mtx_, options_, name_to_option_, values_)) {
		return;
	}

	auto const& def = options_[static_cast<size_t>(opt)];
	auto& val = values_[static_cast<size_t>(opt)];

	// Type check
	if (def.type() != option_type::xml) {
		return;
	}

	set(opt, def, val, std::move(doc));
}

void COptionsBase::set(optionsIndex opt, option_def const& def, option_value& val, int value, bool predefined)
{
	if ((def.flags() & option_flags::predefined_only) && !predefined) {
		return;
	}
	if ((def.flags() & option_flags::predefined_priority) && !predefined && val.predefined_) {
		return;
	}

	if (value < def.min()) {
		if (!(def.flags() & option_flags::numeric_clamp)) {
			return;
		}
		value = def.min();
	}
	else if (value > def.max()) {
		if (!(def.flags() & option_flags::numeric_clamp)) {
			return;
		}
		value = def.max();
	}
	if (def.validator()) {
		if (!reinterpret_cast<bool(*)(int&)>(def.validator())(value)) {
			return;
		}
	}

	val.predefined_ = predefined;
	if (value == val.v_) {
		return;
	}

	val.v_ = value;
	val.str_ = fz::to_wstring(value);
	++val.change_counter_;

	set_changed(opt);
}

void COptionsBase::set(optionsIndex opt, option_def const& def, option_value& val, std::wstring_view const& value, bool predefined)
{
	if ((def.flags() & option_flags::predefined_only) && !predefined) {
		return;
	}
	if ((def.flags() & option_flags::predefined_priority) && !predefined && val.predefined_) {
		return;
	}

	if (value.size() > static_cast<size_t>(def.max())) {
		return;
	}

	if (def.validator()) {
		std::wstring v(value);
		if (!reinterpret_cast<bool(*)(std::wstring&)>(def.validator())(v)) {
			return;
		}

		val.predefined_ = predefined;
		if (v == val.str_) {
			return;
		}
		val.v_ = fz::to_integral<int>(v);
		val.str_ = std::move(v);
	}
	else {

		val.predefined_ = predefined;
		if (value == val.str_) {
			return;
		}
		val.v_ = fz::to_integral<int>(value);
		val.str_ = value;
	}
	++val.change_counter_;

	set_changed(opt);
}

void COptionsBase::set(optionsIndex opt, option_def const& def, option_value& val, pugi::xml_document&& value, bool predefined)
{
	if ((def.flags() & option_flags::predefined_only) && !predefined) {
		return;
	}
	if ((def.flags() & option_flags::predefined_priority) && !predefined && val.predefined_) {
		return;
	}

	if (def.validator()) {
		if (!reinterpret_cast<bool(*)(pugi::xml_node&)>(def.validator())(value)) {
			return;
		}
	}
	*val.xml_ = std::move(value);
	++val.change_counter_;

	set_changed(opt);
}

void COptionsBase::set_changed(optionsIndex opt)
{
	bool notify = can_notify_ && !changed_.any();
	changed_.set(opt);
	if (notify) {
		notify_changed();
	}
}

bool watched_options::any() const
{
	for (auto const& v : options_) {
		if (v) {
			return true;
		}
	}

	return false;
}

void watched_options::set(optionsIndex opt)
{
	auto idx = static_cast<size_t>(opt) / 64;
	if (idx >= options_.size()) {
		options_.resize(idx + 1);
	}

	options_[idx] |= (1ull << static_cast<size_t>(opt) % 64);
}

void watched_options::unset(optionsIndex opt)
{
	auto idx = static_cast<size_t>(opt) / 64;
	if (idx < options_.size()) {
		options_[idx] &= ~(1ull << static_cast<size_t>(opt) % 64);
	}
}

bool watched_options::test(optionsIndex opt) const
{
	auto idx = static_cast<size_t>(opt) / 64;
	if (idx >= options_.size()) {
		return false;
	}

	return options_[idx] & (1ull << static_cast<size_t>(opt) % 64);
}

watched_options& watched_options::operator&=(std::vector<uint64_t> const& op)
{
	size_t s = std::min(options_.size(), op.size());
	options_.resize(s);
	for (size_t i = 0; i < s; ++i) {
		options_[i] &= op[i];
	}
	return *this;
}

void COptionsBase::continue_notify_changed()
{
	watched_options changed;
	{
		fz::scoped_write_lock l(mtx_);
		if (!changed_) {
			return;
		}
		changed = changed_;
		changed_.clear();
		process_changed(changed);
	}

	fz::scoped_lock l(notification_mtx_);

	for (auto const& w : watchers_) {
		watched_options n = changed;
		if (!w.all_) {
			n &= w.options_;
		}
		if (n) {
			w.notifier_(w.handler_, std::move(n));
		}
	}
}

void COptionsBase::watch(optionsIndex opt, std::tuple<void*, watcher_notifier> handler)
{
	if (!std::get<0>(handler) || !std::get<1>(handler)|| opt == optionsIndex::invalid) {
		return;
	}

	fz::scoped_lock l(notification_mtx_);
	for (size_t i = 0; i < watchers_.size(); ++i) {
		if (watchers_[i].handler_ == std::get<0>(handler)) {
			watchers_[i].options_.set(opt);
			return;
		}
	}
	watcher w;
	w.handler_ = std::get<0>(handler);
	w.notifier_ = std::get<1>(handler);
	w.options_.set(opt);
	watchers_.push_back(w);
}

void COptionsBase::watch_all(std::tuple<void*, watcher_notifier> handler)
{
	if (!std::get<0>(handler)) {
		return;
	}

	fz::scoped_lock l(notification_mtx_);
	for (size_t i = 0; i < watchers_.size(); ++i) {
		if (watchers_[i].handler_ == std::get<0>(handler)) {
			watchers_[i].all_ = true;
			return;
		}
	}
	watcher w;
	w.handler_ = std::get<0>(handler);
	w.notifier_ = std::get<1>(handler);
	w.all_ = true;
	watchers_.push_back(w);
}

void COptionsBase::unwatch(optionsIndex opt, std::tuple<void*, watcher_notifier> handler)
{
	if (!std::get<0>(handler) || opt == optionsIndex::invalid) {
		return;
	}

	fz::scoped_lock l(notification_mtx_);
	for (size_t i = 0; i < watchers_.size(); ++i) {
		if (watchers_[i].handler_ == std::get<0>(handler)) {
			watchers_[i].options_.unset(opt);
			if (watchers_[i].options_ || watchers_[i].all_) {
				return;
			}
			watchers_[i] = watchers_.back();
			watchers_.pop_back();
			return;
		}
	}
}

void COptionsBase::unwatch_all(std::tuple<void*, watcher_notifier> handler)
{
	if (!std::get<0>(handler) || !std::get<1>(handler)) {
		return;
	}

	fz::scoped_lock l(notification_mtx_);
	for (size_t i = 0; i < watchers_.size(); ++i) {
		if (watchers_[i].handler_ == std::get<0>(handler)) {
			watchers_[i] = watchers_.back();
			watchers_.pop_back();
			return;
		}
	}
}

void COptionsBase::set_default_value(optionsIndex opt)
{
	::set_default_value(static_cast<size_t>(opt), options_, values_);
}

uint64_t COptionsBase::change_count(optionsIndex opt)
{
	fz::scoped_read_lock l(mtx_);
	if (opt == optionsIndex::invalid || static_cast<size_t>(opt) >= values_.size()) {
		return 0;
	}

	auto& val = values_[static_cast<size_t>(opt)];
	return val.change_counter_;
}

std::wstring_view COptionsBase::get_mnemonic(optionsIndex opt)
{
	if (opt == optionsIndex::invalid) {
		return {};
	}

	auto val = get_int(opt);
	auto const& def = options_[static_cast<size_t>(opt)];
	auto const& mnemonics = def.mnemonics();

	if (val < 0 || val >= static_cast<int>(mnemonics.size())) {
		return {};
	}
	return mnemonics[val];
}
