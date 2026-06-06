#include "filezilla.h"
#include "msgbox.h"

namespace {
int openMessageBoxes = 0;
}

bool IsShowingMessageBox()
{
	return openMessageBoxes != 0;
}

int wxMessageBoxEx(wxString const& message, wxString const& caption
	, long style, wxWindow *parent, int x, int y)
{

	// Some platforms cannot handle long, unbroken words in message boxes.  Artificially insert zero-width spaces.
	std::wstring const chars({ ' ', '\n', '\t', 0x200b /*0-width space*/ });
	wxString out;

	size_t const max = 200;
	if (message.size() > max) {
		out.reserve(message.size() + 10);
	}

	size_t cur{}, prev{};
	while (cur < message.size()) {
		prev = cur;
		cur = message.find_first_of(chars, cur);

		if (cur++ == std::wstring::npos) {
			cur = message.size();
		}

		while ((cur - prev) > max) {
			if (out.empty()) {
				out = message.substr(0, prev);
			}
			out += message.substr(prev, max);
			out += 0x200b;
			prev += max;
		}

		if (!out.empty()) {
			out += message.substr(prev, cur - prev);
		}
	}



	++openMessageBoxes;
	int ret = wxMessageBox(out.empty() ? message : out, caption, style, parent, x, y);
	--openMessageBoxes;

	return ret;
}
