#include <emp-tool/emp-tool.h>
#include <emp-agmpc/emp-agmpc.h>
#include <rpc/server.h>
#include "external/constants.h"
#include "external/ec_add_external.h"
#include <gmp.h>
#include <gmpxx.h>


/// Performs an AES operation over AGMPC. This code is run by the rpc server.
void PerformAGMPCAES() {
  const int party_id = 2;
  std::string file_name = "/home/ubuntu/distributed_trust/circuits/aes128_full.txt";
  printf("Running the massive implementation for the party %d.\n", party_id);
  int port1 = 9002;
  int port2 = port1 + 2 * (NUM_PARTIES + 1) * (NUM_PARTIES + 1) + 1;
  printf("The ports are (%d, %d) for a total of %d parties.\n", port1, port2, NUM_PARTIES);
  NetIOMP<NUM_PARTIES> io(party_id, port1);
  NetIOMP<NUM_PARTIES> io2(party_id, port2);
  NetIOMP<NUM_PARTIES> *ios[2] = {&io, &io2};
  ThreadPool pool(2 * NUM_PARTIES);
  printf("Loading the circuit file");
  CircuitFile cf(file_name.c_str());
  printf("Finished loading the circuit at %s!\n", file_name.c_str());
  CMPC<NUM_PARTIES> *mpc = new CMPC<NUM_PARTIES>(ios, &pool, party_id, &cf);
  // TODO(ryan): Currently the server just inputs 0's to help testing.
  bool *input = new bool[cf.n1 + cf.n2];
  bool *output = new bool[cf.n3];
  memset(input, false, cf.n1);
  memset(output, false, cf.n3);
  mpc->function_independent();
  mpc->function_dependent();
  mpc->online(input, output); 
  delete mpc;
  delete[] input;
  delete[] output;
  return;
}

/// Performs a key derivation operation over AGMPC. This code is run by the rpc server.
void PerformAGMPCHKDF() {
  const int party_id = 2;
  std::string file_name = "/home/ubuntu/distributed_trust/circuits/hkdf.txt";
  printf("Running the massive implementation for the party %d.\n", party_id);
  int port1 = 9002;
  int port2 = port1 + 2 * (NUM_PARTIES + 1) * (NUM_PARTIES + 1) + 1;
  printf("The ports are (%d, %d) for a total of %d parties.\n", port1, port2, NUM_PARTIES);
  NetIOMP<NUM_PARTIES> io(party_id, port1);
  NetIOMP<NUM_PARTIES> io2(party_id, port2);
  NetIOMP<NUM_PARTIES> *ios[2] = {&io, &io2};
  ThreadPool pool(2 * NUM_PARTIES);
  printf("Loading the circuit file");
  CircuitFile cf(file_name.c_str());
  printf("Finished loading the circuit at %s!\n", file_name.c_str());
  CMPC<NUM_PARTIES> *mpc = new CMPC<NUM_PARTIES>(ios, &pool, party_id, &cf);
  // TODO(ryan): Currently the server just inputs 0's to help testing.
  bool *input = new bool[cf.n1 + cf.n2];
  bool *output = new bool[cf.n3];
  memset(input, false, cf.n1);
  memset(output, false, cf.n3);
  mpc->function_independent();
  mpc->function_dependent();
  mpc->online(input, output); 
  delete mpc;
  delete[] input;
  delete[] output;
  return;
}

void PerformECAddition() {
	// RPC server acts as party 0
	
	const int party_id = 0;
	std::map<int, string> party_ip_list;
        party_ip_list[0] = "127.0.0.1";
        party_ip_list[1] = "127.0.0.1";
        party_ip_list[2] = "172.31.12.22";
        party_ip_list[3] = "172.31.13.61";
        party_ip_list[4] = "172.31.7.45";

        cout << "Party ID: " << party_id << endl;
        mpz_class alpha_share;
        if(party_id == 0) {
                alpha_share = 1_mpz;
        }else{
                alpha_share = 0_mpz;
        }

        mpz_class input_x;
        mpz_class input_y;

         switch(party_id) {
                case 0:
                        input_x = mpz_class("48439561293906451759052585252797914202762949526041747995844080717082404635286");
                        input_y = mpz_class("36134250956749795798585127919587881956611106672985015071877198253568414405109");
                        break;
                case 1:
                        input_x = mpz_class("56515219790691171413109057904011688695424810155802929973526481321309856242040");
                        input_y = mpz_class("3377031843712258259223711451491452598088675519751548567112458094635497583569");
                        break;
                case 2:
                        input_x = mpz_class("42877656971275811310262564894490210024759287182177196162425349131675946712428");
                        input_y = mpz_class("61154801112014214504178281461992570017247172004704277041681093927569603776562");
                        break;
                case 3:
                        input_x = mpz_class("102369864249653057322725350723741461599905180004905897298779971437827381725266");
                        input_y = mpz_class("101744491111635190512325668403432589740384530506764148840112137220732283181254");
                        break;
                case 4:
                        input_x = mpz_class("36794669340896883012101473439538929759152396476648692591795318194054580155373");
                        input_y = mpz_class("101659946828913883886577915207667153874746613498030835602133042203824767462820");
                        break;
        }

        // cout << "input_x: " << input_x << endl;
        // cout << "input_y: " << input_y << endl;
	const int num_parties = 2;
        mpz_class res = main_routine(party_ip_list, num_parties, party_id, input_x, input_y, alpha_share);

        // cout << "Result: " << res << endl;

        // expect res: 108677532895904936863904823330600106055145041255062888673713681538132314135903
	
	// system("/home/ubuntu/distributed_trust/spdz/build/online 0");
	// printf("Key exchange completed");
}

