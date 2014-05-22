#include "options.hpp"

#include <stdio.h>
#include <string.h>
#include <stdexcept>

void test_incompatible_args()
{

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


void run_test(void (*testfunc)())
{
    try
    {
        testfunc();
    }
    catch(std::runtime_error& e)
    {
        fprintf(stderr, "%s", e.what());
        exit(EXIT_FAILURE);
    }
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
