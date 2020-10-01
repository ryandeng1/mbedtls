#include <iostream>
#include "rpc/server.h"
#include "external/rpc_server.h"

int main(int argc, char *argv[]) {
	// Creating a server that listens on port 8080
	rpc::server srv(8080);

	// Binding a lambda function to the name "add".
	srv.bind("add", [](int a, int b) {
		return a + b;
	});
	
	srv.bind("PerformAGMPCAES", &PerformAGMPCAES);
	srv.bind("PerformECAddition", &PerformECAddition);
	srv.bind("PerformAGMPCHKDF", &PerformAGMPCHKDF);
	// Run the server loop.
	srv.run();

	return 0;
}
