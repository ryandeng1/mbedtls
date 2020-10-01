#include <rpc/client.h>
#include "external/aes_external.h"

void char_to_bool(bool* data, unsigned char input) {
	for (int i = 0; i < 8; i++) {
		data[i] = (input & (1 << i)) != 0;
	}
}

unsigned char bool_to_char(bool* data) {
	unsigned char res = 0;
	for (int i = 0; i < 8; i++) {
		if(data[7 - i]) {
			res |= 1 << i;
		}
	}
	return res;
}



/// Performs an AES operation over AES. This code is run by the rpc server.
void PerformAGMPCHKDF(unsigned char* key, unsigned char input_[16], unsigned char output_[16]) {
  const int party_id = 1;
  std::string file_name = "/home/ubuntu/distributed_trust/circuits/hkdf.txt";
  printf("Running the massive implementation for the party %d.\n", party_id);
  int port1 = 9002;
  int port2 = port1 + 2 * (NUM_PARTIES + 1) * (NUM_PARTIES + 1) + 1;
  printf("The ports are (%d, %d) for a total of %d parties.\n", port1, port2, NUM_PARTIES);
  NetIOMP<NUM_PARTIES> io(party_id, port1);
  NetIOMP<NUM_PARTIES> io2(party_id, port2);
  NetIOMP<NUM_PARTIES> *ios[2] = {&io, &io2};
  printf("Loading the circuit file");
  ThreadPool pool(2 * NUM_PARTIES);
  CircuitFile cf(file_name.c_str());
  printf("Finished loading the circuit at %s!\n", file_name.c_str());
  CMPC<NUM_PARTIES> *mpc = new CMPC<NUM_PARTIES>(ios, &pool, party_id, &cf);
  // TODO(ryan): Currently the server just inputs 0's to help testing.
  bool *input = new bool[cf.n1 + cf.n2];
  bool* input_ptr = input;
  
  // Assume key is 128 bits here
  
  for (int i = 15; i >= 0; i--) {
	  char_to_bool(input_ptr, key[i]);
	  input_ptr += 8;
  }

  printf("Input length: %d\n", cf.n1 + cf.n2);
  for (int i = 15; i >= 0; i--) {
	  char_to_bool(input_ptr, input_[i]);
	  printf("input_[%d] = %d", i, input_[i]);
	  input_ptr += 8;
  }

  bool *output = new bool[cf.n3];
  printf("Output length: %d\n", cf.n3);
  // memset(input, false, cf.n1);
  memset(output, false, cf.n3);
  mpc->function_independent();
  mpc->function_dependent();
  mpc->online(input, output);
  printf("Bit output agmpc\n");
  for (int i = 0; i < cf.n3; i++) {
	  printf("%d", output[i]);
  }
  printf("\n");
  bool* output_ptr = output;
  for (int i = 0; i < 16; i++) {
	  output_[i] = bool_to_char(output_ptr);
	  printf("output_[%d] = %d", i, output_[i]);
	  output_ptr += 8;
  }
  
  delete mpc;
  delete[] input;
  delete[] output;
  return;
}

extern "C" {
  void aes_external_encrypt(unsigned char* key, unsigned char input[16],
                            unsigned char output[16], int num_rounds) {  
  std::string rpc_server_ip = "127.0.0.1";  
  rpc::client client(rpc_server_ip, 8080);
  auto res = client.async_call("PerformAGMPCHKDF");
  PerformAGMPCHKDF(key, input, output);
  return;
}
}

