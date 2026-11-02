#include "test.h"
#include "shared/game/super_population.h"
using flix::SuperPopulation;
TEST(super_population_fills_five_without_refreshing_living_bosses) {
    SuperPopulation p;p.observe(0,0);
    for(int i=0;i<5;++i){CHECK(p.ready(0));p.spawned();}
    CHECK(!p.ready(0));p.observe(5,900000);CHECK(!p.ready(900000));
}
TEST(super_population_waits_thirty_seconds_after_a_death) {
    SuperPopulation p;p.observe(5,0);p.observe(4,1000);
    CHECK(!p.ready(30999));CHECK(p.ready(31000));p.spawned();CHECK(!p.ready(31000));
}
TEST(super_population_keeps_independent_vacancy_deadlines) {
    SuperPopulation p;p.observe(5,0);p.observe(3,1000);p.observe(2,5000);
    CHECK(!p.ready(30000));CHECK(p.ready(31000));p.spawned();CHECK(p.ready(31000));p.spawned();
    CHECK(!p.ready(34999));CHECK(p.ready(35000));p.spawned();CHECK(!p.ready(35000));
}
TEST(super_population_does_not_cull_excess_or_duplicate_external_replacements) {
    SuperPopulation p;p.observe(8,0);CHECK(!p.ready(0));p.observe(7,1000);CHECK(!p.ready(40000));
    p.observe(4,50000);CHECK(!p.ready(79999));p.observe(5,60000);CHECK(!p.ready(90000));
}
