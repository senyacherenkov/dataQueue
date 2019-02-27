#include "dataqueue.h"
#include <string>
#include <ctime>
#include <chrono>
#include <random>


#define BOOST_TEST_MODULE test_main

#include <boost/test/included/unit_test.hpp>

namespace  {

void unpredictableDelay(int extra){
    std::random_device rand_dev;
    std::default_random_engine generator(rand_dev());
    std::uniform_int_distribution<int> distribution(0, 100);

    std::chrono::milliseconds delay{distribution(generator) + extra}; // 3000 seconds

    std::this_thread::sleep_for(delay);
}

}

using namespace boost::unit_test;
BOOST_AUTO_TEST_SUITE(test_suite_main)

BOOST_AUTO_TEST_CASE(check_dq_using)
{
    DataQueue<int> queue;

    std::thread writer([&]() {
                    for (unsigned long long j = 0; j < 1024ULL * 1024ULL * 32ULL; ++j) {
                        unpredictableDelay(500);
                        q.enqueue(j);
                    }
                });

                SimpleThread reader([&]() {
                    bool canLog = true;
                    unsigned long long element;
                    for (unsigned long long j = 0; j < 1024ULL * 1024ULL * 32ULL;) {
                        if (canLog && (j & (1024 * 1024 * 16 - 1)) == 0) {
                            log << "  ... iteration " << j << std::endl;
                            std::printf("  ... iteration %llu\n", j);
                            canLog = false;
                        }
                        unpredictableDelay();
                        if (q.try_dequeue(element)) {
                            if (element != j) {
                                log << "  ERROR DETECTED: Expected to read " << j << " but found " << element << std::endl;
                                std::printf("  ERROR DETECTED: Expected to read %llu but found %llu", j, element);
                            }
                            ++j;
                            canLog = true;
                        }
                    }
                    if (q.try_dequeue(element)) {
                        log << "  ERROR DETECTED: Expected queue to be empty" << std::endl;
                        std::printf("  ERROR DETECTED: Expected queue to be empty\n");
                    }
                });

                writer.join();
                reader.join();

}

BOOST_AUTO_TEST_SUITE_END()
