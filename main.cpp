#include "dataqueue.h"
#include <string>
#include <ctime>
#include <chrono>
#include <random>

constexpr int QUEUE_SIZE = 10;
constexpr int NUMBER_OF_ELEMENTS = 500;

#define BOOST_TEST_MODULE test_main

#include <boost/test/included/unit_test.hpp>

namespace  {

void unpredictableDelay(int extra = 0) {
    std::random_device rand_dev;
    std::default_random_engine generator(rand_dev());
    std::uniform_int_distribution<int> distribution(0, 10);

    std::chrono::milliseconds delay{distribution(generator) + extra}; // 3000 seconds

    std::this_thread::sleep_for(delay);
}

}

using namespace boost::unit_test;
BOOST_AUTO_TEST_SUITE(test_suite_main)

//BOOST_AUTO_TEST_CASE(check_dq_using_one_writer_one_reader)
//{
//    DataQueue<int, QUEUE_SIZE> queue;

//    std::thread writer([&]() {
//                    for (int j = 0; j < NUMBER_OF_ELEMENTS; ++j) {
//                        unpredictableDelay(10);
//                        queue.waitPush(j);
//                    }

//              });

//    std::thread reader([&]() {
//        int element;
//        for (int j = 0; j < NUMBER_OF_ELEMENTS;) {
//            unpredictableDelay();
//            queue.waitPop(element);
//            BOOST_CHECK_MESSAGE(element == j, "Expected value " << j << "; real value " << element);
//            ++j;
//        }
//        BOOST_CHECK_MESSAGE(!queue.tryPop(element), "Expected queue to be empty");
//    });

//    writer.join();
//    reader.join();
//}

BOOST_AUTO_TEST_CASE(check_dq_using_one_thread_writer_waits_other_thread_stops)
{
    DataQueue<int, QUEUE_SIZE> queue;

    std::thread writer([&]() {
        for (int j = 0; j < NUMBER_OF_ELEMENTS; ++j) {
            queue.waitPush(j);
        }

        BOOST_CHECK_MESSAGE(queue.full(), "queue must be full");
    });

    std::thread reader([&]() {
        std::chrono::milliseconds delay{100};
        std::this_thread::sleep_for(delay);

        bool isFull = false;
        isFull = queue.full();
        BOOST_CHECK_MESSAGE(isFull, "queue must be full");

        if(isFull)
            queue.stopWaiting();
    });

    writer.join();
    reader.join();
}

BOOST_AUTO_TEST_SUITE_END()
