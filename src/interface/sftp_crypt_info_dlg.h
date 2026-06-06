#ifndef FILEZILLA_INTERFACE_SFTP_CRYPT_INFO_DLG_HEADER
#define FILEZILLA_INTERFACE_SFTP_CRYPT_INFO_DLG_HEADER

namespace fz::ssh {
struct algorithm_info;
}

class CSftpEncryptioInfoDialog
{
public:
	void ShowDialog(fz::ssh::algorithm_info const& algs, std::string const& fingerprint);
};

#endif
