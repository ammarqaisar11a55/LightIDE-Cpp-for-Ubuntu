// 1. Two Sum (Easy)
// https://leetcode.com/problems/two-sum/
//
// Write your solution in the USER SOLUTION section. Only that section is copied by
// "LeetCode: Copy Solution". Run the tests with Ctrl+Shift+B (LeetCode: Run Tests).

#include ".lc/leetcode.hpp"  // local only: standard library, LeetCode types, test helpers

// ==================== USER SOLUTION ====================

class Solution {
public:
    vector<int> twoSum(vector<int>& nums, int target) {
        unordered_map<int, int> mp;
        for (int i = 0; i < (int)nums.size(); i++) {
            int need = target - nums[i];
            if (mp.count(need)) return {mp[need], i};
            mp[nums[i]] = i;
        }
        return {};
    }
};

// ==================== TEST HARNESS ======================
// Generated from tests.json by LeetRunner. This section is rewritten
// before every run: add or edit tests in tests.json (or use "LeetCode: Add Custom Test").
// Run this file directly (without arguments) to print every test result, e.g. when debugging.

int main(int argc, char** argv) {
    lc::Harness harness(argc, argv);

    // Test Case 1: nums = [2,7,11,15], target = 9
    harness.test(1, "[0,1]", [&](lc::Timer& timer) {
        vector<int> nums = {2, 7, 11, 15};
        int target = 9;

        Solution solution;
        timer.start();
        auto result = solution.twoSum(nums, target);
        timer.stop();
        return lc::serialize(result);
    });

    // Test Case 2: nums = [3,2,4], target = 6
    harness.test(2, "[1,2]", [&](lc::Timer& timer) {
        vector<int> nums = {3, 2, 4};
        int target = 6;

        Solution solution;
        timer.start();
        auto result = solution.twoSum(nums, target);
        timer.stop();
        return lc::serialize(result);
    });

    // Test Case 3: nums = [3,3], target = 6
    harness.test(3, "[0,1]", [&](lc::Timer& timer) {
        vector<int> nums = {3, 3};
        int target = 6;

        Solution solution;
        timer.start();
        auto result = solution.twoSum(nums, target);
        timer.stop();
        return lc::serialize(result);
    });

    return harness.finish();
}
