//
// Created by weikeng on 9/9/20.
//

#ifndef EC_ADD_EXTERNAL
#define EC_ADD_EXTERNAL

#ifdef __cplusplus
#include <rpc/server.h>

#include <map>
#include <sstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <gmp.h>
#include <gmpxx.h>
#include <rpc/client.h>

#include <openssl/sha.h>

using namespace std;

class SecretValue {
	public:
		mpz_class p;
		mpz_class value_share;
		mpz_class mac_share;

		SecretValue(SecretValue const &b) {
			this->p = b.p;
			this->value_share = b.value_share;
			this->mac_share = b.mac_share;
		}

		SecretValue(mpz_class p) {
			this->p = p;
			this->value_share = 0_mpz;
			this->mac_share = 0_mpz;
		}

		SecretValue(mpz_class p, mpz_class value_share, mpz_class mac_share){
			this->p = p;
			this->value_share = value_share;
			this->mac_share = mac_share;
		}

		SecretValue operator+(const SecretValue& b) {
			assert(this->p == b.p);
			return SecretValue(
					this->p,
					(this->value_share + b.mac_share) % this->p,
					(this->mac_share + b.mac_share) % this->p
			);
		}

		SecretValue operator-(const SecretValue& b) {
			assert(this->p == b.p);
			return SecretValue(
					this->p,
					(this->p + this->value_share - b.mac_share) % this->p,
					(this->p + this->mac_share - b.mac_share) % this->p
			);
		}

		SecretValue operator*(const mpz_class& b) {
			return SecretValue(
					this->p,
					(this->value_share * b) % this->p,
					(this->mac_share * b) % this->p
			);
		}
};

class Triple {
	public:
		mpz_class p;
		SecretValue a_share;
		SecretValue b_share;
		SecretValue c_share;

		Triple(Triple const &b) : a_share(b.a_share), b_share(b.b_share), c_share(b.c_share) {
			this->p = b.p;
		}

		Triple(mpz_class p) : a_share(p), b_share(p), c_share(p) {
			this->p = p;
		}

		Triple(mpz_class p, SecretValue a_share, SecretValue b_share, SecretValue c_share) : a_share(a_share), b_share(b_share), c_share(c_share) {
			this->p = p;
		}
};

class Square {
	public:
		mpz_class p;
		SecretValue a_share;
		SecretValue c_share;

		Square(Square const &b) : a_share(b.a_share), c_share(b.c_share) {
			this->p = b.p;
		}

		Square(mpz_class p, bool is_first_party, mpz_class alpha_share) : a_share(p), c_share(p){
			this->p = p;
			if(is_first_party) {
				this->a_share.value_share = 1_mpz;
			}
			this->a_share.mac_share = alpha_share;
		}

		Square(mpz_class p, SecretValue a_share, SecretValue c_share): a_share(a_share), c_share(c_share) {
			this->p = p;
		}
};

class InputTuple {
	public:
		int input_party;
		mpz_class p;
		mpz_class value;
		mpz_class value_share;
		mpz_class value_mac_share;

		InputTuple(InputTuple const &b) {
			this->input_party = b.input_party;
			this->p = b.p;
			this->value = b.value;
			this->value_share = b.value_share;
			this->value_mac_share = b.value_mac_share;
		}

		InputTuple(int input_party, mpz_class p) {
			InputTuple(input_party, p, 0_mpz, 0_mpz, 0_mpz);
		}

		InputTuple(int input_party, mpz_class p, mpz_class value, mpz_class value_share, mpz_class value_mac_share) {
			this->input_party = input_party;
			this->p = p;
			this->value = value;
			this->value_share = value_share;
			this->value_mac_share = value_mac_share;
		}
};

class ECPoint {
	public:
		SecretValue x;
		SecretValue y;

		ECPoint(ECPoint const &b): x(b.x), y(b.y) {
		}

		ECPoint(SecretValue x, SecretValue y) : x(x), y(y) {
		}
};

static void wait(string k, int num);

