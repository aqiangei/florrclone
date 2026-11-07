#pragma once
#include "server/shard_store.h"
#include <fstream>
#include <limits>

namespace flix::shards {
// Single-writer append journal. Snapshot lock also protects its journal.
// Checkpoint first, then truncate its covered journal. Recovery may see either
// the old or the new snapshot and the old journal after a crash.
class JournalStore {
public:
    explicit JournalStore(const std::string& base):snapshot_(base+".snapshot"),path_(base+".journal"){}
    ~JournalStore() {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
        if(fd_>=0)::close(fd_);
#endif
    }
    bool open(const Json& bootstrap,Json& recovered,std::string& error) {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
        if(fd_>=0||!snapshot_.owned()){error="journal already open or locked";return false;}
        const auto base=path_.substr(0,path_.size()-8);
        struct stat st{};
        const bool journalExists=::stat(path_.c_str(),&st)==0;
        if(::stat((base+".snapshot").c_str(),&st)==0) {
            if(!snapshot_.load(recovered,error))return false;
        } else {
            if(journalExists||!Authority::validState(bootstrap)){error="missing snapshot or invalid bootstrap";return false;}
            if(snapshot_.commit(bootstrap)!=Durability::Committed){error="snapshot durability failed";return false;}
            recovered=bootstrap;
        }
        fd_=::open(path_.c_str(),O_RDWR|O_CREAT|O_APPEND|O_CLOEXEC|O_NOFOLLOW,0600);
        if(fd_<0){error="journal open failed";return false;}
        const auto slash=path_.find_last_of('/');
        const auto parent=slash==std::string::npos?".":path_.substr(0,slash);
        const int dir=::open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        if(dir<0){error="journal parent open failed";return false;}
        const bool synced=::fsync(fd_)==0&&::fsync(dir)==0;::close(dir);
        if(!synced){error="journal creation durability failed";return false;}
        const double checkpointSequence=recovered["sequence"].asDouble();
        std::ifstream input(path_,std::ios::binary);std::string line;off_t confirmed=0;
        double previousRecord=-1;records_=0;
        while(std::getline(input,line)) {
            if(input.eof()) {
                // Only a final unterminated append may be discarded. A complete
                // line with a bad checksum/schema is corruption and fails closed.
                if(::ftruncate(fd_,confirmed)!=0||::fsync(fd_)!=0){error="partial append recovery failed";return false;}
                break;
            }
            if(line.size()>16u*1024u*1024u){error="journal record too large";return false;}
            const auto envelope=Json::parseOrNull(line);
            const auto payload=envelope["payload"].asString();
            const auto delta=Json::parseOrNull(payload);
            if(!envelope["payload"].isString()||envelope["sha256"].asString()!=crypto::sha256Hex(payload)||
               (delta["schema"].asDouble()!=1&&delta["schema"].asDouble()!=2)||!counter(delta["sequence"])||
               (previousRecord>=0&&delta["sequence"].asDouble()!=previousRecord+1)){
                error="journal checksum or sequence invalid";return false;
            }
            previousRecord=delta["sequence"].asDouble();
            // Only entries covered by the already validated durable snapshot
            // may be skipped; newer entries still require strict delta replay.
            if(previousRecord>checkpointSequence){
                if(!Authority::recoverDelta(recovered,delta)){error="journal delta invalid";return false;}
                ++records_;
            }
            confirmed+=static_cast<off_t>(line.size()+1);
        }
        if(input.bad()){error="journal read failed";return false;}
        sequence_=recovered["sequence"].asDouble();bytes_=static_cast<std::uint64_t>(confirmed);
        ready_=true;return true;
#else
        (void)bootstrap;(void)recovered;error="native Linux journal required";return false;
#endif
    }
    Durability append(const Json& delta) {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
        if(!ready_||!counter(delta["sequence"])||delta["sequence"].asDouble()!=sequence_+1)return Durability::NotCommitted;
        const auto payload=delta.dump();Json envelope=Json::object();
        envelope["payload"]=payload;envelope["sha256"]=crypto::sha256Hex(payload);
        const auto bytes=envelope.dump()+"\n";
        if(bytes.size()>16u*1024u*1024u)return Durability::NotCommitted;
        std::size_t at=0;
        while(at<bytes.size()) {
            const auto count=::write(fd_,bytes.data()+at,bytes.size()-at);
            if(count<0&&errno==EINTR)continue;
            if(count<=0){ready_=false;return at?Durability::Unknown:Durability::NotCommitted;}
            at+=static_cast<std::size_t>(count);
        }
        if(::fsync(fd_)!=0){ready_=false;return Durability::Unknown;}
        sequence_=delta["sequence"].asDouble();bytes_+=bytes.size();++records_;
        return Durability::Committed;
#else
        (void)delta;return Durability::NotCommitted;
#endif
    }
    bool compactDue() const {return ready_&&(records_>=2048||bytes_>=64u*1024u*1024u);}
    // The caller must pass the complete authority state after its latest
    // durable append, while serialized with all other requests. Observer is
    // used by crash-injection tests; normal callers omit it.
    Durability checkpoint(const Json& state,const std::function<void(const char*)>& observer={}){
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
        if(!ready_||!Authority::validState(state)||state["sequence"].asDouble()!=sequence_)return Durability::NotCommitted;
        if(observer)observer("before_snapshot");
        const auto result=snapshot_.commit(state);
        if(result!=Durability::Committed){if(result==Durability::Unknown)ready_=false;return result;}
        if(observer)observer("after_snapshot");
        // At this point the snapshot has reached both file and directory fsync.
        // Old journal entries can be removed without discarding any commit.
        if(::ftruncate(fd_,0)!=0){ready_=false;return Durability::Unknown;}
        if(observer)observer("after_truncate");
        if(::fsync(fd_)!=0){ready_=false;return Durability::Unknown;}
        bytes_=0;records_=0;return Durability::Committed;
#else
        (void)state;(void)observer;return Durability::NotCommitted;
#endif
    }
private:
    FileStore snapshot_;
    std::string path_;
    int fd_=-1;
    bool ready_=false;
    double sequence_=0;
    std::uint64_t bytes_=0,records_=0;
};
} // namespace flix::shards
