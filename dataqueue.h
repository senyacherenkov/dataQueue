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
          typename = typename std::enable_if<std::is_default_constructible<T>::value>::type,
          typename = typename std::enable_if<std::is_nothrow_copy_constructible<T>::value>::type
          >
class DataQueue{
public:
    explicit DataQueue() = default;

    DataQueue(const DataQueue& other) = delete;
    DataQueue(DataQueue&& other) = delete;

    DataQueue& operator= (const DataQueue& other) = delete;
    DataQueue& operator= (DataQueue&& other) = delete;

    ~DataQueue() {
        std::unique_lock<std::mutex> guard1(m_headMutex, std::defer_lock);
        std::unique_lock<std::mutex> guard2(m_tailMutex, std::defer_lock);
        std::lock(guard1, guard2);

        if(!m_head)
            return;
        else {
            QueueNode* temp = m_head->next;
            m_allocator.deallocate(m_head, sizeof(QueueNode));

            while(temp) {
                QueueNode* next = temp->next;
                m_allocator.deallocate(temp, sizeof(QueueNode));
                temp = next;
            }
        }
    }

    bool try_push(const T& item) {
        if(!m_size == QUEUE_SIZE)
        {
            if(m_size == 0) {
                std::lock_guard<std::mutex> lckHead{m_headMutex};
                m_head = m_allocator.allocate(sizeof(QueueNode));
                m_allocator.construct(m_head, item);

                std::lock_guard<std::mutex> lckTail{m_tailMutex};
                m_tail = m_head;
            }
            else {
                QueueNode* temp = m_allocator.allocate(sizeof(QueueNode));
                m_allocator.construct(temp, item);

                std::unique_lock<std::mutex> guard1(m_headMutex, std::defer_lock);
                std::unique_lock<std::mutex> guard2(m_tailMutex, std::defer_lock);
                std::lock(guard1, guard2);

                if(m_tail == m_head)
                    m_head->next = temp;
                else
                    m_tail->next = temp;

                guard1.unlock();
                m_tail = temp;
            }

            ++m_size;            

            m_pushed.notify_all();
            return true;
        }
        return false;
    }

    void wait_push(const T& item) {
        {
            std::unique_lock<std::mutex> guard(m_waitMutex);
            m_pushed.wait(guard, [this]{return m_size != QUEUE_SIZE || m_forceStop;});
        }
        if(!m_forceStop)
            try_push(item);
        else
            m_forceStop = false;
    }

    bool try_pop(T& item) {
        if(m_size == 0)
            return false;

        std::lock_guard<std::mutex> guard1(m_headMutex);
        item = m_head->m_item;
        QueueNode* temp = m_head->next;        

        if(temp)
            m_head = temp;

        m_size--;

        return true;
    }

    void wait_pop(T& item) {
        {
            std::unique_lock<std::mutex> guard(m_waitMutex);
            m_pushed.wait(guard, [this]{return m_head != nullptr || m_forceStop;});
        }
        if(!m_forceStop)
            try_pop(item);
        else
            m_forceStop = false;
    }

    bool empty() const { return m_size == 0; }
    bool full() const {return m_size == QUEUE_SIZE; }
    void stop_waiting() {
        m_forceStop = true;
        m_pushed.notify_all();
    }

private:
    struct QueueNode {
        QueueNode() = default;
        QueueNode(const T& item):
            m_item(item)
        {}

        T m_item;
        QueueNode* next = nullptr;
    };
private:
    Allocator                   m_allocator;
    QueueNode*                  m_head = nullptr;
    QueueNode*                  m_tail = nullptr;
    std::mutex                  m_headMutex;
    std::mutex                  m_tailMutex;
    std::mutex                  m_waitMutex;
    std::mutex                  m_allocMutex;
    std::atomic<size_t>         m_size = 0;
    std::atomic<bool>           m_forceStop = false;
    std::condition_variable     m_pushed;
};
