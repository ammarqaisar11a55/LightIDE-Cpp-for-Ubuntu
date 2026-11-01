// 104. Maximum Depth of Binary Tree (Easy)
// https://leetcode.com/problems/maximum-depth-of-binary-tree/
//
// Write your solution in the USER SOLUTION section. Only that section is copied by
// "LeetCode: Copy Solution". Run the tests with Ctrl+Shift+B (LeetCode: Run Tests).

#include ".lc/leetcode.hpp"  // local only: standard library, LeetCode types, test helpers

// ==================== USER SOLUTION ====================

class Solution {
public:
    int maxDepth(TreeNode* root) {
        if (!root) return 0;
        return 1 + max(maxDepth(root->left), maxDepth(root->right));
    }
};

// ==================== TEST HARNESS ======================
// Generated from tests.json by LeetRunner. This section is rewritten
// before every run: add or edit tests in tests.json (or use "LeetCode: Add Custom Test").
// Run this file directly (without arguments) to print every test result, e.g. when debugging.

int main(int argc, char** argv) {
    lc::Harness harness(argc, argv);

    // Test Case 1: root = [3,9,20,null,null,15,7]
    harness.test(1, "3", [&](lc::Timer& timer) {
        TreeNode* root = lc::buildTree<TreeNode>({3, 9, 20, lc::null, lc::null, 15, 7});

        Solution solution;
        timer.start();
        auto result = solution.maxDepth(root);
        timer.stop();
        return lc::serialize(result);
    });

    // Test Case 2: root = [1,null,2]
    harness.test(2, "2", [&](lc::Timer& timer) {
        TreeNode* root = lc::buildTree<TreeNode>({1, lc::null, 2});

        Solution solution;
        timer.start();
        auto result = solution.maxDepth(root);
        timer.stop();
        return lc::serialize(result);
    });

    return harness.finish();
}
