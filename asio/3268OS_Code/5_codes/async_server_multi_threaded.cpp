
/*
   simple connection to server:
    - logs in just with username (no password)
    - all connections are initiated by the client: client asks, server answers
    - server disconnects any client that hasn't pinged for 5 seconds

    Possible client requests:
    - gets a list of all connected clients
    - ping: the server answers either with "ping ok" or "ping client_list_changed"
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
using namespace boost::posix_time;

io_service service; // gobal variable

class talk_to_client;
typedef boost::shared_ptr<talk_to_client> client_ptr;
typedef std::vector<client_ptr> array;
array clients;  // this vector will store all connection, namely session.
boost::recursive_mutex clients_cs;

#define MEM_FN(x)       boost::bind(&self_type::x, shared_from_this())
#define MEM_FN1(x,y)    boost::bind(&self_type::x, shared_from_this(),y)
#define MEM_FN2(x,y,z)  boost::bind(&self_type::x, shared_from_this(),y,z)


void update_clients_changed();

class talk_to_client : public boost::enable_shared_from_this<talk_to_client>
                     , boost::noncopyable {

    typedef talk_to_client self_type;

    talk_to_client() : sock_(service), started_(false), 
                       timer_(service), clients_changed_(false)
                         {}

public:
    typedef boost::system::error_code error_code;
    typedef boost::shared_ptr<talk_to_client> ptr;

    void start() {
        {
            boost::recursive_mutex::scoped_lock lk(clients_cs);
            clients.push_back( shared_from_this());
        }
        boost::recursive_mutex::scoped_lock lk(cs_);
        started_ = true;
        last_ping_ = boost::posix_time::microsec_clock::local_time();
        do_read();
    }
    static ptr new_() {
        ptr new_(new talk_to_client);
        return new_;
    }

    void stop() {
        {
            boost::recursive_mutex::scoped_lock lk(cs_);
            if ( !started_)
                return;
            started_ = false;
            sock_.close();
        }
        ptr self = shared_from_this();
        {
            boost::recursive_mutex::scoped_lock lk(clients_cs);
            // delete corrent session from vector. 
            array::iterator it = std::find(clients.begin(), clients.end(), self);
            clients.erase(it);
        }
        update_clients_changed();
    }

    bool started() const { 
        boost::recursive_mutex::scoped_lock lk(cs_);
        return started_; 
    }

    ip::tcp::socket & sock() { 
        boost::recursive_mutex::scoped_lock lk(cs_);
        return sock_;
    }

    std::string username() const { 
        boost::recursive_mutex::scoped_lock lk(cs_);
        return username_; 
    }

    void set_clients_changed() { 
        boost::recursive_mutex::scoped_lock lk(cs_);
        clients_changed_ = true; 
    }
private:
/*-------------according to receiving msg,do action-----------------------------*/ 
    void on_login(const std::string & msg) {
        boost::recursive_mutex::scoped_lock lk(cs_);
        std::istringstream in(msg);
        in >> username_ >> username_;
        std::cout << username_ << " logged in" << std::endl;
        do_write("login ok\n");
        update_clients_changed();
    }

    void on_ping() {
        boost::recursive_mutex::scoped_lock lk(cs_);
        do_write(clients_changed_ ? "ping client_list_changed\n" : "ping ok\n");
        clients_changed_ = false;
    }

    void on_clients() {
        array copy;
        {
            boost::recursive_mutex::scoped_lock lk(clients_cs);
            copy = clients;
        }
        std::string msg;
        for( array::const_iterator b = copy.begin(), e = copy.end() ; b != e; ++b)
            msg += (*b)->username() + " ";
        do_write("clients " + msg + "\n");
    }
/*---------------------useless function-----------------------------*/
    void do_ping() {
        do_write("ping\n");
    }

    void do_ask_clients() {
        do_write("ask_clients\n");
    }

/*-------------------reading operation----------------------------------------*/
    void do_read() {
        async_read(sock_, buffer(read_buffer_), 
                   MEM_FN2(read_complete,_1,_2), MEM_FN2(on_read,_1,_2));
        post_check_ping();
    }

    void post_check_ping() {
        boost::recursive_mutex::scoped_lock lk(cs_);
        timer_.expires_from_now(boost::posix_time::millisec(5000));
        timer_.async_wait( MEM_FN(on_check_ping));
    }

    void on_check_ping() {
        boost::recursive_mutex::scoped_lock lk(cs_);
        boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();
        if ( (now - last_ping_).total_milliseconds() > 5000) {
            std::cout << "stopping " << username_ << " - no ping in time" << std::endl;
            stop();
        }
        last_ping_ = boost::posix_time::microsec_clock::local_time();
    }

    size_t read_complete(const boost::system::error_code & err, size_t bytes) {
        if ( err) return 0;
        bool found = std::find(read_buffer_, read_buffer_ + bytes, '\n') < read_buffer_ + bytes;
        // we read one-by-one until we get to enter, no buffering
        return found ? 0 : 1;
    }

    void on_read(const error_code & err, size_t bytes) {
        if ( err) stop();
        if ( !started() ) return;

        boost::recursive_mutex::scoped_lock lk(cs_);

        std::string msg(read_buffer_, bytes);
        if ( msg.find("login ") == 0)
            on_login(msg);
        else if ( msg.find("ping") == 0)
            on_ping();
        else if ( msg.find("ask_clients") == 0)
            on_clients();
        else
            std::cerr << "invalid msg " << msg << std::endl;
    }

/*-----------------------writing operation------------------------------------------------------*/

    void do_write(const std::string & msg) {
        if ( !started() ) return;
        boost::recursive_mutex::scoped_lock lk(cs_);
        std::copy(msg.begin(), msg.end(), write_buffer_);
        sock_.async_write_some( buffer(write_buffer_, msg.size()), 
                                MEM_FN2(on_write,_1,_2));
    }

    void on_write(const error_code & err, size_t bytes) {
        do_read();
    }
/*------------------------private member-------------------------------------------------*/
private:
    mutable boost::recursive_mutex cs_;
    ip::tcp::socket sock_;
    enum { max_msg = 1024 };
    char read_buffer_[max_msg];
    char write_buffer_[max_msg];
    bool started_;
    std::string username_;
    deadline_timer timer_;
    boost::posix_time::ptime last_ping_;
    bool clients_changed_;
};

void update_clients_changed() {
    array copy;
    { 
        boost::recursive_mutex::scoped_lock lk(clients_cs);
        copy = clients;
    }
    // all client will be set to false.
    for( array::iterator b = copy.begin(), e = copy.end(); b != e; ++b){
        (*b)->set_clients_changed();
    }
}

ip::tcp::acceptor acceptor(service, ip::tcp::endpoint(ip::tcp::v4(), 8001));

void handle_accept(talk_to_client::ptr client, const boost::system::error_code & err) {
    client->start(); // get into client object by this interface.
    talk_to_client::ptr new_client = talk_to_client::new_();
    acceptor.async_accept(new_client->sock(), boost::bind(handle_accept,new_client,_1));
}

boost::thread_group threads;

void listen_thread() {
    service.run();
}

void start_listen(int thread_count) {
    for ( int i = 0; i < thread_count; ++i)
        threads.create_thread( listen_thread);
}


int main(int argc, char* argv[]) {
    // at server side, the following two rows can accept connection request.
    talk_to_client::ptr client = talk_to_client::new_();
    acceptor.async_accept(client->sock(), boost::bind(handle_accept,client,_1));
    // 
    start_listen(100);
    // 
    threads.join_all();
}















