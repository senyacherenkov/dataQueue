#pragma once
#include <vector>
#include <cstddef>
#include <thread>
#include <mutex>
#include <memory>
#include <condition_variable>
#include <type_traits>
#include <atomic>

template <typename T, std::size_t QUEUE_SIZE = 256, class Allocator = std::allocator<T>,
          typename = typename std::enable_if<std::is_nothrow_copy_constructible<T>::value>::type,
          typename = typename std::enable_if<std::is_default_constructible<T>::value>::type>
class DataQueue{
private:
    struct node
    {
        std::shared_ptr<T> data {nullptr};
        std::unique_ptr<node> next {nullptr};
        int number = 0;
    };

public:
    DataQueue():
        m_head(std::make_unique<node>()), m_tail(m_head.get())
    {}

    DataQueue(const DataQueue& other) = delete;
    DataQueue& operator= (const DataQueue& other) = delete;    

    ~DataQueue() {
    }

    bool tryPush(const T& newItem) {
        std::shared_ptr<T> newData = std::make_shared<T>(newItem);

        std::unique_ptr<node> newVertex(std::make_unique<node>());

        {
            std::lock_guard<std::mutex> tail_lock(m_tailMutex);

            if(m_tail->number > QUEUE_SIZE)
                return false;

            m_tail->number++;
            m_tail->data = newData;
            node* const newTail = newVertex.get();
            m_tail->next = std::move(newVertex);
            m_tail = newTail;
        }

        m_dataAwaiting.notify_one();
        return true;
    }

    void waitPush(const T& item) {

    }

    bool tryPop(T& item) {
        std::unique_ptr<node> const oldHead = tryPopHead(item);
        return oldHead;
    }

    std::shared_ptr<T> tryPop() {
        std::unique_ptr<node> const oldHead = tryPopHead();
        return oldHead? oldHead->data: std::shared_ptr<T>();
    }

    void waitPop(T& item) {
        waitPopHead(item);
    }

    std::shared_ptr<T> waitPop() {
        std::unique_ptr<node> const oldHead = waitPopHead();
        return oldHead->data;
    }

    void clear();    

    bool empty() const {
        std::lock_guard<std::mutex> headLock(m_headMutex);
        return (m_head.get() == getTail());
    }

    bool full() const {}
    void stop_waiting();

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
        m_dataAwaiting.wait(headLock, [&](){ return m_head.get() != getTail(); });
        return headLock;
    }

    std::unique_ptr<node> waitPopHead()
    {
        std::unique_lock<std::mutex> headLock(waitForData());
        return popHead();
    }

    std::unique_ptr<node> waitPopHead(T& item)
    {
        std::unique_lock<std::mutex> headLock(waitForData());
        item = std::move(m_head->data);
        return popHead();
    }

    /*****POP AREA END*****/

    /*****PUSH AREA*****/
    std::unique_lock<std::mutex> waitForRoom()
    {
        std::unique_lock<std::mutex> tailLock(m_tailMutex);
        m_roomAwaiting.wait(tailLock, [&](){ return m_tail->number <= QUEUE_SIZE; });
        return tailLock;
    }

    void waitPushData(T& item)
    {
        std::shared_ptr<T> newData = std::make_shared<T>(item);
        std::unique_lock<std::mutex> tailLock(waitForRoom());
        m_tail->data = newData;
    }
    /*****PUSH AREA END*****/
private:
    Allocator               m_allocator;
    std::mutex              m_headMutex;
    std::unique_ptr<node>   m_head;
    std::mutex              m_tailMutex;
    node*                   m_tail;
    std::condition_variable m_dataAwaiting;
    std::condition_variable m_roomAwaiting;
};
