class Solution {
public:
    double findMedianSortedArrays(vector<int>& nums1, vector<int>& nums2) {
        vector<int> all(nums1);
        all.insert(all.end(), nums2.begin(), nums2.end());
        sort(all.begin(), all.end());
        int n = all.size();
        return n % 2 ? all[n / 2] : (all[n / 2 - 1] + all[n / 2]) / 2.0;
    }
};
