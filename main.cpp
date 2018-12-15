#include "dataqueue.h"
#include <vector>
#include <string>

#define BOOST_TEST_MODULE test_main

#include <boost/test/included/unit_test.hpp>

using namespace boost::unit_test;
BOOST_AUTO_TEST_SUITE(test_suite_main)

BOOST_AUTO_TEST_CASE(check_dq_using)
{
    DataQueue<int> queue;
}

BOOST_AUTO_TEST_SUITE_END()
