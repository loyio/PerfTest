/*
 * 03_circular_reference.cpp - 循环引用导致的内存泄漏
 *
 * 学习要点:
 * 1. shared_ptr 的循环引用问题
 * 2. 使用 weak_ptr 打破循环引用
 * 3. 观察引用计数变化
 *
 * 这是实际项目中非常常见的内存泄漏模式!
 */

#include "profiler.h"
#include <memory>
#include <string>
#include <iostream>
#include <vector>

// ============================================================
// 场景1: 循环引用 - 两个对象互相持有 shared_ptr
// ============================================================
namespace bad_example {

struct Node {
    std::string name;
    std::shared_ptr<Node> next;  // 强引用!
    std::shared_ptr<Node> prev;  // 强引用!

    Node(const std::string& n) : name(n) {
        std::cout << "    [+] Node '" << name << "' created\n";
    }
    ~Node() {
        std::cout << "    [-] Node '" << name << "' destroyed\n";
    }
};

void demo_circular_leak() {
    perf::printHeader("BAD: Circular Reference with shared_ptr");

    auto a = std::make_shared<Node>("A");
    auto b = std::make_shared<Node>("B");

    std::cout << "  Before linking:\n";
    std::cout << "    A ref count: " << a.use_count() << "\n"; // 1
    std::cout << "    B ref count: " << b.use_count() << "\n"; // 1

    // 创建循环引用: A -> B -> A
    a->next = b;
    b->prev = a;

    std::cout << "  After linking A <-> B:\n";
    std::cout << "    A ref count: " << a.use_count() << "\n"; // 2 (a + b->prev)
    std::cout << "    B ref count: " << b.use_count() << "\n"; // 2 (b + a->next)

    std::cout << "  Leaving scope... (watch: destructors NOT called!)\n";
    // a 释放: A ref count 2->1 (b->prev 还持有)
    // b 释放: B ref count 2->1 (a->next 还持有)
    // 引用计数永远不会到 0 -> 内存泄漏!
}

} // namespace bad_example

// ============================================================
// 场景2: 使用 weak_ptr 修复循环引用
// ============================================================
namespace good_example {

struct Node {
    std::string name;
    std::shared_ptr<Node> next;   // 前向用 shared_ptr (拥有关系)
    std::weak_ptr<Node>   prev;   // 后向用 weak_ptr (观察关系)

    Node(const std::string& n) : name(n) {
        std::cout << "    [+] Node '" << name << "' created\n";
    }
    ~Node() {
        std::cout << "    [-] Node '" << name << "' destroyed\n";
    }

    void printPrev() {
        if (auto p = prev.lock()) {  // weak_ptr 必须 lock() 后才能使用
            std::cout << "    " << name << "'s prev is " << p->name << "\n";
        } else {
            std::cout << "    " << name << "'s prev is expired\n";
        }
    }
};

void demo_weak_ptr_fix() {
    perf::printHeader("GOOD: Breaking Cycle with weak_ptr");

    auto a = std::make_shared<Node>("A");
    auto b = std::make_shared<Node>("B");

    a->next = b;
    b->prev = a;  // weak_ptr 不增加引用计数!

    std::cout << "  After linking:\n";
    std::cout << "    A ref count: " << a.use_count() << "\n"; // 1 (只有 a)
    std::cout << "    B ref count: " << b.use_count() << "\n"; // 2 (b + a->next)

    b->printPrev();

    std::cout << "  Leaving scope... (watch: destructors ARE called!)\n";
    // a 释放: A ref count 1->0 -> A 被销毁 -> A->next 释放
    // B ref count 2->1->0 -> B 被销毁
}

} // namespace good_example

// ============================================================
// 场景3: 实际场景 - Observer 模式中的循环引用
// ============================================================
namespace observer_example {

struct EventSystem;

struct Listener : std::enable_shared_from_this<Listener> {
    std::string name;

    Listener(const std::string& n) : name(n) {
        std::cout << "    [+] Listener '" << name << "' created\n";
    }
    ~Listener() {
        std::cout << "    [-] Listener '" << name << "' destroyed\n";
    }

    void onEvent(const std::string& event) {
        std::cout << "    " << name << " received: " << event << "\n";
    }
};

struct EventSystem {
    // 使用 weak_ptr 持有监听者，避免循环引用
    std::vector<std::weak_ptr<Listener>> listeners;

    void subscribe(std::shared_ptr<Listener> listener) {
        listeners.push_back(listener);
    }

    void emit(const std::string& event) {
        // 清理已失效的 weak_ptr 并通知存活的监听者
        auto it = listeners.begin();
        while (it != listeners.end()) {
            if (auto sp = it->lock()) {
                sp->onEvent(event);
                ++it;
            } else {
                // 监听者已被销毁，移除
                it = listeners.erase(it);
            }
        }
    }
};

void demo_observer_pattern() {
    perf::printHeader("Observer Pattern with weak_ptr");

    EventSystem events;

    {
        auto listener1 = std::make_shared<Listener>("Player");
        auto listener2 = std::make_shared<Listener>("UI");

        events.subscribe(listener1);
        events.subscribe(listener2);

        events.emit("game_start");
        std::cout << "  Listeners count: " << events.listeners.size() << "\n";

        // listener2 离开作用域
        std::cout << "  -- listener2 (UI) going out of scope --\n";
    }
    // listener1 也离开了作用域

    // 再次发送事件, weak_ptr 会自动检测到监听者已被销毁
    std::cout << "  Emitting after listeners destroyed:\n";
    events.emit("game_update");
    std::cout << "  Remaining listeners: " << events.listeners.size() << "\n";
}

} // namespace observer_example

int main() {
    perf::initConsole();
    std::cout << "===== 03: Circular Reference Tutorial =====\n";

    bad_example::demo_circular_leak();
    good_example::demo_weak_ptr_fix();
    observer_example::demo_observer_pattern();

    std::cout << "\n===== Key Takeaways =====\n"
              << "1. shared_ptr 循环引用会导致内存永远无法释放\n"
              << "2. 用 weak_ptr 打破循环 (后向引用/观察者用 weak_ptr)\n"
              << "3. weak_ptr::lock() 返回 shared_ptr, 若对象已死则返回空\n"
              << "4. Observer 模式中, 事件系统应用 weak_ptr 持有监听者\n";

    return 0;
}
