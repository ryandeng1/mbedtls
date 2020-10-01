#include <rpc/client.h>
#include "external/ec_add_external.h"

void PerformECAddition() {
	// RPC server acts as party 1
	
	const int party_id = 1;
	std::map<int, std::string> party_ip_list;
        party_ip_list[0] = "127.0.0.1";
        party_ip_list[1] = "127.0.0.1";
        party_ip_list[2] = "172.31.12.22";
        party_ip_list[3] = "172.31.13.61";
        party_ip_list[4] = "172.31.7.45";

	// std::cout << "Party ID: " << party_id << std::endl;
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
	
	// system("/home/ubuntu/distributed_trust/spdz/build/online 1");
}

extern "C" {
	void ec_add_external() {
	  std::string rpc_server_ip = "127.0.0.1";  
  	  rpc::client client(rpc_server_ip, 8080);
  	  auto res = client.async_call("PerformECAddition");
          PerformECAddition();
	  // std::cout << "Performed EC Addition" << std::endl;
	}
}
