#include "tls.h"
#include "../include/engine_options.h"

fz::tls_ver get_min_tls_ver(COptionsBase & options)
{
	auto v = options.get_int(OPTION_MIN_TLS_VER);
	switch (v) {
	case 0:
		return fz::tls_ver::v1_0;
	case 1:
		return fz::tls_ver::v1_1;
	case 2:
		return fz::tls_ver::v1_2;
	default:
		return fz::tls_ver::v1_3;
	}
}
