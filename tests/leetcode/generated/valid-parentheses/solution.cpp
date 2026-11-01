// 20. Valid Parentheses (Easy)
// https://leetcode.com/problems/valid-parentheses/
//
// Write your solution in the USER SOLUTION section. Only that section is copied by
// "LeetCode: Copy Solution". Run the tests with Ctrl+Shift+B (LeetCode: Run Tests).

#include ".lc/leetcode.hpp"  // local only: standard library, LeetCode types, test helpers

// ==================== USER SOLUTION ====================

class Solution {
public:
    bool isValid(string s) {
        stack<char> st;
        for (char c : s) {
            if (c == '(' || c == '[' || c == '{') { st.push(c); continue; }
            if (st.empty()) return false;
            char open = st.top(); st.pop();
            if ((c == ')' && open != '(') || (c == ']' && open != '[') || (c == '}' && open != '{')) return false;
        }
        return st.empty();
    }
};

// ==================== TEST HARNESS ======================
// Generated from tests.json by LeetRunner. This section is rewritten
// before every run: add or edit tests in tests.json (or use "LeetCode: Add Custom Test").
// Run this file directly (without arguments) to print every test result, e.g. when debugging.

int main(int argc, char** argv) {
    lc::Harness harness(argc, argv);

    // Test Case 1: s = "()"
    harness.test(1, "true", [&](lc::Timer& timer) {
        string s = "()";

        Solution solution;
        timer.start();
        auto result = solution.isValid(s);
        timer.stop();
        return lc::serialize(result);
    });

    // Test Case 2: s = "()[]{}"
    harness.test(2, "true", [&](lc::Timer& timer) {
        string s = "()[]{}";

        Solution solution;
        timer.start();
        auto result = solution.isValid(s);
        timer.stop();
        return lc::serialize(result);
    });

    // Test Case 3: s = "(]"
    harness.test(3, "false", [&](lc::Timer& timer) {
        string s = "(]";

        Solution solution;
        timer.start();
        auto result = solution.isValid(s);
        timer.stop();
        return lc::serialize(result);
    });

    // Test Case 4: s = "([])"
    harness.test(4, "true", [&](lc::Timer& timer) {
        string s = "([])";

        Solution solution;
        timer.start();
        auto result = solution.isValid(s);
        timer.stop();
        return lc::serialize(result);
    });

    // Test Case 5: s = "([)]"
    harness.test(5, "false", [&](lc::Timer& timer) {
        string s = "([)]";

        Solution solution;
        timer.start();
        auto result = solution.isValid(s);
        timer.stop();
        return lc::serialize(result);
    });

    return harness.finish();
}
