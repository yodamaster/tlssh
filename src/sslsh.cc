#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include<poll.h>
#include<termios.h>
#include<unistd.h>

#include<iostream>

#include"sslsh.h"
#include"sslsocket.h"
#if 0
	char *randfile = "random.seed";
	int fd;
	RAND_load_file("/dev/urandom", 1024);

	unlink(randfile);
	fd = open(randfile, O_WRONLY | O_CREAT | O_EXCL, 0600);
	close(fd);
	RAND_write_file("random.seed");
#endif




BEGIN_NAMESPACE(sslsh);

struct Options {
	std::string port;       // port to connect to
	std::string certfile;
	std::string keyfile;
	std::string cafile;
	std::string capath;
};
Options options = {
 port: "12345",
 certfile: "client-marvin.pem",
 keyfile: "client-marvin.pem",
 cafile: "class3.crt",
};
	
SSLSocket sock;

int
mainloop(FDWrap &terminal)
{
	struct pollfd fds[2];
	int err;
	std::string to_server;
	std::string to_terminal;
	for (;;) {
		fds[0].fd = sock.getfd();
		fds[0].events = POLLIN;
		if (!to_server.empty()) {
			fds[0].events |= POLLOUT;
		}

		fds[1].fd = terminal.get();
		fds[1].events = POLLIN;
		if (!to_terminal.empty()) {
			fds[1].events |= POLLOUT;
		}

		err = poll(fds, 2, -1);
		if (!err) { // timeout
			continue;
		}
		if (0 > err) { // error
			continue;
		}

		// from client
		if (fds[0].revents & POLLIN) {
			do {
				to_terminal += sock.read();
			} while (sock.ssl_pending());
		}

		// from terminal
		if (fds[1].revents & POLLIN) {
			to_server += terminal.read();
		}

		if ((fds[0].revents & POLLOUT)
		    && !to_server.empty()) {
			size_t n;
			n = sock.write(to_server);
			to_server = to_server.substr(n);
		}

		if ((fds[1].revents & POLLOUT)
		    && !to_terminal.empty()) {
			size_t n;
			n = terminal.write(to_terminal);
			to_terminal = to_terminal.substr(n);
		}
	}
}


struct termios old_tio;
void
reset_tio(void)
{
	tcsetattr(0, TCSADRAIN, &old_tio);
}

/**
 *
 */
int
new_connection()
{
	sock.ssl_connect(options.certfile,
			 options.keyfile,
			 options.cafile,
			 options.capath);

	FDWrap terminal(0);

	tcgetattr(terminal.get(), &old_tio);
	atexit(reset_tio);

	struct termios tio;
	cfmakeraw(&tio);
	tcsetattr(terminal.get(), TCSADRAIN, &tio);

	mainloop(terminal);
	terminal.forget();
}

END_NAMESPACE(sslsh);


BEGIN_LOCAL_NAMESPACE()
using namespace sslsh;
int
main2(int argc, char * const argv[])
{
	Socket rawsock;
	rawsock.connect("127.0.0.1", options.port);
	sock.ssl_attach(rawsock);
	return new_connection();
}
END_LOCAL_NAMESPACE()


int
main(int argc, char **argv)
{
	try {
		return main2(argc, argv);
	} catch (const std::exception &e) {
		std::cout << "std::exception: " << e.what() << std::endl;
	} catch (const char *e) {
		std::cerr << "FIXME: " << std::endl
			  << e << std::endl;
	}

}
