#include "activity_logger_layer.h"
#include "../include/activity_logger.h"

activity_logger_layer::activity_logger_layer(fz::event_handler* handler, fz::socket_interface& next_layer, activity_logger& logger)
	: fz::socket_layer(handler, next_layer, true)
	, activity_logger_(logger)
{
	next_layer.set_event_handler(handler);
}

activity_logger_layer::~activity_logger_layer()
{
	next_layer_.set_event_handler(nullptr);
}

int activity_logger_layer::read(void* buffer, unsigned int size, int& error)
{
	int const read = next_layer_.read(buffer, size, error);
	if (read > 0) {
		activity_logger_.record(activity_logger::recv, read);
	}
	return read;
}

int activity_logger_layer::write(void const* buffer, unsigned int size, int& error)
{
	int const written = next_layer_.write(buffer, size, error);
	if (written > 0) {
		activity_logger_.record(activity_logger::send, written);
	}
	return written;
}
