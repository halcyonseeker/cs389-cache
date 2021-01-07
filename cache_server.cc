/**
 * cache_server.cc
 * Talib Pierson & Thalia Wright
 * October 2020
 */

#include <libgen.h>  // For basename()
#include <unistd.h>  // For getopt()

#include <boost/asio/io_service.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

#include "cache.hh"
#include "evictor.hh"
#include "fifo_evictor.hh"

namespace beast = boost::beast;  // from <boost/beast.hpp>
namespace http = beast::http;    // from <boost/beast/http.hpp>
namespace net = boost::asio;     // from <boost/asio.hpp>
using tcp = net::ip::tcp;        // from <boost/asio/ip/tcp.hpp>

// A global cache object
static std::shared_ptr<Cache> cache;

/**
 * Die gracefully
 */
void signal_handler(int signum) {
    std::cerr << "Received signal " << signum << std::endl;
    // TODO call relevant destructors to prevent libs from leaking core
    exit(signum);
}

/**
 * Spit an error and die
 */
void die(const std::string &croak) {
    std::cerr << "LoopError: " << croak << std::endl;
    exit(EXIT_FAILURE);
}

/**
 * Helper functions to parse the data received through a socket.
 * get_field1() and get_field2() find key and value data respectively
 */
static std::string get_field1(const std::string &msg) {
    const size_t begin = msg.find_first_of('/');
    const size_t end = msg.find_last_of('/');
    if (end == begin)
        return msg.substr(begin + 1,
                          msg.length());  // There one or fewer fields
    return msg.substr(begin + 1, end - 1);
}

static std::string get_field2(const std::string &msg) {
    const size_t begin = msg.find_first_of('/');
    const size_t end = msg.find_last_of('/');
    if (end == begin) return std::string(" ");  // There are no values
    return msg.substr(end + 1, msg.length());
}

/**
 * Process requests
 * @param req the request to process
 * @param sock the socket to write to
 */
void process_requests(http::request<http::string_body> &req,
                      tcp::socket &sock) {
    std::cerr << "==> BEGIN HTTP REQUEST <==" << std::endl
              << req << std::endl
              << "==[ END HTTP REQUEST ]==" << std::endl;

    // Cast the data passed by the client to a string for convenience
    const std::string input = static_cast<std::string>(req.target());

    // An error code object
    beast::error_code ec;

    // Create a new response object to be filled in later
    http::response<http::string_body> res{};

    if (req.method() == http::verb::get) {  // GET /key HTTP/1.1:
        key_type key = get_field1(input);
        Cache::val_type val{};
        std::string json_str;

        try {
            val = cache->get(key);
        } catch (std::exception &e) {
            std::cerr << "GetException: main() GET 1: " << e.what()
                      << std::endl;
            res.result(500);  // 500 Internal Server Error
            if (ec)
                std::cerr << "BoostError: main() GET 2: " << ec.message()
                          << std::endl;
        }

        if (val.data_ == nullptr || val.size_ == 0) {
            res.result(404);  // 404 Not Found
        } else {
            json_str = "{key: \"" + key + "\", val: \"" + val.data_ + "\"}";
            res.result(200);  // 200 OK
            delete[] val.data_;
        }

        res.set(http::field::content_type, "application/json");
        res.body() = json_str;

        std::cerr << "==> BEGIN HTTP RESPONSE <==" << std::endl
                  << res << std::endl
                  << "==[ END HTTP RESPONSE ]==" << std::endl;
        http::write(sock, res, ec);
        if (ec)
            std::cerr << "BoostError: main() GET 3: " << ec.message()
                      << std::endl;

    } else if (req.method() == http::verb::put) {  // PUT /key/value HTTP/1.1:
        key_type key = get_field1(input);
        std::string data = get_field2(input);
        Cache::val_type val{};

        auto *data_buf = new Cache::byte_type[data.size() + 1];
        strncpy(data_buf, data.c_str(), data.length() + 1);
        val.size_ = static_cast<Cache::size_type>(data.size() + 1);
        val.data_ = data_buf;

        if (!cache->set(key, val))
            res.result(500);  // 500 Internal Server Error
        else
            res.result(200);  // 200 OK

        std::cerr << "==> BEGIN HTTP RESPONSE <==" << std::endl
                  << res << std::endl
                  << "==[ END HTTP RESPONSE ]==" << std::endl;
        http::write(sock, res, ec);
        if (ec)
            std::cerr << "BoostError: main() PUT: " << ec.message()
                      << std::endl;

        delete[] data_buf;

    } else if (req.method() == http::verb::delete_) {  // DELETE /key HTTP/1.1:
        key_type key = get_field1(input);

        if (!cache->del(key))
            res.result(404);  // 404 Not Found
        else
            res.result(200);  // 200 OK

        std::cerr << "==> BEGIN HTTP RESPONSE <==" << std::endl
                  << res << std::endl
                  << "==[ END HTTP RESPONSE ]==" << std::endl;
        http::write(sock, res, ec);
        if (ec)
            std::cerr << "BoostError: main() DELETE: " << ec.message()
                      << std::endl;

    } else if (req.method() == http::verb::head) {  // HEAD HTTP/1.1:
        Cache::size_type space_used = cache->space_used();
        double hit_rate = cache->hit_rate();

        if (!std::isnan(space_used)) {
            res.result(200);  // 200 OK
            res.set(http::field::content_type, "application/json");
            res.set(http::field::accept, "application/json");
            res.http::basic_fields<std::allocator<char>>::insert(
                "Space-Used", std::to_string(space_used));
            res.http::basic_fields<std::allocator<char>>::insert(
                "Hit-Rate", std::to_string(hit_rate));
            res.http::basic_fields<std::allocator<char>>::insert(
                "X-Clacks-Overhead", "GNU Terry Pratchett");
        } else {
            res.result(500);  // 500 Internal Server Error
        }

        std::cerr << "==> BEGIN HTTP RESPONSE <==" << std::endl
                  << res << std::endl
                  << "==[ END HTTP RESPONSE ]==" << std::endl;
        http::write(sock, res, ec);
        if (ec)
            std::cerr << "BoostError: main() HEAD: " << ec.message()
                      << std::endl;

    } else if (req.method() == http::verb::post) {  // POST /reset HTTP/1.1:
        std::string cmd = get_field1(input);
        if (cmd != "reset") {
            res.result(400);  // 400 Bad Request
        } else {
            if (!cache->reset())
                res.result(500);  // 500 Internal Server Error
            else
                res.result(205);  // 205 Reset Content
        }

        std::cerr << "==> BEGIN HTTP RESPONSE <==" << std::endl
                  << res << std::endl
                  << "==[ END HTTP RESPONSE ]==" << std::endl;
        http::write(sock, res, ec);
        if (ec)
            std::cerr << "BoostError: main() POST: " << ec.message()
                      << std::endl;

    } else {              // error
        res.result(400);  // 400 Bad Request
    }
}

