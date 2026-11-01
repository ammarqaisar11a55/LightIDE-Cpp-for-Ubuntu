// 49. Group Anagrams (Medium)
// https://leetcode.com/problems/group-anagrams/
//
// Write your solution in the USER SOLUTION section. Only that section is copied by
// "LeetCode: Copy Solution". Run the tests with Ctrl+Shift+B (LeetCode: Run Tests).

#include ".lc/leetcode.hpp"  // local only: standard library, LeetCode types, test helpers

// ==================== USER SOLUTION ====================

class Solution {
public:
    vector<vector<string>> groupAnagrams(vector<string>& strs) {
        unordered_map<string, vector<string>> groups;
        for (const string& s : strs) {
            string key = s;
            sort(key.begin(), key.end());
            groups[key].push_back(s);
        }
        vector<vector<string>> result;
        for (auto& [key, group] : groups) result.push_back(group);
        return result;
    }
};

// ==================== TEST HARNESS ======================
// Generated from tests.json by LeetRunner. This section is rewritten
// before every run: add or edit tests in tests.json (or use "LeetCode: Add Custom Test").
// Run this file directly (without arguments) to print every test result, e.g. when debugging.

int main(int argc, char** argv) {
    lc::Harness harness(argc, argv);

    // Test Case 1: strs = ["eat","tea","tan","ate","nat","bat"]
    harness.test(1, "[[\"bat\"],[\"nat\",\"tan\"],[\"ate\",\"eat\",\"tea\"]]", [&](lc::Timer& timer) {
        vector<string> strs = {"eat", "tea", "tan", "ate", "nat", "bat"};

        Solution solution;
        timer.start();
        auto result = solution.groupAnagrams(strs);
        timer.stop();
        return lc::serialize(result);
    });

    // Test Case 2: strs = [""]
    harness.test(2, "[[\"\"]]", [&](lc::Timer& timer) {
        vector<string> strs = {""};

        Solution solution;
        timer.start();
        auto result = solution.groupAnagrams(strs);
        timer.stop();
        return lc::serialize(result);
    });

    // Test Case 3: strs = ["a"]
    harness.test(3, "[[\"a\"]]", [&](lc::Timer& timer) {
        vector<string> strs = {"a"};

        Solution solution;
        timer.start();
        auto result = solution.groupAnagrams(strs);
        timer.stop();
        return lc::serialize(result);
    });

    return harness.finish();
}
