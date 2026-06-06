#ifndef FILEZILLA_ENGINE_ACTIVITY_LOGGER_LAYER_HEADER
#define FILEZILLA_ENGINE_ACTIVITY_LOGGER_LAYER_HEADER

#include <libfilezilla/socket.hpp>

class activity_logger;
class activity_logger_layer final : public fz::socket_layer
{
public:
	activity_logger_layer(fz::event_handler* handler, fz::socket_interface& next_layer, activity_logger & logger);
	virtual ~activity_logger_layer();

	virtual int read(void* buffer, unsigned int size, int& error) override;
	virtual int write(void const* buffer, unsigned int size, int& error) override;

private:
	activity_logger& activity_logger_;
};

#endif
