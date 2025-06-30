#include <stdio.h>

int is_prime(int n) {
    if (n <= 1) return 0; // 0 and 1 are not prime numbers
    for (int i = 2; i * i <= n; i++) {
        if (n % i == 0) return 0; // Found a divisor, not prime
    }
    return 1; // No divisors found, it's prime
}

int main() {
    int count = 0;

    while (1) {
        if (is_prime(count)) {
            printf("%d \n", count);
        }
        count++;
    }

    return 0;
}
