/*
Cache Simulator
Level one L1 and level two L2 cache parameters are read from file (block size, line per set and set per cache).
The 32 bit address is divided into tag bits (t), set index bits (s) and block offset bits (b)
s = log2(#sets)   b = log2(block size)  t=32-s-b
*/
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <iomanip>
#include <stdlib.h>
#include <cmath>
#include <bitset>
#include <assert.h>
#include <functional>

#ifdef _DEBUG
#include <gtest/gtest.h>
#endif
using namespace std;
//access state:
#define NA 0 // no action
#define RH 1 // read hit
#define RM 2 // read miss
#define WH 3 // Write hit
#define WM 4 // write miss

const int kWriteHit = 1;
const int kWriteMiss = 2;

const int kReadHit = 6;
const int kReadMissEvictClean = 7;
const int kReadMissEvictDirty = 8;
const int kReadMissNoEvict = 9;

//for debug use
int counter = 0;

struct config {
	int L1blocksize;
	int L1setsize;
	int L1size;
	int L2blocksize;
	int L2setsize;
	int L2size;
};
using std::placeholders::_1;
void setter(int*target, int value, int /*dummy*/) { *target = value; }

void decode(const bitset<32>&addr_, int blocksz, int associativity, int cachesize,
	unsigned int * tag, unsigned int *idx, unsigned int * offset)
{
	auto addr = addr_.to_ulong();
	int setcnt = cachesize / associativity / blocksz;
	*offset = (addr & (blocksz - 1));
	*idx = (addr / blocksz) & (setcnt - 1);
	*tag = (addr / blocksz / setcnt);
}
unsigned int encode(int blocksz, int associativity, int cachesize,
	unsigned int tag, unsigned int idx, unsigned int offset) {
	int setcnt = cachesize / associativity / blocksz;
	return tag * blocksz * setcnt + idx * blocksz + offset;
}
class Cache {
public:
	typedef function<void(int)> Callback;
	bool dummy;
	Cache(int _blocksz, int _setsize, int _sz)
		:blocksz(_blocksz),
		associativity(_setsize ==0 ? cachesize/blocksz: _setsize),
		cachesize(_sz),
		dummy(false)
	{
		//conner case
		int setcnt = cachesize / associativity / blocksz;
		sets.reserve(setcnt);
		for (int i = 0;i < setcnt;++i) {
			sets.push_back(Set(associativity));
		}
	}

	//notice that one single eviction in L1 can
	//cause multiple write back to L2,
	// and vice versa. this is because the
	// block size and associativity may differ
	//in different hierarchy,

	//I use auxillary methods like readRange
	//and writeRange to handle this

	void read(unsigned int addr) {
		if (dummy)
			return;
		unsigned int tag, idx, offset, evicted_tag;
		decode(addr, blocksz, associativity, cachesize, &tag, &idx, &offset);
		unsigned int i = addr - offset;
		//offset result is discarded
		auto & set = sets[idx];
		auto res = set.read(tag, &evicted_tag);
		if (res == kReadHit) {
			onReadHit(addr);
			next->onNoAction(addr);
		}
		else if (res == kReadMissEvictDirty) {
			//write back policy
			onReadMiss(addr);
			onEviction(addr);
			unsigned writef = encode(blocksz, associativity, cachesize, evicted_tag, idx, 0);
			next->writeRange(writef, writef + blocksz);
			next->readRange(i, i + blocksz);
		}
		else if (res == kReadMissEvictClean) {
			onReadMiss(addr);
			onEviction(addr);
			next->readRange(i, i + blocksz);
		}
		else if (res == kReadMissNoEvict) {
			onReadMiss(addr);
			next->readRange(i, i + blocksz);
		}
		else {
			assert(false); //should not reach here
		}
		//readRange(addr, addr + 1); //FIXME: potential overflow bug
	}
	void write(unsigned int addr) {
		if (dummy)return;
		unsigned int tag, idx, offset;
		decode(addr, blocksz, associativity, cachesize, &tag, &idx, &offset);
		unsigned int base = addr - offset;
		auto & set = sets[idx];
		int res = set.write(tag);
		if (res == kWriteHit) {
			onWriteHit(addr);
			next->onNoAction(addr);
		}
		else if (res == kWriteMiss) {
			//no-allocate policy
			onWriteMiss(addr);
			next->writeRange(base, base + blocksz);
		}
		else {
			assert(false); //should not reach here
		}
	}

	void readRange(unsigned int f, unsigned int t) {
		//it's possible that [f,t) is smaller/bigger than one
		//block size
		for (unsigned int i = f;i < t;i += blocksz) {
			read(i);
		}
	}

	void writeRange(unsigned int f, unsigned int t) {
		for (unsigned int i = f;i < t;i += blocksz) {
			write(i);
		}
	}

