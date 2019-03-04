#pragma once
#include <vector>
#include <cstddef>
#include <thread>
#include <mutex>
#include <memory>
#include <condition_variable>
#include <type_traits>
#include <atomic>

template <std::size_t QUEUE_SIZE = 256>
class DataQueue{
private:
    template <typename T,
              typename = typename std::enable_if<std::is_nothrow_copy_constructible<T>::value>::type,
              typename = typename std::enable_if<std::is_default_constructible<T>::value>::type>
    struct node
    {
        std::shared_ptr<T> data {nullptr};
        std::unique_ptr<node<T>> next {nullptr};
        int number = 0;
    };

public:

    DataQueue():
        m_head(std::make_unique<node<void*>>()), m_tail(m_head.get())
    {}

    DataQueue(const DataQueue& other) = delete;
    DataQueue& operator= (const DataQueue& other) = delete;    

    ~DataQueue() {
        while(tryPop());
    }

    template <typename T,
              typename = typename std::enable_if<std::is_nothrow_copy_constructible<T>::value>::type,
              typename = typename std::enable_if<std::is_default_constructible<T>::value>::type>
    bool tryPush(const T& newItem) {
        std::shared_ptr<T> newData = std::make_shared<T>(newItem);
        std::unique_ptr<node<T>> newVertex(std::make_unique<node>());

        {
            std::lock_guard<std::mutex> tailLock(m_tailMutex);

            if(m_tail->number > QUEUE_SIZE)
                return false;

            pushToTail(newData, std::move(newVertex));
        }

        m_dataAwaiting.notify_one();
        return true;
    }

    template <typename T,
              typename = typename std::enable_if<std::is_nothrow_copy_constructible<T>::value>::type,
              typename = typename std::enable_if<std::is_default_constructible<T>::value>::type>
    void waitPush(const T& newItem) {
        std::shared_ptr<T> newData = std::make_shared<T>(newItem);
        std::unique_ptr<node<T>> newVertex(std::make_unique<node>());

        {
            std::unique_lock<std::mutex> tailLock(waitForRoom());

            if(m_stopWaitForRoom.exchange(false, std::memory_order_acq_rel)) {
                return;
            }

            pushToTail(newData, std::move(newVertex));
        }

        m_dataAwaiting.notify_one();
    }

    template <typename T,
              typename = typename std::enable_if<std::is_nothrow_copy_constructible<T>::value>::type,
              typename = typename std::enable_if<std::is_default_constructible<T>::value>::type>
    bool tryPop(T& item) {
        std::unique_ptr<node<T>> const oldHead = tryPopHead(item);
        m_roomAwaiting.notify_one();
        return oldHead.get();
    }

    template <typename T,
              typename = typename std::enable_if<std::is_nothrow_copy_constructible<T>::value>::type,
              typename = typename std::enable_if<std::is_default_constructible<T>::value>::type>
    std::shared_ptr<T> tryPop() {
        std::unique_ptr<node<T>> const oldHead = tryPopHead();
        m_roomAwaiting.notify_one();
        return oldHead? oldHead->data: std::shared_ptr<T>();
    }

    template <typename T,
              typename = typename std::enable_if<std::is_nothrow_copy_constructible<T>::value>::type,
              typename = typename std::enable_if<std::is_default_constructible<T>::value>::type>
    void waitPop(T& item) {
        std::unique_ptr<node<T>> const oldHead = waitPopHead(item);
        if(!oldHead.get())
            return;
        m_roomAwaiting.notify_one();
    }

    template <typename T,
              typename = typename std::enable_if<std::is_nothrow_copy_constructible<T>::value>::type,
              typename = typename std::enable_if<std::is_default_constructible<T>::value>::type>
    std::shared_ptr<T> waitPop() {
        std::unique_ptr<node<T>> const oldHead = waitPopHead();
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

private:
    template <typename T>
    node<T>* getTail()
    {
        std::lock_guard<std::mutex> tailLock(m_tailMutex);
        return m_tail;
    }

    /*****POP AREA*****/
    template <typename T>
    std::unique_ptr<node<T>> popHead()
    {
        std::unique_ptr<node<T>> oldHead = std::move(m_head);
        m_head = std::move(oldHead->next);
        return oldHead;
    }

    template <typename T>
    std::unique_ptr<node<T>> tryPopHead()
    {
        std::unique_lock<std::mutex> headLock(m_headMutex);
        if(m_head.get() == getTail())
        {
            return std::unique_ptr<node<T>>();
        }
        getTail()->number--;
        return popHead();
    }

    template <typename T>
    std::unique_ptr<node<T>> tryPopHead(T& item)
    {
        std::unique_lock<std::mutex> headLock(m_headMutex);
        if(m_head.get() == getTail())
        {
            return std::unique_ptr<node<T>>();
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

    template <typename T>
    std::unique_ptr<node<T>> waitPopHead()
    {
        std::unique_lock<std::mutex> headLock(waitForData());
        if(m_stopWaitForData.exchange(false, std::memory_order_acq_rel))
            return std::unique_ptr<node<T>>();
        getTail()->number--;
        return popHead();
    }

    template <typename T>
    std::unique_ptr<node<T>> waitPopHead(T& item)
    {
        std::unique_lock<std::mutex> headLock(waitForData());
        if(m_stopWaitForData.exchange(false, std::memory_order_acq_rel))
            return std::unique_ptr<node<T>>();
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

    template <typename T>
    void pushToTail(std::shared_ptr<T>& newData, std::unique_ptr<node<T>> newVertex)
    {
        std::static_pointer_cast<T>(m_tail->data) = newData;
        node<void*>* const newTail = newVertex.get();
        m_tail->next = std::move(newVertex);
        newTail->number = ++m_tail->number;
        m_tail = newTail;        
    }
    /*****PUSH AREA END*****/
private:
    std::atomic_bool                m_stopWaitForData{false};
    std::atomic_bool                m_stopWaitForRoom{false};
    std::mutex                      m_headMutex;
    std::unique_ptr<node<void*>>    m_head;
    std::mutex                      m_tailMutex;
    node<void*>*                    m_tail;
    std::condition_variable         m_dataAwaiting;
    std::condition_variable         m_roomAwaiting;
};
