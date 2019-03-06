#include "dataqueue.h"
#include <string>
#include <ctime>
#include <chrono>
#include <random>

constexpr int QUEUE_SIZE = 10;
constexpr int NUMBER_OF_ELEMENTS = 50;
constexpr int NUMBER_OF_ELEMENTS_FOR_STOP_TEST = QUEUE_SIZE + 1;

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

BOOST_AUTO_TEST_CASE(one_writer_one_reader_tryPush_tryPop_first_overload)
{
    DataQueue<int, QUEUE_SIZE> queue;

    std::thread writer([&]() {
                    for (int j = 0; j < QUEUE_SIZE; ++j) {
                        BOOST_CHECK_MESSAGE(queue.tryPush(j), "value wasn't pushed");
                    }
                    unpredictableDelay(10);
              });

    std::thread reader([&]() {
        int element;
        int counter = 0;
        unpredictableDelay(10);
        BOOST_CHECK_MESSAGE(!queue.empty(), "queue is empty");

        while (!queue.empty()) {
            BOOST_CHECK_MESSAGE(queue.tryPop(element), "value wasn't popped");
            BOOST_CHECK_MESSAGE(element == counter, "Expected value " << counter << "; real value " << element);
            ++counter;
        }
        BOOST_CHECK_MESSAGE(!queue.tryPop(element), "Expected that queue has no element to be popped");
        BOOST_CHECK_MESSAGE(queue.empty(), "Expected that queue is empty");
    });

    writer.join();
    reader.join();
}

BOOST_AUTO_TEST_CASE(one_writer_one_reader_tryPush_tryPop_second_overload)
{
    DataQueue<int, QUEUE_SIZE> queue;

    std::thread writer([&]() {
                    for (int j = 0; j < QUEUE_SIZE; ++j) {
                        BOOST_CHECK_MESSAGE(queue.tryPush(j), "value wasn't pushed");
                    }
                    unpredictableDelay(10);
              });

    std::thread reader([&]() {
        int counter = 0;
        unpredictableDelay(10);
        BOOST_CHECK_MESSAGE(!queue.empty(), "queue is empty");

        while (!queue.empty()) {
            auto element = queue.tryPop();
            BOOST_CHECK_MESSAGE(element.get() != nullptr, "value wasn't popped");
            BOOST_CHECK_MESSAGE(*element == counter, "Expected value " << counter << "; real value " << element);
            ++counter;
        }
        auto element = queue.tryPop();
        BOOST_CHECK_MESSAGE(element.get() == nullptr, "Expected that queue has no element to be popped");
        BOOST_CHECK_MESSAGE(queue.empty(), "Expected that queue is empty");
    });

    writer.join();
    reader.join();
}

BOOST_AUTO_TEST_CASE(one_writer_one_reader_waitPush_waitPop_first_overload)
{
    DataQueue<int, QUEUE_SIZE> queue;

    std::thread writer([&]() {
                    for (int j = 0; j < NUMBER_OF_ELEMENTS; ++j) {
                        unpredictableDelay(10);
                        queue.waitPush(j);
                    }

              });

    std::thread reader([&]() {
        int element;
        for (int j = 0; j < NUMBER_OF_ELEMENTS;) {
            unpredictableDelay();
            queue.waitPop(element);
            BOOST_CHECK_MESSAGE(element == j, "Expected value " << j << "; real value " << element);
            ++j;
        }
        BOOST_CHECK_MESSAGE(!queue.tryPop(element), "Expected that queue is empty");
    });

    writer.join();
    reader.join();
}


BOOST_AUTO_TEST_CASE(one_writer_one_reader_waitPush_waitPop_second_overload)
{
    DataQueue<int, QUEUE_SIZE> queue;

    std::thread writer([&]() {
                    for (int j = 0; j < NUMBER_OF_ELEMENTS; ++j) {
                        unpredictableDelay(10);
                        queue.waitPush(j);
                    }

              });

    std::thread reader([&]() {
        for (int j = 0; j < NUMBER_OF_ELEMENTS;) {
            unpredictableDelay();
            auto element = queue.waitPop();
            BOOST_CHECK_MESSAGE(*element == j, "Expected value " << j << "; real value " << element);
            ++j;
        }
        auto element = queue.tryPop();
        BOOST_CHECK_MESSAGE(element.get() == nullptr, "Expected that queue is empty");
    });

    writer.join();
    reader.join();
}

BOOST_AUTO_TEST_CASE(one_thread_writer_waits_because_full_other_thread_stops)
{
    DataQueue<int, QUEUE_SIZE> queue;

    std::thread writer([&]() {
        for (int j = 0; j < NUMBER_OF_ELEMENTS_FOR_STOP_TEST; ++j) {
            queue.waitPush(j);
        }

        BOOST_CHECK_MESSAGE(queue.full(), "queue must be full");
    });

    std::thread breaker([&]() {
        std::chrono::milliseconds delay{10};
        std::this_thread::sleep_for(delay);

        bool isFull = false;
        isFull = queue.full();
        BOOST_CHECK_MESSAGE(isFull, "queue must be full");

        if(isFull)
            queue.stopWaiting();
    });

    writer.join();
    breaker.join();
}

BOOST_AUTO_TEST_CASE(one_thread_writer_waits_because_empty_other_thread_stops)
{
    DataQueue<int, QUEUE_SIZE> queue;

    std::thread breaker([&]() {
        std::chrono::milliseconds delay{100};
        std::this_thread::sleep_for(delay);

        bool isEmpty = false;
        isEmpty = queue.empty();
        BOOST_CHECK_MESSAGE(isEmpty, "queue must be empty");

        if(isEmpty)
            queue.stopWaiting();
    });

    std::thread reader([&]() {
        int element;
        queue.waitPop(element);

        BOOST_CHECK_MESSAGE(queue.empty(), "queue must be empty");
    });

    breaker.join();
    reader.join();
}

BOOST_AUTO_TEST_CASE(two_threads_are_writers_other_one_is_reader)
{
    DataQueue<int, QUEUE_SIZE> queue;

    std::thread writer1([&]() {
                    for (int j = 0; j < NUMBER_OF_ELEMENTS; ++j) {
                        unpredictableDelay(5);
                        queue.waitPush(j);
                    }

              });

    std::thread writer2([&]() {
                    for (int j = 0; j < NUMBER_OF_ELEMENTS; ++j) {
                        unpredictableDelay(5);
                        queue.waitPush(j);
                    }

              });

    std::thread reader([&]() {
        int element;
        std::vector<int> poppedData;
        poppedData.reserve(2*NUMBER_OF_ELEMENTS);

        unpredictableDelay(200);
        BOOST_CHECK_MESSAGE(queue.full(), "queue must be full");

        while(!queue.empty()) {
            unpredictableDelay(10);
            queue.waitPop(element);
            poppedData.push_back(element);
        }

        BOOST_CHECK_MESSAGE(poppedData.size() == 2*NUMBER_OF_ELEMENTS, "vector's size has wrong amount of elements");
    });

    writer1.join();
    writer2.join();
    reader.join();
}

BOOST_AUTO_TEST_SUITE_END()
