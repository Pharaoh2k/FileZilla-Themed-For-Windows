#ifndef FILEZILLA_ENGINE_TLS_HEADER
#define FILEZILLA_ENGINE_TLS_HEADER

#include <libfilezilla/tls_layer.hpp>

class COptionsBase;
fz::tls_ver get_min_tls_ver(COptionsBase & options);

#endif
