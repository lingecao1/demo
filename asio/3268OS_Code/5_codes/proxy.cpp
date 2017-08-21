/*
This is a simple async proxy. 

A proxy usually sits between client and server. It takes a request from a client,
might modify it, and forwards it to server.

For every connection, you'll have two sockets, one to the client, and the other
to server.
*/
#ifdef WIN32
#define _WIN32_WINNT 0x0501
#include <stdio.h>
#endif


#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

using namespace boost::asio;
io_service service;
using boost::ref;

#define MEM_FN(x)         boost::bind(&self_type::x, shared_from_this())
#define MEM_FN1(x,y)      boost::bind(&self_type::x, shared_from_this(),y)
#define MEM_FN2(x,y,z)    boost::bind(&self_type::x, shared_from_this(),y,z)
#define MEM_FN3(x,y,z,t)  boost::bind(&self_type::x, shared_from_this(),y,z,t)

class proxy : public boost::enable_shared_from_this<proxy>
                  , boost::noncopyable {

    typedef proxy self_type;

    proxy(ip::tcp::endpoint ep_client, ip::tcp::endpoint ep_server) 
            : client_(service), server_(service), started_(0) {
    }

    void start_impl(ip::tcp::endpoint ep_client, ip::tcp::endpoint ep_svr) {
        // one connection, two sockets.
        client_.async_connect(ep_client, MEM_FN1(on_connect,_1));
        server_.async_connect(ep_svr, MEM_FN1(on_connect,_1));
    }
public:
    typedef boost::system::error_code error_code;
    typedef boost::shared_ptr<proxy> ptr;

    // start point for User.
    static ptr start(ip::tcp::endpoint ep_client, ip::tcp::endpoint ep_svr) {
        ptr new_(new proxy(ep_client, ep_svr));
        new_->start_impl(ep_client, ep_svr);
        return new_;
    }

    void stop() {
        if ( started_ < 2)
            return;
        started_ = 0;
        client_.close();
        server_.close();
    }
    // proxy connection include: one connection between client and proxy.
   //                            the other between proxy and server.
    bool started() {
        return started_ == 2;
    }

private:

    void on_connect(const error_code & err) {
        if ( !err){
            if ( ++started_ == 2) // both real connection have been estimashed 
                on_start();
        } else{
            stop();
        }
    }

    void on_start() {
        do_read(client_, buff_client_);
        do_read(server_, buff_server_);
    }

    void do_read(ip::tcp::socket & sock, char* buff) {
        async_read(sock, buffer(buff, max_msg), 
                   MEM_FN3(read_complete,ref(sock),_1,_2), 
                   MEM_FN3(on_read,ref(sock),_1,_2));
    }

    size_t read_complete(ip::tcp::socket & sock, 
                         const error_code & err, size_t bytes) {
        if ( sock.available() > 0)
            return sock.available();
        return bytes > 0 ? 0 : 1;
    }

    void on_read(ip::tcp::socket & sock, const error_code& err, size_t bytes) {
        if ( err){
            stop();
        }
        // Both connection operations have been done.
        if ( !started() ){ 
            return;
        }
        // forward this to the other party
        char *buff = &sock == &client_ ? buff_client_ : buff_server_;
        /* when reading from client, then write to server.
           when reading from server, then write to client. */
        do_write(&sock == &client_ ? server_ : client_, buff, bytes);
    }

    void do_write(ip::tcp::socket & sock, char * buff, size_t size) {
        if ( !started() )
            return;
        sock.async_write_some( buffer(buff,size), 
                               MEM_FN3(on_write,ref(sock),_1,_2));
    }

    void on_write(ip::tcp::socket & sock, const error_code &err, size_t bytes){
        /* when writing to client, then reading from server.
           when writing to server, then reading from client. */
        if ( &sock == &client_)
            do_read(server_, buff_server_);
        else
            do_read(client_, buff_client_);
    }

private:
    ip::tcp::socket client_, server_;
    enum { max_msg = 1024 };
    char buff_client_[max_msg], buff_server_[max_msg];
    int started_;
};

int main(int argc, char* argv[]) {
    ip::tcp::endpoint ep_c( ip::address::from_string("127.0.0.1"), 8001);
    ip::tcp::endpoint ep_s( ip::address::from_string("127.0.0.1"), 8002);
    proxy::start(ep_c, ep_s);
    service.run(); //
}




