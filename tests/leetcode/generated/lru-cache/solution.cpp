// 146. LRU Cache (Medium)
// https://leetcode.com/problems/lru-cache/
//
// Write your solution in the USER SOLUTION section. Only that section is copied by
// "LeetCode: Copy Solution". Run the tests with Ctrl+Shift+B (LeetCode: Run Tests).

#include ".lc/leetcode.hpp"  // local only: standard library, LeetCode types, test helpers

// ==================== USER SOLUTION ====================

class LRUCache {
public:
    LRUCache(int capacity) : capacity_(capacity) {}

    int get(int key) {
        auto it = index_.find(key);
        if (it == index_.end()) return -1;
        order_.splice(order_.begin(), order_, it->second);
        return it->second->second;
    }

    void put(int key, int value) {
        auto it = index_.find(key);
        if (it != index_.end()) {
            it->second->second = value;
            order_.splice(order_.begin(), order_, it->second);
            return;
        }
        if ((int)order_.size() == capacity_) {
            index_.erase(order_.back().first);
            order_.pop_back();
        }
        order_.emplace_front(key, value);
        index_[key] = order_.begin();
    }

private:
    int capacity_;
    list<pair<int, int>> order_;
    unordered_map<int, list<pair<int, int>>::iterator> index_;
};

// ==================== TEST HARNESS ======================
// Generated from tests.json by LeetRunner. This section is rewritten
// before every run: add or edit tests in tests.json (or use "LeetCode: Add Custom Test").
// Run this file directly (without arguments) to print every test result, e.g. when debugging.

int main(int argc, char** argv) {
    lc::Harness harness(argc, argv);

    // Test Case 1: operations = ["LRUCache","put","put","get","put","get","put","get","get","get"], arguments = [[2],[1,1],[2,...
    harness.test(1, "[null,null,null,1,null,-1,null,-1,3,4]", [&](lc::Timer& timer) {
        lc::OutputList out;
        timer.start();
        LRUCache* obj = new LRUCache(2);
        out.null();
        obj->put(1, 1); out.null();
        obj->put(2, 2); out.null();
        out.add(obj->get(1));
        obj->put(3, 3); out.null();
        out.add(obj->get(2));
        obj->put(4, 4); out.null();
        out.add(obj->get(1));
        out.add(obj->get(3));
        out.add(obj->get(4));
        timer.stop();
        return out.str();
    });

    return harness.finish();
}
