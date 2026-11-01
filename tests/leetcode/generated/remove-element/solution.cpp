// 27. Remove Element (Easy)
// https://leetcode.com/problems/remove-element/
//
// Write your solution in the USER SOLUTION section. Only that section is copied by
// "LeetCode: Copy Solution". Run the tests with Ctrl+Shift+B (LeetCode: Run Tests).

#include ".lc/leetcode.hpp"  // local only: standard library, LeetCode types, test helpers

// ==================== USER SOLUTION ====================

class Solution {
public:
    int removeElement(vector<int>& nums, int val) {
        int k = 0;
        for (int x : nums) if (x != val) nums[k++] = x;
        return k;
    }
};

// ==================== TEST HARNESS ======================
// Generated from tests.json by LeetRunner. This section is rewritten
// before every run: add or edit tests in tests.json (or use "LeetCode: Add Custom Test").
// Run this file directly (without arguments) to print every test result, e.g. when debugging.

int main(int argc, char** argv) {
    lc::Harness harness(argc, argv);

    // Test Case 1: nums = [3,2,2,3], val = 3
    harness.test(1, "2, nums = [2,2,_,_]", [&](lc::Timer& timer) {
        vector<int> nums = {3, 2, 2, 3};
        int val = 3;

        Solution solution;
        timer.start();
        auto result = solution.removeElement(nums, val);
        timer.stop();
        return lc::serializePrefix(result, "nums", nums);
    });

    // Test Case 2: nums = [0,1,2,2,3,0,4,2], val = 2
    harness.test(2, "5, nums = [0,1,4,0,3,_,_,_]", [&](lc::Timer& timer) {
        vector<int> nums = {0, 1, 2, 2, 3, 0, 4, 2};
        int val = 2;

        Solution solution;
        timer.start();
        auto result = solution.removeElement(nums, val);
        timer.stop();
        return lc::serializePrefix(result, "nums", nums);
    });

    return harness.finish();
}
