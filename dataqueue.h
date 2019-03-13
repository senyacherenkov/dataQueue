#pragma once
#include <vector>
#include <cstddef>
#include <thread>
#include <mutex>
#include <memory>
#include <condition_variable>
#include <type_traits>
#include <atomic>
#include <string>
#include <fstream>
#include <chrono>
#include <cstring>
#include <iostream>

namespace {
    template<typename T, typename = typename std::enable_if<std::is_pod<T>::value>::type>
    std::ostream& write(const T& value, std::ofstream& ifs)
    {        
        return ifs.write(reinterpret_cast<const char*>(&value), sizeof (T));
    }

    template<typename T>
    typename std::enable_if<!std::is_pod<T>::value, std::ostream&>::type
    write(const T& value, std::ofstream& ifs)
    {
        return value.serialize(ifs);
    }

    template<typename T, typename = typename std::enable_if<std::is_pod<T>::value>::type>
    T read(std::ifstream& ofs)
    {
        T value;
        ofs.read(reinterpret_cast<char*>(&value), sizeof(T));
        return value;
    }

    template<typename T>
    typename std::enable_if<!std::is_pod<T>::value, T>::type
    read(std::ifstream& ofs)
    {
        T value;
        return value.deserialize(ofs);
    }

    decltype(std::chrono::seconds().count()) getSecondsSinceEpoch()
    {
        // get the current time
        const auto now     = std::chrono::system_clock::now();

        // transform the time into a duration since the epoch
        const auto epoch   = now.time_since_epoch();

        // cast the duration into seconds
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch);

        // return the number of seconds
        return seconds.count();
    }
}

namespace threadsafe {

template <typename T, std::size_t QUEUE_SIZE = 256,
          typename = typename std::enable_if<std::is_nothrow_copy_constructible<T>::value>::type,
          typename = typename std::enable_if<std::is_default_constructible<T>::value>::type>
class queue{
private:
    struct node
    {
        std::shared_ptr<T> data {nullptr};
        std::unique_ptr<node> next {nullptr};
        int number = 0;
    };

public:
    queue():
        m_head(std::make_unique<node>()), m_tail(m_head.get())
    {}

    queue(const queue& other) = delete;
    queue& operator= (const queue& other) = delete;

    ~queue() {
        while(tryPop());
    }

    bool tryPush(const T& newItem) {
        std::shared_ptr<T> newData = std::make_shared<T>(newItem);
        std::unique_ptr<node> newVertex(std::make_unique<node>());

        {
            std::lock_guard<std::mutex> tailLock(m_tailMutex);

            if(m_tail->number > QUEUE_SIZE)
                return false;

            pushToTail(newData, std::move(newVertex));
        }

        m_dataAwaiting.notify_one();
        return true;
    }


    void waitPush(const T& newItem) {
        std::shared_ptr<T> newData = std::make_shared<T>(newItem);
        std::unique_ptr<node> newVertex(std::make_unique<node>());

        {
            std::unique_lock<std::mutex> tailLock(waitForRoom());

            if(m_stopWaitForRoom.exchange(false, std::memory_order_acq_rel)) {
                return;
            }

            pushToTail(newData, std::move(newVertex));
        }

        m_dataAwaiting.notify_one();
    }

    bool tryPop(T& item) {
        std::unique_ptr<node> const oldHead = tryPopHead(item);
        m_roomAwaiting.notify_one();
        return oldHead.get();
    }

    std::shared_ptr<T> tryPop() {
        std::unique_ptr<node> const oldHead = tryPopHead();
        m_roomAwaiting.notify_one();
        return oldHead? oldHead->data: std::shared_ptr<T>();
    }

    void waitPop(T& item) {
        std::unique_ptr<node> const oldHead = waitPopHead(item);
        if(!oldHead.get())
            return;
        m_roomAwaiting.notify_one();
    }

    std::shared_ptr<T> waitPop() {
        std::unique_ptr<node> const oldHead = waitPopHead();
        if(!oldHead.get())
            return std::shared_ptr<T>();
        m_roomAwaiting.notify_one();
        return oldHead->data;
    }    

    bool empty() {
        std::lock_guard<std::mutex> headLock(m_headMutex);
        return (m_head.get() == getTail());
    }

    bool full() {
        return (getTail()->number == QUEUE_SIZE);
    }

    void stopWaiting(){
        if(empty()) {
            m_stopWaitForData.store(true, std::memory_order_release);
            m_dataAwaiting.notify_all();
        } else if(full()) {
            m_stopWaitForRoom.store(true, std::memory_order_release);
            m_roomAwaiting.notify_all();
        }
    }