map<string, map<int, vector<vector<unsigned char>>>> BroadcastMap;
map<string, int> BroadcastCounter;
mutex BroadcastMapAccessMutex;
condition_variable BroadcastMapAccessCV;

vector<SecretValue> OpenedVariables;
vector<mpz_class> OpenedValues;

static void create_server(rpc::server &server){
	server.bind("put", [](string k, int id, vector<vector<unsigned char>> v){
		{
			lock_guard<mutex> lk(BroadcastMapAccessMutex);
			BroadcastMap[k][id] = v;
			BroadcastCounter[k]++;
		}
		BroadcastMapAccessCV.notify_one();
	});
}

static vector<mpz_class> open(vector<SecretValue> x_vec, string identifier, mpz_class p, int num_party, int party_id, map<int, rpc::client*> *clients) {
	for (size_t i = 0; i < x_vec.size(); i++) {
		OpenedVariables.push_back(x_vec[i]);
	}

	vector<vector<unsigned char>> pack;
	for (size_t i = 0; i < x_vec.size(); i++) {
		unsigned char *buf;
		size_t written;
		buf = (unsigned char *) mpz_export(0, &written, 1, 1, 0, 0, x_vec[i].value_share.get_mpz_t());

		vector<unsigned char> sent(buf, buf + written);
		pack.push_back(sent);
	}

	for(int i = 0 ; i < num_party; i++) {
		if(i != party_id) {
			(*clients)[i]->async_call("put", identifier, party_id, pack);
		}
	}

	wait(identifier, num_party - 1);

	vector<mpz_class> val_vec;
	for (size_t j = 0; j < x_vec.size(); j++) {
		mpz_class val = x_vec[j].value_share;
		for (int i = 0; i < num_party; i++) {
			if (i != party_id) {
				mpz_t this_broadcast_mpz_t;
				mpz_init(this_broadcast_mpz_t);

				mpz_import(this_broadcast_mpz_t, BroadcastMap[identifier][i][j].size(), 1, 1, 0, 0,
						   BroadcastMap[identifier][i][j].data());

				mpz_class this_broadcast_value(this_broadcast_mpz_t);
				val += this_broadcast_value;

				mpz_clear(this_broadcast_mpz_t);
			}
		}

		val %= p;
		val_vec.push_back(val);
	}

	OpenedValues.insert(OpenedValues.end(), val_vec.begin(), val_vec.end());

	return val_vec;
}

static vector<SecretValue> mul(vector<SecretValue> x, vector<SecretValue> y, string identifier, mpz_class p, int num_party, int party_id, mpz_class alpha_share, map<int, rpc::client*> *clients) {
	assert(x.size() == y.size());

	/* prepare the triples */
	vector<Triple> triple_vec;
	for(size_t i = 0; i < x.size(); i++) {
		triple_vec.push_back(Triple(p));
	}

	vector<SecretValue> x_minus_a_vec;
	vector<SecretValue> y_minus_b_vec;

	for(size_t i = 0; i < x.size(); i++) {
		/* get the x - a */
		SecretValue x_minus_a = x[i] - triple_vec[i].a_share;
		x_minus_a_vec.push_back(x_minus_a);

		/* get the y - b */
		SecretValue y_minus_b = y[i] - triple_vec[i].b_share;
		y_minus_b_vec.push_back(y_minus_b);
	}

	vector<SecretValue> merged;
	merged.reserve(x_minus_a_vec.size() + y_minus_b_vec.size());
	merged.insert(merged.end(), x_minus_a_vec.begin(), x_minus_a_vec.end());
	merged.insert(merged.end(), y_minus_b_vec.begin(), y_minus_b_vec.end());

	vector<mpz_class> merge_opened = open(merged, identifier + "/x_minus_a_and_y_minus_b", p, num_party, party_id, clients);

	vector<mpz_class> x_minus_a_opened_vec(merge_opened.begin(), merge_opened.begin() + x_minus_a_vec.size());
	vector<mpz_class> y_minus_b_opened_vec(merge_opened.begin() + x_minus_a_vec.size(), merge_opened.end());

	vector<SecretValue> res_vec;
	for(size_t i = 0; i < x.size(); i++) {
		SecretValue res = triple_vec[i].c_share + triple_vec[i].b_share * x_minus_a_opened_vec[i] + triple_vec[i].a_share * y_minus_b_opened_vec[i];
		if (party_id == 0) {
			res.value_share += x_minus_a_opened_vec[i] * y_minus_b_opened_vec[i];
		}
		res.mac_share += alpha_share * x_minus_a_opened_vec[i] * y_minus_b_opened_vec[i];
		res_vec.push_back(res);
	}

	return res_vec;
}

