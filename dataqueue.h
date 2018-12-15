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
          typename = typename std::enable_if<std::is_default_constructible<T>::value>::type
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
        if(!m_isFull)
        {
            if(m_isEmpty) {
                {
                    std::lock_guard<std::mutex> lckHead{m_headMutex};
                    m_head = m_allocator.allocate(sizeof(QueueNode));
                    m_allocator.construct(m_head, item);


                    std::lock_guard<std::mutex> lckTail{m_tailMutex};
                    m_tail = m_head;
                }
                m_isEmpty = false;
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
            if(m_size == QUEUE_SIZE)
                m_isFull = true;

            return true;
        }
        return false;
    }

    void wait_push(const T& item) {
        while(m_isFull);
        try_push(item);
    }

    bool try_pop(T& item) {
        if(m_isEmpty)
            return false;

        std::lock_guard<std::mutex> guard1(m_headMutex);
        item = m_head->m_item;
        QueueNode* temp = m_head->next;

        m_allocator.destroy(m_head);
        m_allocator.deallocate(m_head, 1);

        m_head = temp;

        return true;
    }

    void wait_pop(T& item) {
        while(m_isEmpty);
        try_pop(item);
    }

    void clear();    

    bool empty() const { return m_isEmpty; }
    bool full() const {return m_isFull; }
    void stop_waiting();
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
    Allocator           m_allocator;
    QueueNode*          m_head = nullptr;
    QueueNode*          m_tail = nullptr;
    std::mutex          m_headMutex;
    std::mutex          m_tailMutex;
    std::atomic<bool>   m_isEmpty = true;
    std::atomic<bool>   m_isFull = false;
    std::atomic<size_t> m_size = 0;
};