    std::string storeToDisk(const char* name) {

        std::string filename(name);
        filename += std::to_string(getSecondsSinceEpoch());
        filename += ".txt";

        std::ofstream ifs(filename, std::ios_base::out | std::ios::binary);
        if(ifs.is_open())
        {
            std::lock(m_headMutex, m_tailMutex);
            std::unique_lock<std::mutex> headLock(m_headMutex, std::adopt_lock);
            std::unique_lock<std::mutex> tailLock(m_tailMutex, std::adopt_lock);

            if(m_head.get() != m_tail)
            {
                node* temp = m_head.get();
                while (temp != m_tail) {
                    T& data = *temp->data;
                    write(data);
                    temp = temp->next.get();
                }
            }
        }

        return filename;
    }

    bool tryReadFromDisk(const char* filename) {
        std::ifstream ofs(filename, std::ios_base::in);
        if(ofs.is_open())
        {
            std::string rawData;
            ofs >> rawData;

            unsigned long pos = 0;
            while(!rawData.empty())
            {                

                if(!tryPush(read<T>(temp)))
                    return false;
                rawData.erase(rawData.begin(), std::next(rawData.begin(), static_cast<long>(pos + 1)));
            }
        } else {
            return false;
        }
        return true;
    }

    void waitReadFromDisk(const char* filename) {
        std::ifstream ofs(filename, std::ios_base::in | std::ios_base::trunc);
        if(ofs.is_open())
        {
            std::string rawData;
            ofs >> rawData;

            unsigned long pos = 0;
            while(pos != std::string::npos)
            {
                pos = rawData.find_first_of(DELIMETER);
                std::string temp(rawData.begin(), std::next(rawData.begin(), static_cast<long>(pos)));
                waitPush(read<T>(temp));
                rawData.erase(rawData.begin(), std::next(rawData.begin(), static_cast<long>(pos)));
            }
        }
    }
private:
    node* getTail()
    {
        std::lock_guard<std::mutex> tailLock(m_tailMutex);
        return m_tail;
    }

    /*****POP AREA*****/
    std::unique_ptr<node> popHead()
    {
        std::unique_ptr<node> oldHead = std::move(m_head);
        m_head = std::move(oldHead->next);
        return oldHead;
    }

    std::unique_ptr<node> tryPopHead()
    {
        std::unique_lock<std::mutex> headLock(m_headMutex);
        if(m_head.get() == getTail())
        {
            return std::unique_ptr<node>();
        }
        getTail()->number--;
        return popHead();
    }

    std::unique_ptr<node> tryPopHead(T& item)
    {
        std::unique_lock<std::mutex> headLock(m_headMutex);
        if(m_head.get() == getTail())
        {
            return std::unique_ptr<node>();
        }
        getTail()->number--;
        item = std::move(*m_head->data);
        return popHead();
    }

    std::unique_lock<std::mutex> waitForData()
    {
        std::unique_lock<std::mutex> headLock(m_headMutex);
        m_dataAwaiting.wait(headLock, [&](){ return (m_head.get() != getTail()) ||
                    m_stopWaitForData.load(std::memory_order_acquire); });
        return headLock;
    }

    std::unique_ptr<node> waitPopHead()
    {
        std::unique_lock<std::mutex> headLock(waitForData());
        if(m_stopWaitForData.exchange(false, std::memory_order_acq_rel))
            return std::unique_ptr<node>();
        getTail()->number--;
        return popHead();
    }

    std::unique_ptr<node> waitPopHead(T& item)
    {
        std::unique_lock<std::mutex> headLock(waitForData());
        if(m_stopWaitForData.exchange(false, std::memory_order_acq_rel))
            return std::unique_ptr<node>();
        getTail()->number--;
        item = std::move(*m_head->data);
        return popHead();
    }

    /*****POP AREA END*****/

    /*****PUSH AREA*****/
    std::unique_lock<std::mutex> waitForRoom()
    {
        std::unique_lock<std::mutex> tailLock(m_tailMutex);
        m_roomAwaiting.wait(tailLock, [&](){ return (m_tail->number < QUEUE_SIZE) ||
                    m_stopWaitForRoom.load(std::memory_order_acquire); });
        return tailLock;
    }

    void pushToTail(std::shared_ptr<T>& newData, std::unique_ptr<node> newVertex)
    {
        m_tail->data = newData;
        node* const newTail = newVertex.get();
        m_tail->next = std::move(newVertex);
        newTail->number = ++m_tail->number;
        m_tail = newTail;        
    }
    /*****PUSH AREA END*****/
private:
    std::atomic_bool        m_stopWaitForData{false};
    std::atomic_bool        m_stopWaitForRoom{false};
    std::mutex              m_headMutex;
    std::unique_ptr<node>   m_head;
    std::mutex              m_tailMutex;
    node*                   m_tail;
    std::condition_variable m_dataAwaiting;
    std::condition_variable m_roomAwaiting;
};
} // namespace threadsafe