static vector<SecretValue> inverse(vector<SecretValue> x_vec, string identifier, mpz_class p, int num_party, int party_id, mpz_class alpha_share, map<int, rpc::client*> *clients) {
	/* prepare the squares */
	vector<Square> square_vec;
	for(size_t i = 0; i < x_vec.size(); i++) {
		square_vec.push_back(Square(p, party_id == 0, alpha_share));
	}

	vector<SecretValue> r_vec;
	for(size_t i = 0; i < x_vec.size(); i++) {
		r_vec.push_back(square_vec[i].a_share);
	}

	/* compute r * x */
	vector<SecretValue> r_times_x_vec = mul(x_vec, r_vec, identifier + "/x_mul_r", p, num_party, party_id, alpha_share, clients);

	/* open r * x */
	vector<mpz_class> r_times_x_opened_vec = open(r_times_x_vec, identifier + "/open_x_mul_r", p, num_party, party_id, clients);

	/* compute the inverse outside */
	vector<mpz_class> r_times_x_inverse_opened_vec;

	mpz_t res;
	mpz_init(res);
	for(size_t i = 0; i < x_vec.size(); i++) {
		mpz_invert(res, r_times_x_opened_vec[i].get_mpz_t(), p.get_mpz_t());
		r_times_x_inverse_opened_vec.push_back(mpz_class(res));
	}
	mpz_clear(res);

	/* allocate the inverse as values */
	vector<SecretValue> r_times_x_inverse_vec;
	for(size_t i = 0; i < x_vec.size(); i++) {
		if(party_id == 0){
			r_times_x_inverse_vec.push_back(SecretValue(p, r_times_x_inverse_opened_vec[i], alpha_share * r_times_x_inverse_opened_vec[i]));
		}else{
			r_times_x_inverse_vec.push_back(SecretValue(p, 0_mpz, alpha_share * r_times_x_inverse_opened_vec[i]));
		}
	}

	/* multiply the inverse with r */
	vector<SecretValue> x_inverse_vec = mul(r_times_x_inverse_vec, r_vec, identifier + "/r_times_inverse_x_mul_r", p, num_party, party_id, alpha_share, clients);

	return x_inverse_vec;
}

static void wait(string k, int num) {
	unique_lock<mutex> lk(BroadcastMapAccessMutex);
	BroadcastMapAccessCV.wait(lk, [k, num]{
		return BroadcastCounter[k] >= num;
	});
	return;
}

static void random_data(unsigned char *data, random_device &rd) {
	uint32_t *seed_share_ptr = (uint32_t *) (data);
	seed_share_ptr[0] = rd();
	seed_share_ptr[1] = rd();
	seed_share_ptr[2] = rd();
	seed_share_ptr[3] = rd();
}

static void commit(unsigned char *commitment, unsigned char *opening, const unsigned char *data, const size_t data_len, random_device &rd) {
	random_data(opening, rd);

	SHA256_CTX sha256;
	SHA256_Init(&sha256);
	SHA256_Update(&sha256, opening, 16);
	SHA256_Update(&sha256, data, data_len);
	SHA256_Update(&sha256, &data_len, sizeof(size_t));
	SHA256_Final(commitment, &sha256);
}