	void setNextLevel(Cache* n) {
		next = n;
	}
	void setWriteMissCallback(const Callback& cb) {
		onWriteMiss_ = cb;
	}
	void setWriteHitCallback(const Callback& cb) {
		onWriteHit_ = cb;
	}
	void setReadMissCallback(const Callback& cb) {
		onReadMiss_ = cb;
	}
	void setReadHitCallback(const Callback& cb) {
		onReadHit_ = cb;
	}
	void setNoActionCallback(const Callback& cb) {
		onNoAction_ = cb;
	}
	void setEvictionCallback(const Callback& cb) {
		onEviction_ = cb;
	}
private:
	void onWriteMiss(int val) {
		if (onWriteMiss_)
			onWriteMiss_( val);
	}
	void onWriteHit(int val) {
		if (onWriteHit_)
			onWriteHit_(val);
	}
	void onReadMiss(int val) {
		if (onReadMiss_)
			onReadMiss_(val);
	}
	void onReadHit(int val) {
		if (onReadHit_)
			onReadHit_(val);
	}
	void onNoAction(int val) {
		if (onNoAction_)
			onNoAction_(val);
	}
	void onEviction(int val) {
		if (onEviction_)
			onEviction_(val);
	}
	Callback onWriteMiss_, onWriteHit_, onReadMiss_, onReadHit_, onNoAction_, onEviction_;
	Cache* next;
	class Set {
	public:
		//starts at 0, when idx == ways.size()
		Set(int M)
			:tags(M), dirty_(M), nOccupied(0), evict(0)
		{
		}
		unsigned int nOccupied;
		unsigned int evict;
		unsigned int & operator [](int i) { return tags[i]; }
		unsigned int size() { return tags.size(); }
		vector<unsigned int> tags;
		vector <bool> dirty_;
		//write is much easier, we don't need one addtional 
		//argument to indicate the evicted block
		int write(unsigned int tag) {
			for (unsigned int i = 0;i < nOccupied;++i) {
				if (tags[i] == tag) {
					//write hit!
					dirty_[i] = true;
					return kWriteHit;
				}
			}
			return kWriteMiss;
		}

		//read is a bit complicated, there are several different situations:
		// - read hit
		// - read miss (compulsory miss)
		// - read miss, and the evicted block is clean,
		// - read miss, and the evicted block is dirty. 
		//		(next level need both read and write back)
		int read(unsigned int tag, unsigned int * evicted_tag) {
			assert(nOccupied <= size());
			for (unsigned int i = 0;i < nOccupied;++i) {
				if (tags[i] == tag) {
					//read hit!
					return kReadHit;
				}
			}
			//read miss
			if (nOccupied == size()) {
				//eviction happens
				auto evict_idx = evict++; //round robin
				evict = evict % size();
				*evicted_tag = tags[evict_idx];

				//if the block is dirty, write back
				tags[evict_idx] = tag;
				if (dirty_[evict_idx]) {
					dirty_[evict_idx] = false; //clean
					return kReadMissEvictDirty;
				}
				else {
					return kReadMissEvictClean;
				}
			}
			else {
				//still room for storing block
				tags[nOccupied++] = tag;
				return kReadMissNoEvict;
			}

		}
	};
	vector<Set> sets;
	const unsigned int blocksz, cachesize, associativity;
};
/* you can define the cache class here, or design your own data structure for L1 and L2 cache
class cache {

	  }
*/
#ifdef _DEBUG_
struct Test : public testing::Test {
	Test()
		:L1(8, 2, 32), L2(16, 4, 128), mainMemory(2, 2, 2)
	{
		mainMemory.dummy = true; //IO callbacks does nothing
		L1.setNextLevel(&L2);
		L2.setNextLevel(&mainMemory);

		L1.setReadHitCallback([&]() {opL1.push_back(RH);});
		L1.setWriteHitCallback([&]() {opL1.push_back(WH);});
		L1.setReadMissCallback([&]() {opL1.push_back(RM);});
		L1.setWriteMissCallback([&]() {opL1.push_back(WM);});
		L1.setNoActionCallback([&]() {opL1.push_back(NA);});

		L2.setReadHitCallback([&]() {opL2.push_back(RH);});
		L2.setWriteHitCallback([&]() {opL2.push_back(WH);});
		L2.setReadMissCallback([&]() {opL2.push_back(RM);});
		L2.setWriteMissCallback([&]() {opL2.push_back(WM);});
		L2.setNoActionCallback([&]() {opL2.push_back(NA);});
	}
	Cache L1, L2, mainMemory;
	vector<int> opL1, opL2;
};

TEST_F(Test, RoundRobin) {
	opL1.clear();
	opL2.clear();
	int seq[] = { 0,16,32,0,48,32,0 };
	for (auto i : seq) {
		L1.read(i);
		//opL1.push_back(3);
	}
	vector<int>expected{ RM, RM, RM, RM, RM, RM, RM };
	EXPECT_EQ(opL1, expected);

	vector<int>expected2{ RM, RM,RM,RH,RM,RH,RH };
	EXPECT_EQ(opL2, expected2);
	//EXPECT_TRUE(true);
}

