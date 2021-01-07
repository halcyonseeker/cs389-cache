/**
 * cache_client.cc
 * Talib Pierson & Thalia Wright
 * October 2020
 * Implement the API defined in cache.hh as an HTTP client.
 */
#include <boost/asio/io_service.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <iostream>
#include <string>

#include "cache.hh"

//#define DEBUG

namespace beast = boost::beast;  // from <boost/beast.hpp>
namespace http = beast::http;    // from <boost/beast/http.hpp>
namespace net = boost::asio;     // from <boost/asio.hpp>
using tcp = net::ip::tcp;        // from <boost/asio/ip/tcp.hpp>

/**
 * Implement the private parts of Cache using the pimpl idiom.
 * Elements of Impl need to be public, so a struct makes sense here.
 */
class Cache::Impl {
public:
    // These are set in the constructor
    std::string host;
    std::string port;
    int version = 11;

    /// Required for all I/O
    net::io_context ioc;

    // These objects send our I/O
    tcp::resolver resolver{ioc};
    beast::tcp_stream stream{ioc};

    /// The domain name
    tcp::resolver::results_type results;

    /**
     * Loop up and connect to host.
     * @param ip_addr host
     * @param port_no port
     */
    Impl(std::string ip_addr, std::string port_no) {
        // Use arguments
        host = std::move(ip_addr);
        port = std::move(port_no);

#ifdef DEBUG
        std::cerr << "==> CONNECT TCP STREAM HOST AT PORT <==\n"
                     "host: "
                  << host << ", port: " << port << ", version: " << version
                  << std::endl;
#endif  // DEBUG

        // Look up the domain name
        results = resolver.resolve(host, port);

        /// A boost error code
        beast::error_code ec;
    }

    /**
     * Set up and send an HTTP request message; receive and return HTTP
     * response.
     * @param method HTTP request method
     * @return msg to follow verb
     */
    http::response<http::dynamic_body> send(const http::verb &method,
                                            const std::string &target) {
        /// A boost error code
        beast::error_code ec;

        /// This buffer is used for reading and must be persisted
        beast::flat_buffer buffer;

        /// Declare a container to hold the response
        http::response<http::dynamic_body> res;

        /// Set up an HTTP request
        http::request<http::string_body> req{method, target, version};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

#ifdef DEBUG
        std::cerr << "==> SEND HTTP REQUEST <==\n"
                  << req << "\n==[ END HTTP REQUEST ]==" << std::endl;
#endif  // DEBUG

        try {
            // Make the connection on the IP address we get from a lookup
            stream.connect(results, ec);
            if (ec) std::cerr << "Impl::send(): stream.connect: " << ec.message() << std::endl;

            // TODO: This throws a broken pipe exception and fails the test
            // Send the HTTP request to the remote host
            http::write(stream, req, ec);
            if (ec) std::cerr << "Impl::send(): http::write: " << ec.message() << std::endl;

            // Receive the HTTP response
            // Send the HTTP request to the remote host
            http::read(stream, buffer, res, ec);
            if (ec) std::cerr << "Impl::send(): http::read: " << ec.message() << std::endl;
        } catch (const std::exception &e) {
            std::cerr << "impl::send(): " << e.what() << std::endl;
        }

#ifdef DEBUG
        std::cerr << "==> RECEIVED HTTP RESPONSE <==\n"
                  << res << "\n==[ END HTTP RESPONSE ]==" << std::endl;
#endif  // DEBUG

        return res;
    }
};

/**
 * Don't create a new cache object with the following parameters; it will crash.
 * @param maxmem            The maximum allowance for storage used by values
 * @param max_load_factor   Maximum allowed ratio between buckets and table rows
 * @param evictor           Eviction policy implementation
 * @param hasher            Hash function to use on the keys
 */
Cache::Cache([[maybe_unused]] size_type maxmem,
             [[maybe_unused]] float max_load_factor,
             [[maybe_unused]] Evictor *evictor,
             [[maybe_unused]] hash_func hasher) {
    assert(false);
}

