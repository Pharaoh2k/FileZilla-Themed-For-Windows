#ifndef FILEZILLA_ENGINE_S3SSE_HEADER
#define FILEZILLA_ENGINE_S3SSE_HEADER

namespace s3_sse {

enum class Encryption {
	NONE,
	AES256,
	AWSKMS,
	CUSTOMER
};

enum class KmsKey {
	DEFAULT,
	CUSTOM
};

}

#endif