TEST_F(Test, BlockSize) {
	opL1.clear();
	opL2.clear();
	int seq[] = { 0,8,16,24 };
	int seq2[] = { 32,40,48,56 };
	for (auto i : seq) {
		L1.read(i);
	}
	for (auto i : seq) {
		L1.write(i);
	}
	for (auto i : seq2) {
		L1.read(i);
	}
	for (auto i : seq2) {
		L1.write(i);
	}
	vector<int>expected1 =
	{ RM,RM,RM,RM,WH,WH,WH,WH,
	 RM,RM,RM,RM,WH,WH,WH,WH };
	vector<int>expected2 =
	{ RM,RH,RM,RH,NA,NA,NA,NA,
	 WH,RM,WH,RH,WH,RM,WH,RH,
	NA,NA,NA,NA };
	EXPECT_EQ(opL1, expected1);
	EXPECT_EQ(opL2, expected2);
}


int main(int argc, char*argv[]) {
	testing::InitGoogleTest(&argc, argv);
	auto res = RUN_ALL_TESTS();
	system("pause");
	return res;
}
#else
int main(int, char* argv[]) {
	config cacheconfig;
	ifstream cache_params;
	string dummyLine;
	cache_params.open(argv[1]);
	while (!cache_params.eof())  // read config file
	{
		cache_params >> dummyLine;
		cache_params >> cacheconfig.L1blocksize;
		cache_params >> cacheconfig.L1setsize;
		cache_params >> cacheconfig.L1size;
		cache_params >> dummyLine;
		cache_params >> cacheconfig.L2blocksize;
		cache_params >> cacheconfig.L2setsize;
		cache_params >> cacheconfig.L2size;
	}



	// Implement by you: 
	// initialize the hirearch cache system with those configs
	// probably you may define a Cache class for L1 and L2, or any data structure you like
	Cache L1(cacheconfig.L1blocksize, cacheconfig.L1setsize, cacheconfig.L1size*1024);
	Cache L2(cacheconfig.L2blocksize, cacheconfig.L2setsize, cacheconfig.L2size*1024);
	Cache mainMemory(2, 2, 16); //dummy, the arguments are useless
	mainMemory.dummy = true;
	L1.setNextLevel(&L2);
	L2.setNextLevel(&mainMemory);

	int L1AcceState = 0; // L1 access state variable, can be one of NA, RH, RM, WH, WM;
	int L2AcceState = 0; // L2 access state variable, can be one of NA, RH, RM, WH, WM;

	//register callbacks
	L1.setReadHitCallback(bind(&setter, &L1AcceState, RH, _1));
	L1.setWriteHitCallback(bind(&setter, &L1AcceState, WH, _1));
	L1.setReadMissCallback(bind(&setter, &L1AcceState, RM, _1));
	L1.setWriteMissCallback(bind(&setter, &L1AcceState, WM, _1));
	L1.setNoActionCallback(bind(&setter, &L1AcceState, NA, _1));

	L2.setReadHitCallback(bind(&setter, &L2AcceState, RH, _1));
	L2.setWriteHitCallback(bind(&setter, &L2AcceState, WH, _1));
	L2.setReadMissCallback(bind(&setter, &L2AcceState, RM, _1));
	L2.setWriteMissCallback(bind(&setter, &L2AcceState, WM, _1));
	L2.setNoActionCallback(bind(&setter, &L2AcceState, NA, _1));

	auto onEvict  = [&](unsigned int addr) {
		//int tag, idx, offset;
		//decode(addr, cacheconfig.L2blocksize, cacheconfig.L2setsize, 
		//	cacheconfig.L2size * 1024,&tag,&idx,&offset);
		//if (tag == )
		if (addr & 0xfffffff0 == 0xb77af830) {
			printf("%d, %x", counter, addr);
		}
	};
	L1.setEvictionCallback(onEvict);
	L2.setEvictionCallback(onEvict);

	ifstream traces;
	ofstream tracesout;
	string outname;
	outname = string(argv[2]) + ".out";

	traces.open(argv[2]);
	tracesout.open(outname.c_str());

	string line;
	string accesstype;  // the Read/Write access type from the memory trace;
	string xaddr;       // the address from the memory trace store in hex;
	unsigned int addr;  // the address from the memory trace store in unsigned int;        
	bitset<32> accessaddr; // the address from the memory trace store in the bitset;


	if (traces.is_open() && tracesout.is_open()) {
		while (getline(traces, line)) {   // read mem access file and access Cache
			counter++;
			istringstream iss(line);
			if (!(iss >> accesstype >> xaddr)) { break; }
			stringstream saddr(xaddr);
			saddr >> std::hex >> addr;
			accessaddr = bitset<32>(addr);


			// access the L1 and L2 Cache according to the trace;
			 //no need to access L2 here, the callbacks
			 //will handle it
			if (accesstype.compare("R") == 0)

			{
				//Implement by you:
				// read access to the L1 Cache, 
				//  and then L2 (if required), 
				//  update the L1 and L2 access state variable;
				L1.read(addr);

			}
			else
			{
				//Implement by you:
			   // write access to the L1 Cache, 
			   //and then L2 (if required), 
			   //update the L1 and L2 access state variable;
				L1.write(addr);
			}
			tracesout << L1AcceState << " " << L2AcceState << endl;  // Output hit/miss results for L1 and L2 to the output file;
		}
		traces.close();
		tracesout.close();
	}
	else cout << "Unable to open trace or traceout file ";
	return 0;
}
#endif
