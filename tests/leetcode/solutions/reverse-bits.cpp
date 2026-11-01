class Solution {
public:
    int reverseBits(int n) {
        unsigned int x = n, r = 0;
        for (int i = 0; i < 32; i++) { r = (r << 1) | (x & 1); x >>= 1; }
        return (int)r;
    }
};
