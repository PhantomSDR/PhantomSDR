#ifndef EVENTS_H
#define EVENTS_H

#include "client.h"

class EventsClient : public Client {
public:
    EventsClient(connection_hdl hdl, PacketSender& sender);
    void send_event(std::string& event);
    ~EventsClient() {};
};

#endif
