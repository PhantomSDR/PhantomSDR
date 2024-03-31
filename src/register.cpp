#include "spectrumserver.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "glaze/glaze.hpp"

void broadcast_server::register_server() {
    using namespace boost::asio;
    boost::system::error_code ec;
    std::string server = "phantomsdr.duckdns.org";

    while(running) {
        registration.users = events_connections.size();
        std::string registration_json = glz::write_json(registration);
        std::string request = "POST /api/v1/ping HTTP/1.1\r\n"
                            "Host: " +
                            server +
                            "\r\n"
                            "Content-Type: application/json\r\n"
                            "User-Agent: PhantomSDR server\r\n"
                            "Content-Length: " +
                            std::to_string(registration_json.size()) +
                            "\r\n"
                            "\r\n" +
                            registration_json
                            + "\r\n";

        auto &io_service = m_server.get_io_service();
        ip::tcp::resolver resolver(io_service);
        auto it = resolver.resolve({server, "443"});
        ssl::context ctx(ssl::context::method::sslv23_client);
        ssl::stream<ip::tcp::socket> ssock(io_service, ctx);
        boost::asio::connect(ssock.lowest_layer(), it);
        ssock.handshake(ssl::stream_base::handshake_type::client);

        boost::asio::write(ssock, buffer(request));

        std::string response;
        char buf[1024];
        size_t bytes_transferred = ssock.read_some(buffer(buf), ec);
        response.append(buf, buf + bytes_transferred);

        std::this_thread::sleep_for(std::chrono::minutes(1));
    }
}