class Trie {
public:
    Trie() { nodes_.emplace_back(); }

    void insert(string word) {
        int cur = 0;
        for (char c : word) {
            int& next = nodes_[cur].child[c - 'a'];
            if (!next) { next = nodes_.size(); nodes_.emplace_back(); }
            cur = nodes_[cur].child[c - 'a'];
        }
        nodes_[cur].end = true;
    }

    bool search(string word) { int n = walk(word); return n >= 0 && nodes_[n].end; }

    bool startsWith(string prefix) { return walk(prefix) >= 0; }

private:
    struct Node { int child[26] = {}; bool end = false; };
    vector<Node> nodes_;
    int walk(const string& s) {
        int cur = 0;
        for (char c : s) { cur = nodes_[cur].child[c - 'a']; if (!cur) return -1; }
        return cur;
    }
};
