#include "xmlfunctions.h"

#include "protect.h"

void SetServer(pugi::xml_node node, Site const& site, login_manager& lim, COptionsBase& options)
{
	if (!node) {
		return;
	}

	for (auto child = node.first_child(); child; child = node.first_child()) {
		node.remove_child(child);
	}

	ServerProtocol const protocol = site.server.GetProtocol();

	AddTextElement(node, "Host", site.server.GetHost());
	AddTextElement(node, "Port", site.server.GetPort());
	AddTextElement(node, "Protocol", protocol);
	if (site.server.HasFeature(ProtocolFeature::ServerType)) {
		AddTextElement(node, "Type", site.server.GetType() + 1);
	}

	ProtectedCredentials credentials = site.credentials;

	if (credentials.logonType_ != LogonType::anonymous) {
		AddTextElement(node, "User", site.server.GetUser());

		protect(credentials, lim, options);

		if (credentials.logonType_ == LogonType::normal || credentials.logonType_ == LogonType::account) {
			std::string pass = fz::to_utf8(credentials.GetPass());

			if (credentials.encrypted_) {
				pugi::xml_node passElement = AddTextElementUtf8(node, "Pass", pass);
				if (passElement) {
					SetTextAttribute(passElement, "encoding", L"crypt");
					SetTextAttributeUtf8(passElement, "pubkey", credentials.encrypted_.to_base64(false));
				}
			}
			else {
				pugi::xml_node passElement = AddTextElementUtf8(node, "Pass", fz::base64_encode(pass));
				if (passElement) {
					SetTextAttribute(passElement, "encoding", L"base64");
				}
			}

			if (credentials.logonType_ == LogonType::account) {
				AddTextElement(node, "Account", credentials.account_);
			}
		}
		else if (!credentials.keyFile_.empty()) {
			AddTextElement(node, "Keyfile", credentials.keyFile_);
		}
	}
	AddTextElement(node, "Logontype", static_cast<int>(credentials.logonType_));

	if (site.server.GetTimezoneOffset()) {
		AddTextElement(node, "TimezoneOffset", site.server.GetTimezoneOffset());
	}

	if (CServer::ProtocolHasFeature(site.server.GetProtocol(), ProtocolFeature::TransferMode)) {
		switch (site.server.GetPasvMode())
		{
		case MODE_PASSIVE:
			AddTextElementUtf8(node, "PasvMode", "MODE_PASSIVE");
			break;
		case MODE_ACTIVE:
			AddTextElementUtf8(node, "PasvMode", "MODE_ACTIVE");
			break;
		default:
			AddTextElementUtf8(node, "PasvMode", "MODE_DEFAULT");
			break;
		}
	}
	if (site.connection_limit_) {
		AddTextElement(node, "MaximumMultipleConnections", site.connection_limit_);
	}

	if (CServer::ProtocolHasFeature(site.server.GetProtocol(), ProtocolFeature::Charset)) {
		switch (site.server.GetEncodingType())
		{
		case ENCODING_CUSTOM:
			AddTextElementUtf8(node, "EncodingType", "Custom");
			AddTextElement(node, "CustomEncoding", site.server.GetCustomEncoding());
			break;
		default:
			AddTextElementUtf8(node, "EncodingType", "UTF-8");
			break;
		}
	}

	if (CServer::ProtocolHasFeature(site.server.GetProtocol(), ProtocolFeature::PostLoginCommands)) {
		std::vector<std::wstring> const& postLoginCommands = site.server.GetPostLoginCommands();
		if (!postLoginCommands.empty()) {
			auto element = node.append_child("PostLoginCommands");
			for (auto const& command : postLoginCommands) {
				AddTextElement(element, "Command", command);
			}
		}
	}

	AddTextElementUtf8(node, "BypassProxy", site.server.GetBypassProxy() ? "1" : "0");
	std::wstring const& name = site.GetName();
	if (!name.empty()) {
		AddTextElement(node, "Name", name);
	}

	for (auto const& parameter : site.server.GetExtraParameters()) {
		auto element = AddTextElement(node, "Parameter", parameter.second);
		SetTextAttribute(element, "Name", parameter.first);
	}
}
