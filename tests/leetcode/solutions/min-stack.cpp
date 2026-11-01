class MinStack {
public:
    MinStack() {}

    void push(int val) {
        values_.push_back(val);
        mins_.push_back(mins_.empty() ? val : min(val, mins_.back()));
    }

    void pop() { values_.pop_back(); mins_.pop_back(); }

    int top() { return values_.back(); }

    int getMin() { return mins_.back(); }

private:
    vector<int> values_, mins_;
};