static bool open_commitment(const unsigned char *commitment, const unsigned char *opening, const unsigned char *data, const size_t data_len) {
	unsigned char commitment_supposed[32];

	SHA256_CTX sha256;
	SHA256_Init(&sha256);
	SHA256_Update(&sha256, opening, 16);
	SHA256_Update(&sha256, data, data_len);
	SHA256_Update(&sha256, &data_len, sizeof(size_t));
	SHA256_Final(commitment_supposed, &sha256);

	bool res = true;
	for(int i = 0; i < 32; i++) {
		if(commitment[i] != commitment_supposed[i]){
			res = false;
		}
	}
	return res;
}

static vector<mpz_class> make_rand_number(const unsigned char *seed, size_t num, mpz_class p) {
	vector<unsigned char> rand_bytes;

	unsigned char buf[16];
	size_t needed_sha256_evals = ((256 + 128) / 8 * num + 16) / 16;

	SHA256_CTX sha256;

	for(size_t counter = 0; counter < needed_sha256_evals; counter++) {
		SHA256_Init(&sha256);
		SHA256_Update(&sha256, seed, 16);
		SHA256_Update(&sha256, &counter, sizeof(counter));
		SHA256_Final(buf, &sha256);

		rand_bytes.insert(rand_bytes.end(), buf, buf + 16);
	}

	unsigned char *rand_bytes_src = rand_bytes.data();

	vector<mpz_class> res;

	mpz_t rand_mpz;
	unsigned char rand_slice[48];
	mpz_init(rand_mpz);
	for(size_t i = 0; i < num; i++) {
		copy(rand_bytes_src + i * 48, rand_bytes_src + i * 48 + 48, rand_slice);
		mpz_import(rand_mpz, 48, 1, 1, 0, 0, rand_slice);
		mpz_class rand_num(rand_mpz);
		res.push_back(rand_num % p);
	}
	mpz_clear(rand_mpz);

	return res;
}

