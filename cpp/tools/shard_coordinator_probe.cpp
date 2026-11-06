// LOCAL FAULT-TEST HARNESS ONLY. Not a public/authenticated production service.
// Intentionally not added to the game CMake target or staged into dist/.
#include "server/shard_authority.h"
#include "server/shard_store.h"
#include <csignal>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
using namespace flix;
using namespace flix::shards;
namespace {
volatile std::sig_atomic_t stopped=0;
void stop(int){stopped=1;}
std::string env(const char* key){const auto* value=std::getenv(key);return value?value:"";}
bool sendAll(int fd,const std::string& text){std::size_t at=0;while(at<text.size()){const auto n=::send(fd,text.data()+at,text.size()-at,MSG_NOSIGNAL);if(n<=0)return false;at+=n;}return true;}
}
int main(int argc,char** argv) {
 if(argc!=3&&argc!=4){std::cerr<<"usage: shard-probe STORE SOCKET [BOOTSTRAP_RECORDS]\n";return 2;}
 FileStore store(argv[1]);if(!store.owned()){std::cerr<<"state already locked or unavailable\n";return 3;}
 if(argc==4){struct stat existing{};if(::stat(argv[1],&existing)==0){std::cerr<<"refusing bootstrap over existing state\n";return 4;}
  Json records;std::string error;if(!Json::parseFile(argv[3],records,error)||!records.isObject())return 5;
  if(store.commit(Authority::initial(records))!=Durability::Committed)return 6;
 }
 Json state;std::string error;if(!store.load(state,error)){std::cerr<<error<<"\n";return 7;}
 std::string currentId;Authority authority(state,[&](const Json& next){
  if(!env("SHARD_PROBE_FAIL_REQUEST").empty()&&currentId==env("SHARD_PROBE_FAIL_REQUEST"))return Durability::NotCommitted;
  auto durable=store.commit(next);
  if(durable==Durability::Committed&&!env("SHARD_PROBE_UNKNOWN_REQUEST").empty()&&currentId==env("SHARD_PROBE_UNKNOWN_REQUEST"))return Durability::Unknown;
  return durable;
 });
 const std::string socketPath=argv[2];sockaddr_un address{};address.sun_family=AF_UNIX;
 if(socketPath.size()>=sizeof(address.sun_path))return 8;std::memcpy(address.sun_path,socketPath.c_str(),socketPath.size()+1);
 ::umask(0077);const int listener=::socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(listener<0)return 9;
 if(::bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))<0||::listen(listener,16)<0){::close(listener);return 10;}
 struct sigaction action{};action.sa_handler=stop;::sigemptyset(&action.sa_mask);::sigaction(SIGTERM,&action,nullptr);::sigaction(SIGINT,&action,nullptr);
 std::cout<<"READY local-only coordinator"<<std::endl;
 while(!stopped){
  const int client=::accept4(listener,nullptr,nullptr,SOCK_CLOEXEC);if(client<0){if(errno==EINTR)continue;break;}
  timeval timeout{3,0};::setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));::setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
  std::string data;char buffer[4096];bool complete=false;
  while(data.size()<=1024*1024){const auto n=::recv(client,buffer,sizeof(buffer),0);if(n<=0)break;data.append(buffer,n);if(data.find('\n')!=std::string::npos){complete=true;break;}}
  Json envelope,result;if(!complete||data.size()>1024*1024||!Json::parse(data,envelope,error))result=failure("bad_envelope");
  else if(envelope["request"]["op"].asString()=="inspect")result=authority.snapshot();
  else {currentId=envelope["request"]["requestId"].asString();result=authority.apply(Node{envelope["node"].asString(),envelope["boot"].asString()},envelope["request"]);}
  if(!env("SHARD_PROBE_DROP_REPLY_REQUEST").empty()&&currentId==env("SHARD_PROBE_DROP_REPLY_REQUEST")&&result["ok"].asBool())::_exit(86);
  sendAll(client,result.dump()+"\n");::close(client);
 }
 ::close(listener);::unlink(socketPath.c_str());return 0;
}
