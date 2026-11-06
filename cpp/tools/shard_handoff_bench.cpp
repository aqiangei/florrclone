// Synthetic native-kernel benchmark. Not a GameServer/network transfer test.
#include "server/shard_authority.h"
#include "server/shard_store.h"
#include "server/shard_journal.h"
#include <memory>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
using namespace flix;
using namespace flix::shards;
int main(int argc,char** argv) {
    const bool deltaMode=argc==2&&std::string(argv[1])=="--journal";
    char directory[]="/tmp/shards-handoff-bench-XXXXXX";
    if(!::mkdtemp(directory))return 1;
    const std::string path=std::string(directory)+"/authority.json";
    Json records=Json::object();
    for(int n=0;n<3500;++n) {
        Json record=Json::object();record["totalXP"]=10000+n;
        for(const auto* rarity:{"common","unusual","rare","epic","legendary","mythic"})
            for(int k=0;k<20;++k)record["inventory"][rarity]["petal_bench"+std::to_string(k)]=k+1;
        records["bench"+std::to_string(n)]=std::move(record);
    }
    auto initial=Authority::initial(records);
    std::printf("syntheticAccounts=3500 snapshotBytes=%zu mode=%s scope=native-coordinator-only-no-WASM-no-network\n",initial.dump().size(),deltaMode?"journal":"snapshot");
    {
        FileStore store(path);if(!store.owned())return 2;
        JournalStore journal(path);Json recovered;std::string error;
        std::unique_ptr<Authority> authority;
        if(deltaMode) {
            if(!journal.open(initial,recovered,error))return 4;
            authority=std::make_unique<Authority>(recovered,Authority::DeltaJournal{},[&](const Json& next){return journal.append(next);});
        } else authority=std::make_unique<Authority>(initial,[&](const Json& next){return store.commit(next);});
        int request=0;
        auto run=[&](const Node& node,Json input) {
            input["requestId"]="bench"+std::to_string(++request);
            input["account"]="bench0";
            const auto start=std::chrono::steady_clock::now();
            auto reply=authority->apply(node,input);
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            std::printf("op=%s elapsedMs=%.3f ok=%s\n",input["op"].asString().c_str(),ms,reply["ok"].asBool()?"true":"false");
            if(!reply["ok"].asBool())std::exit(3);
            return reply;
        };
        const Node one{"one","boot-one"},two{"two","boot-two"};
        for(int n=0;n<3;++n) {
            Json req=Json::object();req["op"]="acquire";auto lease=run(one,req);
            req=Json::object();req["op"]="prepare";req["epoch"]=lease["epoch"];req["revision"]=lease["revision"];
            req["record"]=records["bench0"];req["target"]="two";req["transfer"]="transfer"+std::to_string(n);run(one,req);
            req=Json::object();req["op"]="claim";req["transfer"]="transfer"+std::to_string(n);lease=run(two,req);
            req=Json::object();req["op"]="release";req["epoch"]=lease["epoch"];req["revision"]=lease["revision"];
            req["record"]=records["bench0"];run(two,req);
        }
    }
    std::remove(path.c_str());std::remove((path+".lock").c_str());
    std::remove((path+".snapshot").c_str());std::remove((path+".snapshot.lock").c_str());
    std::remove((path+".journal").c_str());::rmdir(directory);
}
