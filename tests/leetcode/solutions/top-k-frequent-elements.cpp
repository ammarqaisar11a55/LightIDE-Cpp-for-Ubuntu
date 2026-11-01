class Solution {
public:
    vector<int> topKFrequent(vector<int>& nums, int k) {
        unordered_map<int, int> freq;
        for (int x : nums) freq[x]++;
        vector<pair<int, int>> items(freq.begin(), freq.end());
        sort(items.begin(), items.end(), [](auto& a, auto& b) { return a.second > b.second; });
        vector<int> result;
        for (int i = 0; i < k; i++) result.push_back(items[i].first);
        return result;
    }
};
