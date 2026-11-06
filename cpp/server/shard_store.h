#pragma once
#include "server/shard_authority.h"
#include <cerrno>
#include <cstdio>
#include <string>
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace flix::shards {
// Native prototype store. Node/WASM production needs an independently validated
// host-filesystem adapter; MEMFS fsync is not a durability guarantee.
class FileStore {
public:
    explicit FileStore(std::string path):path_(std::move(path)) {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
        const auto slash=path_.find_last_of('/');parent_=slash==std::string::npos?".":slash==0?"/":path_.substr(0,slash);
        lock_=::open((path_+".lock").c_str(),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
        if(lock_>=0&&::flock(lock_,LOCK_EX|LOCK_NB)==0)owned_=true;
        else if(lock_>=0){::close(lock_);lock_=-1;}
#endif
    }
    ~FileStore(){
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
        if(lock_>=0)::close(lock_);
#endif
    }
    FileStore(const FileStore&)=delete;FileStore& operator=(const FileStore&)=delete;
    bool owned() const {return owned_;}
    bool load(Json& state,std::string& error) const {
        if(!owned_){error="authority file locked or unsupported platform";return false;}
        if(!Json::parseFile(path_,state,error))return false;
        if(!Authority::validState(state)){error="invalid authority schema";return false;}
        return true;
    }
    Durability commit(const Json& state) {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
        if(!owned_||!Authority::validState(state))return Durability::NotCommitted;
        const auto bytes=state.dump();
        if(bytes.size()>256u*1024u*1024u)return Durability::NotCommitted;
        const int directory=::open(parent_.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        if(directory<0)return Durability::NotCommitted;
        const auto temporary=path_+".tmp";
        const int fd=::open(temporary.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);
        if(fd<0){::close(directory);return Durability::NotCommitted;}
        std::size_t written=0;bool ok=true;
        while(written<bytes.size()) {
            const auto count=::write(fd,bytes.data()+written,bytes.size()-written);
            if(count<0&&errno==EINTR)continue;
            if(count<=0){ok=false;break;}written+=static_cast<std::size_t>(count);
        }
        struct stat st{};ok=ok&&::fstat(fd,&st)==0&&static_cast<std::size_t>(st.st_size)==bytes.size();
        if(ok)ok=::fsync(fd)==0;
        if(::close(fd)!=0)ok=false;
        if(!ok){::unlink(temporary.c_str());::close(directory);return Durability::NotCommitted;}
        if(::rename(temporary.c_str(),path_.c_str())!=0){::unlink(temporary.c_str());::close(directory);return Durability::NotCommitted;}
        const bool synced=::fsync(directory)==0;::close(directory);
        return synced?Durability::Committed:Durability::Unknown;
#else
        (void)state;return Durability::NotCommitted;
#endif
    }
private:
    std::string path_,parent_;
    int lock_=-1;
    bool owned_=false;
};
} // namespace flix::shards
