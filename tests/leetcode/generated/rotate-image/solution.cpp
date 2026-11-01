// 48. Rotate Image (Medium)
// https://leetcode.com/problems/rotate-image/
//
// Write your solution in the USER SOLUTION section. Only that section is copied by
// "LeetCode: Copy Solution". Run the tests with Ctrl+Shift+B (LeetCode: Run Tests).

#include ".lc/leetcode.hpp"  // local only: standard library, LeetCode types, test helpers

// ==================== USER SOLUTION ====================

class Solution {
public:
    void rotate(vector<vector<int>>& matrix) {
        int n = matrix.size();
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++) swap(matrix[i][j], matrix[j][i]);
        for (auto& row : matrix) reverse(row.begin(), row.end());
    }
};

// ==================== TEST HARNESS ======================
// Generated from tests.json by LeetRunner. This section is rewritten
// before every run: add or edit tests in tests.json (or use "LeetCode: Add Custom Test").
// Run this file directly (without arguments) to print every test result, e.g. when debugging.

int main(int argc, char** argv) {
    lc::Harness harness(argc, argv);

    // Test Case 1: matrix = [[1,2,3],[4,5,6],[7,8,9]]
    harness.test(1, "[[7,4,1],[8,5,2],[9,6,3]]", [&](lc::Timer& timer) {
        vector<vector<int>> matrix = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};

        Solution solution;
        timer.start();
        solution.rotate(matrix);
        timer.stop();
        return lc::serialize(matrix);
    });

    // Test Case 2: matrix = [[5,1,9,11],[2,4,8,10],[13,3,6,7],[15,14,12,16]]
    harness.test(2, "[[15,13,2,5],[14,3,4,1],[12,6,8,9],[16,7,10,11]]", [&](lc::Timer& timer) {
        vector<vector<int>> matrix = {{5, 1, 9, 11}, {2, 4, 8, 10}, {13, 3, 6, 7}, {15, 14, 12, 16}};

        Solution solution;
        timer.start();
        solution.rotate(matrix);
        timer.stop();
        return lc::serialize(matrix);
    });

    return harness.finish();
}
