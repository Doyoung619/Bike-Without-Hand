#define F (1 << 14)

int n_to_fp (int n){
    return n * F;
}

int fp_to_n (int x){
    return x / F;
}

int round_fp_to_n (int x) {
    if (x >= 0) return (x + F/2) / F;
    else return (x - F/2) / F;
}

int add_fp_fp (int x, int y) {
    return x + y;
}

int add_fp_n (int x, int n) {
    return x + n * F;
}

int sub_fp_fp (int x, int y) {
    return x - y;
}

int sub_fp_n (int x, int n) {
    return x - n * F;
}

int mul_fp_fp (int x, int y) {
    return ((int64_t) x) * y / F;
}

int mul_fp_n (int x, int n) {
    return x * n;
}

int div_fp_fp (int x, int y) {
    return ((int64_t) x) * F / y;
}

int div_fp_n (int x, int n) {
    return x / n;
}