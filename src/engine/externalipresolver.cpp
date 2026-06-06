#include "filezilla.h"

#include "../include/externalipresolver.h"

#include <libfilezilla/iputils.hpp>

using namespace std::literals;

namespace {
fz::mutex s_sync;
std::string ip_;
bool checked = false;
}

CExternalIPResolver::CExternalIPResolver(fz::thread_pool & pool, fz::event_handler & handler)
	: fz::event_handler(handler, fz::child_event_handler)
	, fz::http::client::client(*this, fz::get_null_logger(), "FileZilla/"s + ENGINE_VERSION)
	, thread_pool_(pool)
	, handler_(handler)
{
}

CExternalIPResolver::~CExternalIPResolver()
{
	remove_handler();
	fz::http::client::client::stop(false);
}

fz::socket_interface* CExternalIPResolver::create_socket(fz::native_string const&, unsigned short, bool tls)
{
	destroy_socket();
	if (tls) {
		return nullptr;
	}
	socket_ = std::make_unique<fz::socket>(thread_pool_, nullptr);
	return socket_.get();
}

void CExternalIPResolver::destroy_socket()
{
	socket_.reset();
}

fz::http::continuation CExternalIPResolver::GetExternalIP(std::wstring const& address, fz::address_type protocol, bool force)
{
	if (srr_) {
		return fz::http::continuation::wait;
	}

	{
		fz::scoped_lock l(s_sync);
		if (checked) {
			if (force) {
				checked = false;
			}
			else {
				return ip_.empty() ? fz::http::continuation::error : fz::http::continuation::done;
			}
		}
	}

	std::string addr = fz::to_utf8(address);
	if (addr.find("://"sv) == std::string::npos) {
		addr = "http://" + addr;
	}

	srr_ = std::make_shared<fz::http::client::request_response_holder<fz::http::client::request, fz::http::client::response>>();
	auto & req = srr_->req();

	if (protocol == fz::address_type::ipv4) {
		req.flags_ |= fz::http::client::request::flag_force_ipv4;
	}
	else {
		req.flags_ |= fz::http::client::request::flag_force_ipv6;
	}

	auto & res = srr_->res();
	res.max_body_size_ = 1024;
	res.on_header_ = [this](auto const& srr) { return OnHeader(srr); };

	redirect_count_ = 0;

	req.uri_.parse(addr);
	if (!req.uri_ || !add_request(srr_)) {
		srr_.reset();
		return fz::http::continuation::error;
	}

	return fz::http::continuation::wait;
}

void CExternalIPResolver::operator()(fz::event_base const& ev)
{
	fz::dispatch<fz::http::client::done_event>(ev, this, &CExternalIPResolver::on_request_done);
}

fz::http::continuation CExternalIPResolver::OnHeader(std::shared_ptr<fz::http::client::request_response_interface> const& srr)
{
	auto & res = srr->res();
	if (!res.is_redirect()) {
		return fz::http::continuation::next;
	}
	if (++redirect_count_ >= 6) {
		return fz::http::continuation::error;
	}

	auto & req = srr->req();

	fz::uri location = fz::uri(res.get_header("Location"));
	if (!location.empty()) {
		location.resolve(req.uri_);
	}

	if (location.scheme_.empty() || location.host_.empty() || !location.is_absolute()) {
		return fz::http::continuation::error;
	}

	req.uri_ = location;

	if (!add_request(srr)) {
		return fz::http::continuation::error;
	}

	return fz::http::continuation::done;
}

void CExternalIPResolver::on_request_done(uint64_t id, bool success)
{
	if (!srr_ || id != srr_->request_id_) {
		return;
	}

	std::string ip;
	if (success) {
		auto & res = srr_->res();
		if (res.success()) {
			auto data = srr_->res().body_.to_view();
			fz::trim(data);

			if (srr_->req().flags_ & fz::http::client::request::flag_force_ipv6) {
				if (!data.empty() && data[0] == '[') {
					if (data.back() != ']') {
						data = {};
					}
					else {
						data = data.substr(1, data.size() - 2);
					}
				}
				if (fz::get_address_type(data) == fz::address_type::ipv6) {
					ip = data;
				}
			}
			else {
				if (fz::get_address_type(data) == fz::address_type::ipv4) {
					ip = data;
				}
			}
		}
	}

	{
		fz::scoped_lock l(s_sync);
		ip_ = ip;
		checked = true;
	}

	handler_.send_event<CExternalIPResolveEvent>();
	srr_.reset();
}

std::string CExternalIPResolver::GetIP() const
{
	fz::scoped_lock l(s_sync);
	return ip_;
}