/**
 * Create a new Cache networked client with a given host and port.
 * Establish a connection with the server or exit the program if it fails.
 * @param host server IP address
 * @param port server port number
 */
Cache::Cache(std::string host, std::string port)
        : pImpl_(new Impl(std::move(host), std::move(port))) {}

/**
 * Gracefully shutdown the tcp stream socket.
 */
Cache::~Cache() {
    beast::error_code ec;
    this->pImpl_->stream.socket().shutdown(tcp::socket::shutdown_both, ec);

    // not_connected happens sometimes so don't bother reporting it
    if (ec && ec != beast::errc::not_connected) {
        std::cerr << "~Cache: tcp socket shutdown error" << std::endl;
    }
}

/**
 * @param status http status
 * @param what name of calling function
 */
static void check_status(http::status status, char const *what) {
    if (status != http::status::ok)
        std::cerr << what << ": " << status << std::endl;
}

/**
 * Add or replace <key, value> pair to the cache.
 * @param key string
 * @param val struct
 * @return true iff the insertion of the data to the store was successful.
 */
bool Cache::set(key_type key, val_type val) {
    return this->pImpl_->send(http::verb::put, '/' + key + '/' + val.data_)
                   .result() == http::status::ok;
}

/**
 * @param key string
 * @return val: a copy of the value associated with key in the cache,
 *         or nullptr with size 0 if not found.
 *         Note that the data_ pointer in the return key is a newly-allocated
 *         copy of the data. It is the caller's responsibility to free it.
 */
Cache::val_type Cache::get(key_type key) const {
    // TODO: FIX THIS!!!
    http::response<http::dynamic_body> response =
            this->pImpl_->send(http::verb::get, '/' + key);
    beast::string_view val = response.find("val")->value();

    if (response.result() == http::status::not_found)
        return {nullptr, 0};

    // deep copy buff from val for return
    auto *buf = new byte_type[val.size()];
    memcpy(buf, val.data(), val.size());

    return {buf, static_cast<size_type>(val.size())};
}

/**
 * Delete object from Cache if object in Cache.
 * @param key of pair to erase
 * @return true if pair erased else false
 */
bool Cache::del(key_type key) {
    return this->pImpl_->send(http::verb::delete_, '/' + key).result() ==
           http::status::ok;
}

/**
 * Get the cache's current space used value from header.
 * @return space used.
 */
Cache::size_type Cache::space_used() const {
    // TODO: FIX THIS!!!
    http::response<http::dynamic_body> response =
            this->pImpl_->send(http::verb::head, "/");
    check_status(response.result(), "space_used");

#ifdef DEBUG
    std::cerr << "response: \"" << response << "\"" << std::endl;
    std::cerr << "base: \"" << response.base() << "\"" << std::endl;
    std::cerr << "value: \"" << response.base().find("Space-Used")->value()
              << "\"" << std::endl;
    std::cerr << "s: \""
              << static_cast<const std::string>(
                      response.base().find("Space-Used")->value())
              << "\"" << std::endl;
#endif  // DEBUG

    try {
        return static_cast<Cache::size_type>(
                std::stoi(static_cast<const std::string>(
                                  response.base().find("Space-Used")->value())));
    } catch (std::exception &e) {
        std::cerr << "delException: " << e.what() << std::endl;
    }
    return 0;
}

/**
 * Get the cache's current hit rate value from header.
 * @return hit rate.
 */
double Cache::hit_rate() const {
    // TODO: FIX THIS!!!
    http::response<http::dynamic_body> response =
            this->pImpl_->send(http::verb::head, "/");
    check_status(response.result(), "hit_rate");

#ifdef DEBUG
    std::cerr << "Hit-Rate: " << response.base().find("Hit-Rate")->value()
              << std::endl;
#endif  // DEBUG

    return std::stod(static_cast<const std::string>(
                             response.base().find("Hit-Rate")->value()));
}

/**
 * Delete all data from the cache.
 * @return true iff successful.
 */
bool Cache::reset() {
    return this->pImpl_->send(http::verb::post, "/reset").result() ==
           http::status::reset_content;
}
