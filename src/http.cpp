#include "compression.h"
#include "spectrumserver.h"

#include <filesystem>
#include <fstream>

#include <boost/algorithm/string.hpp>

std::unordered_map<std::string, std::string> mime_types{
    {".html", "text/html"},
    {".js", "text/javascript"},
    {".css", "text/css"},
    {".wasm", "application/wasm"},
};

std::string get_mime_type(std::string &extension) {
    auto it = mime_types.find(extension);
    std::string mime_type;
    if (it == mime_types.end()) {
        mime_type = "text/plain";
    } else {
        mime_type = it->second;
    }
    return mime_type;
}

void broadcast_server::on_http(connection_hdl hdl) {
    // Upgrade our connection handle to a full connection_ptr
    server::connection_ptr con = m_server.get_con_from_hdl(hdl);

    std::ifstream file;
    std::ifstream filegz;
    // Prevent directory traversal paths
    std::string filename = std::filesystem::weakly_canonical(
                               std::filesystem::path("/" + con->get_resource()))
                               .string();
    std::string response;

    filename = filename.substr(0, filename.find("?"));
    // All the files are under the html root folder
    if (filename == "/") {
        filename = m_docroot + "/" + "index.html";
    } else {
        filename = m_docroot + "/" + filename.substr(1);
    }

    // Figure out the correct mime-type to send to the client
    std::string extension = std::filesystem::path(filename).extension();
    std::string mime_type = get_mime_type(extension);
    con->append_header("content-type", mime_type);
    con->append_header("Connection", "close");
    // Try to open the file
    file.open(filename.c_str(), std::ios::in);
    if (!file) {
        // 404 error
        std::stringstream ss;

        ss << "<!doctype html><html><head>"
           << "<title>Error 404 (Resource not found)</title><body>"
           << "<h1>Error 404</h1>"
           << "<p>The requested URL " << con->get_resource()
           << " was not found on this server.</p>"
           << "</body></head></html>";

        con->append_header("content-type", "text/html");
        con->set_body(ss.str());
        con->set_status(websocketpp::http::status_code::not_found);
        return;
    }

    // Send the file over to the client
    file.seekg(0, std::ios::end);
    response.reserve(file.tellg());
    file.seekg(0, std::ios::beg);

    response.assign(std::istreambuf_iterator<char>(file),
                    std::istreambuf_iterator<char>());

    std::set<std::string> encodings;
    boost::algorithm::split(encodings,
                            con->get_request_header("accept-encoding"),
                            boost::is_any_of(", "), boost::token_compress_on);
    if (encodings.find("gzip") != encodings.end()) {
        response = Gzip::compress(response);
        con->append_header("Content-Encoding", "gzip");
    }

    con->append_header("Cache-Control", "max-age=30");
    con->set_body(response);
    con->set_status(websocketpp::http::status_code::ok);
}