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

struct config{
       int L1blocksize;
       int L1setsize;
       int L1size;
       int L2blocksize;
       int L2setsize;
       int L2size;
       };
int log2 (unsigned int v){
    int cnt=0;
    while (0!=(v&0x1)){
        cnt++; v>>=1;
    }
    return cnt;
}
void decode (const bitset<32>&addr_, int blocksz, int associativity,int cachesize,
        unsigned int * tag,unsigned int *idx, unsigned int * offset)
{
    auto addr = addr_.to_ulong();
    int setcnt = cachesize / associativity / blocksz ;
    *offset = (addr & (blocksz-1));
    *idx = (addr / blocksz ) & (setcnt-1);
    *tag = (addr / blocksz / setcnt ) ;
}
unsigned int encode(int blocksz, int associativity, int cachesize,
	unsigned int tag, unsigned int idx, unsigned int offset) {
    int setcnt = cachesize / associativity / blocksz ;
	return tag * blocksz * setcnt + idx * blocksz + offset;
}
class Cache{
public:
	typedef function<void(void)> Callback;
	bool dummy;
	Cache(int _blocksz, int _setsize, int _sz) 
		:blocksz(_blocksz),
		associativity(_setsize),
		cachesize(_sz),
		dummy(false)
	{
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

    void read(const bitset<32> & addr_){
        auto addr = addr_.to_ulong();
		readRange(addr, addr + 1); //FIXME: potential overflow bug
    }
    void write( const bitset<32> &addr_){
        auto addr = addr_.to_ulong();
		writeRange(addr, addr + 1);
    }

	void readRange(unsigned int f, unsigned int t) {
		if (dummy)
			return;
		for (unsigned int i = f;i < t;i += blocksz) {
			unsigned int tag, idx, offset, evicted_tag;
			decode(f, blocksz, associativity, cachesize, &tag, &idx, &offset);
			//offset result is discarded
			auto & set = sets[idx];
			auto res = set.read(tag, &evicted_tag);
			if (res == kReadHit) {
				onReadHit();
				next->onNoAction();
			}
			else if (res == kReadMissEvictDirty) {
				//write back policy
				onReadMiss();
				int writef = encode(blocksz, associativity, cachesize, evicted_tag, idx, 0);
				next->writeRange(writef, writef + blocksz);
				next->readRange(i, i + blocksz);
			}
			else if (res == kReadMissEvictClean) {
				onReadMiss();
				next->readRange(i, i + blocksz);
			}
			else if (res == kReadMissNoEvict) {
				onReadMiss();
				next->onNoAction();
			}
			else {
				assert(false); //should not reach here
			}
		}
	}

	void writeRange(unsigned int f, unsigned int t) {
		if (dummy)return;
        unsigned int tag, idx,offset;
		for (unsigned int i = f;i < t;i += blocksz) {
			decode(f, blocksz, associativity, cachesize, &tag, &idx, &offset);
			auto & set = sets[idx];
			int res = set.write(tag);
			if (res == kWriteHit) {
				onWriteHit();
				next->onNoAction();
			}
			else if (res == kWriteMiss) {
				//no-allocate policy
				onWriteMiss();
				next->writeRange(i, i + blocksz);
			}
			else {
				assert(false); //should not reach here
			}
		}

	}
	
		void setNextLevel(Cache* n) {
			next = n;
		}
		void setWriteMissCallback(const Callback& cb) {
			onWriteMiss = cb;
		}
		void setWriteHitCallback(const Callback& cb) {
			onWriteHit = cb;
		}
		void setReadMissCallback(const Callback& cb) {
			onReadMiss = cb;
		}
		void setReadHitCallback(const Callback& cb) {
			onReadHit = cb;
		}
		void setNoActionCallback(const Callback& cb) {
			onNoAction = cb;
		}
    private:
		Callback onWriteMiss, onWriteHit, onReadMiss, onReadHit, onNoAction;
		//void onWriteMiss() {}
		//void onWriteHit() {}
		//void onReadMiss() {}
		//void onReadHit() {}
		//void onNoAction() {}

    Cache* next;
	class Set {
	public:
		//starts at 0, when idx == ways.size()
		Set(int M) 
			:tags(M),dirty_(M),nOccupied(0),evict(0)
		{
		}
		int nOccupied;
		int evict;
		int idx;
		unsigned int & operator [](int i) { return tags[i]; }
		unsigned int size() { return tags.size(); }
		bool dirty(int i) { return dirty_[i]; }
		vector<unsigned int> tags;
		vector <bool> dirty_;
		//write is much easier, we don't need one addtional 
		//argument to indicate the evicted block
		int write(unsigned int tag) {
			for (int i = 0;i < nOccupied;++i) {
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
			for (int i = 0;i < nOccupied;++i) {
				if (tags[i] == tag) {
					//read hit!
					return kReadHit;
				}
			}
			//read miss
			if (nOccupied == size()) {
				//eviction happens
				auto evict_idx = evict++; //round robin
				auto evicted = tags[evict_idx];

				//if the block is dirty, write back
				tags[evict] = tag;
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
    const unsigned int blocksz,cachesize,associativity;
};
/* you can define the cache class here, or design your own data structure for L1 and L2 cache
class cache {
      
      }
*/       

int main(int , char* argv[]){
    if (false){
    config cacheconfig;
    ifstream cache_params;
    string dummyLine;
    cache_params.open(argv[1]);
    while(!cache_params.eof())  // read config file
    {
      cache_params>>dummyLine;
      cache_params>>cacheconfig.L1blocksize;
      cache_params>>cacheconfig.L1setsize;              
      cache_params>>cacheconfig.L1size;
      cache_params>>dummyLine;              
      cache_params>>cacheconfig.L2blocksize;           
      cache_params>>cacheconfig.L2setsize;        
      cache_params>>cacheconfig.L2size;
      }
    
  
  
   // Implement by you: 
   // initialize the hirearch cache system with those configs
   // probably you may define a Cache class for L1 and L2, or any data structure you like
   
   
   
   
	int L1AcceState = 0; // L1 access state variable, can be one of NA, RH, RM, WH, WM;
	int L2AcceState = 0; // L2 access state variable, can be one of NA, RH, RM, WH, WM;
   
   
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
    
    if (traces.is_open()&&tracesout.is_open()){    
        while (getline (traces,line)){   // read mem access file and access Cache
            
            istringstream iss(line); 
            if (!(iss >> accesstype >> xaddr)) {break;}
            stringstream saddr(xaddr);
            saddr >> std::hex >> addr;
            accessaddr = bitset<32> (addr);
           
           
           // access the L1 and L2 Cache according to the trace;
              if (accesstype.compare("R")==0)
              
             {    
                 //Implement by you:
                 // read access to the L1 Cache, 
                 //  and then L2 (if required), 
                 //  update the L1 and L2 access state variable;
                 
                 
                 
                 
                 
                 
                 
                 }
             else 
             {    
                   //Implement by you:
                  // write access to the L1 Cache, 
                  //and then L2 (if required), 
                  //update the L1 and L2 access state variable;
                  
                  
                  
                  
                  
                  
                  }
              
              
             
            tracesout<< L1AcceState << " " << L2AcceState << endl;  // Output hit/miss results for L1 and L2 to the output file;
             
             
        }
        traces.close();
        tracesout.close(); 
    }
    else cout<< "Unable to open trace or traceout file ";
    }

	Cache L1(8,2,32); //8 byte block, 2-way association, 32 byte volume
	Cache L2(16, 4, 128);
	Cache mainMemory(2, 2, 2); //dummy object
	mainMemory.dummy = true; //IO callbacks does nothing

	L1.setNextLevel(&L2);
	L1.setReadHitCallback([]() {puts("L1 RH");});
	L1.setWriteHitCallback([]() {puts("L1 WH");});
	L1.setReadMissCallback([]() {puts("L1 RM");});
	L1.setWriteMissCallback([]() {puts("L1 WM");});
	L1.setNoActionCallback([]() {puts("L1 NA");});

	L2.setNextLevel(&mainMemory);
	L2.setReadHitCallback([]() {puts("L2 RH");});
	L2.setWriteHitCallback([]() {puts("L2 WH");});
	L2.setReadMissCallback([]() {puts("L2 RM");});
	L2.setWriteMissCallback([]() {puts("L2 WM");});
	L2.setNoActionCallback([]() {puts("L2 NA");});

	L1.read(0);
	L1.read(0);

	system("pause");
    return 0;
}
