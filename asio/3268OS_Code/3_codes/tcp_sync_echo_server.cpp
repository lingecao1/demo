#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

using namespace boost::asio;
using namespace boost::posix_time;
using boost::system::error_code;

io_service service;

size_t read_complete(char * buff, const error_code & err, size_t bytes) {
    if ( err)
	 return 0;
    bool found = std::find(buff, buff + bytes, '\n') < buff + bytes;
    // we read one-by-one until we get to enter, no buffering
    return found ? 0 : 1;// if return 0, express that all data have been received. if returning non-zero, express that next read operate will receive how much data bytes.
}

void handle_connections() {
    ip::tcp::acceptor acceptor(service, ip::tcp::endpoint(ip::tcp::v4(),8001));
    char buff[1024];
    while ( true) {
        ip::tcp::socket sock(service);
        acceptor.accept(sock);
        int bytes = read(sock, buffer(buff), boost::bind(read_complete, buff, _1, _2));
        std::string msg(buff, bytes);
        sock.write_some(buffer(msg)); // can write_some ensure that all bytes have been writen? yeah, Because write_some will process all available bytes. 
        sock.close();
    }
}

int main(int argc, char* argv[]) {
    handle_connections();
}