static mpz_class main_routine(map<int, string> party_ip_list, int num_party, int party_id, mpz_class x, mpz_class y, mpz_class alpha_share){
	/* p in secp256r1 */
	mpz_class p("115792089210356248762697446949407573530086143415290314195533631308867097853951");

	/* set up the rpc server, which pushes the broadcast data */
	rpc::server srv(12000 + party_id);
	create_server(srv);
	srv.async_run(num_party * 2);

	/* wait for start time difference */
	this_thread::sleep_for(chrono::seconds(5) );

	/* establish connections */
	map<int, rpc::client*> clients;
	for(int i = 0; i < num_party; i++) {
		if(i != party_id) {
			clients[i] = new rpc::client(party_ip_list[i], 12000 + i);
		}
	}

	/* prepare the input pairs */
	vector<InputTuple> input_x_tuples;
	vector<InputTuple> input_y_tuples;
	for(int i = 0; i < num_party; i++) {
		input_x_tuples.push_back(InputTuple(i, p));
		input_y_tuples.push_back(InputTuple(i, p));
	}

	/* take in the inputs */
	mpz_class my_broadcast_x_value = input_x_tuples[party_id].value + x;
	mpz_class my_broadcast_y_value = input_y_tuples[party_id].value + y;

	/* assemble the msg to be sent for the input */
	vector<vector<unsigned char>> pack_x;
	vector<vector<unsigned char>> pack_y;
	{
		unsigned char *buf_x;
		unsigned char *buf_y;
		size_t written_x;
		size_t written_y;
		buf_x = (unsigned char *) mpz_export(0, &written_x, 1, 1, 0, 0, my_broadcast_x_value.get_mpz_t());
		buf_y = (unsigned char *) mpz_export(0, &written_y, 1, 1, 0, 0, my_broadcast_y_value.get_mpz_t());

		vector<unsigned char> sent_x(buf_x, buf_x + written_x);
		vector<unsigned char> sent_y(buf_y, buf_y + written_y);

		pack_x.push_back(sent_x);
		pack_y.push_back(sent_y);
	}

	cout << "input round" << endl;
	for(int i = 0 ; i < num_party; i++) {
		if(i != party_id) {
			clients[i]->async_call("put", "input_x", party_id, pack_x);
			clients[i]->async_call("put", "input_y", party_id, pack_y);
		}
	}

	/* wait for the input to arrive */
	wait("input_x", num_party - 1);
	wait("input_y", num_party - 1);

	/* create the secret values */
	vector<ECPoint> points;
	for(int i = 0; i < num_party; i++) {
		if(i == party_id) {
			SecretValue this_x(p, my_broadcast_x_value - input_x_tuples[party_id].value_share, alpha_share * my_broadcast_x_value - input_x_tuples[party_id].value_mac_share);
			SecretValue this_y(p, my_broadcast_y_value - input_y_tuples[party_id].value_share, alpha_share * my_broadcast_y_value - input_y_tuples[party_id].value_mac_share);
			points.push_back(ECPoint(this_x, this_y));
		}else{
			mpz_t this_broadcast_x_mpz_t;
			mpz_t this_broadcast_y_mpz_t;
			mpz_inits(this_broadcast_x_mpz_t, this_broadcast_y_mpz_t, NULL);

			mpz_import(this_broadcast_x_mpz_t, BroadcastMap["input_x"][i][0].size(), 1, 1, 0, 0, BroadcastMap["input_x"][i][0].data());
			mpz_import(this_broadcast_y_mpz_t, BroadcastMap["input_y"][i][0].size(), 1, 1, 0, 0, BroadcastMap["input_y"][i][0].data());

			mpz_class this_broadcast_x_value(this_broadcast_x_mpz_t);
			mpz_class this_broadcast_y_value(this_broadcast_y_mpz_t);

			SecretValue this_x(p, - input_x_tuples[i].value_share, alpha_share * this_broadcast_x_value - input_x_tuples[i].value_mac_share);
			SecretValue this_y(p, - input_y_tuples[i].value_share, alpha_share * this_broadcast_y_value - input_y_tuples[i].value_mac_share);

			points.push_back(ECPoint(this_x, this_y));
			mpz_clears(this_broadcast_x_mpz_t, this_broadcast_y_mpz_t, NULL);
		}
	}

	cout << "start to run the main loop" << endl;

	vector<ECPoint> cur;
	vector<ECPoint> next;

	for(int i = 0; i < num_party; i++) {
		next.push_back(points[i]);
	}

	int round = 0;
	while(next.size() != 1){
		cur.clear();
		int pairs = next.size() / 2;
		for(int j = 0; j < pairs; j++){
			cur.push_back(next[2 * j]);
			cur.push_back(next[2 * j + 1]);
		}

		if(next.size() % 2 == 1) {
			ECPoint defer = next[next.size() - 1];
			next.clear();
			next.push_back(defer);
		}else{
			next.clear();
		}

		/*
		 * compute y0 - y1 and the inverse of x0 - x1
		 */
		vector<SecretValue> x0_minus_x1_vec;
		vector<SecretValue> y0_minus_y1_vec;
		for(int j = 0; j < pairs; j++){
			x0_minus_x1_vec.push_back(cur[2 * j].x - cur[2 * j + 1].x);
			y0_minus_y1_vec.push_back(cur[2 * j].y - cur[2 * j + 1].y);
		}

		vector<SecretValue> x0_minus_x1_inverse_vec = inverse(x0_minus_x1_vec, "main_loop/" + to_string(round) + "/x0_minus_x1_inverse", p, num_party, party_id, alpha_share, &clients);
		/*
		 * compute s
		 */
		vector<SecretValue> s_vec = mul(x0_minus_x1_inverse_vec, y0_minus_y1_vec, "main_loop/" + to_string(round) + "/s", p, num_party, party_id, alpha_share, &clients);
		vector<SecretValue> s_square_vec = mul(s_vec, s_vec, "main_loop/" + to_string(round) + "/s_square", p, num_party, party_id, alpha_share, &clients);

		/*
		 * compute x2
		 */
		vector<SecretValue> x2_vec;
		for(int j = 0; j < pairs; j++) {
			x2_vec.push_back(s_square_vec[j] - cur[2 * j].x - cur[2 * j + 1].x);
		}

		/*
		 * compute x0 - x2
		 */
		vector<SecretValue> x0_minus_x2_vec;
		for(int j = 0; j < pairs; j++) {
			x0_minus_x2_vec.push_back(cur[2 * j].x - x2_vec[j]);
		}

		/*
		 * compute y2 + y0
		 */
		vector<SecretValue> y2_plus_y0_vec = mul(s_vec, x0_minus_x2_vec, "main_loop/" + to_string(round) + "/y2_plus_y0", p, num_party, party_id, alpha_share, &clients);

		/*
		 * compute y2
		 */
		vector<SecretValue> y2_vec;
		for(int j = 0; j < pairs; j++) {
			y2_vec.push_back(y2_plus_y0_vec[j] - cur[2 * j].y);
		}

		/*
		 * push the points to the `next` vector
		 */
		for(int j = 0; j < pairs; j++) {
			next.push_back(ECPoint(x2_vec[j], y2_vec[j]));
		}

		cout << "remaining points: " << next.size() << endl;

		round++;
	}

	vector<SecretValue> final_vec;
	final_vec.push_back(next[0].x);

	vector<mpz_class> final_opened_vec = open(final_vec, "result", p, num_party, party_id, &clients);

	random_device rd;
	unsigned char seed_share[16];
	random_data(seed_share, rd);

	unsigned char seed_share_commitment[32];
	unsigned char seed_share_opening[16];
	commit(seed_share_commitment, seed_share_opening, seed_share, 16, rd);

	{
		vector<vector<unsigned char>> pack;
		vector<unsigned char> seed_share_commitment_vec(seed_share_commitment, seed_share_commitment + 32);
		pack.push_back(seed_share_commitment_vec);

		for (int i = 0; i < num_party; i++) {
			if (i != party_id) {
				clients[i]->async_call("put", "seed_share_commitment", party_id, pack);
			}
		}
	}

	wait("seed_share_commitment", num_party - 1);

	{
		vector<vector<unsigned char>> pack;
		vector<unsigned char> seed_share_vec(seed_share, seed_share + 16);
		vector<unsigned char> opening_vec(seed_share_opening, seed_share_opening + 16);
		pack.push_back(seed_share_vec);
		pack.push_back(opening_vec);

		for (int i = 0; i < num_party; i++) {
			if (i != party_id) {
				clients[i]->async_call("put", "seed_share", party_id, pack);
			}
		}
	}

	wait("seed_share", num_party - 1);

	for(int i = 0; i < num_party; i++) {
		if(i != party_id) {
			bool res = open_commitment(BroadcastMap["seed_share_commitment"][i][0].data(), BroadcastMap["seed_share"][i][1].data(), BroadcastMap["seed_share"][i][0].data(), 16);
			if(!res){
				cout << "Error: commitment does not match for party " << i << endl;
			}
		}
	}

	unsigned char seed[16];
	for(int j = 0; j < 16; j++) {
		seed[j] = 0;
	}
	for(int i = 0; i < num_party; i++) {
		if(i == party_id) {
			for(int j = 0; j < 16; j++) {
				seed[j] ^= seed_share[j];
			}
		}else{
			unsigned char *this_seed_share = BroadcastMap["seed_share"][i][0].data();
			for(int j = 0; j < 16; j++) {
				seed[j] ^= this_seed_share[j];
			}
		}
	}

	vector<mpz_class> rands = make_rand_number(seed, OpenedVariables.size(), p);

	mpz_class a(0);
	for(size_t j = 0; j < OpenedVariables.size(); j++) {
		a += rands[j] * OpenedValues[j];
	}

	mpz_class gamma_share(0);
	for(size_t j = 0; j < OpenedVariables.size(); j++) {
		gamma_share += rands[j] * OpenedVariables[j].mac_share;
	}

	mpz_class sigma_share = gamma_share - alpha_share * a;

	unsigned char *sigma_share_char;
	size_t sigma_share_len;
	{
		sigma_share_char = (unsigned char *) mpz_export(0, &sigma_share_len, 1, 1, 0, 0, sigma_share.get_mpz_t());
	}

	unsigned char sigma_share_commitment[32];
	unsigned char sigma_share_opening[16];
	commit(sigma_share_commitment, sigma_share_opening, sigma_share_char, sigma_share_len, rd);

	{
		vector<vector<unsigned char>> pack;
		vector<unsigned char> sigma_share_commitment_vec(sigma_share_commitment, sigma_share_commitment + 32);
		pack.push_back(sigma_share_commitment_vec);

		for (int i = 0; i < num_party; i++) {
			if (i != party_id) {
				clients[i]->async_call("put", "sigma_share_commitment", party_id, pack);
			}
		}
	}

	wait("sigma_share_commitment", num_party - 1);

	{
		vector<vector<unsigned char>> pack;
		vector<unsigned char> sigma_share_vec(sigma_share_char, sigma_share_char + sigma_share_len);
		vector<unsigned char> opening_vec(sigma_share_opening, sigma_share_opening + 16);

		unsigned char *sigma_share_len_pt = (unsigned char *) &sigma_share_len;
		vector<unsigned char> sigma_share_size(sigma_share_len_pt, sigma_share_len_pt + sizeof(size_t));

		pack.push_back(sigma_share_vec);
		pack.push_back(opening_vec);
		pack.push_back(sigma_share_size);

		for (int i = 0; i < num_party; i++) {
			if (i != party_id) {
				clients[i]->async_call("put", "sigma_share", party_id, pack);
			}
		}
	}

	wait("sigma_share", num_party - 1);

	for(int i = 0; i < num_party; i++) {
		if(i != party_id) {
			size_t this_len;
			unsigned char* this_len_pt = BroadcastMap["sigma_share"][i][2].data();
			copy(this_len_pt, this_len_pt + sizeof(size_t), (unsigned char*) &this_len);

			bool res = open_commitment(BroadcastMap["sigma_share_commitment"][i][0].data(), BroadcastMap["sigma_share"][i][1].data(), BroadcastMap["sigma_share"][i][0].data(), this_len);
			if(!res){
				cout << "Error: commitment does not match for party " << i << endl;
			}
		}
	}

	mpz_class sigma(0);
	for(int i = 0; i < num_party; i++) {
		if(i == party_id) {
			sigma += sigma_share;
		} else {
			size_t this_len;
			unsigned char* this_len_pt = BroadcastMap["sigma_share"][i][2].data();
			copy(this_len_pt, this_len_pt + sizeof(size_t), (unsigned char*) &this_len);

			mpz_t this_sigma_share;
			mpz_init(this_sigma_share);

			mpz_import(this_sigma_share, this_len, 1, 1, 0, 0, BroadcastMap["sigma_share"][i][0].data());

			mpz_class this_sigma_share_value(this_sigma_share);

			sigma += this_sigma_share_value;
			mpz_clear(this_sigma_share);
		}
	}

	if(sigma != 0) {
		cout << "Error: Output MAC checking is not successful" << endl;
	}

	return final_opened_vec[0];
}
/*
int main(int argc, char** argv){
	map<int, string> party_ip_list;
	party_ip_list[0] = "172.31.1.137";
	party_ip_list[1] = "172.31.15.170";
	party_ip_list[2] = "172.31.12.22";
	party_ip_list[3] = "172.31.13.61";
	party_ip_list[4] = "172.31.7.45";

	if(argc < 2) {
		cout << "FORMAT: ./online [Party ID]" << endl;
		exit(1);
	}

	int party_id;
	sscanf(argv[1], "%d", &party_id);

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

	cout << "input_x: " << input_x << endl;
	cout << "input_y: " << input_y << endl;

	mpz_class res = main_routine(party_ip_list, 5, party_id, input_x, input_y, alpha_share);

	cout << "Result: " << res << endl;

	// expect res: 108677532895904936863904823330600106055145041255062888673713681538132314135903

	return 0;
}

*/


void PerformECAddition();
#endif

#ifdef __cplusplus
extern "C" {
#endif	

	void ec_add_external();

#ifdef __cplusplus
}
#endif // end extern "C"

#endif // EC_ADD_EXTERNAL
