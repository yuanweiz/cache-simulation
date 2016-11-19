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

using namespace std;
//access state:
#define NA 0 // no action
#define RH 1 // read hit
#define RM 2 // read miss
#define WH 3 // Write hit
#define WM 4 // write miss




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
class L1{
    L1(int blocksz,int setsize,int sz);
    void read(const bitset<32> & addr_){
        unsigned int tag, idx,offset;
        auto addr = addr_.to_ulong();
        decode (addr_,blocksz,associativity,cachesize,&tag,&idx,&offset);
        auto & set = cache[idx];
        
        assert(set.nOccupied <=set.size());
        for (int i=0;i<set.nOccupied;++i){
            if (set[i] == tag ){
                //read hit!
                onReadHit(addr);
                return ;
            }
        }
        //read miss
        if (set.nOccupied == set.size()){
            //eviction
            auto evict_idx = set.evict ++ ; //round robin
            auto evicted = set[evict_idx];

            //if the block is dirty
            set[set.evict]=tag;
            if (set.dirty_[evict_idx]){
                next->write(evicted); //FIXME: get the addr
                set.dirty_[evict_idx]=false; //clean
            }
            
        }
        else {
            set[set.nOccupied++]=tag;
        }
    }
    void write(){
        unsigned int tag, idx,offset;
        auto addr = addr_.to_ulong();
        decode (addr_,blocksz,associativity,cachesize,&tag,&idx,&offset);
        auto & set = cache[idx];

        for (int i=0;i<set.nOccupied; ++i){
            if (set.tags[i] == tag){
                //write hit!
                set.dirty_[i]= true;
                return ;
            }
        }
        next->write(addr);
    }
    private:
    void onWriteMiss(unsigned int);
    void onWriteHit(unsigned int);
    void onReadMiss(unsigned int);
    void onReadHit(unsigned int);
    void onNoAction(unsigned int);

    L1* next;
    class Set {
        public:
    //starts at 0, when idx == ways.size()
    int nOccupied;
    unsigned int & operator [](int i){return tags[i];}
    unsigned int size(){return tags.size();}
    bool dirty(int i){return dirty_[i];}
    vector<unsigned int> tags;
    vector <bool> dirty_;
    };
    int idx; 
    //the set will be full;
    //starts at 0, indicates the block to be evicted
    int evict; 

    vector<Set > cache;
    unsigned int blocksz,cachesize,associativity;
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
   
   
   
   
  int L1AcceState =0; // L1 access state variable, can be one of NA, RH, RM, WH, WM;
  int L2AcceState =0; // L2 access state variable, can be one of NA, RH, RM, WH, WM;
   
   
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
    unsigned int tag,idx,off;
    decode(bitset<32>(0xFFFFFFFF), 8,4,32768,&tag,&idx,&off);
    printf("%x %x %x",tag,idx,off);
    return 0;
}