/**
 * Handle incoming connections
 * This function will be called multiple times by std::thread
 * @param &sock the tcp socket to read a connection from
 */
void handle_sessions(tcp::socket& sock) {
    beast::error_code ec;
    
    // Buffer for reading requests
    beast::flat_buffer buf;

    // A request object
    http::request<http::string_body> req = {};

    // Read into that object
    http::read(sock, buf, req, ec);

    // In case of errors
    if (ec == http::error::end_of_stream) return;
    if (ec) std::cerr << "http::read(): " << ec.message() << std::endl;

    // Process the request and send the appropriate response
    process_requests(req, sock);

    // Send a graceful shutdown signal
    sock.shutdown(tcp::socket::shutdown_send);
}

/**
 * Optional arguments:
 * -m maxmem  : Maximum memory, passed to cache
 * -s server  : assume localhost for now
 * -p port    : port to bind to
 * -t threads : ignore for now
 */
int main(int argc, char *argv[]) {
    // Default values for arguments
    size_t maxmem = 65536;
    net::ip::address server = net::ip::make_address("127.0.0.1");
    // server.make_address("127.0.0.1");
    unsigned short port = 42069;
    int threads = 1;

    // Catch SIGTERMs
    signal(SIGTERM, signal_handler);

    // A fatal help function
    auto usage = [&, argv](int status) {
        std::cout << "Usage: " << basename(argv[0]) << std::endl
                  << "\t-m [65536]     Cache's capacity in bytes." << std::endl
                  << "\t-s [127.0.0.1] address to listen on." << std::endl
                  << "\t-p [42069]     Port to listen on." << std::endl
                  << "\t-t [1]         Number of threads to use." << std::endl
                  << "\t-h             Print this message." << std::endl;
        exit(status);
    };

    // Process command line arguments
    int option;
    while ((option = getopt(argc, argv, "m:s:p:t:h")) != -1) {
        switch (option) {
            case 'm':
                maxmem = strtoul(optarg, nullptr, 10);
                if (maxmem <= 0) usage(EXIT_FAILURE);
                break;
            case 's':
                if (optarg) server = net::ip::make_address(optarg);
                break;
            case 'p':
                port =
                        static_cast<unsigned short>(strtoul(optarg, nullptr, 10));
                if (port <= 0) usage(EXIT_FAILURE);
                break;
            case 't':
                threads = std::stoi(optarg, nullptr, 10);
                break;
            case 'h':
                usage(EXIT_SUCCESS);
                break;
            default:
                std::cerr << "Unknown option " << option << std::endl;
                usage(EXIT_FAILURE);
        }
    }

    // Debugging
    std::cerr << "==> ARGUMENTS <==" << std::endl
              << "maxmem : " << maxmem << std::endl
              << "server : " << server << std::endl
              << "port   : " << port << std::endl
              << "threads: " << threads << std::endl
              << "==[ END ARGUMENTS ]==" << std::endl;

    // Set up the cache
    Cache::hash_func hasher = std::hash<key_type>();
    auto *evictor = new Fifo_Evictor();
    cache = std::make_shared<Cache>(maxmem, 0.75, evictor, hasher);

    // An error message object
    beast::error_code ec;

    try {
        /// The io_context is required for all I/O
        net::io_context ioc{threads};
        
        /// The acceptor receives incoming connections
        tcp::acceptor acceptor{ioc, {server, port}};
        
        //
        // Loop until the program is killed or something breaks
        //
        for (;;) {
            /// This will receive the new connection
            tcp::socket sock{ioc};
            
            // Block until we recieve a connection
            acceptor.accept(sock);

            // Launch the session, transfer ownership of the socket
            std::thread{std::bind(&handle_sessions, std::move(sock))}.detach();
        }
        
    } catch (const std::exception &e) {
        std::cerr << "main(): " << e.what() << std::endl;
    }
    return EXIT_SUCCESS;
}
