#include "options.hpp"

#include <stdio.h>
#include <string.h>
#include <stdexcept>

void run_test(void (*testfunc)())
{
    try
    {
        testfunc();
    }
    catch(std::exception& e)
    {
        fprintf(stderr, "%s", e.what());
        exit(EXIT_FAILURE);
    }
}

void test_incompatible_args()
{
    try
    {
        char * argv[] = {"osm2pgsql", "-a", "-c", "--slim", "tests/liechtenstein-2013-08-03.osm.pbf"};
        options_t::parse(5, argv);
    }
    catch(std::runtime_error& e)
    {
        if(strcmp(e.what(), "Error: --append and --create options can not be used at the same time!\n"))
            throw std::logic_error("Append and create options should have clashed\n");
    }
}

void test_middles()
{

}

void test_outputs()
{

}

void test_random_perms()
{

}

int main(int argc, char *argv[])
{

    //try each test if any fail we will exit
    run_test(test_incompatible_args);
    run_test(test_middles);
    run_test(test_outputs);
    run_test(test_random_perms);

    //passed
    return 0;
}
