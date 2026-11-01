// 21. Merge Two Sorted Lists (Easy)
// https://leetcode.com/problems/merge-two-sorted-lists/
//
// Write your solution in the USER SOLUTION section. Only that section is copied by
// "LeetCode: Copy Solution". Run the tests with Ctrl+Shift+B (LeetCode: Run Tests).

#include ".lc/leetcode.hpp"  // local only: standard library, LeetCode types, test helpers

// ==================== USER SOLUTION ====================

/**
 * Definition for singly-linked list.
 * struct ListNode {
 *     int val;
 *     ListNode *next;
 *     ListNode() : val(0), next(nullptr) {}
 *     ListNode(int x) : val(x), next(nullptr) {}
 *     ListNode(int x, ListNode *next) : val(x), next(next) {}
 * };
 */
class Solution {
public:
    ListNode* mergeTwoLists(ListNode* list1, ListNode* list2) {
        ListNode dummy;
        ListNode* tail = &dummy;
        while (list1 && list2) {
            if (list1->val <= list2->val) { tail->next = list1; list1 = list1->next; }
            else { tail->next = list2; list2 = list2->next; }
            tail = tail->next;
        }
        tail->next = list1 ? list1 : list2;
        return dummy.next;
    }
};

// ==================== TEST HARNESS ======================
// Generated from tests.json by LeetRunner. This section is rewritten
// before every run: add or edit tests in tests.json (or use "LeetCode: Add Custom Test").
// Run this file directly (without arguments) to print every test result, e.g. when debugging.

int main(int argc, char** argv) {
    lc::Harness harness(argc, argv);

    // Test Case 1: list1 = [1,2,4], list2 = [1,3,4]
    harness.test(1, "[1,1,2,3,4,4]", [&](lc::Timer& timer) {
        ListNode* list1 = lc::buildList<ListNode>({1, 2, 4});
        ListNode* list2 = lc::buildList<ListNode>({1, 3, 4});

        Solution solution;
        timer.start();
        auto result = solution.mergeTwoLists(list1, list2);
        timer.stop();
        return lc::serialize(result);
    });

    // Test Case 2: list1 = [], list2 = []
    harness.test(2, "[]", [&](lc::Timer& timer) {
        ListNode* list1 = nullptr;
        ListNode* list2 = nullptr;

        Solution solution;
        timer.start();
        auto result = solution.mergeTwoLists(list1, list2);
        timer.stop();
        return lc::serialize(result);
    });

    // Test Case 3: list1 = [], list2 = [0]
    harness.test(3, "[0]", [&](lc::Timer& timer) {
        ListNode* list1 = nullptr;
        ListNode* list2 = lc::buildList<ListNode>({0});

        Solution solution;
        timer.start();
        auto result = solution.mergeTwoLists(list1, list2);
        timer.stop();
        return lc::serialize(result);
    });

    return harness.finish();
}
