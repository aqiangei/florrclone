#include "test.h"
#include "client/shard_directory.h"
using namespace flix;
TEST(shard_directory_is_bound_to_current_connection_and_explicit_choices) {
    auto input=Json::parseOrNull(R"({"current":"one","choices":[{"id":"one","label":"One","host":"192.0.2.1","port":8088,"available":true},{"id":"two","label":"Two","host":"192.0.2.2","port":8088,"available":false}]})");
    ShardDirectory directory;
    CHECK(directory.parse(input,"192.0.2.1",8088));CHECK(directory.find("one")!=nullptr);
    CHECK(!directory.find("two")->available);CHECK(directory.find("fake")==nullptr);
    CHECK(!directory.parse(input,"192.0.2.2",8088));CHECK(!directory.parse(input,"192.0.2.1",8080));
    input["choices"][1]["host"]="user:password@host";CHECK(!directory.parse(input,"192.0.2.1",8088));
    CHECK_EQ(directory.find("two")->host,"192.0.2.2");
}
